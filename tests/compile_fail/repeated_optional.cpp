#include <cstdint>
#include <optional>

#include <micropb/schema.hpp>

struct msg
{
	pb::repeated<std::optional<std::int32_t>, 2> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
