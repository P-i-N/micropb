#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Error codes of the codec. Functions return them as `std::expected` values.
enum class error : uint8_t
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
	/// reader: a varint or zigzag value is outside the range of the target type.
	OUT_OF_RANGE,
};

/// Wire types of proto3 that this library supports. It does not support the group types (3, 4).
enum class wire_type : uint8_t
{
	VARINT = 0,
	FIXED64 = 1,
	LENGTH_DELIMITED = 2,
	FIXED32 = 5,
};

////////////////////////////////////////////////////////////////////////////////

/// Wire encoding of an integer field, element, optional value or oneof alternative.
enum class int_encoding : uint8_t
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
          uint32_t Number,
          size_t Offset,
          size_t Alternative = 0,
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

/// Gives the codec the `_storage` and `_index` members of `oneof`.
struct oneof_access;

/// Smallest unsigned integer type that holds counts `0..N`.
template <size_t N>
using count_type_t = std::conditional_t<N <= 255, uint8_t, std::conditional_t<N <= 65535, uint16_t, size_t>>;

template <size_t I, typename... Ts>
struct type_at;

template <typename Head, typename... Tail>
struct type_at<0, Head, Tail...>
{
	using type = Head;
};

template <size_t I, typename Head, typename... Tail>
struct type_at<I, Head, Tail...> : type_at<I - 1, Tail...>
{};

template <typename T, typename... Ts>
inline constexpr size_t type_count_v = ( static_cast<size_t>( std::is_same_v<T, Ts> ) + ... + 0 );

template <typename T, typename... Ts>
[[nodiscard]] consteval size_t find_type_index()
{
	constexpr bool match[]{ std::is_same_v<T, Ts>... };
	for ( size_t i = 0; i < sizeof...( Ts ); ++i )
		if ( match[i] )
			return i + 1;
	return 0;
}

template <typename U, typename... Ts>
[[nodiscard]] consteval size_t find_convertible_index()
{
	using D = std::decay_t<U>;
	if constexpr ( type_count_v<D, Ts...> == 1 )
		return find_type_index<D, Ts...>();
	else if constexpr ( type_count_v<D, Ts...> == 0 )
	{
		constexpr bool conv[]{ ( !std::is_same_v<std::remove_cv_t<Ts>, bool> && std::is_convertible_v<U, Ts> )... };
		size_t found = 0;
		for ( size_t i = 0; i < sizeof...( Ts ); ++i )
		{
			if ( conv[i] )
			{
				if ( found != 0 )
					return 0;
				found = i + 1;
			}
		}
		return found;
	}
	else
		return 0;
}

template <typename... Ts>
union oneof_storage;

template <>
union oneof_storage<>
{};

template <typename Head, typename... Tail>
union oneof_storage<Head, Tail...>
{
	Head head;
	oneof_storage<Tail...> tail;

	constexpr oneof_storage() noexcept {}
	constexpr oneof_storage( const oneof_storage& ) = default;
	constexpr oneof_storage& operator=( const oneof_storage& ) = default;
	constexpr ~oneof_storage() = default;

	template <size_t I, typename... Args>
	constexpr auto& emplace_at( Args&&... args )
	{
		if constexpr ( I == 0 )
		{
			std::construct_at( const_cast<std::remove_const_t<Head>*>( &head ), std::forward<Args>( args )... );
			return head;
		}
		else
		{
			std::construct_at( &tail );
			return tail.template emplace_at<I - 1>( std::forward<Args>( args )... );
		}
	}

	template <size_t I>
	[[nodiscard]] constexpr auto& at() noexcept
	{
		if constexpr ( I == 0 )
			return head;
		else
			return tail.template at<I - 1>();
	}

	template <size_t I>
	[[nodiscard]] constexpr const auto& at() const noexcept
	{
		if constexpr ( I == 0 )
			return head;
		else
			return tail.template at<I - 1>();
	}
};

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Sequence that keeps up to `N` items in the object. Does not allocate.
template <typename T, size_t N>
struct repeated
{
	static constexpr size_t max_size = N;

	constexpr repeated() = default;

