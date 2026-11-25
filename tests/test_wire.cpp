#include <array>
#include <initializer_list>
#include <limits>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include <micropb/wire.hpp>

using namespace pb;

namespace {

constexpr uint64_t u64_max = std::numeric_limits<uint64_t>::max();

template <size_t N>
constexpr bool equal( const std::array<uint8_t, N>& actual, const std::array<uint8_t, N>& expected )
{
	return actual == expected;
}

constexpr bool test_varint_size()
{
	return detail::varint_size( 0 ) == 1 && detail::varint_size( 127 ) == 1 && detail::varint_size( 128 ) == 2 &&
	       detail::varint_size( 16383 ) == 2 && detail::varint_size( 16384 ) == 3 && detail::varint_size( u64_max ) == 10;
}

constexpr bool test_zigzag()
{
	constexpr auto i32_max = std::numeric_limits<int32_t>::max();
	constexpr auto i32_min = std::numeric_limits<int32_t>::min();
	constexpr auto i64_min = std::numeric_limits<int64_t>::min();

	if ( detail::zigzag_encode( int32_t{ 0 } ) != 0 || detail::zigzag_encode( int32_t{ -1 } ) != 1 ||
	     detail::zigzag_encode( int32_t{ 1 } ) != 2 || detail::zigzag_encode( int32_t{ -2 } ) != 3 )
		return false;
	if ( detail::zigzag_encode( i32_max ) != 0xFFFFFFFEu || detail::zigzag_encode( i32_min ) != 0xFFFFFFFFu )
		return false;
	if ( detail::zigzag_encode( i64_min ) != u64_max )
		return false;

	for ( int32_t v : std::initializer_list<int32_t>{ 0, -1, 1, -2, 2, i32_max, i32_min } )
		if ( detail::zigzag_decode( detail::zigzag_encode( v ) ) != v )
			return false;
	for ( int64_t v : { int64_t{ 0 },
	                         int64_t{ -1 },
	                         int64_t{ 1 },
	                         i64_min,
	                         std::numeric_limits<int64_t>::max() } )
		if ( detail::zigzag_decode( detail::zigzag_encode( v ) ) != v )
			return false;
	return true;
}

constexpr bool test_write_varint()
{
	{
		std::array<uint8_t, 2> buf{};
		writer w( buf );
		w.write_varint( 150 );
		if ( !w.fits() || w.size() != 2 || !equal( buf, { 0x96, 0x01 } ) )
			return false;
	}
	{
		std::array<uint8_t, 2> buf{};
		writer w( buf );
		w.write_varint( 300 );
		if ( !w.fits() || !equal( buf, { 0xAC, 0x02 } ) )
			return false;
	}
	{
		std::array<uint8_t, 10> buf{};
		writer w( buf );
		w.write_varint( u64_max );
		if ( !w.fits() || w.size() != 10 )
			return false;
		return equal( buf, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 } );
	}
}

constexpr bool test_write_fixed()
{
	std::array<uint8_t, 4> a{};
	writer w32( a );
	w32.write_fixed32( 0x12345678 );
	if ( !w32.fits() || !equal( a, { 0x78, 0x56, 0x34, 0x12 } ) )
		return false;

	std::array<uint8_t, 8> b{};
	writer w64( b );
	w64.write_fixed64( 1 );
	return w64.fits() && equal( b, { 1, 0, 0, 0, 0, 0, 0, 0 } );
}

constexpr bool test_write_key()
{
	std::array<uint8_t, 4> buf{};
	writer w( buf );
	w.write_key( 1, wire_type::VARINT );
	w.write_key( 2, wire_type::LENGTH_DELIMITED );
	w.write_key( 16, wire_type::VARINT );
	return w.fits() && w.size() == 4 && equal( buf, { 0x08, 0x12, 0x80, 0x01 } );
}

