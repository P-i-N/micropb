#pragma once

#include <micropb/meta_schema.hpp>
#include <micropb/types.hpp>
#include <micropb/wire.hpp>

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
/// `M` loses its upper bits, as in the Google C++ runtime.
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
        return static_cast<M>(static_cast<std::underlying_type_t<M>>(bits));
    else
        return static_cast<M>(bits);
}

template <typename M>
[[nodiscard]] constexpr std::expected<M, error> read_scalar(reader& in) {
    constexpr wire_type type = wire_type_of<M>();

    if constexpr (type == wire_type::FIXED32) {
        const auto bits = in.read_fixed32();
        if (!bits)
            return std::unexpected(bits.error());
        return scalar_from_bits<M>(*bits);
    } else if constexpr (type == wire_type::FIXED64) {
        const auto bits = in.read_fixed64();
        if (!bits)
            return std::unexpected(bits.error());
        return scalar_from_bits<M>(*bits);
    } else {
        const auto bits = in.read_varint();
        if (!bits)
            return std::unexpected(bits.error());
        return scalar_from_bits<M>(*bits);
    }
}

template <typename T>
[[nodiscard]] constexpr std::expected<void, error> decode_message(std::span<const std::uint8_t> bytes, T& message);

/// Reads the value of one field into `value`. A field with the wrong wire type
/// is skipped, as an unknown field.
template <typename M>
[[nodiscard]] constexpr std::expected<void, error> decode_field(reader& in, wire_type type, M& value) {
    constexpr field_kind kind = kind_of<M>();

    if constexpr (kind == field_kind::SCALAR) {
        if (type != wire_type_of<M>())
            return in.skip(type);

        const auto scalar = read_scalar<M>(in);
        if (!scalar)
            return std::unexpected(scalar.error());
        value = *scalar;
        return {};
    } else if constexpr (kind == field_kind::OPTIONAL) {
        if (type != wire_type_of<M>())
            return in.skip(type);

        if (!value)
            value.emplace();
        return decode_field(in, type, *value);
    } else if constexpr (kind == field_kind::REPEATED || kind == field_kind::ONEOF) {
        static_assert(false, "pb: decode: repeated fields and oneofs are not supported yet");
    } else {
        if (type != wire_type::LENGTH_DELIMITED)
            return in.skip(type);

        const auto bytes = in.read_length_delimited();
        if (!bytes)
            return std::unexpected(bytes.error());

        if constexpr (kind == field_kind::STRING) {
            value = std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        } else if constexpr (kind == field_kind::BYTES) {
            if constexpr (is_instance_of<M>(^^std::span)) {
                value = *bytes;
            } else {
                if (bytes->size() > value.capacity())
                    return std::unexpected(error::CAPACITY_EXCEEDED);
                value.clear();
                for (const std::uint8_t byte : *bytes)
                    static_cast<void>(value.push_back(byte));
            }
        } else if constexpr (kind == field_kind::FIXED_BYTES) {
            if (bytes->size() > std::tuple_size_v<M>)
                return std::unexpected(error::CAPACITY_EXCEEDED);
            value = {};
            std::copy(bytes->begin(), bytes->end(), value.begin());
        } else {
            return decode_message(*bytes, value);
        }
        return {};
    }
}

/// Reads the value of the field `key` into `message`. A field number that the
/// message does not have is skipped.
template <typename T>
[[nodiscard]] constexpr std::expected<void, error> decode_known(reader& in, field_key key, T& message) {
    template for (constexpr field_info field : build_tag_table<T>()) {
        if (field.number == key.number) {
            static_assert(field.alternative == no_alternative, "pb: decode: oneofs are not supported yet");
            return decode_field(in, key.type, message.[:field.member:]);
        }
    }
    return in.skip(key.type);
}

/// Reads all fields of `bytes` into `message`. Does not reset `message`: a field
/// that occurs again changes the value that is there.
template <typename T>
[[nodiscard]] constexpr std::expected<void, error> decode_message(std::span<const std::uint8_t> bytes, T& message) {
    reader in(bytes);
    while (!in.empty()) {
        const auto key = in.read_key();
        if (!key)
            return std::unexpected(key.error());

        const auto status = decode_known(in, *key, message);
        if (!status)
            return std::unexpected(status.error());
    }
    return {};
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Decodes the proto3 message in `in` into `msg` and returns `in.size()`. Resets
/// `msg` first. A `pb::span_buffer` keeps its storage. On error, `msg` holds the
/// fields that were read before the error. A `std::string_view` or a
/// `std::span` of `msg` points into `in`.
template <typename T>
[[nodiscard]] constexpr std::expected<std::size_t, error> decode(std::span<const std::uint8_t> in, T& msg) {
    detail::reset_message(msg);

    const auto status = detail::decode_message(in, msg);
    if (!status)
        return std::unexpected(status.error());
    return in.size();
}

} // namespace pb
