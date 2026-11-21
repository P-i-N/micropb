#include <cstddef>
#include <cstdint>
#include <variant>

#include <micropb/codec.hpp>

struct msg
{
	std::variant<std::monostate, std::variant<std::monostate, std::int32_t>> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
