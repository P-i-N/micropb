#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include <micropb/meta_schema.hpp>
#include <micropb/types.hpp>
#include <micropb/wire.hpp>

namespace pb {

namespace detail {

////////////////////////////////////////////////////////////////////////////////

template <typename M>
constexpr void reset_value(M& value);

/// Resets each member of `message`, also in a nested message. A `span_buffer`
/// has no default value, so it keeps its storage and only sets its count to 0.
template <typename T>
constexpr void reset_message(T& message) {
    template for (constexpr std::meta::info member : members<T>())
        reset_value(message.[:member:]);
}

template <typename M>
constexpr void reset_value(M& value) {
    if constexpr (is_instance_of<M>(^^span_buffer))
        value.clear();
    else if constexpr (kind_of<M>() == field_kind::MESSAGE)
        reset_message(value);
    else
        value = M{};
}

////////////////////////////////////////////////////////////////////////////////

/// Scalar from the value that the wire carries. A value that is too wide for
/// `M` loses its upper bits, as in the Google C++ runtime. An enum is `int32`.
template <typename M>
[[nodiscard]] constexpr M scalar_from_bits(std::uint64_t bits) {
    if constexpr (std::is_same_v<M, sint32>)
        return M(zigzag_decode(static_cast<std::uint32_t>(bits)));
    else if constexpr (std::is_same_v<M, sint64>)
        return M(zigzag_decode(bits));
    else if constexpr (is_one_of<M, fixed32, fixed64, sfixed32, sfixed64>)
        return M(static_cast<decltype(M::value)>(bits));
    else if constexpr (std::is_same_v<M, float>)
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    else if constexpr (std::is_same_v<M, double>)
        return std::bit_cast<double>(bits);
    else if constexpr (std::is_same_v<M, bool>)
        return bits != 0;
    else if constexpr (std::is_enum_v<M>)
        return static_cast<M>(static_cast<std::int32_t>(bits));
    else
        return static_cast<M>(bits);
}

/// Reads the value of a scalar without a key. Returns the scalar of 0 when the
/// read fails.
template <typename M>
[[nodiscard]] constexpr M read_scalar(reader& in) {
    constexpr wire_type type = wire_type_of<M>();

    if constexpr (type == wire_type::FIXED32)
        return scalar_from_bits<M>(in.read_fixed32());
    else if constexpr (type == wire_type::FIXED64)
        return scalar_from_bits<M>(in.read_fixed64());
    else
        return scalar_from_bits<M>(in.read_varint());
}

template <typename T>
constexpr void decode_message(reader& in, T& message);

template <typename M>
constexpr void decode_field(reader& in, wire_type type, M& value);

/// Adds `item` to `sequence`, but not after a failed read. A full sequence is
/// an error.
template <typename S, typename E>
constexpr void append(reader& in, S& sequence, const E& item) {
    if (!in.failed() && !sequence.push_back(item))
        in.fail(error::CAPACITY_EXCEEDED);
}

/// Reads one record of a repeated field into `sequence`. A scalar element can
/// be a packed record or a single value. Any other element is one record, which
/// starts from a value-initialized element: records do not merge.
template <typename S>
constexpr void decode_repeated(reader& in, wire_type type, S& sequence) {
    if constexpr (is_instance_of<S>(^^std::array) || is_instance_of<S>(^^std::span)) {
        static_assert(false, "pb: decode_encode_only: std::array and std::span<const T> are encode-only, "
                             "use pb::repeated or pb::span_buffer to decode");
    } else if constexpr (kind_of<element_t<S>>() == field_kind::SCALAR) {
        using element = element_t<S>;

        if (type == wire_type::LENGTH_DELIMITED) {
            reader packed(in.read_length_delimited());
            while (!packed.empty())
                append(packed, sequence, read_scalar<element>(packed));
            in.absorb(packed);
        } else if (type == wire_type_of<element>()) {
            append(in, sequence, read_scalar<element>(in));
        } else {
            in.skip(type);
        }
    } else {
        if (type != wire_type::LENGTH_DELIMITED)
            return in.skip(type);

        element_t<S> item{};
        decode_field(in, type, item);
        append(in, sequence, item);
    }
}

/// Reads the value of one field into `value`. A field with the wrong wire type
/// is skipped, as an unknown field. A read that fails does not change a scalar,
/// a string or a bytes field.
template <typename M>
constexpr void decode_field(reader& in, wire_type type, M& value) {
    constexpr field_kind kind = kind_of<M>();

    if constexpr (kind == field_kind::SCALAR) {
        if (type != wire_type_of<M>())
            return in.skip(type);

        const M scalar = read_scalar<M>(in);
        if (!in.failed())
            value = scalar;
    } else if constexpr (kind == field_kind::OPTIONAL) {
        if (type != wire_type_of<M>())
            return in.skip(type);

        if (!value)
            value.emplace();
        decode_field(in, type, *value);
    } else if constexpr (kind == field_kind::REPEATED) {
        decode_repeated(in, type, value);
    } else if constexpr (kind == field_kind::ONEOF) {
        static_assert(false, "pb: decode: a oneof is decoded by its alternatives, not as one field");
    } else {
        if (type != wire_type::LENGTH_DELIMITED)
            return in.skip(type);

        const std::span<const std::uint8_t> bytes = in.read_length_delimited();
        if (in.failed())
            return;

        if constexpr (kind == field_kind::STRING) {
            value = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else if constexpr (kind == field_kind::BYTES) {
            if constexpr (is_instance_of<M>(^^std::span)) {
                value = bytes;
            } else {
                if (bytes.size() > value.capacity())
                    return in.fail(error::CAPACITY_EXCEEDED);

                value.clear();
                for (const std::uint8_t byte : bytes)
                    static_cast<void>(value.push_back(byte));
            }
        } else if constexpr (kind == field_kind::FIXED_BYTES) {
            if (bytes.size() > std::tuple_size_v<M>)
                return in.fail(error::CAPACITY_EXCEEDED);

            value = {};
            std::copy(bytes.begin(), bytes.end(), value.begin());
        } else {
            reader part(bytes);
            decode_message(part, value);
            in.absorb(part);
        }
    }
}

/// Reads the value of alternative `I` of `variant`. A different active
/// alternative gives way to a value-initialized one. The same alternative stays,
/// so a message merges. A wrong wire type is skipped and keeps the variant.
template <std::size_t I, typename V>
constexpr void decode_alternative(reader& in, wire_type type, V& variant) {
    using alternative = std::variant_alternative_t<I, V>;
    if (type != wire_type_of<alternative>())
        return in.skip(type);

    if (variant.index() != I)
        variant.template emplace<I>();
    decode_field(in, type, *std::get_if<I>(&variant));
}

/// Reads the value of the field `key` into `message`. A field number that the
/// message does not have is skipped.
template <typename T>
constexpr void decode_known(reader& in, field_key key, T& message) {
    template for (constexpr field_info field : build_tag_table<T>()) {
        // `field` has a type that exists only at compile time. Run-time code
        // must use a copy of the number, or the function becomes `consteval`.
        constexpr std::uint32_t number = field.number;

        if (key.number == number) {
            if constexpr (field.alternative == no_alternative)
                return decode_field(in, key.type, message.[:field.member:]);
            else
                return decode_alternative<field.alternative>(in, key.type, message.[:field.member:]);
        }
    }
    in.skip(key.type);
}

/// Reads all fields of `in` into `message`, until the end of the input or the
/// first error. Does not reset `message`: a field that occurs again changes the
/// value that is there.
template <typename T>
constexpr void decode_message(reader& in, T& message) {
    while (!in.empty()) {
        const field_key key = in.read_key();
        if (in.failed())
            return;

        decode_known(in, key, message);
    }
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Decodes the proto3 message in `in` into `msg` and returns `in.size()`. Resets
/// `msg` first. A `pb::span_buffer` keeps its storage. On error, `msg` holds the
/// fields that were read before the error. A `std::string_view` or a
/// `std::span` of `msg` points into `in`.
/// Decoding a `std::string_view` field is not `constexpr`: C++ does not allow a
/// zero-copy character view of byte input in constant evaluation.
template <typename T>
[[nodiscard]] constexpr std::expected<std::size_t, error> decode(std::span<const std::uint8_t> in, T& msg) {
    detail::reset_message(msg);

    reader source(in);
    detail::decode_message(source, msg);
    if (source.failed())
        return std::unexpected(source.status().error());
    return in.size();
}

} // namespace pb
