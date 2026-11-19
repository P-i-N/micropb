#include <cstdint>

#include <micropb/codec.hpp>

struct inner
{
	std::int32_t x;
};

struct msg
{
	const inner c;
};

msg value{};
const auto result = pb::decode( {}, value );
