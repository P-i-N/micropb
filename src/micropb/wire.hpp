#pragma once

#include <algorithm>
#include <bit>
#include <cstring>
#include <expected>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include <micropb/types.hpp>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

namespace detail {
/// Largest field number of proto3.
inline constexpr uint32_t max_field_number = ( 1u << 29 ) - 1;

/// Number of bytes that `value` uses as a varint (1 to 10).
[[nodiscard]] constexpr size_t varint_size( uint64_t value )
{
	size_t size = 1;
	while ( value >= 0x80 )
	{
		value >>= 7;
		++size;
	}
	return size;
}

[[nodiscard]] constexpr uint32_t zigzag_encode( int32_t value )
{
	return ( static_cast<uint32_t>( value ) << 1 ) ^ static_cast<uint32_t>( value >> 31 );
}

[[nodiscard]] constexpr uint64_t zigzag_encode( int64_t value )
{
	return ( static_cast<uint64_t>( value ) << 1 ) ^ static_cast<uint64_t>( value >> 63 );
}

[[nodiscard]] constexpr int32_t zigzag_decode( uint32_t value )
{
	return static_cast<int32_t>( ( value >> 1 ) ^ ( ~( value & 1 ) + 1 ) );
}

[[nodiscard]] constexpr int64_t zigzag_decode( uint64_t value )
{
	return static_cast<int64_t>( ( value >> 1 ) ^ ( ~( value & 1 ) + 1 ) );
}

} // namespace detail

////////////////////////////////////////////////////////////////////////////////

/// Field number and wire type of one field in the encoded stream.
struct field_key
{
	uint32_t number;
	wire_type type;
};

////////////////////////////////////////////////////////////////////////////////

/// Writes wire primitives into a fixed buffer. The writer does not write a byte
/// that does not fit, but it counts the byte: `size()` is the size that the
/// output needs, and `fits()` tells if the span holds all of it. A writer
/// without a span only counts. The writer never writes past the span.
struct writer
{
	constexpr writer() = default;
	constexpr explicit writer( std::span<uint8_t> out ) : _out( out ) {}

	constexpr void write_varint( uint64_t value )
	{
		while ( value >= 0x80 )
		{
			put( static_cast<uint8_t>( value | 0x80 ) );
			value >>= 7;
		}
		put( static_cast<uint8_t>( value ) );
	}

	constexpr void write_fixed32( uint32_t value ) { write_le( value, 4 ); }

	constexpr void write_fixed64( uint64_t value ) { write_le( value, 8 ); }

	/// Writes all items of `bytes`, or none of them when they do not all fit.
	/// `bytes` is a contiguous range of `uint8_t` or `char`.
	template <typename B>
	constexpr void write_bytes( const B& bytes )
	{
		if ( _pos <= _out.size() && bytes.size() <= _out.size() - _pos )
			std::copy( bytes.begin(), bytes.end(), _out.data() + _pos );
		advance( bytes.size() );
	}

	constexpr void write_key( uint32_t number, wire_type type )
	{
		write_varint( ( static_cast<uint64_t>( number ) << 3 ) | static_cast<uint64_t>( type ) );
	}

	/// Counts `count` bytes and writes nothing. The count stops at the largest
	/// `size_t`: a size that is too large for the type never fits.
	constexpr void advance( size_t count )
	{
		constexpr size_t limit = size_t( -1 );
		_pos = count > limit - _pos ? limit : _pos + count;
	}

	/// `true` when the span has no space: from here, the writer only counts the bytes.
	[[nodiscard]] constexpr bool counts_only() const { return _pos >= _out.size(); }

	/// Number of bytes that the output needs. It is the number of bytes that
	/// the writer wrote only when `fits()` is `true`.
	[[nodiscard]] constexpr size_t size() const { return _pos; }

	[[nodiscard]] constexpr bool fits() const { return _pos <= _out.size(); }

	/// Prefix of the buffer that the writer has used, clamped to the buffer size.
	[[nodiscard]] constexpr std::span<uint8_t> used_span() const
	{
		return _out.first( _pos < _out.size() ? _pos : _out.size() );
	}

	/// Unused suffix of the buffer. Empty when full or when `fits()` is `false`.
	[[nodiscard]] constexpr std::span<uint8_t> remaining_span() const
	{
		return _pos < _out.size() ? _out.subspan( _pos ) : std::span<uint8_t>{};
	}

private:
	constexpr void put( uint8_t byte )
	{
		if ( _pos < _out.size() )
		{
#ifdef MICROPB_FAST
			_out[_pos++] = byte;
			return;
#else
			_out[_pos] = byte;
#endif
		}
		advance( 1 );
	}

	constexpr void write_le( uint64_t value, size_t count )
	{
		if ( !std::is_constant_evaluated() )
		{
			if constexpr ( std::endian::native == std::endian::little )
			{
				if ( _pos <= _out.size() && count <= _out.size() - _pos )
					std::memcpy( _out.data() + _pos, &value, count );
				advance( count );
				return;
			}
		}
		for ( size_t i = 0; i < count; ++i )
			put( static_cast<uint8_t>( value >> ( 8 * i ) ) );
	}

	std::span<uint8_t> _out;
	size_t _pos = 0;
};

////////////////////////////////////////////////////////////////////////////////

