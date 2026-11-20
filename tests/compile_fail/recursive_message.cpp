#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

struct node
{
	std::int32_t id;
	std::span<const node> children;
};

struct a;

struct b
{
	std::span<const a> as;
};

struct a
{
	b inner;
};

const node direct{};
const a indirect{};
const std::size_t direct_size = pb::encoded_size( direct );
const std::size_t indirect_size = pb::encoded_size( indirect );
