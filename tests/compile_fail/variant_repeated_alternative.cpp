#include <cstddef>
#include <cstdint>
#include <variant>

#include <micropb/codec.hpp>

struct msg
{
	std::variant<std::monostate, pb::repeated<std::int32_t, 4>> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
