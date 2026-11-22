#pragma once

#include <algorithm>
#include <array>
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
	REPEATED
};

namespace detail {

inline constexpr uint32_t reserved_first = 19000;
inline constexpr uint32_t reserved_last = 19999;
inline constexpr size_t max_offset = 65535;

template <typename T, typename... U>
inline constexpr bool is_one_of = ( std::is_same_v<T, U> || ... );

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

/// The sequence types: `std::array`, `std::span<const T>`, `pb::repeated` and `pb::bytes`. `element` has no `const`.
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
	static constexpr bool is_const_element = std::is_const_v<T>;
	using element = std::remove_const_t<T>;
};

template <typename T, size_t E>
struct sequence_traits<std::span<T, E>>
{
	static constexpr bool is_sequence = std::is_const_v<T>;
	static constexpr size_t capacity = 0;
	static constexpr bool is_const_element = false;
	using element = std::remove_const_t<T>;
};

template <typename T, size_t N>
struct sequence_traits<repeated<T, N>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	static constexpr bool is_const_element = std::is_const_v<T>;
	using element = std::remove_const_t<T>;
};

template <size_t N>
struct sequence_traits<bytes<N>>
{
	static constexpr bool is_sequence = true;
	static constexpr size_t capacity = N;
	static constexpr bool is_const_element = false;
	using element = uint8_t;
};

/// Element type of a sequence type.
template <typename T>
using element_t = typename sequence_traits<T>::element;

/// A standard integer type: any integer type by size and sign, but not `bool` and not the character types.
/// Only the sizes 1, 2, 4 and 8 map to the wire: `__int128`, which `-std=gnu++23` makes integral, does not.
/// The rule does not accept a volatile-qualified type: the codec reads and writes through `memcpy`, without
/// `volatile`. `int32_t` is `int` on a PC and `long` on `arm-none-eabi`. The rule accepts both.
template <typename T>
[[nodiscard]] consteval bool is_standard_integer()
{
	return std::is_integral_v<T> && !std::is_volatile_v<T> &&
	       ( sizeof( T ) == 1 || sizeof( T ) == 2 || sizeof( T ) == 4 || sizeof( T ) == 8 ) &&
	       !is_one_of<std::remove_cv_t<T>, bool, char, wchar_t, char8_t, char16_t, char32_t>;
}

template <typename T>
[[nodiscard]] consteval bool is_scalar()
{
	return std::is_enum_v<T> || is_standard_integer<T>() || is_one_of<T, bool, float, double>;
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Kind of the member type `T`. A type without a proto3 mapping is a compile error.
template <typename T>
[[nodiscard]] consteval field_kind kind_of()
{
	if constexpr ( detail::is_scalar<T>() )
	{
		if constexpr ( std::is_enum_v<T> )
			static_assert( sizeof( T ) <= 4, "pb: enum_too_wide: a proto3 enum is int32, so the enum must fit in 32 bits" );
		return field_kind::SCALAR;
	}
	else if constexpr ( std::is_same_v<T, std::string_view> || detail::is_string<T> )
	{
		return field_kind::STRING;
	}
	else if constexpr ( detail::is_optional<T> )
	{
		return field_kind::OPTIONAL;
	}
	else if constexpr ( detail::is_oneof<T> )
	{
		return field_kind::ONEOF;
	}
	else if constexpr ( detail::sequence_traits<T>::is_sequence )
	{
		if constexpr ( !std::is_same_v<detail::element_t<T>, uint8_t> )
			return field_kind::REPEATED;
		else if constexpr ( detail::is_std_array<T> )
			return field_kind::FIXED_BYTES;
		else
			return field_kind::BYTES;
	}
	else if constexpr ( std::is_class_v<T> && std::is_aggregate_v<T> )
	{
		return field_kind::MESSAGE;
	}
	else
	{
		static_assert( !std::is_same_v<T, T>, "pb: unmapped_type: the member type has no proto3 mapping" );
	}
}

/// Wire type of a field of type `T`. A `std::optional` has the wire type of its value.
/// A oneof or a repeated field has no single wire type.
template <typename T, int_encoding Encoding = int_encoding::VARINT>
[[nodiscard]] consteval wire_type wire_type_of()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::OPTIONAL )
		return wire_type_of<typename T::value_type, Encoding>();
	else if constexpr ( kind == field_kind::ONEOF || kind == field_kind::REPEATED )
		static_assert( !std::is_same_v<T, T>, "pb: wire_type_of: a oneof or repeated field has no single wire type" );
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
template <typename T>
[[nodiscard]] consteval bool needs_arena();
template <typename T>
consteval void check_member_type();
template <typename T>
consteval void check_decodable();