	template <size_t M>
		requires( M <= N )
	constexpr repeated( const T ( &items )[M] ) : _count( static_cast<count_type>( M ) )
	{
		std::copy_n( items, M, _items.data() );
	}

	[[nodiscard]] constexpr size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr T* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const T* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr T* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr T* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const T* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr T& operator[]( size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const T& operator[]( size_t index ) const noexcept { return _items[index]; }

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

	template <size_t M>
	[[nodiscard]] constexpr bool operator==( const repeated<T, M>& other ) const
	{
		return _count == other.size() && std::equal( begin(), end(), other.begin() );
	}

private:
	friend struct detail::sequence_access;

	using count_type = detail::count_type_t<N>;

	std::array<T, N> _items;
	count_type _count = 0;
};

/// Key-value pair of a proto3 `map<K, V>` field.
template <typename K,
          typename V,
          int_encoding KeyEnc = int_encoding::VARINT,
          int_encoding ValEnc = int_encoding::VARINT>
struct kv_pair
{
	using key_type = K;
	using mapped_type = V;
	static constexpr int_encoding key_encoding = KeyEnc;
	static constexpr int_encoding value_encoding = ValEnc;

	K key = {};
	V value = {};

	[[nodiscard]] constexpr bool operator==( const kv_pair& ) const = default;
};

namespace detail {

template <typename Pair>
using kv_pair_field_list = field_list<Pair,
                                      field_at<typename Pair::key_type, 1, offsetof( Pair, key ), 0, Pair::key_encoding>,
                                      field_at<typename Pair::mapped_type, 2, offsetof( Pair, value ), 0, Pair::value_encoding>>;

} // namespace detail

template <typename K, typename V, int_encoding KeyEnc, int_encoding ValEnc>
struct fields_of<kv_pair<K, V, KeyEnc, ValEnc>> : detail::kv_pair_field_list<kv_pair<K, V, KeyEnc, ValEnc>>
{};

/// Fixed-capacity proto3 `map<K, V>` that keeps up to `N` entries in the object.
/// Does not allocate. Lookup is linear in `size()`.
template <typename K,
          typename V,
          size_t N,
          int_encoding KeyEnc = int_encoding::VARINT,
          int_encoding ValEnc = int_encoding::VARINT>
struct map
{
	using key_type = K;
	using mapped_type = V;
	using value_type = kv_pair<K, V, KeyEnc, ValEnc>;
	static constexpr size_t max_size = N;

	constexpr map() = default;

	template <size_t M>
		requires( M <= N )
	constexpr map( const value_type ( &items )[M] )
	{
		for ( size_t i = 0; i < M; ++i )
			static_cast<void>( insert_or_assign( items[i].key, items[i].value ) );
	}

	[[nodiscard]] constexpr size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }
	[[nodiscard]] constexpr bool full() const noexcept { return _count == N; }

	[[nodiscard]] constexpr value_type* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const value_type* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr value_type* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr value_type* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const value_type* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const value_type* end() const noexcept { return _items.data() + _count; }

	[[nodiscard]] constexpr std::span<value_type> span() noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr std::span<const value_type> span() const noexcept { return { _items.data(), _count }; }

	/// Returns a pointer to the value for `key`, or null if absent.
	template <typename Key = K>
	[[nodiscard]] constexpr V* find( const Key& key ) noexcept
	{
		for ( size_t i = 0; i < _count; ++i )
			if ( _items[i].key == key )
				return &_items[i].value;
		return nullptr;
	}

	template <typename Key = K>
	[[nodiscard]] constexpr const V* find( const Key& key ) const noexcept
	{
		for ( size_t i = 0; i < _count; ++i )
			if ( _items[i].key == key )
				return &_items[i].value;
		return nullptr;
	}

	template <typename Key = K>
	[[nodiscard]] constexpr bool contains( const Key& key ) const noexcept
	{
		return find( key ) != nullptr;
	}

	/// Updates the entry for `key`, or appends a new one. Returns `false` when full.
	[[nodiscard]] constexpr bool insert_or_assign( const K& key, const V& value )
	{
		if ( V* existing = find( key ) )
		{
			*existing = value;
			return true;
		}
		if ( _count == N )
			return false;
		_items[_count++] = value_type{ key, value };
		return true;
	}

