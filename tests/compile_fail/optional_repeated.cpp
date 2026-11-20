#include <cstdint>
#include <optional>

#include <micropb/schema.hpp>

struct msg
{
	std::optional<pb::repeated<std::int32_t, 4>> values;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
