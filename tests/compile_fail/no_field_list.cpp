#include <array>
#include <stdint.h>

#include <micropb/micropb.hpp>

// `msg` has no `pb::fields_of`.
struct msg
{
	int32_t a;
};

std::array<uint8_t, 8> buffer{};
const auto result = pb::encode( msg{ 1 }, buffer );
