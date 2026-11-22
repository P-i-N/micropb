#include <array>
#include <optional>
#include <stddef.h>
#include <stdint.h>

#include <micropb/micropb.hpp>

// Only a `std::variant` member has an alternative index.
struct msg
{
	int32_t a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a ), 1>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
