#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct msg
{
	msg() {}

	std::int32_t a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
