#pragma once

#include <array>
#include <limits>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>
#include <utility>

#include <micropb/types.hpp>
#include <micropb/wire.hpp>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Category of a member type. It sets how the codec writes and reads the member.
enum class field_kind : uint8_t
{
	SCALAR,
	STRING,
	BYTES,
	FIXED_BYTES,
	MESSAGE,
	OPTIONAL,
	ONEOF,
	REPEATED,
	POINTER
};

namespace detail {

inline constexpr size_t max_offset = 65535;

template <typename T>
inline constexpr bool is_optional = false;
template <typename T>
inline constexpr bool is_optional<std::optional<T>> = true;

template <typename T>
inline constexpr bool is_oneof = false;
template <typename... A>
inline constexpr bool is_oneof<oneof<A...>> = true;

template <typename T>
inline constexpr bool is_std_array = false;
template <typename T, size_t N>
inline constexpr bool is_std_array<std::array<T, N>> = true;

template <typename T>
inline constexpr bool is_std_span = false;
template <typename T, size_t E>
inline constexpr bool is_std_span<std::span<T, E>> = true;

/// A `std::span` with a static extent.
template <typename T>
inline constexpr bool is_fixed_span = false;
template <typename T, size_t E>
inline constexpr bool is_fixed_span<std::span<T, E>> = E != std::dynamic_extent;

template <typename T>
inline constexpr bool is_repeated = false;
template <typename T, size_t N>
inline constexpr bool is_repeated<repeated<T, N>> = true;

template <typename T>
inline constexpr bool is_bytes = false;
template <size_t N>
inline constexpr bool is_bytes<bytes<N>> = true;

template <typename T>
inline constexpr bool is_string = false;
template <size_t N>
inline constexpr bool is_string<string<N>> = true;

template <typename T>
inline constexpr bool is_kv_pair = false;
template <typename K, typename V, int_encoding KeyEnc, int_encoding ValEnc>
inline constexpr bool is_kv_pair<kv_pair<K, V, KeyEnc, ValEnc>> = true;

template <typename T>
inline constexpr bool is_map = false;
template <typename K, typename V, size_t N, int_encoding KeyEnc, int_encoding ValEnc>
inline constexpr bool is_map<map<K, V, N, KeyEnc, ValEnc>> = true;

/// The sequence types: `std::array`, `std::span<const T>`, `pb::repeated`, `pb::map` and `pb::bytes`. `element` has no `const`.
/// `capacity` is the size `N` of a type that has one, else 0.
template <typename T>
struct sequence_traits
{
	static constexpr bool is_sequence = false;
};

template <typename T, size_t N>
struct sequence_traits<std::array<T, N>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	using element = std::remove_const_t<T>;
};

template <typename T, size_t E>
struct sequence_traits<std::span<T, E>>
{
	static constexpr bool is_sequence = std::is_const_v<T>;
	static constexpr size_t capacity = 0;
	using element = std::remove_const_t<T>;
};

template <typename T, size_t N>
struct sequence_traits<repeated<T, N>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	using element = std::remove_const_t<T>;
};

template <typename K, typename V, size_t N, int_encoding KeyEnc, int_encoding ValEnc>
struct sequence_traits<map<K, V, N, KeyEnc, ValEnc>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	using element = kv_pair<K, V, KeyEnc, ValEnc>;
};

template <size_t N>
struct sequence_traits<bytes<N>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	using element = uint8_t;
};

/// Element type of a sequence type.
template <typename T>
using element_t = typename sequence_traits<T>::element;

