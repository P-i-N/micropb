#include <optional>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

struct msg
{
	std::optional<std::optional<int32_t>> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
