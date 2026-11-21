#include <array>
#include <stddef.h>
#include <stdint.h>
#include <variant>

#include <micropb/codec.hpp>

struct msg
{
	std::array<std::variant<std::monostate, int32_t>, 2> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