/// A writer that is full counts the bytes and does not write past its span.
constexpr bool test_writer_full()
{
	std::array<uint8_t, 2> buf{ 0, 0xEE };
	writer w( std::span<uint8_t>( buf ).first( 1 ) );
	if ( w.counts_only() )
		return false;

	w.write_varint( 300 );
	if ( w.fits() || !w.counts_only() || w.size() != 2 || buf[0] != 0xAC || buf[1] != 0xEE )
		return false;

	w.write_fixed32( 1 );
	w.write_fixed64( 1 );
	if ( w.fits() || w.size() != 14 || buf[0] != 0xAC || buf[1] != 0xEE )
		return false;

	constexpr std::array<uint8_t, 2> data{ 1, 2 };
	w.write_bytes( data );
	return !w.fits() && w.size() == 16 && buf[0] == 0xAC && buf[1] == 0xEE;
}

/// A writer that fills its span exactly fits. One more byte does not.
constexpr bool test_writer_exact()
{
	std::array<uint8_t, 2> buf{};
	writer w( buf );
	w.write_varint( 300 );
	if ( !w.fits() || !w.counts_only() || w.size() != 2 )
		return false;

	w.write_varint( 0 );
	return !w.fits() && w.size() == 3 && equal( buf, { 0xAC, 0x02 } );
}

constexpr bool test_write_text()
{
	std::array<uint8_t, 3> buf{ 0xEE, 0xEE, 0xEE };
	writer w( buf );
	w.write_bytes( std::string_view( "az" ) );
	if ( !w.fits() || w.size() != 2 )
		return false;

	w.write_bytes( std::string_view( "bc" ) );
	return !w.fits() && w.size() == 4 && buf[0] == 'a' && buf[1] == 'z' && buf[2] == 0xEE;
}

/// A writer without a span gives the size that the output needs.
constexpr bool test_writer_counts()
{
	constexpr std::array<uint8_t, 3> data{ 1, 2, 3 };
	writer w;
	if ( !w.fits() || !w.counts_only() || w.size() != 0 )
		return false;

	w.write_key( 16, wire_type::VARINT );
	w.write_varint( 300 );
	w.write_fixed32( 1 );
	w.write_fixed64( 1 );
	w.write_bytes( data );
	w.advance( 5 );
	return !w.fits() && w.size() == 2 + 2 + 4 + 8 + 3 + 5;
}

/// The count stops at the largest `size_t`. It does not go back to 0.
constexpr bool test_writer_saturates()
{
	constexpr size_t limit = std::numeric_limits<size_t>::max();
	std::array<uint8_t, 2> buf{ 0xEE, 0xEE };
	writer w( buf );
	w.advance( limit - 1 );
	w.advance( 2 );
	w.write_varint( 300 );
	w.write_bytes( buf );
	return !w.fits() && w.size() == limit && buf[0] == 0xEE && buf[1] == 0xEE;
}

constexpr bool fails_with( const reader& r, error expected )
{
	const auto status = r.status();
	return r.failed() && r.empty() && !status && status.error() == expected;
}