template <typename T, typename... Fields>
[[nodiscard]] field_list<T, Fields...> list_of( const field_list<T, Fields...>& );

/// The `pb::field_list` that `pb::fields_of<T>` derives from.
template <typename T>
using field_list_of = decltype( list_of( std::declval<const fields_of<T>&>() ) );

template <size_t N>
[[nodiscard]] consteval bool has_number_in( const std::array<uint32_t, N>& numbers, uint32_t first, uint32_t last )
{
	return std::any_of( numbers.begin(), numbers.end(), [&]( uint32_t n ) { return n >= first && n <= last; } );
}

/// Applies the rules for field numbers. `Sorted` is a `std::array` of the numbers in ascending order.
template <auto Sorted>
consteval void check_field_numbers()
{
	static_assert( !has_number_in( Sorted, 0, 0 ), "pb: tag_zero: field number 0 is not valid" );
	static_assert( !has_number_in( Sorted, max_field_number + 1, uint32_t( -1 ) ),
	               "pb: tag_too_large: the largest field number is 2^29 - 1" );
	static_assert( !has_number_in( Sorted, reserved_first, reserved_last ),
	               "pb: tag_reserved: field numbers 19000 to 19999 are reserved by protobuf" );
	static_assert( std::adjacent_find( Sorted.begin(), Sorted.end() ) == Sorted.end(),
	               "pb: duplicate_tag: two fields have the same field number" );
}

/// `true` when the entry of a field list agrees with the member type `M`: a
/// `pb::oneof` member has one entry for each alternative (`1..K`), each other
/// member has the alternative 0.
template <typename M>
[[nodiscard]] consteval bool is_alternative_valid( size_t alternative )
{
	if constexpr ( is_oneof<M> )
		return alternative >= 1 && alternative <= M::alternative_count;
	else
		return alternative == 0;
}

template <typename T>
inline constexpr char type_mark = 0;

/// The bytes of one entry of a field list, for the rules that compare entries.
struct entry_shape
{
	size_t offset;
	size_t size;
	size_t alternative;
	/// The number of alternatives of a `std::variant` member, else 0.
	size_t alternatives;
	/// Identity of the member type without `const`.
	const char* type;
	/// `true` for an empty struct. An empty struct can share its address with a member of a different type.
	bool empty;
};

/// `true` when two entries name the same bytes of the message. The entries of
/// different alternatives of one `std::variant` member do not overlap.
[[nodiscard]] consteval bool entries_overlap( std::span<const entry_shape> entries )
{
	for ( size_t i = 0; i < entries.size(); ++i )
	{
		for ( size_t j = i + 1; j < entries.size(); ++j )
		{
			const entry_shape& a = entries[i];
			const entry_shape& b = entries[j];
			if ( a.empty && b.empty )
			{
				if ( a.offset == b.offset && a.type == b.type )
					return true;
				continue;
			}
			if ( a.empty || b.empty )
				continue;

			const bool same_variant = a.alternative != 0 && b.alternative != 0 && a.offset == b.offset;
			if ( same_variant ? a.alternative == b.alternative
			                  : a.offset < b.offset + b.size && b.offset < a.offset + a.size )
				return true;
		}
	}
	return false;
}