	/// Removes the entry for `key`. Returns `true` if an entry was removed.
	template <typename Key = K>
	[[nodiscard]] constexpr bool erase( const Key& key )
	{
		for ( size_t i = 0; i < _count; ++i )
		{
			if ( _items[i].key == key )
			{
				for ( size_t j = i + 1; j < _count; ++j )
					_items[j - 1] = _items[j];
				--_count;
				return true;
			}
		}
		return false;
	}

	constexpr void clear() noexcept { _count = 0; }

	template <size_t M>
	[[nodiscard]] constexpr bool operator==( const map<K, V, M, KeyEnc, ValEnc>& other ) const
	{
		if ( _count != other.size() )
			return false;
		for ( size_t i = 0; i < _count; ++i )
		{
			const V* found = other.find( _items[i].key );
			if ( found == nullptr || !( *found == _items[i].value ) )
				return false;
		}
		return true;
	}

private:
	friend struct detail::sequence_access;

	using count_type = detail::count_type_t<N>;

	std::array<value_type, N> _items;
	count_type _count = 0;
};

/// Byte sequence that keeps up to `N` bytes in the object. Does not allocate.
template <size_t N>
struct bytes
{
	static constexpr size_t max_size = N;

	constexpr bytes() = default;

	template <size_t M>
		requires( M <= N )
	constexpr bytes( const uint8_t ( &data )[M] ) noexcept : _count( static_cast<count_type>( M ) )
	{
		std::copy_n( data, M, _items.data() );
	}

	template <size_t M>
		requires( M <= N )
	constexpr bytes( const std::array<uint8_t, M>& data ) noexcept : _count( static_cast<count_type>( M ) )
	{
		std::copy_n( data.data(), M, _items.data() );
	}

	[[nodiscard]] constexpr size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr uint8_t* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const uint8_t* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr uint8_t* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr uint8_t* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const uint8_t* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const uint8_t* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr uint8_t& operator[]( size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const uint8_t& operator[]( size_t index ) const noexcept { return _items[index]; }

	[[nodiscard]] constexpr std::span<uint8_t> span() noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr std::span<const uint8_t> span() const noexcept { return { _items.data(), _count }; }
	[[nodiscard]] constexpr operator std::span<const uint8_t>() const noexcept { return span(); }

	/// Returns `false` and does not add `byte` when full.
	[[nodiscard]] constexpr bool push_back( uint8_t byte ) noexcept
	{
		if ( _count == N )
			return false;

		_items[_count++] = byte;
		return true;
	}

	/// Copies `data` into the buffer. Returns `false` without changing the buffer when `data.size() > N`.
	[[nodiscard]] constexpr bool assign( std::span<const uint8_t> data ) noexcept
	{
		if ( data.size() > N )
			return false;

		std::copy_n( data.data(), data.size(), _items.data() );
		_count = static_cast<count_type>( data.size() );
		return true;
	}

	constexpr void clear() noexcept { _count = 0; }

	template <size_t M>
	[[nodiscard]] constexpr bool operator==( const bytes<M>& other ) const noexcept
	{
		return _count == other.size() && std::equal( begin(), end(), other.begin() );
	}

	[[nodiscard]] friend constexpr bool operator==( const bytes& a, std::span<const uint8_t> b ) noexcept
	{
		return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin() );
	}

private:
	friend struct detail::sequence_access;

	using count_type = detail::count_type_t<N>;

	std::array<uint8_t, N> _items;
	count_type _count = 0;
};

/// Null-terminated text that keeps up to `N` bytes (plus a trailing `'\0'`) in the object. Does not allocate.
template <size_t N>
struct string
{
	static constexpr size_t max_size = N;

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
	template <size_t M>
		requires( M >= 1 && M - 1 <= N )
	constexpr string( const char ( &text )[M] ) noexcept
	{
		if consteval
		{
			_items.fill( '\0' );
		}
		const char* stop = std::find( text, text + ( M - 1 ), '\0' );
		_count = static_cast<count_type>( stop - text );
		std::copy_n( text, _count, _items.data() );
		_items[_count] = '\0';
	}

