#include <stdint.h>

#include <micropb/types.hpp>

enum class plain_enum : int32_t
{
	A = 0,
	B = 1,
};

constexpr plain_enum trigger = pb::enum_min<plain_enum>;