/// Reads wire primitives from a byte span. Every read checks the remaining
/// bytes first, so the reader never reads past the span.
/// The first read that fails sets the error and moves the reader to the end of
/// the input. Each read after that returns 0 or an empty span, and the error
/// stays. The caller can read many values without a check, and examine
/// `failed()` or `status()` after them.
struct reader
{
	/// Largest field number of proto3.
	static constexpr uint32_t max_field_number = detail::max_field_number;

	constexpr explicit reader( std::span<const uint8_t> in ) : _in( in ) {}

	[[nodiscard]] constexpr uint64_t read_varint()
	{
#ifdef MICROPB_FAST
		if ( _pos >= _in.size() )
		{
			fail( error::TRUNCATED );
			return 0;
		}
		const uint8_t first = _in[_pos];
		if ( first < 0x80 )
		{
			++_pos;
			return first;
		}

		uint64_t value = first & 0x7Fu;
		for ( size_t i = 1; i < 10; ++i )
#else
		uint64_t value = 0;
		for ( size_t i = 0; i < 10; ++i )
#endif
		{
			if ( _pos + i >= _in.size() )
			{
				fail( error::TRUNCATED );
				return 0;
			}

			const uint8_t byte = _in[_pos + i];
			if ( i == 9 && byte > 1 )
				break;

			value |= static_cast<uint64_t>( byte & 0x7Fu ) << ( 7 * i );
			if ( ( byte & 0x80 ) == 0 )
			{
				_pos += i + 1;
				return value;
			}
		}
		fail( error::INVALID_VARINT );
		return 0;
	}

	[[nodiscard]] constexpr uint32_t read_fixed32() { return static_cast<uint32_t>( read_le( 4 ) ); }

	[[nodiscard]] constexpr uint64_t read_fixed64() { return read_le( 8 ); }

	/// Returns a view into the input. Does not copy.
	[[nodiscard]] constexpr std::span<const uint8_t> read_length_delimited()
	{
		// The comparison with the remaining bytes comes before an addition, so it
		// cannot overflow. After a failed `read_varint`, the length is 0 and no
		// bytes remain.
		const uint64_t length = read_varint();
		if ( length > remaining() )
		{
			fail( error::TRUNCATED );
			return {};
		}

		const auto bytes = _in.subspan( _pos, static_cast<size_t>( length ) );
		_pos += bytes.size();
		return bytes;
	}

	/// The key is valid only when `failed()` is `false` after the call.
	[[nodiscard]] constexpr field_key read_key()
	{
		const uint64_t key = read_varint();
		if ( _failed )
			return {};

		const uint64_t number = key >> 3;
		const auto type = static_cast<uint8_t>( key & 7 );
		if ( number == 0 || number > max_field_number )
		{
			fail( error::INVALID_TAG );
			return {};
		}
		if ( type == 3 || type == 4 || type > 5 )
		{
			fail( error::INVALID_WIRE_TYPE );
			return {};
		}
		return { static_cast<uint32_t>( number ), static_cast<wire_type>( type ) };
	}

	constexpr void skip( wire_type type )
	{
		switch ( type )
		{
			case wire_type::VARINT: static_cast<void>( read_varint() ); break;
			case wire_type::FIXED32: static_cast<void>( read_le( 4 ) ); break;
			case wire_type::FIXED64: static_cast<void>( read_le( 8 ) ); break;
			case wire_type::LENGTH_DELIMITED: static_cast<void>( read_length_delimited() ); break;
		}
	}

	/// Sets the error, if there is none, and moves the reader to the end.
	constexpr void fail( error code )
	{
		if ( !_failed )
			_error = code;
		_failed = true;
		_pos = _in.size();
	}

	/// Takes the error of `part`, a reader of a section of this input.
	constexpr void absorb( const reader& part )
	{
		if ( part._failed )
			fail( part._error );
	}

	[[nodiscard]] constexpr bool failed() const { return _failed; }

	[[nodiscard]] constexpr std::expected<void, error> status() const
	{
		if ( _failed )
			return std::unexpected( _error );
		return {};
	}

	/// `true` at the end of the input, and after a failure.
	[[nodiscard]] constexpr bool empty() const { return _pos == _in.size(); }

	/// Prefix of the input that the reader has consumed.
	[[nodiscard]] constexpr std::span<const uint8_t> used_span() const { return _in.first( _pos ); }

	/// Unread suffix of the input. Empty at the end of the input and after a failure.
	[[nodiscard]] constexpr std::span<const uint8_t> remaining_span() const { return _in.subspan( _pos ); }

private:
	[[nodiscard]] constexpr size_t remaining() const { return _in.size() - _pos; }

	[[nodiscard]] constexpr uint64_t read_le( size_t count )
	{
		if ( remaining() < count )
		{
			fail( error::TRUNCATED );
			return 0;
		}

		uint64_t value = 0;
		if ( !std::is_constant_evaluated() )
		{
			if constexpr ( std::endian::native == std::endian::little )
			{
				std::memcpy( &value, _in.data() + _pos, count );
				_pos += count;
				return value;
			}
		}
		for ( size_t i = 0; i < count; ++i )
			value |= static_cast<uint64_t>( _in[_pos + i] ) << ( 8 * i );
		_pos += count;
		return value;
	}

	std::span<const uint8_t> _in;
	size_t _pos = 0;
	error _error{};
	bool _failed = false;
};

} // namespace pb
