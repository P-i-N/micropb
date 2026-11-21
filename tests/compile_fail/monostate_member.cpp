#include <cstddef>
#include <variant>

#include <micropb/codec.hpp>

struct msg
{
	std::monostate m;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::m ), 1, offsetof( msg, m )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
