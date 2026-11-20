#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <micropb/codec.hpp>

using namespace pb;

namespace {

using namespace cases;
using namespace messages;

constexpr std::uint8_t guard = 0xAA;

/// `encode` writes exactly `expected`, and `encoded_size` agrees.
template <typename T, std::size_t N>
bool matches( const T& value, const std::array<std::uint8_t, N>& expected )
{
	std::array<std::uint8_t, N + 1> buffer{};
	buffer.fill( guard );

	const auto size = encode( value, std::span( buffer ).first( N ) );
	return size && *size == N && encoded_size( value ) == N &&
	       std::equal( expected.begin(), expected.end(), buffer.begin() ) && buffer[N] == guard;
}

/// `lists` with the container types that only the encoder accepts.
struct lists_views
{
	std::span<const std::int32_t> ints;
	std::array<float, 2> floats;
};

struct lists_owned
{
	pb::repeated<std::int32_t, 8> ints;
	pb::repeated<float, 8> floats;
};

struct ints_array
{
	std::array<std::int32_t, 3> ints;
};

struct ints_repeated
{
	pb::repeated<std::int32_t, 3> ints;
};

struct back
{
	std::int32_t a;
	std::int32_t b;
};

enum class wide_unsigned : std::uint32_t
{
	ZERO,
	MAX = 0xFFFFFFFF
};

struct unsigned_enum
{
	wide_unsigned e;
};

struct nothing
{};

struct holds_nothing
{
	nothing n;
	std::uint32_t x;
};

struct level3
{
	std::string_view s;
};

struct level2
{
	level3 c;
};

struct level1
{
	level2 c;
};

struct spans
{
	std::span<const std::int32_t> ints;
	std::span<const std::uint8_t> bytes;
};

struct fixed_view
{
	std::span<const std::uint8_t, 6> mac;
	std::uint32_t after;
};

struct const_middle
{
	const inner deep;
	std::uint32_t m;
};

struct const_holder
{
	std::optional<const_middle> maybe;
	std::variant<std::monostate, const_middle> choice;
};

} // namespace

template <>
struct pb::fields_of<lists_views>
  : pb::field_list<lists_views,
                   pb::field_at<decltype( lists_views::ints ), 1, offsetof( lists_views, ints )>,
                   pb::field_at<decltype( lists_views::floats ), 2, offsetof( lists_views, floats )>>
{};

template <>
struct pb::fields_of<lists_owned>
  : pb::field_list<lists_owned,
                   pb::field_at<decltype( lists_owned::ints ), 1, offsetof( lists_owned, ints )>,
                   pb::field_at<decltype( lists_owned::floats ), 2, offsetof( lists_owned, floats )>>
{};

template <>
struct pb::fields_of<ints_array>
  : pb::field_list<ints_array, pb::field_at<decltype( ints_array::ints ), 1, offsetof( ints_array, ints )>>
{};

template <>
struct pb::fields_of<ints_repeated>
  : pb::field_list<ints_repeated, pb::field_at<decltype( ints_repeated::ints ), 1, offsetof( ints_repeated, ints )>>
{};

template <>
struct pb::fields_of<back> : pb::field_list<back,
                                            pb::field_at<decltype( back::a ), 2, offsetof( back, a )>,
                                            pb::field_at<decltype( back::b ), 1, offsetof( back, b )>>
{};

template <>
struct pb::fields_of<unsigned_enum>
  : pb::field_list<unsigned_enum, pb::field_at<decltype( unsigned_enum::e ), 1, offsetof( unsigned_enum, e )>>
{};

template <>
struct pb::fields_of<nothing> : pb::field_list<nothing>
{};

template <>
struct pb::fields_of<holds_nothing>
  : pb::field_list<holds_nothing,
                   pb::field_at<decltype( holds_nothing::n ), 1, offsetof( holds_nothing, n )>,
                   pb::field_at<decltype( holds_nothing::x ), 2, offsetof( holds_nothing, x )>>
{};

template <>
struct pb::fields_of<level3>
  : pb::field_list<level3, pb::field_at<decltype( level3::s ), 1, offsetof( level3, s )>>
{};

template <>
struct pb::fields_of<level2>
  : pb::field_list<level2, pb::field_at<decltype( level2::c ), 1, offsetof( level2, c )>>
{};

template <>
struct pb::fields_of<level1>
  : pb::field_list<level1, pb::field_at<decltype( level1::c ), 1, offsetof( level1, c )>>
{};

template <>
struct pb::fields_of<spans> : pb::field_list<spans,
                                             pb::field_at<decltype( spans::ints ), 1, offsetof( spans, ints )>,
                                             pb::field_at<decltype( spans::bytes ), 2, offsetof( spans, bytes )>>
{};

