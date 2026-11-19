#include <variant>

#include <micropb/meta_schema.hpp>

struct msg
{
	std::monostate m;
};

static_assert( pb::build_tag_table<msg>().size() == 1 );