/// `true` when each `pb::oneof` member has one entry for each alternative.
[[nodiscard]] consteval bool alternatives_complete( std::span<const entry_shape> entries )
{
	for ( const entry_shape& entry : entries )
	{
		if ( entry.alternatives == 0 )
			continue;

		const auto same_member = [&]( const entry_shape& other ) {
			return other.offset == entry.offset && other.alternative != 0;
		};
		const auto count = static_cast<size_t>( std::count_if( entries.begin(), entries.end(), same_member ) );
		if ( count != entry.alternatives )
			return false;
	}
	return true;
}

template <typename M>
inline constexpr size_t alternative_count = 0;
template <typename... A>
inline constexpr size_t alternative_count<oneof<A...>> = sizeof...( A );

/// The alternatives of a `pb::oneof` type.
template <typename V>
struct oneof_traits;

template <typename... A>
struct oneof_traits<oneof<A...>>
{
	[[nodiscard]] static consteval bool has_kind( field_kind kind ) { return ( ( kind_of<A>() == kind ) || ... ); }

	template <typename Target, typename... Visited>
	[[nodiscard]] static consteval bool reaches()
	{
		return ( detail::reaches<Target, A, Visited...>() || ... );
	}

	[[nodiscard]] static consteval bool needs_arena() { return ( detail::needs_arena<A>() || ... ); }

	static consteval void check_decodable() { ( detail::check_decodable<A>(), ... ); }
};

/// Unwraps one `std::optional` or repeated sequence to reach its scalar leaf type.
template <typename T>
struct encoding_leaf
{
	using type = T;
};

template <typename T>
struct encoding_leaf<std::optional<T>>
{
	using type = std::remove_const_t<T>;
};

template <typename S>
	requires( sequence_traits<S>::is_sequence && !std::is_same_v<element_t<S>, uint8_t> )
struct encoding_leaf<S>
{
	using type = element_t<S>;
};

template <typename Leaf, int_encoding Encoding>
consteval void check_leaf_encoding()
{
	if constexpr ( Encoding == int_encoding::ZIGZAG )
	{
		static_assert( is_standard_integer<Leaf>() && std::is_signed_v<Leaf>,
		               "pb: zigzag_unsigned: a ZigZag (sint32, sint64) field needs a signed integer type" );
	}
	else if constexpr ( Encoding == int_encoding::FIXED )
	{
		static_assert( is_standard_integer<Leaf>() && ( sizeof( Leaf ) == 4 || sizeof( Leaf ) == 8 ),
		               "pb: fixed_width: a fixed-width integer field needs a 32-bit or 64-bit integer type" );
	}
}

template <typename U, size_t Alternative, int_encoding Encoding>
consteval void check_field_encoding()
{
	if constexpr ( Encoding != int_encoding::VARINT && is_alternative_valid<U>( Alternative ) )
	{
		if constexpr ( Alternative != 0 )
			check_leaf_encoding<std::remove_const_t<typename U::template alternative_t<Alternative>>, Encoding>();
		else
			check_leaf_encoding<typename encoding_leaf<U>::type, Encoding>();
	}
}

