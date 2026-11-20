#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct inner
{
	std::int32_t x;
};

struct msg
{
	const inner c;
};

template <>
struct pb::fields_of<inner> : pb::field_list<inner, pb::field_at<decltype( inner::x ), 1, offsetof( inner, x )>>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::c ), 1, offsetof( msg, c )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
