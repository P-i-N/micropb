#include <array>
#include <cstdint>

#include <micropb/micropb.hpp>

// `msg` has no `pb::fields_of`.
struct msg
{
	std::int32_t a;
};

std::array<std::uint8_t, 8> buffer{};
const auto result = pb::encode( msg{ 1 }, buffer );
