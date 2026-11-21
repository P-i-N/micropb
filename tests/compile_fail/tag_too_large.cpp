#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

struct msg
{
	int32_t a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), ( 1u << 29 ), offsetof( msg, a )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
