#include <cstddef>
#include <cstdint>
#include <optional>

#include <micropb/codec.hpp>

struct msg
{
	std::optional<pb::repeated<std::int32_t, 4>> values;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::values ), 1, offsetof( msg, values )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
