#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

struct inner
{
	std::span<const std::int32_t> ints;
};

struct msg
{
	inner child;
};

msg value{};
const auto result = pb::decode( {}, value );
