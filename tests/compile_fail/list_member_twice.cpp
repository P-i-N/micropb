#include <array>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <variant>

#include <micropb/micropb.hpp>

// The member `a` is in the list two times.
struct msg
{
	int32_t a;
	int32_t b;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>,
                                           pb::field_at<decltype( msg::a ), 2, offsetof( msg, a )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
