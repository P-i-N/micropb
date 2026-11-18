#pragma once

#include <micropb/types.hpp>
#include <micropb/wire.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Category of a member type. It sets how the codec writes and reads the member.
enum class field_kind { SCALAR, STRING, BYTES, FIXED_BYTES, MESSAGE, OPTIONAL, ONEOF, REPEATED };

/// `field_info::alternative` of a field that is not a `std::variant` alternative.
inline constexpr std::size_t no_alternative = std::size_t(-1);

/// One entry of the tag table: a plain member, or one alternative of a `std::variant` member.
struct field_info {
    std::meta::info member;
    std::uint32_t number;
    /// Index into the `std::variant`, or `no_alternative`.
    std::size_t alternative;
};

////////////////////////////////////////////////////////////////////////////////

namespace detail {

inline constexpr std::uint32_t max_tag = (1u << 29) - 1;
inline constexpr std::uint32_t reserved_first = 19000;
inline constexpr std::uint32_t reserved_last = 19999;

template <typename T, typename... U>
inline constexpr bool is_one_of = (std::is_same_v<T, U> || ...);

/// `true` when `type` is a specialization of the class template `templ`.
consteval bool is_instance(std::meta::info type, std::meta::info templ) {
    type = std::meta::dealias(type);
    return std::meta::has_template_arguments(type) && std::meta::template_of(type) == templ;
}

template <typename T>
consteval bool is_instance_of(std::meta::info templ) {
    return is_instance(^^T, templ);
}

/// Template argument `I` of the specialization `T`, as written.
template <typename T, std::size_t I>
using argument_t = typename[:std::meta::template_arguments_of(std::meta::dealias(^^T))[I]:];

/// Element type of a sequence type.
template <typename T>
using element_t = std::remove_const_t<argument_t<T, 0>>;

template <typename T>
consteval bool is_scalar() {
    return std::is_enum_v<T> ||
           is_one_of<T, bool, std::int8_t, std::int16_t, std::int32_t, std::int64_t, std::uint8_t, std::uint16_t,
                     std::uint32_t, std::uint64_t, float, double, sint32, sint64, fixed32, fixed64, sfixed32,
                     sfixed64>;
}

/// `true` for `std::array`, `std::span<const T>`, `pb::repeated` and `pb::span_buffer`.
template <typename T>
consteval bool is_sequence() {
    if constexpr (is_instance_of<T>(^^std::span))
        return std::is_const_v<argument_t<T, 0>>;
    else
        return is_instance_of<T>(^^std::array) || is_instance_of<T>(^^repeated) || is_instance_of<T>(^^span_buffer);
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Kind of the member type `T`. A type without a proto3 mapping is a compile error.
template <typename T>
[[nodiscard]] consteval field_kind kind_of() {
    if constexpr (detail::is_scalar<T>()) {
        return field_kind::SCALAR;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return field_kind::STRING;
    } else if constexpr (detail::is_instance_of<T>(^^std::optional)) {
        return field_kind::OPTIONAL;
    } else if constexpr (detail::is_instance_of<T>(^^std::variant)) {
        return field_kind::ONEOF;
    } else if constexpr (detail::is_sequence<T>()) {
        if constexpr (!std::is_same_v<detail::element_t<T>, std::uint8_t>)
            return field_kind::REPEATED;
        else if constexpr (detail::is_instance_of<T>(^^std::array))
            return field_kind::FIXED_BYTES;
        else
            return field_kind::BYTES;
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>) {
        return field_kind::MESSAGE;
    } else {
        static_assert(false, "pb: unmapped_type: the member type has no proto3 mapping");
    }
}

/// Wire type of a field of type `T`. A `std::optional` has the wire type of its value.
/// A oneof or a repeated field has no single wire type.
template <typename T>
[[nodiscard]] consteval wire_type wire_type_of() {
    constexpr field_kind kind = kind_of<T>();
    if constexpr (kind == field_kind::OPTIONAL)
        return wire_type_of<typename T::value_type>();
    else if constexpr (kind == field_kind::ONEOF || kind == field_kind::REPEATED)
        static_assert(false, "pb: wire_type_of: a oneof or repeated field has no single wire type");
    else if constexpr (kind != field_kind::SCALAR)
        return wire_type::LENGTH_DELIMITED;
    else if constexpr (detail::is_one_of<T, float, fixed32, sfixed32>)
        return wire_type::FIXED32;
    else if constexpr (detail::is_one_of<T, double, fixed64, sfixed64>)
        return wire_type::FIXED64;
    else
        return wire_type::VARINT;
}

/// `true` when a repeated field with elements of type `T` is packed.
template <typename T>
[[nodiscard]] consteval bool is_packed() {
    return kind_of<T>() == field_kind::SCALAR;
}

////////////////////////////////////////////////////////////////////////////////

namespace detail {

template <typename T>
consteval std::span<const std::meta::info> members() {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()));
}

/// Alternatives of the `std::variant` type `T`.
template <typename T>
consteval std::span<const std::meta::info> alternatives() {
    return std::define_static_array(std::meta::template_arguments_of(^^T));
}

/// `true` when `T` has a `pb::span_buffer` in a place that `decode` cannot bind:
/// in it, or in a member, alternative, value or element of it.
template <typename T>
consteval bool contains_span_buffer() {
    constexpr field_kind kind = kind_of<T>();
    if constexpr (is_instance_of<T>(^^span_buffer)) {
        return true;
    } else if constexpr (kind == field_kind::OPTIONAL) {
        return contains_span_buffer<typename T::value_type>();
    } else if constexpr (kind == field_kind::REPEATED) {
        return contains_span_buffer<element_t<T>>();
    } else if constexpr (kind == field_kind::ONEOF) {
        template for (constexpr std::meta::info alternative : alternatives<T>()) {
            if (contains_span_buffer<typename[:alternative:]>())
                return true;
        }
        return false;
    } else if constexpr (kind == field_kind::MESSAGE) {
        template for (constexpr std::meta::info member : members<T>()) {
            if (contains_span_buffer<typename[:std::meta::type_of(member):]>())
                return true;
        }
        return false;
    } else {
        return false;
    }
}

inline constexpr std::string_view monostate_message =
    "pb: monostate_member: std::monostate is allowed only as the first alternative of a std::variant";

template <typename V>
consteval bool alternatives_unique() {
    const std::span<const std::meta::info> types = alternatives<V>();
    for (std::size_t i = 0; i < types.size(); ++i) {
        for (std::size_t j = i + 1; j < types.size(); ++j) {
            if (std::meta::dealias(types[i]) == std::meta::dealias(types[j]))
                return false;
        }
    }
    return true;
}

/// `true` when an alternative of the `std::variant` type `V` has the kind `kind`.
template <typename V>
consteval bool has_alternative_of_kind(field_kind kind) {
    template for (constexpr std::meta::info alternative : alternatives<V>()) {
        if (kind_of<typename[:alternative:]>() == kind)
            return true;
    }
    return false;
}

/// Rejects member types that have a mapping but that proto3 or `decode` cannot use.
template <typename M>
consteval void check_member_type() {
    constexpr field_kind kind = kind_of<M>();
    static_assert(!std::is_same_v<M, std::monostate>, monostate_message);

    if constexpr (kind == field_kind::OPTIONAL) {
        using value = typename M::value_type;
        constexpr field_kind value_kind = kind_of<value>();
        static_assert(!std::is_same_v<value, std::monostate>, monostate_message);
        static_assert(value_kind != field_kind::REPEATED,
                      "pb: optional_repeated: a repeated field cannot have explicit presence");
        static_assert(value_kind != field_kind::OPTIONAL,
                      "pb: optional_optional: the value of a std::optional cannot be a std::optional");
        static_assert(value_kind != field_kind::ONEOF,
                      "pb: optional_variant: the value of a std::optional cannot be a std::variant");
        static_assert(!contains_span_buffer<value>(),
                      "pb: span_buffer_in_optional: use pb::repeated in a std::optional");
    } else if constexpr (kind == field_kind::ONEOF) {
        static_assert(std::meta::dealias(alternatives<M>()[0]) == ^^std::monostate,
                      "pb: variant_no_monostate: the first alternative of a std::variant must be std::monostate");
        static_assert(alternatives_unique<M>(), "pb: variant_duplicate_alternative: each alternative of a "
                                                "std::variant must have a different type");
        static_assert(!has_alternative_of_kind<M>(field_kind::REPEATED),
                      "pb: variant_repeated_alternative: a repeated field cannot be a oneof alternative");
        static_assert(!has_alternative_of_kind<M>(field_kind::OPTIONAL),
                      "pb: variant_optional_alternative: a std::optional cannot be a oneof alternative");
        static_assert(!has_alternative_of_kind<M>(field_kind::ONEOF),
                      "pb: variant_variant_alternative: a std::variant cannot be a oneof alternative");
        static_assert(!contains_span_buffer<M>(), "pb: span_buffer_in_variant: use pb::repeated in a std::variant");
    } else if constexpr (kind == field_kind::REPEATED) {
        using element = element_t<M>;
        constexpr field_kind element_kind = kind_of<element>();
        static_assert(!std::is_same_v<element, std::monostate>, monostate_message);
        static_assert(element_kind != field_kind::REPEATED,
                      "pb: repeated_repeated: the element of a repeated field cannot be a repeated field");
        static_assert(element_kind != field_kind::OPTIONAL,
                      "pb: repeated_optional: the element of a repeated field cannot be a std::optional");
        static_assert(element_kind != field_kind::ONEOF,
                      "pb: repeated_variant: the element of a repeated field cannot be a std::variant");
        static_assert(!contains_span_buffer<element>(),
                      "pb: span_buffer_in_repeated: use pb::repeated as the element of a repeated field");
    }
}

/// Field number from the `pb::tag` annotation of `member`.
consteval std::optional<std::uint32_t> tag_annotation(std::meta::info member) {
    for (std::meta::info annotation : std::meta::annotations_of(member)) {
        if (std::meta::remove_cvref(std::meta::type_of(annotation)) == ^^tag)
            return std::meta::extract<tag>(annotation).value;
    }
    return std::nullopt;
}

/// Type of the `pb::oneof_tags` annotation of `member`.
consteval std::optional<std::meta::info> oneof_tags_annotation(std::meta::info member) {
    for (std::meta::info annotation : std::meta::annotations_of(member)) {
        const std::meta::info type = std::meta::remove_cvref(std::meta::type_of(annotation));
        if (is_instance(type, ^^oneof_tags))
            return type;
    }
    return std::nullopt;
}

template <typename T>
consteval std::size_t field_count() {
    std::size_t count = 0;
    for (std::meta::info member : members<T>()) {
        const std::meta::info type = std::meta::dealias(std::meta::type_of(member));
        if (is_instance(type, ^^std::variant))
            count += std::meta::template_arguments_of(type).size() - 1;
        else
            ++count;
    }
    return count;
}

/// Tag table in declaration order. Applies the tag rules of the spec.
template <typename T>
consteval std::array<field_info, field_count<T>()> collect_fields() {
    std::array<field_info, field_count<T>()> fields{};
    std::size_t index = 0;
    std::uint32_t next = 1;

    template for (constexpr std::meta::info member : members<T>()) {
        using type = typename[:std::meta::type_of(member):];
        check_member_type<type>();

        constexpr std::optional<std::uint32_t> number = tag_annotation(member);
        constexpr std::optional<std::meta::info> listed = oneof_tags_annotation(member);

        if constexpr (kind_of<type>() != field_kind::ONEOF) {
            static_assert(!listed, "pb: oneof_tags_on_non_variant: use pb::tag on a member that is not a std::variant");
            if (number)
                next = *number;
            fields[index++] = {member, next++, no_alternative};
        } else {
            static_assert(!number, "pb: tag_on_variant: use pb::oneof_tags on a std::variant member");
            constexpr std::size_t count = alternatives<type>().size() - 1;
            if constexpr (listed) {
                static_assert(std::meta::template_arguments_of(*listed).size() == count,
                              "pb: oneof_tags_count: pb::oneof_tags must list one tag for each alternative after "
                              "std::monostate");
            }

            for (std::size_t i = 0; i < count; ++i) {
                if constexpr (listed)
                    next = std::meta::extract<std::uint32_t>(std::meta::template_arguments_of(*listed)[i]);
                fields[index++] = {member, next++, i + 1};
            }
        }
    }
    return fields;
}

template <typename T>
consteval std::array<field_info, field_count<T>()> sorted_fields() {
    auto fields = collect_fields<T>();
    std::sort(fields.begin(), fields.end(),
              [](const field_info& a, const field_info& b) { return a.number < b.number; });
    return fields;
}

consteval bool has_number_in(std::span<const field_info> fields, std::uint32_t first, std::uint32_t last) {
    return std::any_of(fields.begin(), fields.end(),
                       [&](const field_info& f) { return f.number >= first && f.number <= last; });
}

/// `fields` must be sorted by number.
consteval bool has_duplicate(std::span<const field_info> fields) {
    return std::adjacent_find(fields.begin(), fields.end(), [](const field_info& a, const field_info& b) {
               return a.number == b.number;
           }) != fields.end();
}

} // namespace detail

