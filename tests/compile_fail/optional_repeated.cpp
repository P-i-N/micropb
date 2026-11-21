#include <optional>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

struct msg
{
	std::optional<pb::repeated<int32_t, 4>> values;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::values ), 1, offsetof( msg, values )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
