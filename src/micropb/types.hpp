#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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
	/// decode: a field has more elements or bytes than its `pb::repeated`, `pb::bytes`, `pb::string` or `std::array` holds.
	CAPACITY_EXCEEDED,
	/// decode: the arena has no space for the items of a repeated field.
	ARENA_FULL,
};

/// Wire types of proto3 that this library supports. It does not support the group types (3, 4).
enum class wire_type : std::uint8_t
{
	VARINT = 0,
	FIXED64 = 1,
	LENGTH_DELIMITED = 2,
	FIXED32 = 5,
};

////////////////////////////////////////////////////////////////////////////////

/// Wire encoding of an integer field, element, optional value or oneof alternative.
enum class int_encoding : std::uint8_t
{
	VARINT,
	ZIGZAG,
	FIXED,
};

////////////////////////////////////////////////////////////////////////////////

/// One field of a message: the type `M` of the member, the field number, the
/// offset of the member in bytes, the 1-based `std::variant` alternative index
/// (0 for plain members), and the wire encoding for integer scalars.
template <typename M,
          std::uint32_t Number,
          std::size_t Offset,
          std::size_t Alternative = 0,
          int_encoding Encoding = int_encoding::VARINT>
struct field_at
{};

/// The fields of the message type `T`, in any order.
template <typename T, typename... Fields>
struct field_list
{};

/// The field list of the message type `T`: a struct that derives from
/// `pb::field_list<T, ...>`. `protoc-gen-micropb` or a person writes a
/// specialization:
///
///     template <>
///     struct pb::fields_of<ping> : pb::field_list<ping,
///         pb::field_at<decltype(ping::seq), 1, offsetof(ping, seq)>> {};
template <typename T>
struct fields_of;

namespace detail {
/// Gives the codec the `_items` and `_count` members of `repeated`, `bytes` and `string`.
struct sequence_access;
} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Sequence that keeps up to `N` items in the object. Does not allocate.
template <typename T, std::size_t N>
struct repeated
{
	static constexpr std::size_t max_size = N;

	constexpr repeated() = default;

	template <std::size_t M>
		requires( M <= N )
	constexpr repeated( const T ( &items )[M] ) : _count( M )
	{
		std::copy_n( items, M, _items.data() );
	}

	[[nodiscard]] constexpr std::size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr T* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const T* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr T* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr T* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const T* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr T& operator[]( std::size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const T& operator[]( std::size_t index ) const noexcept { return _items[index]; }

	[[nodiscard]] constexpr std::span<T> span() noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr std::span<const T> span() const noexcept { return { _items.data(), _count }; }

	/// Returns `false` and does not add `item` when the sequence is full.
	[[nodiscard]] constexpr bool push_back( const T& item )
	{
		if ( _count == N )
			return false;

		_items[_count++] = item;
		return true;
	}

	constexpr void clear() noexcept { _count = 0; }

	template <std::size_t M>
	[[nodiscard]] constexpr bool operator==( const repeated<T, M>& other ) const
	{
		return _count == other.size() && std::equal( begin(), end(), other.begin() );
	}

private:
	friend struct detail::sequence_access;

	std::array<T, N> _items;
	std::size_t _count = 0;
};

/// Byte sequence that keeps up to `N` bytes in the object. Does not allocate.
template <std::size_t N>
struct bytes
{
	static constexpr std::size_t max_size = N;

	constexpr bytes() = default;

	template <std::size_t M>
		requires( M <= N )
	constexpr bytes( const std::uint8_t ( &data )[M] ) noexcept : _count( M )
	{
		std::copy_n( data, M, _items.data() );
	}

	template <std::size_t M>
		requires( M <= N )
	constexpr bytes( const std::array<std::uint8_t, M>& data ) noexcept : _count( M )
	{
		std::copy_n( data.data(), M, _items.data() );
	}

	[[nodiscard]] constexpr std::size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr std::uint8_t* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const std::uint8_t* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr std::uint8_t* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr std::uint8_t* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const std::uint8_t* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const std::uint8_t* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr std::uint8_t& operator[]( std::size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const std::uint8_t& operator[]( std::size_t index ) const noexcept { return _items[index]; }

	[[nodiscard]] constexpr std::span<std::uint8_t> span() noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr std::span<const std::uint8_t> span() const noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr operator std::span<const std::uint8_t>() const noexcept { return span(); }

	/// Returns `false` and does not add `byte` when full.
	[[nodiscard]] constexpr bool push_back( std::uint8_t byte ) noexcept
	{
		if ( _count == N )
			return false;

		_items[_count++] = byte;
		return true;
	}

	/// Copies `data` into the buffer. Returns `false` without changing the buffer when `data.size() > N`.
	[[nodiscard]] constexpr bool assign( std::span<const std::uint8_t> data ) noexcept
	{
		if ( data.size() > N )
			return false;

		std::copy_n( data.data(), data.size(), _items.data() );
		_count = data.size();
		return true;
	}

	constexpr void clear() noexcept { _count = 0; }

	template <std::size_t M>
	[[nodiscard]] constexpr bool operator==( const bytes<M>& other ) const noexcept
	{
		return _count == other.size() && std::equal( begin(), end(), other.begin() );
	}

	[[nodiscard]] friend constexpr bool operator==( const bytes& a, std::span<const std::uint8_t> b ) noexcept
	{
		return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin() );
	}

private:
	friend struct detail::sequence_access;

