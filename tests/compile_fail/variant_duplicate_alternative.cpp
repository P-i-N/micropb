#include <cstdint>
#include <variant>

#include <micropb/schema.hpp>

struct msg
{
	std::variant<std::monostate, std::int32_t, std::int32_t> v;
};

static_assert( pb::build_tag_table<msg>().size() == 2 );
