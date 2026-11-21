#include <cstddef>

#include <micropb/micropb.hpp>

struct empty
{};

// The empty member `e` is in the list two times.
struct msg
{
	empty e;
};

template <>
struct pb::fields_of<empty> : pb::field_list<empty>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::e ), 1, offsetof( msg, e )>,
                                           pb::field_at<decltype( msg::e ), 2, offsetof( msg, e )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