	std::array<std::uint8_t, N> _items;
	std::size_t _count = 0;
};

/// Null-terminated text that keeps up to `N` bytes (plus a trailing `'\0'`) in the object. Does not allocate.
template <std::size_t N>
struct string
{
	static constexpr std::size_t max_size = N;

	constexpr string() noexcept
	{
		if consteval
		{
			_items.fill( '\0' );
		}
		else
		{
			_items[0] = '\0';
		}
	}

	/// Copies characters from `text` up to the first `'\0'` (at most `M - 1` bytes).
	template <std::size_t M>
		requires( M >= 1 && M - 1 <= N )
	constexpr string( const char ( &text )[M] ) noexcept
	{
		if consteval
		{
			_items.fill( '\0' );
		}
		const char* stop = std::find( text, text + ( M - 1 ), '\0' );
		_count = static_cast<std::size_t>( stop - text );
		std::copy_n( text, _count, _items.data() );
		_items[_count] = '\0';
	}

	[[nodiscard]] constexpr std::size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr std::size_t length() const noexcept { return _count; }
	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr const char* c_str() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr char* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const char* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr char* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr char* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const char* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const char* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr char& operator[]( std::size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const char& operator[]( std::size_t index ) const noexcept { return _items[index]; }

	[[nodiscard]] constexpr std::string_view view() const noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr operator std::string_view() const noexcept { return view(); }

	/// Returns `false` and does not add `ch` when full.
	[[nodiscard]] constexpr bool push_back( char ch ) noexcept
	{
		if ( _count == N )
			return false;

		_items[_count++] = ch;
		_items[_count] = '\0';
		return true;
	}

	/// Copies `text` into the buffer. Returns `false` without changing the buffer when `text.size() > N`.
	[[nodiscard]] constexpr bool assign( std::string_view text ) noexcept
	{
		if ( text.size() > N )
			return false;

		std::copy_n( text.data(), text.size(), _items.data() );
		_count = text.size();
		_items[_count] = '\0';
		return true;
	}

	constexpr void clear() noexcept
	{
		_count = 0;
		_items[0] = '\0';
	}

	template <std::size_t M>
	[[nodiscard]] constexpr bool operator==( const string<M>& other ) const noexcept
	{
		return view() == other.view();
	}

	[[nodiscard]] friend constexpr bool operator==( const string& a, std::string_view b ) noexcept
	{
		return a.view() == b;
	}

private:
	friend struct detail::sequence_access;

	std::array<char, N + 1> _items;
	std::size_t _count = 0;
};

/// Memory for the items of repeated fields that `decode` makes. The caller owns
/// the storage. The arena gives out aligned blocks from the start of the storage
/// and never frees one block: `clear()` frees all. Does not allocate.
struct arena
{
	constexpr explicit arena( std::span<std::uint8_t> storage ) : _storage( storage ) {}

	/// Bytes that the blocks use, with the padding for alignment.
	[[nodiscard]] constexpr std::size_t used() const { return _used; }
	[[nodiscard]] constexpr std::size_t capacity() const { return _storage.size(); }

	/// Frees all blocks. Each message that `decode` wrote with this arena is then not valid.
	constexpr void clear() { _used = 0; }

	/// A block of `size` bytes at a multiple of `alignment`, or null when the
	/// storage is too small. `alignment` is a power of 2.
	[[nodiscard]] void* allocate( std::size_t size, std::size_t alignment )
	{
		const auto address = reinterpret_cast<std::uintptr_t>( _storage.data() ) + _used;
		const std::size_t padding = ( ( address + alignment - 1 ) & ~( alignment - 1 ) ) - address;
		const std::size_t remaining = _storage.size() - _used;
		if ( padding > remaining || size > remaining - padding )
			return nullptr;

		std::uint8_t* block = _storage.data() + _used + padding;
		_used += padding + size;
		return block;
	}

private:
	std::span<std::uint8_t> _storage;
	std::size_t _used = 0;
};

} // namespace pb
