#include <array>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include <micropb/micropb.hpp>

// The standard integer types are accepted by size and sign, so a message with `int` or `long`
// members works on each target, whatever `int32_t` is there.

static_assert( pb::kind_of<signed char>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<short>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<int>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<long>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<long long>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<unsigned short>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<unsigned int>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<unsigned long>() == pb::field_kind::SCALAR );
static_assert( pb::kind_of<unsigned long long>() == pb::field_kind::SCALAR );
static_assert( pb::wire_type_of<long>() == pb::wire_type::VARINT );
static_assert( pb::kind_of<pb::repeated<signed char, 4>>() == pb::field_kind::REPEATED ); // not bytes
static_assert( pb::kind_of<pb::repeated<unsigned char, 4>>() == pb::field_kind::BYTES );
static_assert( pb::is_packed<signed char>() ); // the element type of a repeated field
static_assert( pb::wire_type_of<int16_t, pb::int_encoding::ZIGZAG>() == pb::wire_type::VARINT );

namespace {

/// The fixed-width type of the same size and sign as `long` and `unsigned long`.
using long_equivalent = std::conditional_t<sizeof( long ) == 4, int32_t, int64_t>;
using ulong_equivalent = std::conditional_t<sizeof( unsigned long ) == 4, uint32_t, uint64_t>;

struct standard
{
	int a;
	long b;
	unsigned long long c;
	short d;
	signed char e;
	unsigned int f;
	unsigned long g;
};

struct fixed_width
{
	int32_t a;
	long_equivalent b;
	uint64_t c;
	int16_t d;
	int8_t e;
	uint32_t f;
	ulong_equivalent g;
};

struct narrow
{
	int8_t a;
	int16_t b;
	int c;
	long long d;
	pb::repeated<int16_t, 3> e;
};

struct wide
{
	int32_t a;
	int32_t b;
	int32_t c;
	int64_t d;
	pb::repeated<int32_t, 3> e;
};

} // namespace

