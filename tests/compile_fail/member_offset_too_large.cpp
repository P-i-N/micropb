#include <array>
#include <cstdint>

#include <micropb/codec.hpp>

struct msg
{
	std::array<std::uint8_t, 70000> big;
	std::uint32_t after;
};

static_assert( pb::detail::message_descriptor<msg>::value.count == 2 );
