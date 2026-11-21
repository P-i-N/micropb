#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/micropb.hpp>

// A member type rule applies to a field list that a person wrote.
struct msg
{
	std::optional<pb::repeated<std::int32_t, 2>> a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
