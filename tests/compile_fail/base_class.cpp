#include <cstdint>

#include <micropb/schema.hpp>

struct base
{
	std::int32_t a;
};

struct msg : base
{
	std::int32_t b;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