constexpr bool test_read_varint()
{
	{
		constexpr std::array<uint8_t, 2> in{ 0x96, 0x01 };
		reader r( in );
		if ( r.read_varint() != 150 || r.failed() || !r.status() || !r.empty() )
			return false;
	}
	{
		constexpr std::array<uint8_t, 1> in{ 0x80 };
		reader r( in );
		if ( r.read_varint() != 0 || !fails_with( r, error::TRUNCATED ) )
			return false;
	}
	{
		constexpr std::array<uint8_t, 12> in{ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01 };
		reader r( in );
		if ( r.read_varint() != 0 || !fails_with( r, error::INVALID_VARINT ) )
			return false;
	}
	{
		// The tenth byte has more than the one bit that fits in 64 bits.
		constexpr std::array<uint8_t, 10> in{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02 };
		reader r( in );
		if ( r.read_varint() != 0 || !fails_with( r, error::INVALID_VARINT ) )
			return false;
	}
	{
		constexpr std::array<uint8_t, 10> in{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
		reader r( in );
		return r.read_varint() == u64_max && !r.failed();
	}
}

constexpr bool test_read_fixed()
{
	{
		constexpr std::array<uint8_t, 12> in{ 0x78, 0x56, 0x34, 0x12, 1, 0, 0, 0, 0, 0, 0, 0x80 };
		reader r( in );
		if ( r.read_fixed32() != 0x12345678 || r.read_fixed64() != 0x8000000000000001u || r.failed() || !r.empty() )
			return false;
	}
	{
		constexpr std::array<uint8_t, 3> in{ 1, 2, 3 };
		reader r( in );
		if ( r.read_fixed32() != 0 || !fails_with( r, error::TRUNCATED ) )
			return false;
	}
	constexpr std::array<uint8_t, 7> in{ 1, 2, 3, 4, 5, 6, 7 };
	reader r( in );
	return r.read_fixed64() == 0 && fails_with( r, error::TRUNCATED );
}

constexpr bool key_error( std::span<const uint8_t> in, error expected )
{
	reader r( in );
	const field_key k = r.read_key();
	return k.number == 0 && fails_with( r, expected );
}

constexpr bool test_read_key()
{
	{
		constexpr std::array<uint8_t, 1> in{ 0x08 };
		reader r( in );
		const field_key k = r.read_key();
		if ( r.failed() || k.number != 1 || k.type != wire_type::VARINT )
			return false;
	}
	{
		constexpr std::array<uint8_t, 2> in{ 0x85, 0x01 };
		reader r( in );
		const field_key k = r.read_key();
		if ( r.failed() || k.number != 16 || k.type != wire_type::FIXED32 )
			return false;
	}
	constexpr std::array<uint8_t, 1> zero{ 0x00 };
	constexpr std::array<uint8_t, 1> type3{ 0x0B };
	constexpr std::array<uint8_t, 1> type4{ 0x0C };
	constexpr std::array<uint8_t, 1> type6{ 0x0E };
	constexpr std::array<uint8_t, 1> type7{ 0x0F };
	constexpr std::array<uint8_t, 1> cut{ 0x80 };
	// number 2^29, key 2^32
	constexpr std::array<uint8_t, 5> big{ 0x80, 0x80, 0x80, 0x80, 0x10 };
	return key_error( zero, error::INVALID_TAG ) && key_error( type3, error::INVALID_WIRE_TYPE ) &&
	       key_error( type4, error::INVALID_WIRE_TYPE ) && key_error( type6, error::INVALID_WIRE_TYPE ) &&
	       key_error( type7, error::INVALID_WIRE_TYPE ) && key_error( big, error::INVALID_TAG ) &&
	       key_error( cut, error::TRUNCATED );
}

constexpr bool length_error( std::span<const uint8_t> in )
{
	reader r( in );
	return r.read_length_delimited().empty() && fails_with( r, error::TRUNCATED );
}

constexpr bool test_read_length()
{
	{
		constexpr std::array<uint8_t, 3> in{ 0x02, 0xAA, 0xBB };
		reader r( in );
		const auto v = r.read_length_delimited();
		if ( r.failed() || v.size() != 2 || v.data() != in.data() + 1 || !r.empty() )
			return false;
	}
	constexpr std::array<uint8_t, 1> cut{ 0x80 };
	constexpr std::array<uint8_t, 2> short_in{ 0x05, 0xAA };
	constexpr std::array<uint8_t, 5> near32{ 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
	constexpr std::array<uint8_t, 10> near64{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
	return length_error( cut ) && length_error( short_in ) && length_error( near32 ) && length_error( near64 );
}

constexpr bool test_skip()
{
	{
		constexpr std::array<uint8_t, 2> in{ 0x96, 0x01 };
		reader r( in );
		r.skip( wire_type::VARINT );
		if ( r.failed() || !r.empty() )
			return false;
	}
	{
		constexpr std::array<uint8_t, 4> in{ 1, 2, 3, 4 };
		reader r( in );
		r.skip( wire_type::FIXED32 );
		if ( r.failed() || !r.empty() )
			return false;
	}
	{
		constexpr std::array<uint8_t, 8> in{ 1, 2, 3, 4, 5, 6, 7, 8 };
		reader r( in );
		r.skip( wire_type::FIXED64 );
		if ( r.failed() || !r.empty() )
			return false;
	}
	{
		constexpr std::array<uint8_t, 3> in{ 0x01, 0xAA, 0x07 };
		reader r( in );
		r.skip( wire_type::LENGTH_DELIMITED );
		if ( r.failed() || r.empty() || r.read_varint() != 7 || !r.empty() )
			return false;
	}
	constexpr std::array<uint8_t, 3> in{ 1, 2, 3 };
	reader r( in );
	r.skip( wire_type::FIXED64 );
	return fails_with( r, error::TRUNCATED );
}

/// After the first failure each read returns 0 or an empty span, and the first error stays.
constexpr bool test_reader_sticky()
{
	constexpr std::array<uint8_t, 4> in{ 0x80, 0x01, 0x02, 0x03 };
	reader r( std::span<const uint8_t>( in ).first( 1 ) );
	if ( r.read_varint() != 0 || !fails_with( r, error::TRUNCATED ) )
		return false;

	r.fail( error::CAPACITY_EXCEEDED );
	r.skip( wire_type::FIXED32 );
	const bool zeros = r.read_varint() == 0 && r.read_fixed32() == 0 && r.read_fixed64() == 0 &&
	                   r.read_length_delimited().empty() && r.read_key().number == 0;
	return zeros && fails_with( r, error::TRUNCATED );
}

/// The reader of a section gives its error to the reader of the complete input.
constexpr bool test_reader_absorb()
{
	constexpr std::array<uint8_t, 3> in{ 0x01, 0x80, 0x07 };
	reader r( in );
	reader good( r.read_length_delimited() );
	r.absorb( good );
	if ( r.failed() || r.empty() )
		return false;

	reader bad( std::span<const uint8_t>( in ).subspan( 1, 1 ) );
	static_cast<void>( bad.read_varint() );
	r.absorb( bad );
	return fails_with( r, error::TRUNCATED ) && r.read_varint() == 0;
}

constexpr bool test_writer_and_reader_spans()
{
	std::array<uint8_t, 5> buf{};
	writer w( buf );
	if ( !w.used_span().empty() || w.remaining_span().size() != 5 || w.remaining_span().data() != buf.data() )
		return false;

	w.write_varint( 150 ); // 2 bytes: 0x96 0x01
	if ( w.used_span().size() != 2 || w.used_span().data() != buf.data() || w.remaining_span().size() != 3 ||
	     w.remaining_span().data() != buf.data() + 2 )
		return false;

	w.write_fixed32( 0x04030201u ); // overflows buffer (pos == 6 > 5)
	if ( w.fits() || w.used_span().size() != 5 || !w.remaining_span().empty() )
		return false;

	// Counting-only writer has empty spans.
	writer counter;
	counter.write_varint( 42 );
	if ( !counter.used_span().empty() || !counter.remaining_span().empty() )
		return false;

	constexpr std::array<uint8_t, 4> in{ 0x96, 0x01, 0x05, 0x06 };
	reader r( in );
	if ( !r.used_span().empty() || r.remaining_span().size() != 4 || r.remaining_span().data() != in.data() )
		return false;

	if ( r.read_varint() != 150 )
		return false;
	if ( r.used_span().size() != 2 || r.used_span().data() != in.data() || r.remaining_span().size() != 2 ||
	     r.remaining_span().data() != in.data() + 2 )
		return false;

	r.fail( error::TRUNCATED );
	return r.used_span().size() == 4 && r.remaining_span().empty();
}

enum class signed_mode : int8_t
{
	NEG = -1,
	ZERO = 0,
	POS = 1,
};

enum class unsigned_mode : uint8_t
{
	ZERO = 0,
	MAX = 255,
};

template <typename T>
concept can_read_varint = requires( reader& r, T& out ) { r.read_varint( out ); };

template <typename T>
concept can_read_zigzag = requires( reader& r, T& out ) { r.read_zigzag( out ); };

template <typename T>
concept can_read_fixed = requires( reader& r, T& out ) { r.read_fixed( out ); };

static_assert( can_read_varint<uint8_t> && can_read_varint<int32_t> && can_read_varint<bool> &&
               can_read_varint<signed_mode> && can_read_varint<unsigned_mode> );
static_assert( !can_read_varint<const uint32_t> && !can_read_varint<float> && !can_read_varint<char> );
static_assert( can_read_zigzag<int8_t> && can_read_zigzag<int32_t> && can_read_zigzag<int64_t> );
static_assert( !can_read_zigzag<uint32_t> && !can_read_zigzag<bool> && !can_read_zigzag<float> );
static_assert( can_read_fixed<uint32_t> && can_read_fixed<int32_t> && can_read_fixed<uint64_t> &&
               can_read_fixed<int64_t> && can_read_fixed<float> && can_read_fixed<double> );
static_assert( !can_read_fixed<uint16_t> && !can_read_fixed<int8_t> && !can_read_fixed<bool> );

template <typename T>
constexpr bool varint_round_trip( uint64_t raw, T expected )
{
	std::array<uint8_t, 10> buf{};
	writer w( buf );
	w.write_varint( raw );
	reader r( w.used_span() );
	T out{};
	return r.read_varint( out ) && out == expected && !r.failed() && r.empty();
}

template <typename T>
constexpr bool varint_fails( uint64_t raw, error expected_error, T sentinel )
{
	std::array<uint8_t, 10> buf{};
	writer w( buf );
	w.write_varint( raw );
	reader r( w.used_span() );
	T out = sentinel;
	return !r.read_varint( out ) && out == sentinel && fails_with( r, expected_error );
}

constexpr bool test_read_varint_typed()
{
	// Unsigned limits and out-of-range.
	if ( !varint_round_trip<uint8_t>( 0, 0 ) || !varint_round_trip<uint8_t>( 255, 255 ) ||
	     !varint_fails<uint8_t>( 256, error::OUT_OF_RANGE, 42 ) )
		return false;
	if ( !varint_round_trip<uint16_t>( 65535, 65535 ) || !varint_fails<uint16_t>( 65536, error::OUT_OF_RANGE, 42 ) )
		return false;
	if ( !varint_round_trip<uint32_t>( 0xFFFFFFFFu, 0xFFFFFFFFu ) ||
	     !varint_fails<uint32_t>( 0x100000000ull, error::OUT_OF_RANGE, 42 ) )
		return false;
	if ( !varint_round_trip<uint64_t>( u64_max, u64_max ) )
		return false;

	// Signed limits (proto3 sign-extends negative varints to 64 bits).
	if ( !varint_round_trip<int8_t>( 127, 127 ) ||
	     !varint_round_trip<int8_t>( static_cast<uint64_t>( int64_t{ -128 } ), -128 ) ||
	     !varint_round_trip<int8_t>( u64_max, -1 ) || !varint_fails<int8_t>( 128, error::OUT_OF_RANGE, 7 ) ||
	     !varint_fails<int8_t>( static_cast<uint64_t>( int64_t{ -129 } ), error::OUT_OF_RANGE, 7 ) )
		return false;
	if ( !varint_round_trip<int32_t>( 0x7FFFFFFFu, std::numeric_limits<int32_t>::max() ) ||
	     !varint_round_trip<int32_t>( static_cast<uint64_t>( int64_t{ std::numeric_limits<int32_t>::min() } ),
	                                  std::numeric_limits<int32_t>::min() ) ||
	     !varint_round_trip<int32_t>( u64_max, -1 ) ||
	     !varint_fails<int32_t>( 0x80000000ull, error::OUT_OF_RANGE, 99 ) ||
	     !varint_fails<int32_t>( static_cast<uint64_t>( int64_t{ std::numeric_limits<int32_t>::min() } - 1 ),
	                             error::OUT_OF_RANGE,
	                             99 ) )
		return false;
	if ( !varint_round_trip<int64_t>( static_cast<uint64_t>( std::numeric_limits<int64_t>::min() ),
	                                  std::numeric_limits<int64_t>::min() ) ||
	     !varint_round_trip<int64_t>( static_cast<uint64_t>( std::numeric_limits<int64_t>::max() ),
	                                  std::numeric_limits<int64_t>::max() ) )
		return false;

	// Bool follows proto3: 0 is false, any non-zero is true.
	if ( !varint_round_trip<bool>( 0, false ) || !varint_round_trip<bool>( 1, true ) ||
	     !varint_round_trip<bool>( 2, true ) || !varint_round_trip<bool>( u64_max, true ) )
		return false;

	// Enums check underlying type range.
	if ( !varint_round_trip<signed_mode>( u64_max, signed_mode::NEG ) ||
	     !varint_fails<signed_mode>( 128, error::OUT_OF_RANGE, signed_mode::POS ) ||
	     !varint_round_trip<unsigned_mode>( 255, unsigned_mode::MAX ) ||
	     !varint_fails<unsigned_mode>( 256, error::OUT_OF_RANGE, unsigned_mode::ZERO ) )
		return false;

	// Truncated and already-failed readers leave `out` unchanged and keep the first error.
	constexpr std::array<uint8_t, 1> cut{ 0x80 };
	reader r( cut );
	uint32_t u = 123;
	if ( r.read_varint( u ) || u != 123 || !fails_with( r, error::TRUNCATED ) )
		return false;
	bool b = true;
	return !r.read_varint( b ) && b && fails_with( r, error::TRUNCATED );
}

constexpr bool test_read_zigzag_typed()
{
	auto round_trip = []<typename T>( int64_t wire_value, T expected ) {
		std::array<uint8_t, 10> buf{};
		writer w( buf );
		w.write_varint( detail::zigzag_encode( wire_value ) );
		reader r( w.used_span() );
		T out{};
		return r.read_zigzag( out ) && out == expected && !r.failed() && r.empty();
	};
	auto fails_range = []<typename T>( int64_t wire_value, T sentinel ) {
		std::array<uint8_t, 10> buf{};
		writer w( buf );
		w.write_varint( detail::zigzag_encode( wire_value ) );
		reader r( w.used_span() );
		T out = sentinel;
		return !r.read_zigzag( out ) && out == sentinel && fails_with( r, error::OUT_OF_RANGE );
	};

	if ( !round_trip( 0, int8_t{ 0 } ) || !round_trip( -1, int8_t{ -1 } ) || !round_trip( 127, int8_t{ 127 } ) ||
	     !round_trip( -128, int8_t{ -128 } ) || !fails_range( 128, int8_t{ 5 } ) || !fails_range( -129, int8_t{ 5 } ) )
		return false;

	constexpr auto i32_max = std::numeric_limits<int32_t>::max();
	constexpr auto i32_min = std::numeric_limits<int32_t>::min();
	if ( !round_trip( i32_max, i32_max ) || !round_trip( i32_min, i32_min ) ||
	     !fails_range( int64_t{ i32_max } + 1, int32_t{ 7 } ) || !fails_range( int64_t{ i32_min } - 1, int32_t{ 7 } ) )
		return false;

	constexpr auto i64_max = std::numeric_limits<int64_t>::max();
	constexpr auto i64_min = std::numeric_limits<int64_t>::min();
	if ( !round_trip( i64_max, i64_max ) || !round_trip( i64_min, i64_min ) )
		return false;

	constexpr std::array<uint8_t, 1> cut{ 0x80 };
	reader r( cut );
	int32_t out = 55;
	return !r.read_zigzag( out ) && out == 55 && fails_with( r, error::TRUNCATED );
}

constexpr bool test_read_fixed_typed()
{
	std::array<uint8_t, 36> buf{};
	writer w( buf );
	w.write_fixed32( 0x12345678u );
	w.write_fixed32( std::bit_cast<uint32_t>( int32_t{ -42 } ) );
	w.write_fixed32( std::bit_cast<uint32_t>( -1.5f ) );
	w.write_fixed64( 0x8000000000000001ull );
	w.write_fixed64( std::bit_cast<uint64_t>( int64_t{ -99 } ) );
	w.write_fixed64( std::bit_cast<uint64_t>( 3.25 ) );

	reader r( w.used_span() );
	uint32_t u32 = 0;
	int32_t i32 = 0;
	float f = 0.0f;
	uint64_t u64 = 0;
	int64_t i64 = 0;
	double d = 0.0;
	if ( !r.read_fixed( u32 ) || !r.read_fixed( i32 ) || !r.read_fixed( f ) || !r.read_fixed( u64 ) ||
	     !r.read_fixed( i64 ) || !r.read_fixed( d ) )
		return false;
	if ( u32 != 0x12345678u || i32 != -42 || f != -1.5f || u64 != 0x8000000000000001ull || i64 != -99 || d != 3.25 ||
	     !r.empty() )
		return false;

	// Truncated leaves `out` unchanged.
	uint32_t sentinel32 = 77;
	double sentinel64 = 9.5;
	return !r.read_fixed( sentinel32 ) && sentinel32 == 77 && fails_with( r, error::TRUNCATED ) &&
	       !r.read_fixed( sentinel64 ) && sentinel64 == 9.5;
}

constexpr bool test_read_bytes_typed()
{
	constexpr std::array<uint8_t, 4> in{ 0x02, 0xAA, 0xBB, 0x00 };
	reader r( in );
	std::span<const uint8_t> out;
	if ( !r.read_bytes( out ) || out.size() != 2 || out[0] != 0xAA || out[1] != 0xBB )
		return false;
	if ( !r.read_bytes( out ) || !out.empty() || !r.empty() )
		return false;

	constexpr std::array<uint8_t, 2> sentinel{ 0x11, 0x22 };
	out = sentinel;
	return !r.read_bytes( out ) && out.data() == sentinel.data() && out.size() == 2 &&
	       fails_with( r, error::TRUNCATED );
}

bool test_read_string_typed()
{
	constexpr std::array<uint8_t, 4> in{ 0x02, 'o', 'k', 0x00 };
	reader r( in );
	std::string_view out = "init";
	if ( !r.read_string( out ) || out != "ok" )
		return false;
	if ( !r.read_string( out ) || !out.empty() || !r.empty() )
		return false;

	out = "keep";
	return !r.read_string( out ) && out == "keep" && fails_with( r, error::TRUNCATED );
}

////////////////////////////////////////////////////////////////////////////////

static_assert( test_varint_size() );
static_assert( test_zigzag() );
static_assert( test_write_varint() );
static_assert( test_write_fixed() );
static_assert( test_write_key() );
static_assert( test_writer_full() );
static_assert( test_writer_exact() );
static_assert( test_write_text() );
static_assert( test_writer_counts() );
static_assert( test_writer_saturates() );
static_assert( test_read_varint() );
static_assert( test_read_fixed() );
static_assert( test_read_key() );
static_assert( test_read_length() );
static_assert( test_skip() );
static_assert( test_reader_sticky() );
static_assert( test_reader_absorb() );
static_assert( test_writer_and_reader_spans() );
static_assert( test_read_varint_typed() );
static_assert( test_read_zigzag_typed() );
static_assert( test_read_fixed_typed() );
static_assert( test_read_bytes_typed() );

} // namespace

int main()
{
	const bool ok = test_varint_size() && test_zigzag() && test_write_varint() && test_write_fixed() &&
	                test_write_key() && test_writer_full() && test_writer_exact() && test_write_text() &&
	                test_writer_counts() && test_writer_saturates() && test_read_varint() && test_read_fixed() &&
	                test_read_key() && test_read_length() && test_skip() && test_reader_sticky() &&
	                test_reader_absorb() && test_writer_and_reader_spans() && test_read_varint_typed() &&
	                test_read_zigzag_typed() && test_read_fixed_typed() && test_read_bytes_typed() &&
	                test_read_string_typed();
	return ok ? 0 : 1;
}
