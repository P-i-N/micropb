#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct other
{
	std::int32_t a;
};

struct msg
{
	std::int32_t a;
};

// The list names `other`, but it is the list of `msg`.
template <>
struct pb::fields_of<msg> : pb::field_list<other, pb::field_at<decltype( other::a ), 1, offsetof( other, a )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
