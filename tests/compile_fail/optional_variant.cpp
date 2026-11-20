#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/schema.hpp>

struct msg
{
	std::optional<std::variant<std::monostate, std::int32_t>> v;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