	[[nodiscard]] constexpr size_t size() const noexcept { return _count; }
	[[nodiscard]] constexpr size_t length() const noexcept { return _count; }
	[[nodiscard]] constexpr size_t capacity() const noexcept { return N; }
	[[nodiscard]] constexpr bool empty() const noexcept { return _count == 0; }

	[[nodiscard]] constexpr const char* c_str() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr char* data() noexcept { return _items.data(); }
	[[nodiscard]] constexpr const char* data() const noexcept { return _items.data(); }

	[[nodiscard]] constexpr char* begin() noexcept { return _items.data(); }
	[[nodiscard]] constexpr char* end() noexcept { return _items.data() + _count; }
	[[nodiscard]] constexpr const char* begin() const noexcept { return _items.data(); }
	[[nodiscard]] constexpr const char* end() const noexcept { return _items.data() + _count; }

	/// `index` must be less than `size()`.
	[[nodiscard]] constexpr char& operator[]( size_t index ) noexcept { return _items[index]; }
	[[nodiscard]] constexpr const char& operator[]( size_t index ) const noexcept { return _items[index]; }

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
		_count = static_cast<count_type>( text.size() );
		_items[_count] = '\0';
		return true;
	}

	constexpr void clear() noexcept
	{
		_count = 0;
		_items[0] = '\0';
	}

	template <size_t M>
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

	using count_type = detail::count_type_t<N>;

	std::array<char, N + 1> _items;
	count_type _count = 0;
};

/// Tagged union for proto3 `oneof` fields. Alternatives use 1-based indices
/// `1..sizeof...(Ts)` in declaration order; `index()` returns `0` when unset.
/// Does not allocate and requires no `std::monostate`.
template <typename... Ts>
struct oneof
{
	static_assert( sizeof...( Ts ) > 0, "pb::oneof must have at least one alternative" );
	static_assert( ( std::is_trivially_destructible_v<Ts> && ... ),
	               "pb::oneof alternatives must be trivially destructible" );

	static constexpr size_t alternative_count = sizeof...( Ts );

	template <size_t I>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	using alternative_t = typename detail::type_at<I - 1, Ts...>::type;

	constexpr oneof() noexcept = default;

	template <size_t I, typename... Args>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	constexpr explicit oneof( std::in_place_index_t<I>, Args&&... args )
	{
		emplace<I>( std::forward<Args>( args )... );
	}

	template <typename U>
		requires( !std::is_same_v<std::decay_t<U>, oneof> && detail::find_convertible_index<U, Ts...>() != 0 )
	constexpr oneof( U&& value )
	{
		emplace<detail::find_convertible_index<U, Ts...>()>( std::forward<U>( value ) );
	}

	[[nodiscard]] constexpr size_t index() const noexcept { return _index; }
	[[nodiscard]] constexpr bool has_value() const noexcept { return _index != 0; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return _index != 0; }

	constexpr void reset() noexcept { _index = 0; }

	template <size_t I, typename... Args>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	constexpr alternative_t<I>& emplace( Args&&... args )
	{
		alternative_t<I>& slot = _storage.template emplace_at<I - 1>( std::forward<Args>( args )... );
		_index = static_cast<index_type>( I );
		return slot;
	}

	template <typename T, typename... Args>
		requires( detail::type_count_v<T, Ts...> == 1 )
	constexpr T& emplace( Args&&... args )
	{
		return emplace<detail::find_type_index<T, Ts...>()>( std::forward<Args>( args )... );
	}

	template <size_t I>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	[[nodiscard]] constexpr alternative_t<I>* get_if() noexcept
	{
		return _index == I ? &_storage.template at<I - 1>() : nullptr;
	}

	template <size_t I>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	[[nodiscard]] constexpr const alternative_t<I>* get_if() const noexcept
	{
		return _index == I ? &_storage.template at<I - 1>() : nullptr;
	}

	template <typename T>
		requires( detail::type_count_v<T, Ts...> == 1 )
	[[nodiscard]] constexpr T* get_if() noexcept
	{
		return get_if<detail::find_type_index<T, Ts...>()>();
	}

