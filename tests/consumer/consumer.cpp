// A project that uses MicroPB through its CMake target. Two non-inline functions use the
// codec, so the compiler must instantiate it.
#include <expected>
#include <span>
#include <stddef.h>
#include <stdint.h>

#include <micropb/micropb.hpp>

#include "vectors/vectors.pb.hpp"

std::expected<size_t, pb::error> consumer_encode( const micropb::test::scalars& message, std::span<uint8_t> out )
{
	return pb::encode( message, out );
}

std::expected<size_t, pb::error> consumer_decode( std::span<const uint8_t> in, micropb::test::scalars& message )
{
	return pb::decode( in, message );
}
