#include <cstddef>
#include <cstdint>
#include <optional>

#include <micropb/codec.hpp>

struct msg
{
	pb::repeated<std::optional<std::int32_t>, 2> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
