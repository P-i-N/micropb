#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/micropb.hpp>

// The list has no entry for the alternative 1, so the codec would not see it.
struct msg
{
	std::variant<std::monostate, std::int32_t, float> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 2, offsetof( msg, v ), 2>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
