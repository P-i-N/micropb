#include <array>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <variant>

#include <micropb/micropb.hpp>

// The list has no entry for the alternative 1, so the codec would not see it.
struct msg
{
	std::variant<std::monostate, int32_t, float> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 2, offsetof( msg, v ), 2>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