/// The entries of a field list. `M` can have `const`.
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

	[[nodiscard]] static consteval std::array<uint32_t, count> sorted_numbers()
	{
		std::array<uint32_t, count> numbers{ Number... };
		std::sort( numbers.begin(), numbers.end() );
		return numbers;
	}

	/// Applies the rules for the message type, the entries, the field numbers, the member types and recursion.
	static consteval void check()
	{
		static_assert( std::is_class_v<T> && std::is_aggregate_v<T>,
		               "pb: not_aggregate: a message must be a class aggregate" );
		static_assert( ( ( Offset <= max_offset ) && ... ),
		               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
		static_assert( ( ( Offset + sizeof( M ) <= sizeof( T ) ) && ... ),
		               "pb: field_list_offset: the member of a pb::field_at is not in the message, use offsetof" );
		static_assert( ( is_alternative_valid<std::remove_const_t<M>>( Alternative ) && ... ),
		               "pb: field_list_alternative: a std::variant member needs one pb::field_at for each "
		               "alternative with its index, each other member has no index" );

		constexpr std::array<entry_shape, count> shapes{ entry_shape{ Offset,
			                                                            sizeof( M ),
			                                                            Alternative,
			                                                            alternative_count<std::remove_const_t<M>>,
			                                                            &type_mark<std::remove_const_t<M>>,
			                                                            std::is_empty_v<M> }... };
		static_assert( !entries_overlap( shapes ),
		               "pb: field_list_overlap: two pb::field_at of a field list name the same member" );
		static_assert( alternatives_complete( shapes ),
		               "pb: field_list_alternative: a std::variant member needs one pb::field_at for each "
		               "alternative with its index, each other member has no index" );

		check_field_numbers<sorted_numbers()>();
		( check_member_type<std::remove_const_t<M>>(), ... );
		( check_field_encoding<std::remove_const_t<M>, Alternative, Encoding>(), ... );
		static_assert( !is_recursive<T>(),
		               "pb: recursive_message: a message cannot contain itself, also through a std::span or other "
		               "messages" );
	}

	/// `true` when a member reaches `Target`. `Visited` has the message types on the path, `T` included.
	template <typename Target, typename... Visited>
	[[nodiscard]] static consteval bool reaches()
	{
		return ( detail::reaches<Target, std::remove_const_t<M>, Visited...>() || ... );
	}

	[[nodiscard]] static consteval bool needs_arena() { return ( detail::needs_arena<std::remove_const_t<M>>() || ... ); }

	static consteval void check_decodable()
	{
		static_assert( ( !std::is_const_v<M> && ... ), "pb: decode_const_member: decode cannot write to a const member" );
		( detail::check_decodable<std::remove_const_t<M>>(), ... );
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
/// type `T` is `T`, also through other message types. `decode` recurses for each
/// message in a message: for a recursive type, the stack has no bound.
template <typename T>
[[nodiscard]] consteval bool is_recursive()
{
	return list_traits<field_list_of<T>>::template reaches<T, T>();
}

/// `true` when `T` has a `std::span<const E>` repeated field: in it, or in a member,
/// alternative, value or element of it. `decode` puts the items of such a field in a
/// `pb::arena`. The walk stops at a recursive message type: the rule
/// `recursive_message` rejects it.
template <typename T>
[[nodiscard]] consteval bool needs_arena()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::REPEATED )
		return ( is_std_span<T> && !is_fixed_span<T> ) || needs_arena<element_t<T>>();
	else if constexpr ( kind == field_kind::OPTIONAL )
		return needs_arena<typename T::value_type>();
	else if constexpr ( kind == field_kind::ONEOF )
		return oneof_traits<T>::needs_arena();
	else if constexpr ( kind == field_kind::MESSAGE )
		return !is_recursive<T>() && list_traits<field_list_of<T>>::needs_arena();
	else
		return false;
}

