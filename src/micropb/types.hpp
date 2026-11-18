#pragma once

#include <cstdint>

namespace pb {

////////////////////////////////////////////////////////////////////////////////

/// Error codes of the codec. Functions return them as `std::expected` values.
enum class error {
    BUFFER_TOO_SMALL,
    TRUNCATED,
    INVALID_VARINT,
    INVALID_WIRE_TYPE,
    INVALID_TAG,
    CAPACITY_EXCEEDED,
};

/// Wire types of proto3 that this library supports. Group types (3, 4) are not supported.
enum class wire_type : std::uint8_t {
    VARINT = 0,
    FIXED64 = 1,
    LENGTH_DELIMITED = 2,
    FIXED32 = 5,
};

} // namespace pb
