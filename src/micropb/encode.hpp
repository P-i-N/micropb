#pragma once

#include <micropb/meta_schema.hpp>
#include <micropb/types.hpp>
#include <micropb/wire.hpp>

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

/// Sink with the interface of `writer` that only counts bytes. The encoder
/// uses one code path for the size and for the bytes.
struct counter {
    [[nodiscard]] constexpr bool write_varint(std::uint64_t value) { return add(varint_size(value)); }
    [[nodiscard]] constexpr bool write_fixed32(std::uint32_t) { return add(4); }
    [[nodiscard]] constexpr bool write_fixed64(std::uint64_t) { return add(8); }
    [[nodiscard]] constexpr bool write_bytes(std::span<const std::uint8_t> bytes) { return add(bytes.size()); }
    [[nodiscard]] constexpr bool write_bytes(std::string_view text) { return add(text.size()); }

    [[nodiscard]] constexpr bool write_key(std::uint32_t number, wire_type type) {
        return write_varint((static_cast<std::uint64_t>(number) << 3) | static_cast<std::uint64_t>(type));
    }

    /// Adds the size of a part that the caller already measured.
    [[nodiscard]] constexpr bool add(std::size_t count) {
        _size += count;
        return true;
    }

    [[nodiscard]] constexpr std::size_t size() const { return _size; }

private:
    std::size_t _size = 0;
};

////////////////////////////////////////////////////////////////////////////////

/// Value of a scalar as the wire carries it: the varint value, or the bits of a
/// fixed-width value. A negative integer sign-extends to 64 bits. Zero bits
/// mean the default value, also for `-0.0`, which has a sign bit.
template <typename M>
[[nodiscard]] constexpr std::uint64_t scalar_bits(const M& value) {
    if constexpr (is_one_of<M, sint32, sint64>)
        return zigzag_encode(value.value);
    else if constexpr (is_one_of<M, fixed32, fixed64, sfixed32, sfixed64>)
        return static_cast<std::uint64_t>(value.value);
    else if constexpr (std::is_same_v<M, float>)
        return std::bit_cast<std::uint32_t>(value);
    else if constexpr (std::is_same_v<M, double>)
        return std::bit_cast<std::uint64_t>(value);
    else
        return static_cast<std::uint64_t>(value);
}

template <typename S, typename M>
[[nodiscard]] constexpr bool emit_scalar(S& sink, std::uint32_t number, const M& value) {
    constexpr wire_type type = wire_type_of<M>();
    const std::uint64_t bits = scalar_bits(value);

    if (!sink.write_key(number, type))
        return false;
    if constexpr (type == wire_type::FIXED32)
        return sink.write_fixed32(static_cast<std::uint32_t>(bits));
    else if constexpr (type == wire_type::FIXED64)
        return sink.write_fixed64(bits);
    else
        return sink.write_varint(bits);
}

template <typename S, typename B>
[[nodiscard]] constexpr bool emit_delimited(S& sink, std::uint32_t number, std::size_t size, const B& bytes) {
    return sink.write_key(number, wire_type::LENGTH_DELIMITED) && sink.write_varint(size) && sink.write_bytes(bytes);
}

template <typename S, typename T>
[[nodiscard]] constexpr bool emit_message(S& sink, const T& message);

/// Encoded size of the fields of `message`, without key and length prefix.
template <typename T>
[[nodiscard]] constexpr std::size_t message_size(const T& message) {
    counter size;
    static_cast<void>(emit_message(size, message));
    return size.size();
}

/// Writes one field to `sink`. `always` is `true` for explicit presence: the
/// field is written also when its value is the default value.
template <typename S, typename M>
[[nodiscard]] constexpr bool emit_field(S& sink, std::uint32_t number, const M& value, bool always) {
    constexpr field_kind kind = kind_of<M>();

    if constexpr (kind == field_kind::SCALAR) {
        if (!always && scalar_bits(value) == 0)
            return true;
        return emit_scalar(sink, number, value);
    } else if constexpr (kind == field_kind::STRING) {
        const std::string_view text = value;
        if (!always && text.empty())
            return true;
        return emit_delimited(sink, number, text.size(), text);
    } else if constexpr (kind == field_kind::BYTES) {
        const std::span<const std::uint8_t> bytes(value.begin(), value.end());
        if (!always && bytes.empty())
            return true;
        return emit_delimited(sink, number, bytes.size(), bytes);
    } else if constexpr (kind == field_kind::FIXED_BYTES) {
        constexpr std::size_t length = std::tuple_size_v<M>;
        if (!always && length == 0)
            return true;
        return emit_delimited(sink, number, length, std::span<const std::uint8_t>(value));
    } else if constexpr (kind == field_kind::MESSAGE) {
        constexpr std::optional<std::size_t> known = known_size<M>();
        const std::size_t length = known ? *known : message_size(value);

        if (!sink.write_key(number, wire_type::LENGTH_DELIMITED) || !sink.write_varint(length))
            return false;
        if constexpr (std::is_same_v<S, counter>)
            return sink.add(length);
        else
            return emit_message(sink, value);
    } else if constexpr (kind == field_kind::OPTIONAL) {
        if (!value)
            return true;
        return emit_field(sink, number, *value, true);
    } else {
        static_assert(false, "pb: encode: repeated fields and oneofs are not supported yet");
    }
}

/// Writes the fields of `message` in ascending field number.
template <typename S, typename T>
[[nodiscard]] constexpr bool emit_message(S& sink, const T& message) {
    template for (constexpr field_info field : build_tag_table<T>()) {
        using member = typename[:std::meta::type_of(field.member):];
        if (!emit_field<S, member>(sink, field.number, message.[:field.member:], false))
            return false;
    }
    return true;
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Size in bytes of the encoding of `message`.
template <typename T>
[[nodiscard]] constexpr std::size_t encoded_size(const T& message) {
    return detail::message_size(message);
}

/// Encodes `message` into `out` in proto3 wire format and returns the number of
/// bytes written. Does not write past `out`. When `out` is too small, returns
/// `error::BUFFER_TOO_SMALL`; the bytes of `out` that it wrote are not valid.
template <typename T>
[[nodiscard]] constexpr std::expected<std::size_t, error> encode(const T& message, std::span<std::uint8_t> out) {
    writer sink(out);
    if (!detail::emit_message(sink, message))
        return std::unexpected(error::BUFFER_TOO_SMALL);
    return sink.size();
}

} // namespace pb
