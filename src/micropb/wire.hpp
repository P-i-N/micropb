#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include <micropb/types.hpp>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Number of bytes that `value` uses as a varint (1 to 10).
[[nodiscard]] constexpr std::size_t varint_size(std::uint64_t value) {
    std::size_t size = 1;
    while (value >= 0x80) {
        value >>= 7;
        ++size;
    }
    return size;
}

[[nodiscard]] constexpr std::uint32_t zigzag_encode(std::int32_t value) {
    return (static_cast<std::uint32_t>(value) << 1) ^ static_cast<std::uint32_t>(value >> 31);
}

[[nodiscard]] constexpr std::uint64_t zigzag_encode(std::int64_t value) {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

[[nodiscard]] constexpr std::int32_t zigzag_decode(std::uint32_t value) {
    return static_cast<std::int32_t>((value >> 1) ^ (~(value & 1) + 1));
}

[[nodiscard]] constexpr std::int64_t zigzag_decode(std::uint64_t value) {
    return static_cast<std::int64_t>((value >> 1) ^ (~(value & 1) + 1));
}

////////////////////////////////////////////////////////////////////////////////

/// Field number and wire type of one field in the encoded stream.
struct field_key {
    std::uint32_t number;
    wire_type type;
};

////////////////////////////////////////////////////////////////////////////////

/// Writes wire primitives into a fixed buffer. A write that does not fit
/// returns `false` and writes nothing. The writer never writes past the span.
struct writer {
    constexpr explicit writer(std::span<std::uint8_t> out) : _out(out) {}

    [[nodiscard]] constexpr bool write_varint(std::uint64_t value) {
        if (varint_size(value) > _out.size() - _pos)
            return false;

        while (value >= 0x80) {
            _out[_pos++] = static_cast<std::uint8_t>(value | 0x80);
            value >>= 7;
        }
        _out[_pos++] = static_cast<std::uint8_t>(value);
        return true;
    }

    [[nodiscard]] constexpr bool write_fixed32(std::uint32_t value) { return write_le(value, 4); }

    [[nodiscard]] constexpr bool write_fixed64(std::uint64_t value) { return write_le(value, 8); }

    [[nodiscard]] constexpr bool write_bytes(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > _out.size() - _pos)
            return false;

        for (std::uint8_t byte : bytes)
            _out[_pos++] = byte;
        return true;
    }

    [[nodiscard]] constexpr bool write_bytes(std::string_view text) {
        if (text.size() > _out.size() - _pos)
            return false;

        for (char c : text)
            _out[_pos++] = static_cast<std::uint8_t>(c);
        return true;
    }

    [[nodiscard]] constexpr bool write_key(std::uint32_t number, wire_type type) {
        return write_varint((static_cast<std::uint64_t>(number) << 3) | static_cast<std::uint64_t>(type));
    }

    [[nodiscard]] constexpr std::size_t size() const { return _pos; }

private:
    [[nodiscard]] constexpr bool write_le(std::uint64_t value, std::size_t count) {
        if (count > _out.size() - _pos)
            return false;

        for (std::size_t i = 0; i < count; ++i)
            _out[_pos++] = static_cast<std::uint8_t>(value >> (8 * i));
        return true;
    }

    std::span<std::uint8_t> _out;
    std::size_t _pos = 0;
};

////////////////////////////////////////////////////////////////////////////////

/// Reads wire primitives from a byte span. Every read checks the remaining
/// bytes first, so the reader never reads past the span.
struct reader {
    /// Largest field number of proto3.
    static constexpr std::uint32_t max_field_number = (1u << 29) - 1;

    constexpr explicit reader(std::span<const std::uint8_t> in) : _in(in) {}

    [[nodiscard]] constexpr std::expected<std::uint64_t, error> read_varint() {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < 10; ++i) {
            if (_pos + i >= _in.size())
                return std::unexpected(error::TRUNCATED);

            const std::uint8_t byte = _in[_pos + i];
            if (i == 9 && byte > 1)
                return std::unexpected(error::INVALID_VARINT);

            value |= static_cast<std::uint64_t>(byte & 0x7F) << (7 * i);
            if ((byte & 0x80) == 0) {
                _pos += i + 1;
                return value;
            }
        }
        return std::unexpected(error::INVALID_VARINT);
    }

    [[nodiscard]] constexpr std::expected<std::uint32_t, error> read_fixed32() {
        if (remaining() < 4)
            return std::unexpected(error::TRUNCATED);
        return static_cast<std::uint32_t>(read_le(4));
    }

    [[nodiscard]] constexpr std::expected<std::uint64_t, error> read_fixed64() {
        if (remaining() < 8)
            return std::unexpected(error::TRUNCATED);
        return read_le(8);
    }

    /// Returns a view into the input. Does not copy.
    [[nodiscard]] constexpr std::expected<std::span<const std::uint8_t>, error> read_length_delimited() {
        const auto length = read_varint();
        if (!length)
            return std::unexpected(length.error());

        // Compare with the remaining bytes before any addition: no overflow.
        if (*length > remaining())
            return std::unexpected(error::TRUNCATED);

        const auto bytes = _in.subspan(_pos, static_cast<std::size_t>(*length));
        _pos += bytes.size();
        return bytes;
    }

    [[nodiscard]] constexpr std::expected<field_key, error> read_key() {
        const auto key = read_varint();
        if (!key)
            return std::unexpected(key.error());

        const std::uint64_t number = *key >> 3;
        if (number == 0 || number > max_field_number)
            return std::unexpected(error::INVALID_TAG);

        const auto n = static_cast<std::uint32_t>(number);
        switch (*key & 7) {
        case 0: return field_key{n, wire_type::VARINT};
        case 1: return field_key{n, wire_type::FIXED64};
        case 2: return field_key{n, wire_type::LENGTH_DELIMITED};
        case 5: return field_key{n, wire_type::FIXED32};
        default: return std::unexpected(error::INVALID_WIRE_TYPE);
        }
    }

    [[nodiscard]] constexpr std::expected<void, error> skip(wire_type type) {
        switch (type) {
        case wire_type::VARINT: {
            const auto value = read_varint();
            if (!value)
                return std::unexpected(value.error());
            return {};
        }
        case wire_type::FIXED32: return skip_bytes(4);
        case wire_type::FIXED64: return skip_bytes(8);
        case wire_type::LENGTH_DELIMITED: {
            const auto bytes = read_length_delimited();
            if (!bytes)
                return std::unexpected(bytes.error());
            return {};
        }
        }
        return std::unexpected(error::INVALID_WIRE_TYPE);
    }

    [[nodiscard]] constexpr bool empty() const { return _pos == _in.size(); }

    [[nodiscard]] constexpr std::size_t position() const { return _pos; }

private:
    [[nodiscard]] constexpr std::size_t remaining() const { return _in.size() - _pos; }

    [[nodiscard]] constexpr std::uint64_t read_le(std::size_t count) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i)
            value |= static_cast<std::uint64_t>(_in[_pos + i]) << (8 * i);
        _pos += count;
        return value;
    }

    [[nodiscard]] constexpr std::expected<void, error> skip_bytes(std::size_t count) {
        if (remaining() < count)
            return std::unexpected(error::TRUNCATED);
        _pos += count;
        return {};
    }

    std::span<const std::uint8_t> _in;
    std::size_t _pos = 0;
};

} // namespace pb