template <typename T>
[[nodiscard]] consteval bool is_scalar()
{
	return std::is_enum_v<T> || is_standard_integer<T>() || is_one_of<T, bool, float, double>;
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Kind of the member type `T`.
template <typename T>
[[nodiscard]] consteval field_kind kind_of()
{
	if constexpr ( detail::is_scalar<T>() )
		return field_kind::SCALAR;
	else if constexpr ( std::is_same_v<T, std::string_view> || detail::is_string<T> )
		return field_kind::STRING;
	else if constexpr ( detail::is_optional<T> )
		return field_kind::OPTIONAL;
	else if constexpr ( detail::is_oneof<T> )
		return field_kind::ONEOF;
	else if constexpr ( detail::sequence_traits<T>::is_sequence )
	{
		if constexpr ( !std::is_same_v<detail::element_t<T>, uint8_t> )
			return field_kind::REPEATED;
		else if constexpr ( detail::is_std_array<T> )
			return field_kind::FIXED_BYTES;
		else
			return field_kind::BYTES;
	}
	else if constexpr ( std::is_pointer_v<T> )
	{
		using pointee = std::remove_pointer_t<T>;
		if constexpr ( std::is_const_v<pointee> && !std::is_volatile_v<pointee> && !std::is_void_v<pointee> &&
		               !std::is_pointer_v<pointee> )
		{
			if constexpr ( kind_of<std::remove_const_t<pointee>>() == field_kind::MESSAGE )
				return field_kind::POINTER;
		}
		return field_kind::MESSAGE;
	}
	else
		return field_kind::MESSAGE;
}

/// Wire type of a field of type `T`. A `std::optional` has the wire type of its value.
template <typename T, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval wire_type wire_type_of()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::OPTIONAL )
		return wire_type_of<typename T::value_type, Encoding>();
	else if constexpr ( kind != field_kind::SCALAR )
		return wire_type::LENGTH_DELIMITED;
	else if constexpr ( std::is_same_v<T, float> || ( Encoding == int_encoding::FIXED && sizeof( T ) == 4 ) )
		return wire_type::FIXED32;
	else if constexpr ( std::is_same_v<T, double> || ( Encoding == int_encoding::FIXED && sizeof( T ) == 8 ) )
		return wire_type::FIXED64;
	else
		return wire_type::VARINT;
}

/// `true` when the codec packs a repeated field with elements of type `T`.
template <typename T>
[[nodiscard]] consteval bool is_packed()
{
	return kind_of<T>() == field_kind::SCALAR;
}

////////////////////////////////////////////////////////////////////////////////

namespace detail {

template <typename Target, typename T, typename... Visited>
[[nodiscard]] consteval bool reaches();
template <typename T>
[[nodiscard]] consteval bool is_recursive();
template <typename T, typename... Visited>
[[nodiscard]] consteval bool needs_arena();
template <typename T, typename... Visited>
consteval void check_decodable();
template <typename T, uint32_t Number, int_encoding Encoding, bool Always, typename... Visited>
[[nodiscard]] consteval std::optional<size_t> max_field_size();

[[nodiscard]] consteval size_t key_size( uint32_t number )
{
	return varint_size( uint64_t{ number } << 3 );
}

[[nodiscard]] consteval size_t max_delimited_size( uint32_t number, size_t payload, bool always )
{
	if ( payload == 0 && !always )
		return 0;
	return key_size( number ) + varint_size( uint64_t{ payload } ) + payload;
}

template <typename T, int_encoding Encoding>
[[nodiscard]] consteval size_t max_scalar_value_size()
{
	if constexpr ( std::is_same_v<T, bool> )
		return 1;
	else if constexpr ( std::is_same_v<T, float> || ( Encoding == int_encoding::FIXED && sizeof( T ) == 4 ) )
		return 4;
	else if constexpr ( std::is_same_v<T, double> || ( Encoding == int_encoding::FIXED && sizeof( T ) == 8 ) )
		return 8;
	else if constexpr ( std::is_enum_v<T> )
	{
		if constexpr ( has_enum_traits<T> )
		{
			if constexpr ( static_cast<int64_t>( enum_traits<T>::min ) < 0 )
				return 10;
			else
				return varint_size( static_cast<uint64_t>( enum_traits<T>::max ) );
		}
		else
		{
			using U = std::underlying_type_t<T>;
			if constexpr ( std::is_signed_v<U> || sizeof( T ) == 4 )
				return 10;
			else
				return varint_size( uint64_t{ std::numeric_limits<U>::max() } );
		}
	}
	else if constexpr ( Encoding == int_encoding::ZIGZAG )
		return varint_size( zigzag_encode( static_cast<int64_t>( std::numeric_limits<T>::min() ) ) );
	else if constexpr ( std::is_signed_v<T> )
		return 10;
	else
		return varint_size( uint64_t{ std::numeric_limits<T>::max() } );
}

template <typename T>
[[nodiscard]] consteval std::optional<size_t> sequence_max_count()
{
	if constexpr ( is_std_span<T> )
	{
		if constexpr ( T::extent == std::dynamic_extent )
			return std::nullopt;
		else
			return T::extent;
	}
	else
		return sequence_traits<T>::capacity;
}

template <typename M, uint32_t Number, size_t Alternative, int_encoding Encoding, bool Always, typename... Visited>
[[nodiscard]] consteval std::optional<size_t> entry_max_size()
{
	if constexpr ( Alternative != 0 )
		return max_field_size<typename M::template alternative_t<Alternative>, Number, Encoding, true, Visited...>();
	else
		return max_field_size<M, Number, Encoding, Always, Visited...>();
}

template <typename T, typename... Fields>
[[nodiscard]] field_list<T, Fields...> list_of( const field_list<T, Fields...>& );

/// The `pb::field_list` that `pb::fields_of<T>` derives from.
template <typename T>
using field_list_of = decltype( list_of( std::declval<const fields_of<T>&>() ) );

/// The alternatives of a `pb::oneof` type.
template <typename V>
struct oneof_traits;

template <typename... A>
struct oneof_traits<oneof<A...>>
{
	template <typename Target, typename... Visited>
	[[nodiscard]] static consteval bool reaches()
	{
		return ( detail::reaches<Target, A, Visited...>() || ... );
	}

