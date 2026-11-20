#include <cstddef>

#include <micropb/codec.hpp>

struct msg
{
	char* p;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::p ), 1, offsetof( msg, p )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
