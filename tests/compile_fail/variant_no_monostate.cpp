#include <cstdint>
#include <variant>

#include <micropb/schema.hpp>

struct msg
{
	std::variant<std::int32_t, float> v;
};

static_assert( pb::build_tag_table<msg>().size() == 2 );
