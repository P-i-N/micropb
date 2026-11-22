#include <array>
#include <optional>
#include <stddef.h>
#include <stdint.h>

#include <micropb/micropb.hpp>

// The variant has the alternatives 1 and 2. The index 3 is not one of them.
struct msg
{
	pb::oneof<int32_t, float> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>,
                                           pb::field_at<decltype( msg::v ), 2, offsetof( msg, v ), 3>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
