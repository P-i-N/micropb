#include <array>

#include <micropb/schema.hpp>

struct msg
{
	std::array<std::array<float, 3>, 2> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
