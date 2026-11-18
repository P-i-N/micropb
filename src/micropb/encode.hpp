#pragma once

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

/// Value of a scalar as the wire carries it: the varint value, or the bits of a
/// fixed-width value. A negative integer sign-extends to 64 bits. An enum is
/// `int32`, so an unsigned enum value of 2^31 or more is negative. Zero bits
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
    else if constexpr (std::is_enum_v<M>)
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
    else
        return static_cast<std::uint64_t>(value);
}

/// Writes the value of a scalar without a key.
template <typename M>
constexpr void emit_value(writer& sink, const M& value) {
    constexpr wire_type type = wire_type_of<M>();
    const std::uint64_t bits = scalar_bits(value);

    if constexpr (type == wire_type::FIXED32)
        sink.write_fixed32(static_cast<std::uint32_t>(bits));
    else if constexpr (type == wire_type::FIXED64)
        sink.write_fixed64(bits);
    else
        sink.write_varint(bits);
}

/// Writes a string or bytes field: key, length, then the items of `bytes`.
template <typename B>
constexpr void emit_delimited(writer& sink, std::uint32_t number, const B& bytes) {
    sink.write_key(number, wire_type::LENGTH_DELIMITED);
    sink.write_varint(bytes.size());
    sink.write_bytes(bytes);
}

template <typename T>
constexpr void emit_message(writer& sink, const T& message);

/// Size of the values of a packed field, without key and length prefix.
template <typename E, typename R>
[[nodiscard]] constexpr std::size_t packed_size(const R& values) {
    constexpr std::size_t width = fixed_width<E>();
    if constexpr (width != 0) {
        return width * values.size();
    } else {
        writer size;
        for (const E& value : values)
            emit_value(size, value);
        return size.size();
    }
}

/// Encoded size of the fields of `message`, without key and length prefix.
/// A writer without a span only counts, so the size and the bytes come from
/// one code path.
template <typename T>
[[nodiscard]] constexpr std::size_t message_size(const T& message) {
    writer size;
    emit_message(size, message);
    return size.size();
}

/// Writes one field to `sink`. `always` is `true` for explicit presence: the
/// field is written also when its value is the default value.
template <typename M>
constexpr void emit_field(writer& sink, std::uint32_t number, const M& value, bool always) {
    constexpr field_kind kind = kind_of<M>();

    if constexpr (kind == field_kind::SCALAR) {
        if (!always && scalar_bits(value) == 0)
            return;

        sink.write_key(number, wire_type_of<M>());
        emit_value(sink, value);
    } else if constexpr (kind == field_kind::STRING) {
        if (always || !value.empty())
            emit_delimited(sink, number, value);
    } else if constexpr (kind == field_kind::BYTES) {
        const std::span<const std::uint8_t> bytes(value.begin(), value.end());
        if (always || !bytes.empty())
            emit_delimited(sink, number, bytes);
    } else if constexpr (kind == field_kind::FIXED_BYTES) {
        if (always || !value.empty())
            emit_delimited(sink, number, value);
    } else if constexpr (kind == field_kind::MESSAGE) {
        constexpr std::optional<std::size_t> known = known_size<M>();
        sink.write_key(number, wire_type::LENGTH_DELIMITED);

        if (!sink.counts_only()) {
            // The length prefix comes before the fields, so measure them first.
            sink.write_varint(known ? *known : message_size(value));
            emit_message(sink, value);
        } else if constexpr (known) {
            sink.write_varint(*known);
            sink.advance(*known);
        } else {
            // No byte is written, so the order is free: count the fields, then
            // the length prefix. This walks the message only one time.
            const std::size_t start = sink.size();
            emit_message(sink, value);
            sink.advance(varint_size(sink.size() - start));
        }
    } else if constexpr (kind == field_kind::OPTIONAL) {
        if (value)
            emit_field(sink, number, *value, true);
    } else if constexpr (kind == field_kind::REPEATED) {
        using element = element_t<M>;
        if (value.size() == 0)
            return;

        if constexpr (is_packed<element>()) {
            sink.write_key(number, wire_type::LENGTH_DELIMITED);
            sink.write_varint(packed_size<element>(value));
            for (const element& item : value)
                emit_value(sink, item);
        } else {
            for (const element& item : value)
                emit_field(sink, number, item, true);
        }
    } else {
        static_assert(false, "pb: encode: a oneof is written by emit_message");
    }
}

/// Writes the fields of `message` in ascending field number.
template <typename T>
constexpr void emit_message(writer& sink, const T& message) {
    template for (constexpr field_info field : build_tag_table<T>()) {
        // `field` has a type that exists only at compile time. Run-time code
        // must use a copy of the number, or the function becomes `consteval`.
        constexpr std::uint32_t number = field.number;
        using member = typename[:std::meta::type_of(field.member):];

        if constexpr (field.alternative == no_alternative) {
            emit_field<member>(sink, number, message.[:field.member:], false);
        } else {
            if (const auto* active = std::get_if<field.alternative>(&message.[:field.member:]))
                emit_field(sink, number, *active, true);
        }
    }
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
    detail::emit_message(sink, message);
    if (!sink.fits())
        return std::unexpected(error::BUFFER_TOO_SMALL);
    return sink.size();
}

} // namespace pb