/// Fields of the message type `T`, sorted by field number. The encoder, the
/// decoder and the schema generator use this table. An invalid tag or member
/// type is a compile error.
template <typename T>
[[nodiscard]] consteval std::span<const field_info> build_tag_table() {
    constexpr auto fields = detail::sorted_fields<T>();
    static_assert(!detail::has_number_in(fields, 0, 0), "pb: tag_zero: field number 0 is not valid");
    static_assert(!detail::has_number_in(fields, detail::max_tag + 1, std::uint32_t(-1)),
                  "pb: tag_too_large: the largest field number is 2^29 - 1");
    static_assert(!detail::has_number_in(fields, detail::reserved_first, detail::reserved_last),
                  "pb: tag_reserved: field numbers 19000 to 19999 are reserved by protobuf");
    static_assert(!detail::has_duplicate(fields), "pb: duplicate_tag: two fields have the same field number");
    return std::define_static_array(fields);
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
[[nodiscard]] consteval std::optional<std::size_t> known_size();

namespace detail {

constexpr std::size_t delimited_size(std::uint32_t number, std::size_t length) {
    return varint_size(std::uint64_t{number} << 3) + varint_size(length) + length;
}

/// Width of a scalar with a fixed wire width, else 0.
template <typename E>
consteval std::size_t fixed_width() {
    if constexpr (kind_of<E>() != field_kind::SCALAR)
        return 0;
    else if constexpr (wire_type_of<E>() == wire_type::FIXED32)
        return 4;
    else if constexpr (wire_type_of<E>() == wire_type::FIXED64)
        return 8;
    else
        return 0;
}

/// Encoded size of a field of type `M` when it does not depend on the value.
/// The encoder omits an empty `std::array`, so its size is 0.
template <typename M>
consteval std::optional<std::size_t> known_field_size(std::uint32_t number) {
    constexpr field_kind kind = kind_of<M>();
    if constexpr (kind == field_kind::FIXED_BYTES) {
        constexpr std::size_t length = std::tuple_size_v<M>;
        return length == 0 ? 0 : delimited_size(number, length);
    } else if constexpr (kind == field_kind::MESSAGE) {
        constexpr std::optional<std::size_t> length = known_size<M>();
        if (!length)
            return std::nullopt;
        return delimited_size(number, *length);
    } else if constexpr (kind == field_kind::REPEATED && is_instance_of<M>(^^std::array)) {
        constexpr std::size_t width = fixed_width<typename M::value_type>();
        constexpr std::size_t length = width * std::tuple_size_v<M>;
        if (width == 0)
            return std::nullopt;
        return length == 0 ? 0 : delimited_size(number, length);
    } else {
        return std::nullopt;
    }
}

} // namespace detail

/// Encoded size of each message of type `T`, when all messages of the type have
/// the same size, else `std::nullopt`. Fields with implicit presence can be
/// omitted, so only fixed-width arrays, fixed-length bytes and nested messages of
/// known size give a known size.
template <typename T>
[[nodiscard]] consteval std::optional<std::size_t> known_size() {
    std::size_t total = 0;
    template for (constexpr field_info field : build_tag_table<T>()) {
        constexpr std::optional<std::size_t> size =
            detail::known_field_size<typename[:std::meta::type_of(field.member):]>(field.number);
        if (!size)
            return std::nullopt;
        total += *size;
    }
    return total;
}

} // namespace pb
