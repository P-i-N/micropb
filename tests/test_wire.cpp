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

} // namespace

int main()
{
	const bool ok = test_varint_size() && test_zigzag() && test_write_varint() && test_write_fixed() &&
	                test_write_key() && test_writer_full() && test_writer_exact() && test_write_text() &&
	                test_writer_counts() && test_writer_saturates() && test_read_varint() && test_read_fixed() &&
	                test_read_key() && test_read_length() && test_skip() && test_reader_sticky() && test_reader_absorb();
	return ok ? 0 : 1;
}
