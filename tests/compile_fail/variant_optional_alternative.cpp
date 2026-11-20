#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/schema.hpp>

struct msg
{
	std::variant<std::monostate, std::optional<std::int32_t>> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