	template <typename... Visited>
	[[nodiscard]] static consteval bool needs_arena()
	{
		return ( detail::needs_arena<A, Visited...>() || ... );
	}

	template <typename... Visited>
	static consteval void check_decodable()
	{
		( detail::check_decodable<A, Visited...>(), ... );
	}
};

/// The entries of a field list.
template <typename L>
struct list_traits;

template <typename T,
          typename... M,
          uint32_t... Number,
          size_t... Offset,
          size_t... Alternative,
          int_encoding... Encoding>
struct list_traits<field_list<T, field_at<M, Number, Offset, Alternative, Encoding>...>>
{
	using message = T;
	static constexpr size_t count = sizeof...( M );

	/// Applies the layout rules that the code generator cannot verify.
	static consteval void check()
	{
		static_assert( ( ( Offset <= max_offset ) && ... ),
		               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
	}

	/// `true` when a member reaches `Target`. `Visited` has the message types on the path, `T` included.
	template <typename Target, typename... Visited>
	[[nodiscard]] static consteval bool reaches()
	{
		return ( detail::reaches<Target, std::remove_const_t<M>, Visited...>() || ... );
	}

	template <typename... Visited>
	[[nodiscard]] static consteval bool needs_arena()
	{
		return ( detail::needs_arena<std::remove_const_t<M>, Visited...>() || ... );
	}

	template <typename... Visited>
	static consteval void check_decodable()
	{
		( detail::check_decodable<std::remove_const_t<M>, Visited...>(), ... );
	}

	template <typename... Visited>
	[[nodiscard]] static consteval std::optional<size_t> max_size()
	{
		if constexpr ( count == 0 )
		{
			return size_t{ 0 };
		}
		else
		{
			constexpr bool always = is_kv_pair<T>;
			constexpr size_t offsets[]{ Offset... };
			constexpr size_t alts[]{ Alternative... };
			constexpr std::optional<size_t> sizes[]{
				entry_max_size<std::remove_const_t<M>, Number, Alternative, Encoding, always, Visited...>()...
			};

			size_t total = 0;
			for ( size_t i = 0; i < count; ++i )
			{
				if ( !sizes[i].has_value() )
					return std::nullopt;
				if ( alts[i] == 0 )
				{
					total += *sizes[i];
					continue;
				}
				bool seen = false;
				for ( size_t j = 0; j < i; ++j )
					if ( alts[j] != 0 && offsets[j] == offsets[i] )
						seen = true;
				if ( seen )
					continue;
				size_t group_max = *sizes[i];
				for ( size_t j = i + 1; j < count; ++j )
				{
					if ( alts[j] != 0 && offsets[j] == offsets[i] )
					{
						if ( !sizes[j].has_value() )
							return std::nullopt;
						if ( *sizes[j] > group_max )
							group_max = *sizes[j];
					}
				}
				total += group_max;
			}
			return total;
		}
	}
};

/// `true` when `T`, or a member, element, optional value or alternative of it, is
/// `Target`, also through other message types. The walk does not enter a message
/// type in `Visited` again, thus it stops at a cycle.
template <typename Target, typename T, typename... Visited>
[[nodiscard]] consteval bool reaches()
{
	if constexpr ( std::is_same_v<T, Target> )
	{
		return true;
	}
	else
	{
		constexpr field_kind kind = kind_of<T>();
		if constexpr ( kind == field_kind::OPTIONAL )
			return reaches<Target, typename T::value_type, Visited...>();
		else if constexpr ( kind == field_kind::POINTER )
			return reaches<Target, std::remove_const_t<std::remove_pointer_t<T>>, Visited...>();
		else if constexpr ( kind == field_kind::REPEATED )
			return reaches<Target, element_t<T>, Visited...>();
		else if constexpr ( kind == field_kind::ONEOF )
			return oneof_traits<T>::template reaches<Target, Visited...>();
		else if constexpr ( kind == field_kind::MESSAGE && !is_one_of<T, Visited...> )
			return list_traits<field_list_of<T>>::template reaches<Target, T, Visited...>();
		else
			return false;
	}
}

/// `true` when a member, element, optional value or alternative of the message
/// type `T` is `T`, also through other message types.
template <typename T>
[[nodiscard]] consteval bool is_recursive()
{
	return list_traits<field_list_of<T>>::template reaches<T, T>();
}

/// `true` when `T` has a `std::span<const E>` repeated field or a `const M*`
/// message pointer: in it, or in a member, alternative, value or element of it.
/// `decode` allocates such fields in a `pb::arena`.
template <typename T, typename... Visited>
[[nodiscard]] consteval bool needs_arena()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::POINTER )
		return true;
	else if constexpr ( kind == field_kind::REPEATED )
		return ( is_std_span<T> && !is_fixed_span<T> ) || needs_arena<element_t<T>, Visited...>();
	else if constexpr ( kind == field_kind::OPTIONAL )
		return needs_arena<typename T::value_type, Visited...>();
	else if constexpr ( kind == field_kind::ONEOF )
		return oneof_traits<T>::template needs_arena<Visited...>();
	else if constexpr ( kind == field_kind::MESSAGE && !is_one_of<T, Visited...> )
		return list_traits<field_list_of<T>>::template needs_arena<T, Visited...>();
	else
		return false;
}

