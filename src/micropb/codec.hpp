#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include <micropb/meta_schema.hpp>
#include <micropb/types.hpp>
#include <micropb/wire.hpp>

// The codec has two parts.
// Part 1 makes descriptors: constant tables that say where each field of a
// message is and how to read and write it. Reflection makes them at compile
// time, one time for each type.
// Part 2 is the interpreter: functions that are not templates. They encode,
// decode and reset each message type from its descriptors.

namespace pb {

namespace detail {

////////////////////////////////////////////////////////////////////////////////
// Part 1: descriptors

// A scalar code tells the interpreter how to load and store a scalar. The low
// two bits are log2 of the size in bytes. The other bits are flags.
inline constexpr std::uint8_t scalar_size_mask = 0x03;
/// The type has a sign: the loaded value sign-extends to 64 bits.
inline constexpr std::uint8_t scalar_signed = 0x04;
/// `sint32` or `sint64`: ZigZag varint.
inline constexpr std::uint8_t scalar_zigzag = 0x08;
/// Fixed width on the wire: 4 bytes or 8 bytes, the size of the type.
inline constexpr std::uint8_t scalar_fixed = 0x10;
inline constexpr std::uint8_t scalar_bool = 0x20;
/// An enum: `int32` on the wire.
inline constexpr std::uint8_t scalar_enum = 0x40;

template <typename T>
[[nodiscard]] consteval std::uint8_t scalar_code_of() {
    constexpr std::uint8_t size = sizeof(T) == 1 ? 0 : sizeof(T) == 2 ? 1 : sizeof(T) == 4 ? 2 : 3;

    if constexpr (std::is_same_v<T, bool>)
        return size | scalar_bool;
    else if constexpr (std::is_enum_v<T>)
        return size | scalar_enum | (std::is_signed_v<std::underlying_type_t<T>> ? scalar_signed : 0);
    else if constexpr (is_one_of<T, sint32, sint64>)
        return size | scalar_signed | scalar_zigzag;
    else if constexpr (wire_type_of<T>() != wire_type::VARINT)
        return size | scalar_fixed;
    else
        return size | (std::is_signed_v<T> ? scalar_signed : 0);
}

struct message_desc;
struct sequence_desc;
struct optional_desc;
struct variant_desc;

/// How to read and write a value of one type. All members, elements, optional
/// values and alternatives of the same type share one descriptor.
struct value_desc {
    field_kind kind;
    /// SCALAR: the scalar code.
    std::uint8_t scalar;
    union {
        /// FIXED_BYTES: the number of bytes. 0 for SCALAR and STRING.
        std::uint32_t size;
        /// MESSAGE.
        const message_desc* message;
        /// REPEATED and BYTES. Null for the view `std::span<const std::uint8_t>`.
        const sequence_desc* sequence;
        /// OPTIONAL.
        const optional_desc* optional;
        /// ONEOF.
        const variant_desc* variant;
    };
};

/// One entry of the tag table: a member of a message, or one alternative of a
/// `std::variant` member.
struct field_desc {
    std::uint32_t number;
    /// Offset of the member in the message, in bytes.
    std::uint16_t offset;
    /// 0 for a plain member, else the index of the alternative.
    std::uint16_t alternative;
    /// Type of the member. For an alternative, this is the `std::variant`.
    const value_desc* type;
};

/// The fields of a message type, sorted by field number.
struct message_desc {
    const field_desc* fields;
    std::uint16_t count;
};

/// The items of a sequence at this moment.
struct sequence_view {
    void* data;
    std::size_t count;
    std::size_t capacity;
    /// The count that `decode` changes. Null for a type that is encode-only.
    std::size_t* stored_count;
};

using sequence_accessor = sequence_view (*)(void* sequence);

enum class sequence_storage : std::uint8_t {
    /// `pb::repeated`: the items are at offset 0, the count is at `count_offset`.
    REPEATED,
    /// `std::array`: the items are at offset 0, the count is `capacity`.
    ARRAY,
    /// `std::span<const T>` and `pb::span_buffer<T>`: `access` gives the items.
    ACCESSOR,
};

struct sequence_desc {
    const value_desc* element;
    sequence_accessor access;
    std::uint32_t element_size;
    std::uint32_t capacity;
    std::uint16_t count_offset;
    sequence_storage storage;
};

enum class optional_op : std::uint8_t {
    /// Gives the address of the value, or null when there is no value.
    GET,
    /// Makes a value-initialized value if there is none. Gives its address.
    ENGAGE,
    /// Removes the value. Gives null.
    RESET,
};

using optional_accessor = void* (*)(void* optional, optional_op op);

struct optional_desc {
    const value_desc* value;
    optional_accessor access;
};

/// Gives the address of alternative `index` when it is active. If it is not
/// active and `select` is `true`, makes it active with a value-initialized
/// value first. Else gives null. Index 0 is `std::monostate`, so
/// `access(variant, 0, true)` resets the variant.
using variant_accessor = void* (*)(void* variant, std::size_t index, bool select);

struct variant_desc {
    /// One entry for each alternative. The entry of `std::monostate` is null.
    const value_desc* const* alternatives;
    variant_accessor access;
};

static_assert(sizeof(field_desc) <= (sizeof(void*) == 4 ? 12 : 16));

template <typename T>
struct value_descriptor {
    static const value_desc value;
};

template <typename T>
struct message_descriptor {
    static const message_desc value;
};

////////////////////////////////////////////////////////////////////////////////

/// The data member `name` of the class `type`, also when it is private.
consteval std::meta::info member_named(std::meta::info type, std::string_view name) {
    for (std::meta::info member : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
        if (std::meta::identifier_of(member) == name)
            return member;
    }
    return std::meta::info{};
}

/// The size `N` of `std::array<T, N>` or `pb::repeated<T, N>`.
template <typename S>
consteval std::uint32_t static_capacity() {
    return static_cast<std::uint32_t>(std::meta::extract<std::size_t>(std::meta::template_arguments_of(^^S)[1]));
}

/// Accessor of `std::span<const T>` and `pb::span_buffer<T>`. These types keep
/// their items in a different object, so the interpreter cannot find the items
/// from an offset.
template <typename S>
sequence_view access_sequence(void* sequence) {
    S& items = *static_cast<S*>(sequence);
    if constexpr (is_instance_of<S>(^^span_buffer)) {
        return {items.begin(), items.size(), items.capacity(), &items.[:member_named(^^S, "_count"):]};
    } else {
        const void* data = items.data();
        return {const_cast<void*>(data), items.size(), items.size(), nullptr};
    }
}

template <typename O>
void* access_optional(void* optional, optional_op op) {
    O& value = *static_cast<O*>(optional);
    if (op == optional_op::RESET)
        value.reset();
    else if (op == optional_op::ENGAGE && !value)
        value.emplace();
    return value ? &*value : nullptr;
}

template <typename V>
void* access_variant(void* variant, std::size_t index, bool select) {
    V& value = *static_cast<V*>(variant);
    std::size_t current = 0;
    template for (constexpr std::meta::info reflected : alternatives<V>()) {
        using alternative = typename[:reflected:];
        if (index == current) {
            if (select && !std::holds_alternative<alternative>(value))
                value.template emplace<alternative>();
            return std::get_if<alternative>(&value);
        }
        ++current;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

template <typename S>
consteval sequence_desc describe_sequence() {
    using element = element_t<S>;
    constexpr const value_desc* item = &value_descriptor<element>::value;
    constexpr std::uint32_t item_size = sizeof(element);

    if constexpr (is_instance_of<S>(^^repeated)) {
        static_assert(std::meta::offset_of(member_named(^^S, "_items")).bytes == 0);
        constexpr std::size_t count_offset = std::meta::offset_of(member_named(^^S, "_count")).bytes;
        static_assert(count_offset <= 65535,
                      "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less");
        return {item, nullptr, item_size, static_capacity<S>(), static_cast<std::uint16_t>(count_offset),
                sequence_storage::REPEATED};
    } else if constexpr (is_instance_of<S>(^^std::array)) {
        return {item, nullptr, item_size, static_capacity<S>(), 0, sequence_storage::ARRAY};
    } else {
        return {item, access_sequence<S>, item_size, 0, 0, sequence_storage::ACCESSOR};
    }
}

template <typename S>
inline constexpr sequence_desc sequence_descriptor = describe_sequence<S>();

template <typename O>
inline constexpr optional_desc optional_descriptor{&value_descriptor<typename O::value_type>::value,
                                                    access_optional<O>};

template <typename V>
consteval std::span<const value_desc* const> describe_alternatives() {
    std::array<const value_desc*, std::variant_size_v<V>> descriptors{};
    std::size_t index = 0;
    template for (constexpr std::meta::info reflected : alternatives<V>()) {
        using alternative = typename[:reflected:];
        if constexpr (!std::is_same_v<alternative, std::monostate>)
            descriptors[index] = &value_descriptor<alternative>::value;
        ++index;
    }
    return std::define_static_array(descriptors);
}

template <typename V>
inline constexpr variant_desc variant_descriptor{describe_alternatives<V>().data(), access_variant<V>};

/// The field table of the message type `T`: one entry for each entry of the
/// tag table, in the same order.
template <typename T>
consteval std::span<const field_desc> describe_fields() {
    std::array<field_desc, build_tag_table<T>().size()> fields{};
    [[maybe_unused]] std::size_t index = 0;
    template for (constexpr field_info field : build_tag_table<T>()) {
        using member = std::remove_const_t<typename[:std::meta::type_of(field.member):]>;
        constexpr std::size_t offset = std::meta::offset_of(field.member).bytes;
        static_assert(offset <= 65535,
                      "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less");

        fields[index++] = {field.number, static_cast<std::uint16_t>(offset),
                           static_cast<std::uint16_t>(field.alternative == no_alternative ? 0 : field.alternative),
                           &value_descriptor<member>::value};
    }
    return std::define_static_array(fields);
}

template <typename T>
consteval value_desc describe_value() {
    constexpr field_kind kind = kind_of<T>();

    if constexpr (kind == field_kind::SCALAR)
        return {.kind = kind, .scalar = scalar_code_of<T>(), .size = 0};
    else if constexpr (kind == field_kind::STRING)
        return {.kind = kind, .scalar = 0, .size = 0};
    else if constexpr (kind == field_kind::FIXED_BYTES)
        return {.kind = kind, .scalar = 0, .size = static_capacity<T>()};
    else if constexpr (kind == field_kind::BYTES && is_instance_of<T>(^^std::span))
        return {.kind = kind, .scalar = 0, .sequence = nullptr};
    else if constexpr (kind == field_kind::BYTES || kind == field_kind::REPEATED)
        return {.kind = kind, .scalar = 0, .sequence = &sequence_descriptor<T>};
    else if constexpr (kind == field_kind::MESSAGE)
        return {.kind = kind, .scalar = 0, .message = &message_descriptor<T>::value};
    else if constexpr (kind == field_kind::OPTIONAL)
        return {.kind = kind, .scalar = 0, .optional = &optional_descriptor<T>};
    else
        return {.kind = kind, .scalar = 0, .variant = &variant_descriptor<T>};
}

template <typename T>
constexpr value_desc value_descriptor<T>::value = describe_value<T>();

template <typename T>
constexpr message_desc message_descriptor<T>::value{describe_fields<T>().data(),
                                                     static_cast<std::uint16_t>(describe_fields<T>().size())};

} // namespace detail

} // namespace pb
