#include <cstdint>

#include <micropb/meta_schema.hpp>

struct msg
{
	msg() {}

	std::int32_t a;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
