#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Error codes of the codec. Functions return them as `std::expected` values.
enum class error
{
	/// encode: the output span is too short.
	BUFFER_TOO_SMALL,
	/// decode: the input ends in a field, or a length prefix is larger than the remaining input.
	TRUNCATED,
	/// decode: a varint is longer than 10 bytes or has more than 64 bits.
	INVALID_VARINT,
	/// decode: a key has wire type 3, 4, 6 or 7.
	INVALID_WIRE_TYPE,
	/// decode: a key has field number 0 or a field number above 2^29 - 1.
	INVALID_TAG,
	/// decode: a field has more elements or bytes than its container holds.
	CAPACITY_EXCEEDED,
};

/// Wire types of proto3 that this library supports. Group types (3, 4) are not supported.
enum class wire_type : std::uint8_t
{
	VARINT = 0,
	FIXED64 = 1,
	LENGTH_DELIMITED = 2,
	FIXED32 = 5,
};

////////////////////////////////////////////////////////////////////////////////

/// Integer encoding on the wire: zigzag varint or fixed width.
enum class int_encoding
{
	ZIGZAG,
	FIXED
};

/// Integer field with a fixed wire encoding. Converts implicitly to and from `T`.
template <typename T, int_encoding E>
struct wire_int
{
	constexpr wire_int() = default;
	constexpr wire_int( T v ) : value( v ) {}
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
struct tag
{
	std::uint32_t value;
};

/// Field numbers that belong to one oneof group.
template <std::uint32_t... Tags>
struct oneof_tags
{};

////////////////////////////////////////////////////////////////////////////////

/// One field of a message: the type `M` of the member, the field number and
/// the offset of the member in bytes. For an alternative of a `std::variant`
/// member, `Alternative` is the index of the alternative, which is 1 or more.
template <typename M, std::uint32_t Number, std::size_t Offset, std::size_t Alternative = 0>
struct field_at
{};

/// The fields of the message type `T`, in any order.
template <typename T, typename... Fields>
struct field_list
{};

/// The field list of the message type `T`: a struct that derives from
/// `pb::field_list<T, ...>`. A compiler with reflection makes it from the
/// members of `T`. For a compiler without reflection, protoc-gen-micropb or a
/// person writes a specialization:
///
///     template <>
///     struct pb::fields_of<ping> : pb::field_list<ping,
///         pb::field_at<decltype(ping::seq), 1, offsetof(ping, seq)>> {};
template <typename T>
struct fields_of;

namespace detail {
/// Gives the codec the count member of `repeated` and `span_buffer`.
struct sequence_access;
} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Sequence of up to `N` items stored inline. Does not allocate.
template <typename T, std::size_t N>
struct repeated
{
	[[nodiscard]] constexpr std::size_t size() const { return _count; }
	[[nodiscard]] constexpr std::size_t capacity() const { return N; }
	[[nodiscard]] constexpr bool empty() const { return _count == 0; }

	[[nodiscard]] constexpr T* begin() { return _items.data(); }
	[[nodiscard]] constexpr T* end() { return _items.data() + _count; }
	[[nodiscard]] constexpr const T* begin() const { return _items.data(); }
	[[nodiscard]] constexpr const T* end() const { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	constexpr T& operator[]( std::size_t index ) { return _items[index]; }
	constexpr const T& operator[]( std::size_t index ) const { return _items[index]; }

	/// Returns `false` and does not add `item` when the sequence is full.
	[[nodiscard]] constexpr bool push_back( const T& item )
	{
		if ( _count == N )
			return false;

		_items[_count++] = item;
		return true;
	}

	constexpr void clear() { _count = 0; }

private:
	friend struct detail::sequence_access;

	std::array<T, N> _items{};
	std::size_t _count = 0;
};

/// Sequence that stores its items in caller-owned storage. The storage must
/// outlive the buffer. Does not allocate.
template <typename T>
struct span_buffer
{
	constexpr explicit span_buffer( std::span<T> storage ) : _storage( storage ) {}

	[[nodiscard]] constexpr std::size_t size() const { return _count; }
	[[nodiscard]] constexpr std::size_t capacity() const { return _storage.size(); }
	[[nodiscard]] constexpr bool empty() const { return _count == 0; }

	[[nodiscard]] constexpr T* begin() { return _storage.data(); }
	[[nodiscard]] constexpr T* end() { return _storage.data() + _count; }
	[[nodiscard]] constexpr const T* begin() const { return _storage.data(); }
	[[nodiscard]] constexpr const T* end() const { return _storage.data() + _count; }

	/// `index` must be less than `size()`.
	constexpr T& operator[]( std::size_t index ) { return _storage[index]; }
	constexpr const T& operator[]( std::size_t index ) const { return _storage[index]; }

	/// Returns `false` and does not add `item` when the storage is full.
	[[nodiscard]] constexpr bool push_back( const T& item )
	{
		if ( _count == _storage.size() )
			return false;

		_storage[_count++] = item;
		return true;
	}

	constexpr void clear() { _count = 0; }

private:
	friend struct detail::sequence_access;

	std::span<T> _storage;
	std::size_t _count = 0;
};

} // namespace pb
