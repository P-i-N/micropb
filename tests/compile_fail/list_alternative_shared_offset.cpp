#include <cstddef>
#include <cstdint>
#include <variant>

#include <micropb/micropb.hpp>

struct empty
{};

// `e` shares the offset 0 with `v`, and `v` has no entry for the alternative 2.
struct msg
{
	[[no_unique_address]] empty e;
	std::variant<std::monostate, std::int32_t, float> v;
};

template <>
struct pb::fields_of<empty> : pb::field_list<empty>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::e ), 1, offsetof( msg, e )>,
                                           pb::field_at<decltype( msg::v ), 2, offsetof( msg, v ), 1>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
