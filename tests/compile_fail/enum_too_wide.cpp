#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

enum class wide : int64_t
{
	ZERO
};

struct msg
{
	wide e;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::e ), 1, offsetof( msg, e )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
