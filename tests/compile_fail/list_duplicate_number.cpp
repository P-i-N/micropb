#include <array>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <variant>

#include <micropb/micropb.hpp>

// The two entries with the number 5 are not next to each other.
struct msg
{
	int32_t a;
	int32_t b;
	int32_t c;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::a ), 5, offsetof( msg, a )>,
                                           pb::field_at<decltype( msg::b ), 1, offsetof( msg, b )>,
                                           pb::field_at<decltype( msg::c ), 5, offsetof( msg, c )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
