#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#if defined( __cpp_impl_reflection )
#include <meta>
#endif

#include <micropb/types.hpp>
#include <micropb/wire.hpp>

// This header has two parts.
// Part 1 has the rules for member types, field numbers and field lists. It
// does not use reflection.
// Part 2 makes the field list of a struct with reflection. A compiler without
// reflection does not get this part: there, each message type needs a
// `pb::fields_of` specialization.

namespace pb {

////////////////////////////////////////////////////////////////////////////////
// Part 1: rules

/// Category of a member type. It sets how the codec writes and reads the member.
enum class field_kind : std::uint8_t
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

inline constexpr std::uint32_t max_tag = ( 1u << 29 ) - 1;
inline constexpr std::uint32_t reserved_first = 19000;
inline constexpr std::uint32_t reserved_last = 19999;
inline constexpr std::size_t max_offset = 65535;

template <typename T, typename... U>
inline constexpr bool is_one_of = ( std::is_same_v<T, U> || ... );

template <typename T>
inline constexpr bool is_optional = false;
template <typename T>
inline constexpr bool is_optional<std::optional<T>> = true;

template <typename T>
inline constexpr bool is_variant = false;
template <typename... A>
inline constexpr bool is_variant<std::variant<A...>> = true;

template <typename T>
inline constexpr bool is_std_array = false;
template <typename T, std::size_t N>
inline constexpr bool is_std_array<std::array<T, N>> = true;

template <typename T>
inline constexpr bool is_std_span = false;
template <typename T, std::size_t E>
inline constexpr bool is_std_span<std::span<T, E>> = true;

template <typename T>
inline constexpr bool is_repeated = false;
template <typename T, std::size_t N>
inline constexpr bool is_repeated<repeated<T, N>> = true;

template <typename T>
inline constexpr bool is_span_buffer = false;
template <typename T>
inline constexpr bool is_span_buffer<span_buffer<T>> = true;

/// The sequence types: `std::array`, `std::span<const T>`, `pb::repeated` and
/// `pb::span_buffer`. `element` has no `const`. `capacity` is the size `N` of
/// a type that has one, else 0.
template <typename T>
struct sequence_traits
{
	static constexpr bool is_sequence = false;
};

template <typename T, std::size_t N>
struct sequence_traits<std::array<T, N>>
{
	static constexpr bool is_sequence = true;
	static constexpr std::size_t capacity = N;
	using element = std::remove_const_t<T>;
};

template <typename T, std::size_t E>
struct sequence_traits<std::span<T, E>>
{
	static constexpr bool is_sequence = std::is_const_v<T>;
	static constexpr std::size_t capacity = 0;
	using element = std::remove_const_t<T>;
};

template <typename T, std::size_t N>
struct sequence_traits<repeated<T, N>>
{
	static constexpr bool is_sequence = true;
	static constexpr std::size_t capacity = N;
	using element = std::remove_const_t<T>;
};

template <typename T>
struct sequence_traits<span_buffer<T>>
{
	static constexpr bool is_sequence = true;
	static constexpr std::size_t capacity = 0;
	using element = std::remove_const_t<T>;
};

/// Element type of a sequence type.
template <typename T>
using element_t = typename sequence_traits<T>::element;

/// A standard integer type: any integer type by size and sign, but not `bool` and not the character types.
/// Only the sizes 1, 2, 4 and 8 map to the wire: `__int128`, which `-std=gnu++23` makes integral, does not.
/// A volatile-qualified type is not accepted: the codec reads and writes through `memcpy`, without `volatile`.
/// `std::int32_t` is `int` on a PC and `long` on `arm-none-eabi`, and both are accepted.
template <typename T>
consteval bool is_standard_integer()
{
	return std::is_integral_v<T> && !std::is_volatile_v<T> &&
	       ( sizeof( T ) == 1 || sizeof( T ) == 2 || sizeof( T ) == 4 || sizeof( T ) == 8 ) &&
	       !is_one_of<std::remove_cv_t<T>, bool, char, wchar_t, char8_t, char16_t, char32_t>;
}

/// `wire_int<T, ZIGZAG>`: `sint32` or `sint64` with a stored integer type of any size.
template <typename T>
inline constexpr bool is_zigzag = false;
template <typename T>
inline constexpr bool is_zigzag<wire_int<T, int_encoding::ZIGZAG>> = true;

/// The stored integer type of a `wire_int`.
template <typename T>
using stored_t = decltype( T::value );

/// A ZigZag member whose stored type is a signed standard integer: 1, 2 or 4 bytes are `sint32`, 8 bytes are `sint64`.
template <typename T>
consteval bool is_signed_zigzag()
{
	if constexpr ( is_zigzag<T> )
		return is_standard_integer<stored_t<T>>() && std::is_signed_v<stored_t<T>>;
	else
		return false;
}

/// A ZigZag member whose stored type is an unsigned standard integer.
template <typename T>
consteval bool is_unsigned_zigzag()
{
	if constexpr ( is_zigzag<T> )
		return is_standard_integer<stored_t<T>>() && !std::is_signed_v<stored_t<T>>;
	else
		return false;
}

template <typename T>
consteval bool is_scalar()
{
	return std::is_enum_v<T> || is_standard_integer<T>() || is_signed_zigzag<T>() ||
	       is_one_of<T, bool, float, double, fixed32, fixed64, sfixed32, sfixed64>;
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
	else if constexpr ( detail::is_unsigned_zigzag<T>() )
	{
		static_assert( !std::is_same_v<T, T>,
		               "pb: zigzag_unsigned: a ZigZag (sint32, sint64) member needs a signed integer type" );
	}
	else if constexpr ( std::is_same_v<T, std::string_view> )
	{
		return field_kind::STRING;
	}
	else if constexpr ( detail::is_optional<T> )
	{
		return field_kind::OPTIONAL;
	}
	else if constexpr ( detail::is_variant<T> )
	{
		return field_kind::ONEOF;
	}
	else if constexpr ( detail::sequence_traits<T>::is_sequence )
	{
		if constexpr ( !std::is_same_v<detail::element_t<T>, std::uint8_t> )
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
template <typename T>
[[nodiscard]] consteval wire_type wire_type_of()
{
	constexpr field_kind kind = kind_of<T>();
	if constexpr ( kind == field_kind::OPTIONAL )
		return wire_type_of<typename T::value_type>();
	else if constexpr ( kind == field_kind::ONEOF || kind == field_kind::REPEATED )
		static_assert( !std::is_same_v<T, T>, "pb: wire_type_of: a oneof or repeated field has no single wire type" );
	else if constexpr ( kind != field_kind::SCALAR )
		return wire_type::LENGTH_DELIMITED;
	else if constexpr ( detail::is_one_of<T, float, fixed32, sfixed32> )
		return wire_type::FIXED32;
	else if constexpr ( detail::is_one_of<T, double, fixed64, sfixed64> )
		return wire_type::FIXED64;
	else
		return wire_type::VARINT;
}

/// `true` when a repeated field with elements of type `T` is packed.
template <typename T>
[[nodiscard]] consteval bool is_packed()
{
	return kind_of<T>() == field_kind::SCALAR;
}

////////////////////////////////////////////////////////////////////////////////

namespace detail {

template <typename T>
consteval bool contains_span_buffer();
template <typename T>
consteval void check_member_type();
template <typename T>
consteval void check_decodable();

template <typename T, typename... Fields>
field_list<T, Fields...> list_of( const field_list<T, Fields...>& );

/// The `pb::field_list` that `pb::fields_of<T>` derives from.
template <typename T>
using field_list_of = decltype( list_of( std::declval<const fields_of<T>&>() ) );

template <std::size_t N>
consteval bool has_number_in( const std::array<std::uint32_t, N>& numbers, std::uint32_t first, std::uint32_t last )
{
	return std::any_of( numbers.begin(), numbers.end(), [&]( std::uint32_t n ) { return n >= first && n <= last; } );
}

/// Applies the rules for field numbers. `Sorted` is a `std::array` of the numbers in ascending order.
template <auto Sorted>
consteval void check_field_numbers()
{
	static_assert( !has_number_in( Sorted, 0, 0 ), "pb: tag_zero: field number 0 is not valid" );
	static_assert( !has_number_in( Sorted, max_tag + 1, std::uint32_t( -1 ) ),
	               "pb: tag_too_large: the largest field number is 2^29 - 1" );
	static_assert( !has_number_in( Sorted, reserved_first, reserved_last ),
	               "pb: tag_reserved: field numbers 19000 to 19999 are reserved by protobuf" );
	static_assert( std::adjacent_find( Sorted.begin(), Sorted.end() ) == Sorted.end(),
	               "pb: duplicate_tag: two fields have the same field number" );
}

/// `true` when the entry of a field list agrees with the member type `M`: a
/// `std::variant` member has one entry for each alternative after
/// `std::monostate`, each other member has the alternative 0.
template <typename M>
consteval bool is_alternative_valid( std::size_t alternative )
{
	if constexpr ( is_variant<M> )
		return alternative >= 1 && alternative < std::variant_size_v<M>;
	else
		return alternative == 0;
}

/// The bytes of one entry of a field list, for the rules that compare entries.
struct entry_shape
{
	std::size_t offset;
	std::size_t size;
	std::size_t alternative;
	/// The number of alternatives of a `std::variant` member, else 0.
	std::size_t alternatives;
	/// An empty struct. It can share its address with a different member.
	bool empty;
};

/// `true` when two entries name the same bytes of the message. The entries of
/// the different alternatives of one `std::variant` member are not such a case.
consteval bool entries_overlap( std::span<const entry_shape> entries )
{
	for ( std::size_t i = 0; i < entries.size(); ++i )
	{
		for ( std::size_t j = i + 1; j < entries.size(); ++j )
		{
			const entry_shape& a = entries[i];
			const entry_shape& b = entries[j];
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

/// `true` when each `std::variant` member has one entry for each alternative after `std::monostate`.
consteval bool alternatives_complete( std::span<const entry_shape> entries )
{
	for ( const entry_shape& entry : entries )
	{
		const auto same_member = [&]( const entry_shape& other ) { return other.offset == entry.offset; };
		if ( entry.alternatives != 0 && static_cast<std::size_t>( std::count_if(
		                                  entries.begin(), entries.end(), same_member ) ) != entry.alternatives - 1 )
			return false;
	}
	return true;
}

template <typename M>
inline constexpr std::size_t alternative_count = 0;
template <typename... A>
inline constexpr std::size_t alternative_count<std::variant<A...>> = sizeof...( A );

/// The alternatives of a `std::variant` type.
template <typename V>
struct variant_traits;

template <typename First, typename... More>
struct variant_traits<std::variant<First, More...>>
{
	template <typename A>
	static constexpr std::size_t count =
	  ( std::size_t{ std::is_same_v<A, First> } + ... + std::size_t{ std::is_same_v<A, More> } );

	static constexpr bool monostate_first = std::is_same_v<First, std::monostate>;
	static constexpr bool unique = count<First> == 1 && ( ( count<More> == 1 ) && ... );

	static consteval bool has_kind( field_kind kind )
	{
		return kind_of<First>() == kind || ( ( kind_of<More>() == kind ) || ... );
	}

	static consteval bool contains_span_buffer()
	{
		return detail::contains_span_buffer<First>() || ( detail::contains_span_buffer<More>() || ... );
	}

	static consteval void check_decodable()
	{
		detail::check_decodable<First>();
		( detail::check_decodable<More>(), ... );
	}
};

/// The entries of a field list. `M` can have `const`.
template <typename L>
struct list_traits;

template <typename T, typename... M, std::uint32_t... Number, std::size_t... Offset, std::size_t... Alternative>
struct list_traits<field_list<T, field_at<M, Number, Offset, Alternative>...>>
{
	using message = T;
	static constexpr std::size_t count = sizeof...( M );

	static consteval std::array<std::uint32_t, count> sorted_numbers()
	{
		std::array<std::uint32_t, count> numbers{ Number... };
		std::sort( numbers.begin(), numbers.end() );
		return numbers;
	}

	/// Applies the rules for the entries, the field numbers and the member types.
	static consteval void check()
	{
		static_assert( ( ( Offset <= max_offset ) && ... ),
		               "pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less" );
		static_assert( ( ( Offset + sizeof( M ) <= sizeof( T ) ) && ... ),
		               "pb: field_list_offset: the member of a pb::field_at is not in the message, use offsetof" );
		static_assert( ( is_alternative_valid<std::remove_const_t<M>>( Alternative ) && ... ),
		               "pb: field_list_alternative: a std::variant member needs one pb::field_at for each "
		               "alternative with its index, each other member has no index" );

		constexpr std::array<entry_shape, count> shapes{ entry_shape{
			Offset, sizeof( M ), Alternative, alternative_count<std::remove_const_t<M>>, std::is_empty_v<M> }... };
		static_assert( !entries_overlap( shapes ),
		               "pb: field_list_overlap: two pb::field_at of a field list name the same member" );
		static_assert( alternatives_complete( shapes ),
		               "pb: field_list_alternative: a std::variant member needs one pb::field_at for each "
		               "alternative with its index, each other member has no index" );

		check_field_numbers<sorted_numbers()>();
		( check_member_type<std::remove_const_t<M>>(), ... );
	}

	static consteval bool contains_span_buffer()
	{
		return ( detail::contains_span_buffer<std::remove_const_t<M>>() || ... );
	}

	static consteval void check_decodable()
	{
		static_assert( ( !std::is_const_v<M> && ... ), "pb: decode_const_member: decode cannot write to a const member" );
		( detail::check_decodable<std::remove_const_t<M>>(), ... );
	}
};

/// `true` when `T` has a `pb::span_buffer` in a place that `decode` cannot bind:
/// in it, or in a member, alternative, value or element of it.
template <typename T>
consteval bool contains_span_buffer()
{
	if constexpr ( std::is_same_v<T, std::monostate> )
	{
		return false;
	}
	else
	{
		constexpr field_kind kind = kind_of<T>();
		if constexpr ( is_span_buffer<T> )
			return true;
		else if constexpr ( kind == field_kind::OPTIONAL )
			return contains_span_buffer<typename T::value_type>();
		else if constexpr ( kind == field_kind::REPEATED )
			return contains_span_buffer<element_t<T>>();
		else if constexpr ( kind == field_kind::ONEOF )
			return variant_traits<T>::contains_span_buffer();
		else if constexpr ( kind == field_kind::MESSAGE )
			return list_traits<field_list_of<T>>::contains_span_buffer();
		else
			return false;
	}
}

template <typename T>
consteval void check_not_monostate()
{
	static_assert( !std::is_same_v<T, std::monostate>,
	               "pb: monostate_member: std::monostate is allowed only as the first alternative of a std::variant" );
}

/// Rejects member types that have a mapping but that proto3 or `decode` cannot use.
template <typename M>
consteval void check_member_type()
{
	constexpr field_kind kind = kind_of<M>();
	check_not_monostate<M>();

	if constexpr ( kind == field_kind::OPTIONAL )
	{
		using value = typename M::value_type;
		constexpr field_kind value_kind = kind_of<value>();
		check_not_monostate<value>();
		static_assert( value_kind != field_kind::REPEATED,
		               "pb: optional_repeated: a repeated field cannot have explicit presence" );
		static_assert( value_kind != field_kind::OPTIONAL,
		               "pb: optional_optional: the value of a std::optional cannot be a std::optional" );
		static_assert( value_kind != field_kind::ONEOF,
		               "pb: optional_variant: the value of a std::optional cannot be a std::variant" );
		static_assert( !contains_span_buffer<value>(), "pb: span_buffer_in_optional: use pb::repeated in a std::optional" );
	}
	else if constexpr ( kind == field_kind::ONEOF )
	{
		static_assert( variant_traits<M>::monostate_first,
		               "pb: variant_no_monostate: the first alternative of a std::variant must be std::monostate" );
		static_assert( variant_traits<M>::unique,
		               "pb: variant_duplicate_alternative: each alternative of a "
		               "std::variant must have a different type" );
		static_assert( !variant_traits<M>::has_kind( field_kind::REPEATED ),
		               "pb: variant_repeated_alternative: a repeated field cannot be a oneof alternative" );
		static_assert( !variant_traits<M>::has_kind( field_kind::OPTIONAL ),
		               "pb: variant_optional_alternative: a std::optional cannot be a oneof alternative" );
		static_assert( !variant_traits<M>::has_kind( field_kind::ONEOF ),
		               "pb: variant_variant_alternative: a std::variant cannot be a oneof alternative" );
		static_assert( !contains_span_buffer<M>(), "pb: span_buffer_in_variant: use pb::repeated in a std::variant" );
	}
	else if constexpr ( kind == field_kind::REPEATED )
	{
		using element = element_t<M>;
		constexpr field_kind element_kind = kind_of<element>();
		check_not_monostate<element>();
		static_assert( element_kind != field_kind::REPEATED,
		               "pb: repeated_repeated: the element of a repeated field cannot be a repeated field" );
		static_assert( element_kind != field_kind::OPTIONAL,
		               "pb: repeated_optional: the element of a repeated field cannot be a std::optional" );
		static_assert( element_kind != field_kind::ONEOF,
		               "pb: repeated_variant: the element of a repeated field cannot be a std::variant" );
		static_assert( !contains_span_buffer<element>(),
		               "pb: span_buffer_in_repeated: use pb::repeated as the element of a repeated field" );
	}
}

/// Rejects types that `decode` cannot write: a repeated field in an
/// encode-only container, bytes in a span with a static extent, and a `const`
/// member. It examines `T`, and each member, alternative, value and element of it.
template <typename T>
consteval void check_decodable()
{
	if constexpr ( !std::is_same_v<T, std::monostate> )
	{
		constexpr field_kind kind = kind_of<T>();

		if constexpr ( kind == field_kind::REPEATED )
		{
			static_assert( !is_std_array<T> && !is_std_span<T>,
			               "pb: decode_encode_only: std::array and std::span<const T> are encode-only, "
			               "use pb::repeated or pb::span_buffer to decode" );
			check_decodable<element_t<T>>();
		}
		else if constexpr ( kind == field_kind::BYTES )
		{
			static_assert( !is_std_span<T> || std::is_same_v<T, std::span<const std::uint8_t>>,
			               "pb: decode_fixed_span: a std::span with a static extent is encode-only, "
			               "use std::span<const std::uint8_t> or std::array to decode" );
		}
		else if constexpr ( kind == field_kind::OPTIONAL )
		{
			check_decodable<typename T::value_type>();
		}
		else if constexpr ( kind == field_kind::ONEOF )
		{
			variant_traits<T>::check_decodable();
		}
		else if constexpr ( kind == field_kind::MESSAGE )
		{
			list_traits<field_list_of<T>>::check_decodable();
		}
	}
}

} // namespace detail

#if defined( __cpp_impl_reflection )

////////////////////////////////////////////////////////////////////////////////
// Part 2: field lists from reflection

/// `field_info::alternative` of a field that is not a `std::variant` alternative.
inline constexpr std::size_t no_alternative = std::size_t( -1 );

/// One entry of the tag table: a plain member, or one alternative of a `std::variant` member.
struct field_info
{
	std::meta::info member;
	std::uint32_t number;
	/// Index into the `std::variant`, or `no_alternative`.
	std::size_t alternative;
};

namespace detail {

/// `true` when `type` is a specialization of the class template `templ`.
consteval bool is_instance( std::meta::info type, std::meta::info templ )
{
	type = std::meta::dealias( type );
	return std::meta::has_template_arguments( type ) && std::meta::template_of( type ) == templ;
}

template <typename T>
consteval std::span<const std::meta::info> members()
{
	return std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) );
}

/// Field number from the `pb::tag` annotation of `member`.
consteval std::optional<std::uint32_t> tag_annotation( std::meta::info member )
{
	for ( std::meta::info annotation : std::meta::annotations_of( member ) )
	{
		if ( std::meta::remove_cvref( std::meta::type_of( annotation ) ) == ^^tag )
			return std::meta::extract<tag>( annotation ).value;
	}
	return std::nullopt;
}

/// Type of the `pb::oneof_tags` annotation of `member`.
consteval std::optional<std::meta::info> oneof_tags_annotation( std::meta::info member )
{
	for ( std::meta::info annotation : std::meta::annotations_of( member ) )
	{
		const std::meta::info type = std::meta::remove_cvref( std::meta::type_of( annotation ) );
		if ( is_instance( type, ^^oneof_tags ) )
			return type;
	}
	return std::nullopt;
}

template <typename T>
consteval bool has_bit_field()
{
	for ( std::meta::info member : std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::unchecked() ) )
	{
		if ( std::meta::is_bit_field( member ) )
			return true;
	}
	return false;
}

/// Rejects message types of which reflection does not show all data members,
/// and members that the codec cannot address.
template <typename T>
consteval void check_message_type()
{
	static_assert( std::is_class_v<T> && std::is_aggregate_v<T>,
	               "pb: not_aggregate: a message must be a class aggregate" );
	static_assert( std::meta::bases_of( ^^T, std::meta::access_context::unchecked() ).empty(),
	               "pb: base_class: a message cannot have a base class, because the codec does not see its members" );
	static_assert(
	  std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::unchecked() ).size() == members<T>().size(),
	  "pb: inaccessible_member: all data members of a message must be public" );
	static_assert( !has_bit_field<T>(), "pb: bit_field: a message member cannot be a bit-field" );
}

template <typename T>
consteval std::size_t field_count()
{
	std::size_t count = 0;
	for ( std::meta::info member : members<T>() )
	{
		const std::meta::info type = std::meta::dealias( std::meta::type_of( member ) );
		if ( is_instance( type, ^^std::variant ) )
			count += std::meta::template_arguments_of( type ).size() - 1;
		else
			++count;
	}
	return count;
}

/// Tag table in declaration order. Applies the tag rules of the spec.
template <typename T>
consteval std::array<field_info, field_count<T>()> collect_fields()
{
	std::array<field_info, field_count<T>()> fields{};
	std::size_t index = 0;
	std::uint32_t next = 1;

	template for ( constexpr std::meta::info member : members<T>() )
	{
		using type = std::remove_const_t<typename[:std::meta::type_of( member ):]>;
		check_member_type<type>();

		constexpr std::optional<std::uint32_t> number = tag_annotation( member );
		constexpr std::optional<std::meta::info> listed = oneof_tags_annotation( member );

		if constexpr ( kind_of<type>() != field_kind::ONEOF )
		{
			static_assert( !listed, "pb: oneof_tags_on_non_variant: use pb::tag on a member that is not a std::variant" );
			if ( number )
				next = *number;
			fields[index++] = { member, next++, no_alternative };
		}
		else
		{
			static_assert( !number, "pb: tag_on_variant: use pb::oneof_tags on a std::variant member" );
			constexpr std::size_t count = std::variant_size_v<type> - 1;
			if constexpr ( listed )
			{
				static_assert( std::meta::template_arguments_of( *listed ).size() == count,
				               "pb: oneof_tags_count: pb::oneof_tags must list one tag for each alternative after "
				               "std::monostate" );
			}

			for ( std::size_t i = 0; i < count; ++i )
			{
				if constexpr ( listed )
					next = std::meta::extract<std::uint32_t>( std::meta::template_arguments_of( *listed )[i] );
				fields[index++] = { member, next++, i + 1 };
			}
		}
	}
	return fields;
}

template <typename T>
consteval std::array<field_info, field_count<T>()> sorted_fields()
{
	auto fields = collect_fields<T>();
	std::sort(
	  fields.begin(), fields.end(), []( const field_info& a, const field_info& b ) { return a.number < b.number; } );
	return fields;
}

/// The `pb::field_list` of the struct `T`: one `pb::field_at` for each entry
/// of the tag table, in declaration order.
template <typename T>
consteval std::meta::info make_field_list()
{
	std::array<std::meta::info, field_count<T>() + 1> arguments{ ^^T };
	std::size_t index = 1;
	for ( const field_info& field : collect_fields<T>() )
	{
		const auto offset = static_cast<std::size_t>( std::meta::offset_of( field.member ).bytes );
		const std::size_t alternative = field.alternative == no_alternative ? 0 : field.alternative;
		arguments[index++] = std::meta::substitute( ^^field_at,
		                                            {
		                                              std::meta::type_of( field.member ),
		                                              std::meta::reflect_constant( field.number ),
		                                              std::meta::reflect_constant( offset ),
		                                              std::meta::reflect_constant( alternative ) } );
	}
	return std::meta::substitute( ^^field_list, arguments );
}

/// First the rules for the message type, so that their error comes before an error of reflection.
template <typename T>
consteval std::meta::info reflect_field_list()
{
	check_message_type<T>();
	return make_field_list<T>();
}

template <typename T>
using reflected_fields = typename[:reflect_field_list<T>():];

} // namespace detail

/// Fields of the struct `T`, sorted by field number, with the reflection of
/// each member. The schema generator uses this table. It reads the members and
/// annotations of `T`, not a `pb::fields_of` specialization. An invalid message
/// type, tag or member type is a compile error, by the same rules as in the codec.
template <typename T>
[[nodiscard]] consteval std::span<const field_info> build_tag_table()
{
	detail::list_traits<detail::reflected_fields<T>>::check();
	return std::define_static_array( detail::sorted_fields<T>() );
}

/// A struct without a `pb::fields_of` specialization gets its field list from reflection.
template <typename T>
struct fields_of : detail::reflected_fields<T>
{};

#else

template <typename T>
struct fields_of
{
	static_assert( sizeof( T ) == 0,
	               "pb: no_field_list: this message type has no pb::fields_of. Generate it with protoc-gen-micropb, "
	               "write it by hand, or use a compiler with reflection" );
};

#endif

} // namespace pb
