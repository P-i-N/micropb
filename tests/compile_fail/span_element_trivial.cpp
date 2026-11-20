#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

// The arena never calls a destructor, and a merge copies the items with `memcpy`.
struct item
{
	std::int32_t x;
	~item() {}
};

struct msg
{
	std::span<const item> items;
};

const msg value{};
const std::size_t size = pb::encoded_size( value );