template <>
struct pb::fields_of<narrow>
  : pb::field_list<narrow,
                   pb::field_at<decltype( narrow::a ), 1, offsetof( narrow, a ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( narrow::b ), 2, offsetof( narrow, b ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( narrow::c ), 3, offsetof( narrow, c ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( narrow::d ), 4, offsetof( narrow, d ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( narrow::e ), 5, offsetof( narrow, e ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<wide>
  : pb::field_list<wide,
                   pb::field_at<decltype( wide::a ), 1, offsetof( wide, a ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( wide::b ), 2, offsetof( wide, b ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( wide::c ), 3, offsetof( wide, c ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( wide::d ), 4, offsetof( wide, d ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( wide::e ), 5, offsetof( wide, e ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<standard> : pb::field_list<standard,
                                                pb::field_at<decltype( standard::a ), 1, offsetof( standard, a )>,
                                                pb::field_at<decltype( standard::b ), 2, offsetof( standard, b )>,
                                                pb::field_at<decltype( standard::c ), 3, offsetof( standard, c )>,
                                                pb::field_at<decltype( standard::d ), 4, offsetof( standard, d )>,
                                                pb::field_at<decltype( standard::e ), 5, offsetof( standard, e )>,
                                                pb::field_at<decltype( standard::f ), 6, offsetof( standard, f )>,
                                                pb::field_at<decltype( standard::g ), 7, offsetof( standard, g )>>
{};

template <>
struct pb::fields_of<fixed_width>
  : pb::field_list<fixed_width,
                   pb::field_at<decltype( fixed_width::a ), 1, offsetof( fixed_width, a )>,
                   pb::field_at<decltype( fixed_width::b ), 2, offsetof( fixed_width, b )>,
                   pb::field_at<decltype( fixed_width::c ), 3, offsetof( fixed_width, c )>,
                   pb::field_at<decltype( fixed_width::d ), 4, offsetof( fixed_width, d )>,
                   pb::field_at<decltype( fixed_width::e ), 5, offsetof( fixed_width, e )>,
                   pb::field_at<decltype( fixed_width::f ), 6, offsetof( fixed_width, f )>,
                   pb::field_at<decltype( fixed_width::g ), 7, offsetof( fixed_width, g )>>
{};

namespace {

/// The standard integer types encode and decode like the fixed-width types of the same size and sign.
bool test_standard_integers()
{
	const standard value{ -1, -123456, 0xFFFF'FFFF'FFFF'FFFFull, -300, -5, 4000000000u, 4000000000ul };
	const fixed_width twin{ -1, -123456, 0xFFFF'FFFF'FFFF'FFFFull, -300, -5, 4000000000u, 4000000000ul };

	std::array<uint8_t, 96> bytes{};
	std::array<uint8_t, 96> twin_bytes{};
	const auto size = pb::encode( value, std::span<uint8_t>( bytes ) );
	const auto twin_size = pb::encode( twin, std::span<uint8_t>( twin_bytes ) );
	if ( !size || !twin_size || *size != *twin_size || bytes != twin_bytes )
		return false;
	if ( pb::encoded_size( value ) != *size || pb::encoded_size( twin ) != *size )
		return false;

	standard decoded{};
	const auto used = pb::decode( std::span<const uint8_t>( bytes.data(), *size ), decoded );
	return used && *used == *size && decoded.a == value.a && decoded.b == value.b && decoded.c == value.c &&
	       decoded.d == value.d && decoded.e == value.e && decoded.f == value.f && decoded.g == value.g;
}

/// A narrow ZigZag member writes the same bytes as `sint32` and `sint64`.
bool test_narrow_zigzag()
{
	narrow n{};
	n.a = int8_t{ -128 };
	n.b = int16_t{ -32768 };
	n.c = -1;
	n.d = -5'000'000'000ll;
	( void )n.e.push_back( int16_t{ -1 } );
	( void )n.e.push_back( int16_t{ 0 } );
	( void )n.e.push_back( int16_t{ 32767 } );

	wide w{};
	w.a = -128;
	w.b = -32768;
	w.c = -1;
	w.d = int64_t{ -5'000'000'000ll };
	( void )w.e.push_back( -1 );
	( void )w.e.push_back( 0 );
	( void )w.e.push_back( 32767 );

	std::array<uint8_t, 64> narrow_bytes{};
	std::array<uint8_t, 64> wide_bytes{};
	const auto narrow_size = pb::encode( n, std::span<uint8_t>( narrow_bytes ) );
	const auto wide_size = pb::encode( w, std::span<uint8_t>( wide_bytes ) );
	if ( !narrow_size || !wide_size || *narrow_size != *wide_size || narrow_bytes != wide_bytes )
		return false;
	if ( pb::encoded_size( n ) != *narrow_size || pb::encoded_size( w ) != *wide_size )
		return false;

	narrow decoded{};
	const auto used = pb::decode( std::span<const uint8_t>( narrow_bytes.data(), *narrow_size ), decoded );
	if ( !used || *used != *narrow_size || decoded.a != n.a || decoded.b != n.b || decoded.c != n.c || decoded.d != n.d ||
	     decoded.e.size() != 3 || decoded.e[0] != -1 || decoded.e[1] != 0 || decoded.e[2] != 32767 )
		return false;

	// A value that is too wide for the member loses its upper bits, as for `int32` into 16 bits.
	wide big{};
	big.b = 100000;
	std::array<uint8_t, 64> big_bytes{};
	const auto big_size = pb::encode( big, std::span<uint8_t>( big_bytes ) );
	if ( !big_size )
		return false;
	narrow truncated{};
	const auto truncated_used = pb::decode( std::span<const uint8_t>( big_bytes.data(), *big_size ), truncated );
	return truncated_used && truncated.b == static_cast<int16_t>( 100000 );
}

/// A `sint64` value that does not fit the member. The two decode rules differ here (see the specification):
/// a 4-byte member truncates the varint to 32 bits and then decodes ZigZag, as Google does for `sint32`;
/// a 1-, 2- or 8-byte member decodes 64-bit ZigZag and then truncates.
/// 2^31 is the ZigZag varint 2^32. 4 bytes: the low 32 bits of 2^32 are 0, and ZigZag(0) is 0.
/// 2 bytes: ZigZag-decoding 2^32 gives 2^31, whose low 16 bits are 0.
struct one_sint64
{
	int64_t a;
};
struct one_sint32
{
	int32_t a;
};
struct one_zz16
{
	int16_t a;
};

} // namespace

template <>
struct pb::fields_of<one_sint64>
  : pb::field_list<one_sint64,
                   pb::field_at<decltype( one_sint64::a ), 1, offsetof( one_sint64, a ), 0, pb::int_encoding::ZIGZAG>>
{};
template <>
struct pb::fields_of<one_sint32>
  : pb::field_list<one_sint32,
                   pb::field_at<decltype( one_sint32::a ), 1, offsetof( one_sint32, a ), 0, pb::int_encoding::ZIGZAG>>
{};
template <>
struct pb::fields_of<one_zz16>
  : pb::field_list<one_zz16,
                   pb::field_at<decltype( one_zz16::a ), 1, offsetof( one_zz16, a ), 0, pb::int_encoding::ZIGZAG>>
{};

namespace {

bool test_out_of_range_sint64()
{
	one_sint64 source{};
	source.a = int64_t{ 2147483648 };
	std::array<uint8_t, 16> bytes{};
	const auto size = pb::encode( source, std::span<uint8_t>( bytes ) );
	if ( !size )
		return false;
	const std::span<const uint8_t> input( bytes.data(), *size );

	one_sint32 four{};
	four.a = 7;
	const auto four_used = pb::decode( input, four );
	if ( !four_used || four.a != 0 )
		return false;

	one_zz16 two{};
	two.a = int16_t{ 7 };
	const auto two_used = pb::decode( input, two );
	return two_used && two.a == 0;
}

} // namespace

int main()
{
	const bool ok = test_standard_integers() && test_narrow_zigzag() && test_out_of_range_sint64();
	return ok ? 0 : 1;
}