/// Rejects encode-only repeated containers (`std::array`) in `decode`.
template <typename T, typename... Visited>
consteval void check_decodable()
{
	constexpr field_kind kind = kind_of<T>();

	if constexpr ( kind == field_kind::REPEATED )
	{
		static_assert( !is_std_array<T>,
		               "pb: decode_encode_only: std::array is encode-only, use pb::repeated or std::span<const T> "
		               "to decode" );
		check_decodable<element_t<T>, Visited...>();
	}
	else if constexpr ( kind == field_kind::POINTER )
	{
		check_decodable<std::remove_const_t<std::remove_pointer_t<T>>, Visited...>();
	}
	else if constexpr ( kind == field_kind::OPTIONAL )
	{
		check_decodable<typename T::value_type, Visited...>();
	}
	else if constexpr ( kind == field_kind::ONEOF )
	{
		oneof_traits<T>::template check_decodable<Visited...>();
	}
	else if constexpr ( kind == field_kind::MESSAGE && !is_one_of<T, Visited...> )
	{
		list_traits<field_list_of<T>>::template check_decodable<T, Visited...>();
	}
}

/// Rejects a message for `decode` without a `pb::arena` when it has a
/// `std::span<const T>` repeated field or a `const T*` message pointer.
template <typename T>
consteval void check_no_arena()
{
	static_assert( !needs_arena<T>(),
	               "pb: decode_needs_arena: a std::span<const T> repeated field or message pointer needs a pb::arena, "
	               "use decode( in, msg, arena )" );
}

