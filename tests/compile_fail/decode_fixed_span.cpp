#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

constexpr std::uint8_t storage[6]{};

struct msg
{
	std::span<const std::uint8_t, 6> mac{ storage };
};

msg value{};
const auto result = pb::decode( {}, value );
