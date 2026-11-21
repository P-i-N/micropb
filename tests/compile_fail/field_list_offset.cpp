#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct msg
{
	std::int32_t a;
};

// The 4 bytes of `a` at the offset 2 are not all in `msg`.
template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, 2>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