template <typename T, uint32_t Number, int_encoding Encoding, bool Always, typename... Visited>
[[nodiscard]] consteval std::optional<size_t> max_field_size()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::SCALAR )
		return key_size( Number ) + max_scalar_value_size<T, Encoding>();
	else if constexpr ( kind == field_kind::STRING )
	{
		if constexpr ( std::is_same_v<T, std::string_view> )
			return std::nullopt;
		else
			return max_delimited_size( Number, T::max_size, Always );
	}
	else if constexpr ( kind == field_kind::FIXED_BYTES )
		return max_delimited_size( Number, sequence_traits<T>::capacity, Always );
	else if constexpr ( kind == field_kind::BYTES )
	{
		constexpr std::optional<size_t> count = sequence_max_count<T>();
		if constexpr ( !count.has_value() )
			return std::nullopt;
		else
			return max_delimited_size( Number, *count, Always );
	}
	else if constexpr ( kind == field_kind::OPTIONAL )
		return max_field_size<typename T::value_type, Number, Encoding, true, Visited...>();
	else if constexpr ( kind == field_kind::POINTER )
	{
		using pointee = std::remove_const_t<std::remove_pointer_t<T>>;
		if constexpr ( is_one_of<pointee, Visited...> )
			return std::nullopt;
		else
		{
			constexpr std::optional<size_t> body =
			  list_traits<field_list_of<pointee>>::template max_size<pointee, Visited...>();
			if constexpr ( !body.has_value() )
				return std::nullopt;
			else
				return key_size( Number ) + varint_size( uint64_t{ *body } ) + *body;
		}
	}
	else if constexpr ( kind == field_kind::MESSAGE )
	{
		if constexpr ( is_one_of<T, Visited...> )
			return std::nullopt;
		else
		{
			constexpr std::optional<size_t> body = list_traits<field_list_of<T>>::template max_size<T, Visited...>();
			if constexpr ( !body.has_value() )
				return std::nullopt;
			else
				return key_size( Number ) + varint_size( uint64_t{ *body } ) + *body;
		}
	}
	else if constexpr ( kind == field_kind::REPEATED )
	{
		constexpr std::optional<size_t> count = sequence_max_count<T>();
		if constexpr ( !count.has_value() )
			return std::nullopt;
		else if constexpr ( *count == 0 )
			return size_t{ 0 };
		else
		{
			using elem = element_t<T>;
			if constexpr ( is_packed<elem>() )
			{
				constexpr size_t payload = *count * max_scalar_value_size<elem, Encoding>();
				return key_size( Number ) + varint_size( uint64_t{ payload } ) + payload;
			}
			else
			{
				constexpr std::optional<size_t> item = max_field_size<elem, Number, Encoding, true, Visited...>();
				if constexpr ( !item.has_value() )
					return std::nullopt;
				else
					return *count * *item;
			}
		}
	}
	else
		return std::nullopt;
}

template <typename T>
[[nodiscard]] consteval std::optional<size_t> max_encoded_size_of()
{
	return list_traits<field_list_of<T>>::template max_size<T>();
}

template <typename T>
[[nodiscard]] consteval size_t checked_max_encoded_size()
{
	constexpr std::optional<size_t> bound = max_encoded_size_of<T>();
	static_assert( bound.has_value(),
	               "pb: unbounded_encoded_size: message contains an unbounded view or recursive field; check "
	               "pb::has_max_encoded_size<T>" );
	return bound.value_or( 0 );
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

template <typename T>
struct fields_of
{
	static_assert( sizeof( T ) == 0,
	               "pb: no_field_list: this message type has no pb::fields_of. Generate it with protoc-gen-micropb" );
};

/// `true` when every field of message `T` has a compile-time upper bound on its encoded size.
template <typename T>
inline constexpr bool has_max_encoded_size = detail::max_encoded_size_of<std::remove_cv_t<T>>().has_value();

/// Upper bound in bytes on `pb::encoded_size( msg )` for any value of message `T`.
/// Fails at compile time (`pb: unbounded_encoded_size`) when `T` contains an unbounded view
/// or a recursive pointer.
template <typename T>
inline constexpr size_t max_encoded_size = detail::checked_max_encoded_size<std::remove_cv_t<T>>();

} // namespace pb
