#include <cstdint>

#include <micropb/schema.hpp>

enum class wide : std::int64_t
{
	ZERO
};

struct msg
{
	wide e;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