template <>
struct pb::fields_of<fixed_view>
  : pb::field_list<fixed_view,
                   pb::field_at<decltype( fixed_view::mac ), 1, offsetof( fixed_view, mac )>,
                   pb::field_at<decltype( fixed_view::after ), 2, offsetof( fixed_view, after )>>
{};

template <>
struct pb::fields_of<const_middle>
  : pb::field_list<const_middle,
                   pb::field_at<decltype( const_middle::deep ), 1, offsetof( const_middle, deep )>,
                   pb::field_at<decltype( const_middle::m ), 2, offsetof( const_middle, m )>>
{};

template <>
struct pb::fields_of<const_holder>
  : pb::field_list<const_holder,
                   pb::field_at<decltype( const_holder::maybe ), 1, offsetof( const_holder, maybe )>,
                   pb::field_at<decltype( const_holder::choice ), 2, offsetof( const_holder, choice ), 1>>
{};

namespace {

////////////////////////////////////////////////////////////////////////////////

bool test_encode_scalars_zero()
{
	return matches( scalars{}, vectors::scalars_zero );
}

bool test_encode_scalars_max()
{
	return matches( scalars_max(), vectors::scalars_max );
}

bool test_encode_scalars_negative()
{
	return matches( scalars_negative(), vectors::scalars_negative );
}

bool test_encode_scalars_enum_neg()
{
	return matches( scalars_enum_neg(), vectors::scalars_enum_neg );
}

bool test_encode_text_short()
{
	return matches( text{ "hi", short_data, mac }, vectors::text_short );
}

bool test_encode_text_long()
{
	return matches( text_long(), vectors::text_long );
}

bool test_encode_outer_set()
{
	return matches( outer{ .child = { 1, {} }, .maybe = 0, .maybe_child = inner{}, .gap = 3 }, vectors::outer_set );
}

bool test_encode_outer_unset()
{
	return matches( outer{}, vectors::outer_unset );
}

bool test_encode_lists_empty()
{
	return matches( lists{}, vectors::lists_empty );
}

bool test_encode_lists_full()
{
	return matches( lists_full(), vectors::lists_full );
}

bool test_encode_pose_zero()
{
	return matches( pose{}, vectors::pose_zero );
}

bool test_encode_envelope_none()
{
	return matches( envelope{ 1, {} }, vectors::envelope_none );
}

bool test_encode_envelope_pose()
{
	return matches( envelope{ 2, pose{ { 1, 2, 3 }, { 0, 0, 0, 1 } } }, vectors::envelope_pose );
}

bool test_encode_envelope_ping()
{
	return matches( envelope{ 3, ping{} }, vectors::envelope_ping );
}

bool test_encode_envelope_long()
{
	return matches( envelope{ 4, text_long() }, vectors::envelope_long );
}

bool test_encode_spans()
{
	constexpr std::array<std::int32_t, 3> three{ 1, -2, 3 };
	lists_owned owned;
	for ( const std::int32_t item : three )
		static_cast<void>( owned.ints.push_back( item ) );
	static_cast<void>( owned.floats.push_back( 1.5f ) );
	static_cast<void>( owned.floats.push_back( -2.5f ) );

	const lists_views views{ std::span<const std::int32_t>( three ), { 1.5f, -2.5f } };
	std::array<std::uint8_t, 64> a{};
	std::array<std::uint8_t, 64> b{};
	const auto size_a = encode( owned, a );
	const auto size_b = encode( views, b );

	const ints_array array_only{ three };
	const ints_repeated repeated_only{ repeated_of<pb::repeated<std::int32_t, 3>>( three ) };

	std::array<std::uint8_t, 16> c{};
	std::array<std::uint8_t, 16> d{};
	const auto size_c = encode( array_only, c );
	const auto size_d = encode( repeated_only, d );

	return size_a && size_b && *size_a == *size_b && std::equal( a.begin(), a.begin() + *size_a, b.begin() ) && size_c &&
	       size_d && *size_c == *size_d && *size_c == encoded_size( array_only ) &&
	       std::equal( c.begin(), c.begin() + *size_c, d.begin() );
}

bool test_encode_too_small()
{
	const text value = text_long();
	const std::size_t size = encoded_size( value );

	std::array<std::uint8_t, 256> buffer{};
	buffer.fill( guard );
	const auto result = encode( value, std::span( buffer ).first( size - 1 ) );
	if ( result || result.error() != error::BUFFER_TOO_SMALL )
		return false;

	return buffer[size - 1] == guard;
}

/// An enum is `int32` on the wire: an unsigned value of 2^31 or more is a negative `int32`.
bool test_encode_unsigned_enum()
{
	constexpr std::array<std::uint8_t, 11> expected{ 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01 };
	return matches( unsigned_enum{ wide_unsigned::MAX }, expected );
}

bool test_encode_order()
{
	constexpr std::array<std::uint8_t, 4> expected{ 0x08, 0x01, 0x10, 0x01 };
	return matches( back{ .a = 1, .b = 1 }, expected );
}

////////////////////////////////////////////////////////////////////////////////

/// A message type without fields has a field table of length 0.
bool test_encode_empty_message()
{
	constexpr std::array<std::uint8_t, 4> expected{ 0x0a, 0x00, 0x10, 0x01 };
	return matches( holds_nothing{ {}, 1 }, expected ) && encoded_size( nothing{} ) == 0;
}

/// Three nested messages, each with a length prefix of two bytes.
bool test_encode_deep()
{
	const std::string text( 200, 'x' );
	std::array<std::uint8_t, 256> buffer{};
	const auto size = encode( level1{ { { text } } }, buffer );
	constexpr std::array<std::uint8_t, 10> head{ 0x0a, 0xce, 0x01, 0x0a, 0xcb, 0x01, 0x0a, 0xc8, 0x01, 'x' };
	return size && *size == 209 && encoded_size( level1{ { { text } } } ) == 209 &&
	       std::equal( head.begin(), head.end(), buffer.begin() ) && buffer[208] == 'x';
}

/// `encode` into each buffer that is too short gives an error and does not write past the buffer.
template <typename T>
bool each_short_buffer_fails( const T& value )
{
	const std::size_t size = encoded_size( value );
	for ( std::size_t length = 0; length < size; ++length )
	{
		std::array<std::uint8_t, 512> buffer{};
		buffer.fill( guard );
		const auto result = encode( value, std::span( buffer ).first( length ) );
		if ( result || result.error() != error::BUFFER_TOO_SMALL )
			return false;
		if ( !std::all_of( buffer.begin() + length, buffer.end(), []( std::uint8_t b ) { return b == guard; } ) )
			return false;
	}
	return true;
}

bool test_encode_each_short_buffer()
{
	return each_short_buffer_fails( text_long() ) && each_short_buffer_fails( envelope{ 4, text_long() } ) &&
	       each_short_buffer_fails( lists_full() );
}

bool test_encode_span_and_bytes()
{
	constexpr std::array<std::int32_t, 3> int_values{ 1, -2, 3 };
	constexpr std::array<std::uint8_t, 3> byte_values{ 7, 8, 9 };
	const spans value{ int_values, byte_values };

	constexpr std::array<std::uint8_t, 19> expected{ 0x0a, 0x0c, 0x01, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	                                                 0xff, 0xff, 0x01, 0x03, 0x12, 0x03, 7,    8,    9 };
	return matches( value, expected );
}

/// A `std::span` with a static extent is a different type than the bytes view: it holds only a pointer.
bool test_encode_fixed_extent_span()
{
	constexpr std::array<std::uint8_t, 6> address{ 1, 2, 3, 4, 5, 6 };
	constexpr std::array<std::uint8_t, 10> expected{ 0x0a, 0x06, 1, 2, 3, 4, 5, 6, 0x10, 0x05 };
	return matches( fixed_view{ std::span<const std::uint8_t, 6>( address ), 5 }, expected );
}

/// A message with a `const` nested struct is encode-only, also in a `std::optional` and a `std::variant`.
bool test_encode_const_nested()
{
	constexpr std::array<std::uint8_t, 16> expected{ 0x0a, 0x06, 0x0a, 0x02, 0x08, 0x01, 0x10, 0x02,
	                                                 0x12, 0x06, 0x0a, 0x02, 0x08, 0x03, 0x10, 0x04 };
	return matches( const_holder{ const_middle{ { 1, {} }, 2 }, const_middle{ { 3, {} }, 4 } }, expected );
}

} // namespace

int main()
{
	const bool ok = test_encode_scalars_zero() && test_encode_scalars_max() && test_encode_scalars_negative() &&
	                test_encode_scalars_enum_neg() && test_encode_text_short() && test_encode_text_long() &&
	                test_encode_outer_set() && test_encode_outer_unset() && test_encode_lists_empty() &&
	                test_encode_lists_full() && test_encode_pose_zero() && test_encode_envelope_none() &&
	                test_encode_envelope_pose() && test_encode_envelope_ping() && test_encode_envelope_long() &&
	                test_encode_spans() && test_encode_too_small() && test_encode_order() &&
	                test_encode_unsigned_enum() && test_encode_empty_message() && test_encode_deep() &&
	                test_encode_each_short_buffer() && test_encode_span_and_bytes() && test_encode_fixed_extent_span() &&
	                test_encode_const_nested();
	return ok ? 0 : 1;
}
