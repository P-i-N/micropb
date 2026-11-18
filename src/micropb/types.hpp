#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Error codes of the codec. Functions return them as `std::expected` values.
enum class error {
    BUFFER_TOO_SMALL,
    TRUNCATED,
    INVALID_VARINT,
    INVALID_WIRE_TYPE,
    INVALID_TAG,
    CAPACITY_EXCEEDED,
};

/// Wire types of proto3 that this library supports. Group types (3, 4) are not supported.
enum class wire_type : std::uint8_t {
    VARINT = 0,
    FIXED64 = 1,
    LENGTH_DELIMITED = 2,
    FIXED32 = 5,
};

////////////////////////////////////////////////////////////////////////////////

/// Integer encoding on the wire: zigzag varint or fixed width.
enum class int_encoding { ZIGZAG, FIXED };

/// Integer field with a fixed wire encoding. Converts implicitly to and from `T`.
template <typename T, int_encoding E>
struct wire_int {
    constexpr wire_int() = default;
    constexpr wire_int(T v) : value(v) {}
    constexpr operator T() const { return value; }

    T value{};
};

using sint32 = wire_int<std::int32_t, int_encoding::ZIGZAG>;
using sint64 = wire_int<std::int64_t, int_encoding::ZIGZAG>;
using fixed32 = wire_int<std::uint32_t, int_encoding::FIXED>;
using fixed64 = wire_int<std::uint64_t, int_encoding::FIXED>;
using sfixed32 = wire_int<std::int32_t, int_encoding::FIXED>;
using sfixed64 = wire_int<std::int64_t, int_encoding::FIXED>;

/// Field number of a message field, used as a compile-time value.
struct tag {
    std::uint32_t value;
};

/// Field numbers that belong to one oneof group.
template <std::uint32_t... Tags>
struct oneof_tags {};

////////////////////////////////////////////////////////////////////////////////

/// Sequence of up to `N` items stored inline. Does not allocate.
template <typename T, std::size_t N>
struct repeated {
    [[nodiscard]] constexpr std::size_t size() const { return _count; }
    [[nodiscard]] constexpr std::size_t capacity() const { return N; }
    [[nodiscard]] constexpr bool empty() const { return _count == 0; }

    constexpr T* begin() { return _items.data(); }
    constexpr T* end() { return _items.data() + _count; }
    constexpr const T* begin() const { return _items.data(); }
    constexpr const T* end() const { return _items.data() + _count; }

    constexpr T& operator[](std::size_t index) { return _items[index]; }
    constexpr const T& operator[](std::size_t index) const { return _items[index]; }

    /// Returns `false` and does not add `item` when the sequence is full.
    [[nodiscard]] constexpr bool push_back(const T& item) {
        if (_count == N)
            return false;

        _items[_count++] = item;
        return true;
    }

    constexpr void clear() { _count = 0; }

private:
    std::array<T, N> _items{};
    std::size_t _count = 0;
};

/// Sequence that stores its items in caller-owned storage. The storage must
/// outlive the buffer. Does not allocate.
template <typename T>
struct span_buffer {
    constexpr explicit span_buffer(std::span<T> storage) : _storage(storage) {}

    [[nodiscard]] constexpr std::size_t size() const { return _count; }
    [[nodiscard]] constexpr std::size_t capacity() const { return _storage.size(); }
    [[nodiscard]] constexpr bool empty() const { return _count == 0; }

    constexpr T* begin() { return _storage.data(); }
    constexpr T* end() { return _storage.data() + _count; }
    constexpr const T* begin() const { return _storage.data(); }
    constexpr const T* end() const { return _storage.data() + _count; }

    constexpr T& operator[](std::size_t index) { return _storage[index]; }
    constexpr const T& operator[](std::size_t index) const { return _storage[index]; }

    /// Returns `false` and does not add `item` when the storage is full.
    [[nodiscard]] constexpr bool push_back(const T& item) {
        if (_count == _storage.size())
            return false;

        _storage[_count++] = item;
        return true;
    }

    constexpr void clear() { _count = 0; }

private:
    std::span<T> _storage;
    std::size_t _count = 0;
};

} // namespace pb
