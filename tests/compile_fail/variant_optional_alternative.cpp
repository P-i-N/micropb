#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/codec.hpp>

struct msg
{
	std::variant<std::monostate, std::optional<std::int32_t>> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
