#include <array>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

struct msg
{
	std::array<uint8_t, 70000> big;
	uint32_t after;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::big ), 1, offsetof( msg, big )>,
                                           pb::field_at<decltype( msg::after ), 2, offsetof( msg, after )>>
{};

static_assert( pb::detail::value_descriptor<msg>::value.kind == pb::field_kind::MESSAGE );
