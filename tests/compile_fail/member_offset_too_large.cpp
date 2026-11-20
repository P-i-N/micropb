#include <array>
#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct msg
{
	std::array<std::uint8_t, 70000> big;
	std::uint32_t after;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
                                           pb::field_at<decltype( msg::big ), 1, offsetof( msg, big )>,
                                           pb::field_at<decltype( msg::after ), 2, offsetof( msg, after )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 2 );
