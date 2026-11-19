// A project that uses micropb through its CMake target. Two non-inline functions use the
// codec, so the compiler must instantiate it.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <micropb/micropb.hpp>

#include "vectors/vectors.pb.hpp"

std::expected<std::size_t, pb::error> consumer_encode(const micropb::test::scalars& message, std::span<std::uint8_t> out) {
    return pb::encode(message, out);
}

std::expected<std::size_t, pb::error> consumer_decode(std::span<const std::uint8_t> in, micropb::test::scalars& message) {
    return pb::decode(in, message);
}