/// Rejects member types that have a mapping but that proto3 or `decode` cannot use.
template <typename M>
consteval void check_member_type()
{
	constexpr field_kind kind = kind_of<M>();

	if constexpr ( kind == field_kind::OPTIONAL )
	{
		using value = typename M::value_type;
		constexpr field_kind value_kind = kind_of<value>();
		static_assert( value_kind != field_kind::REPEATED,
		               "pb: optional_repeated: a repeated field cannot have explicit presence" );
		static_assert( value_kind != field_kind::OPTIONAL,
		               "pb: optional_optional: the value of a std::optional cannot be a std::optional" );
		static_assert( value_kind != field_kind::ONEOF,
		               "pb: optional_variant: the value of a std::optional cannot be a pb::oneof" );
	}
	else if constexpr ( kind == field_kind::ONEOF )
	{
		static_assert( !oneof_traits<M>::has_kind( field_kind::REPEATED ),
		               "pb: variant_repeated_alternative: a repeated field cannot be a oneof alternative" );
		static_assert( !oneof_traits<M>::has_kind( field_kind::OPTIONAL ),
		               "pb: variant_optional_alternative: a std::optional cannot be a oneof alternative" );
		static_assert( !oneof_traits<M>::has_kind( field_kind::ONEOF ),
		               "pb: variant_variant_alternative: a pb::oneof cannot be a oneof alternative" );
	}
	else if constexpr ( kind == field_kind::REPEATED )
	{
		using element = element_t<M>;
		constexpr field_kind element_kind = kind_of<element>();
		static_assert( element_kind != field_kind::REPEATED,
		               "pb: repeated_repeated: the element of a repeated field cannot be a repeated field" );
		static_assert( element_kind != field_kind::OPTIONAL,
		               "pb: repeated_optional: the element of a repeated field cannot be a std::optional" );
		static_assert( element_kind != field_kind::ONEOF,
		               "pb: repeated_variant: the element of a repeated field cannot be a pb::oneof" );
		// The arena never calls a destructor, and a new block gets the old items with `memcpy`.
		if constexpr ( is_std_span<M> && !is_fixed_span<M> )
			static_assert( std::is_trivially_copyable_v<element> && std::is_trivially_destructible_v<element>,
			               "pb: span_element_trivial: the element of a std::span<const T> repeated field must be "
			               "trivially copyable and trivially destructible" );
	}
}

/// Rejects types that `decode` cannot write: a repeated field in an
/// encode-only container, a span with a static extent, and a `const` member.
/// It examines `T`, and each member, alternative, value and element of it.
/// It stops at a recursive message type: the rule `recursive_message` rejects it.
template <typename T>
consteval void check_decodable()
{
	static_assert( !std::is_const_v<T>, "pb: decode_const_member: decode cannot write to a const member" );
	if constexpr ( !std::is_const_v<T> )
	{
		constexpr field_kind kind = kind_of<T>();

		if constexpr ( kind == field_kind::REPEATED )
		{
			static_assert( !is_std_array<T>,
			               "pb: decode_encode_only: std::array is encode-only, use pb::repeated or std::span<const T> "
			               "to decode" );
			static_assert( !is_fixed_span<T>,
			               "pb: decode_fixed_span: a std::span with a static extent is encode-only, "
			               "use std::span<const T> or pb::repeated to decode" );
			static_assert( !sequence_traits<T>::is_const_element,
			               "pb: decode_const_member: decode cannot write to a const member" );
			check_decodable<element_t<T>>();
		}
		else if constexpr ( kind == field_kind::BYTES )
		{
			static_assert( !is_std_span<T> || std::is_same_v<T, std::span<const uint8_t>>,
			               "pb: decode_fixed_span: a std::span with a static extent is encode-only, "
			               "use std::span<const uint8_t> or std::array to decode" );
			static_assert( !sequence_traits<T>::is_const_element,
			               "pb: decode_const_member: decode cannot write to a const member" );
		}
		else if constexpr ( kind == field_kind::FIXED_BYTES )
		{
			static_assert( !sequence_traits<T>::is_const_element,
			               "pb: decode_const_member: decode cannot write to a const member" );
		}
		else if constexpr ( kind == field_kind::OPTIONAL )
		{
			check_decodable<typename T::value_type>();
		}
		else if constexpr ( kind == field_kind::ONEOF )
		{
			oneof_traits<T>::check_decodable();
		}
		else if constexpr ( kind == field_kind::MESSAGE )
		{
			if constexpr ( !is_recursive<T>() )
				list_traits<field_list_of<T>>::check_decodable();
		}
	}
}

/// Rejects a message for `decode` without a `pb::arena` when it has a `std::span<const T>` repeated field.
template <typename T>
consteval void check_no_arena()
{
	static_assert( !needs_arena<T>(),
	               "pb: decode_needs_arena: a std::span<const T> repeated field needs a pb::arena, use decode( in, msg, "
	               "arena )" );
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

template <typename T>
struct fields_of
{
	static_assert( sizeof( T ) == 0,
	               "pb: no_field_list: this message type has no pb::fields_of. Generate it with protoc-gen-micropb "
	               "or write it by hand" );
};

} // namespace pb
