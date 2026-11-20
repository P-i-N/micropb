#include <array>
#include <cstdint>
#include <variant>

#include <micropb/schema.hpp>

struct msg
{
	std::array<std::variant<std::monostate, std::int32_t>, 2> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
