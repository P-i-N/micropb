#include <cstdint>
#include <variant>

#include <micropb/meta_schema.hpp>

struct msg
{
	std::variant<std::monostate, pb::repeated<std::int32_t, 4>> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