	template <typename T>
		requires( detail::type_count_v<T, Ts...> == 1 )
	[[nodiscard]] constexpr const T* get_if() const noexcept
	{
		return get_if<detail::find_type_index<T, Ts...>()>();
	}

	template <size_t I>
		requires( I >= 1 && I <= sizeof...( Ts ) )
	[[nodiscard]] constexpr bool holds() const noexcept
	{
		return _index == I;
	}

	template <typename T>
		requires( detail::type_count_v<T, Ts...> == 1 )
	[[nodiscard]] constexpr bool holds() const noexcept
	{
		return holds<detail::find_type_index<T, Ts...>()>();
	}

	[[nodiscard]] constexpr bool operator==( const oneof& other ) const
	{
		if ( _index != other._index )
			return false;
		if ( _index == 0 )
			return true;
		return equal_at( other, std::make_index_sequence<sizeof...( Ts )>{} );
	}

private:
	friend struct detail::oneof_access;

	using index_type = detail::count_type_t<sizeof...( Ts )>;

	template <size_t... Is>
	[[nodiscard]] constexpr bool equal_at( const oneof& other, std::index_sequence<Is...> ) const
	{
		bool eq = false;
		static_cast<void>(
		  ( ( _index == Is + 1 && ( eq = ( _storage.template at<Is>() == other._storage.template at<Is>() ), true ) ) ||
		    ... ) );
		return eq;
	}

	detail::oneof_storage<Ts...> _storage;
	index_type _index = 0;
};

template <size_t I, typename... Ts>
[[nodiscard]] constexpr auto* get_if( oneof<Ts...>* value ) noexcept
{
	return value != nullptr ? value->template get_if<I>() : nullptr;
}

template <size_t I, typename... Ts>
[[nodiscard]] constexpr auto* get_if( const oneof<Ts...>* value ) noexcept
{
	return value != nullptr ? value->template get_if<I>() : nullptr;
}

template <typename T, typename... Ts>
[[nodiscard]] constexpr T* get_if( oneof<Ts...>* value ) noexcept
{
	return value != nullptr ? value->template get_if<T>() : nullptr;
}

template <typename T, typename... Ts>
[[nodiscard]] constexpr const T* get_if( const oneof<Ts...>* value ) noexcept
{
	return value != nullptr ? value->template get_if<T>() : nullptr;
}

template <typename T, typename... Ts>
[[nodiscard]] constexpr bool holds_alternative( const oneof<Ts...>& value ) noexcept
{
	return value.template holds<T>();
}

/// Memory for the items of repeated fields that `decode` makes. The caller owns
/// the storage. The arena gives out aligned blocks from the start of the storage
/// and never frees one block: `clear()` frees all. Does not allocate.
struct arena
{
	constexpr explicit arena( std::span<uint8_t> storage ) : _storage( storage ) {}

	/// Bytes that the blocks use, with the padding for alignment.
	[[nodiscard]] constexpr size_t used() const { return _used; }
	[[nodiscard]] constexpr size_t capacity() const { return _storage.size(); }

	/// Prefix of the storage that the arena has allocated, including alignment padding.
	[[nodiscard]] constexpr std::span<uint8_t> used_span() const { return _storage.first( _used ); }

	/// Unallocated suffix of the storage.
	[[nodiscard]] constexpr std::span<uint8_t> remaining_span() const { return _storage.subspan( _used ); }

	/// Frees all blocks. Each message that `decode` wrote with this arena is then not valid.
	constexpr void clear() { _used = 0; }

	/// A block of `size` bytes at a multiple of `alignment`, or null when the
	/// storage is too small. `alignment` is a power of 2.
	[[nodiscard]] void* allocate( size_t size, size_t alignment )
	{
		const auto address = reinterpret_cast<uintptr_t>( _storage.data() ) + _used;
		const size_t padding = ( ( address + alignment - 1 ) & ~( alignment - 1 ) ) - address;
		const size_t remaining = _storage.size() - _used;
		if ( padding > remaining || size > remaining - padding )
			return nullptr;

		uint8_t* block = _storage.data() + _used + padding;
		_used += padding + size;
		return block;
	}

private:
	std::span<uint8_t> _storage;
	size_t _used = 0;
};

} // namespace pb
