#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/codec.hpp>

struct inner
{
	std::span<const std::int32_t> ints;
};

struct msg
{
	inner child;
};

template <>
struct pb::fields_of<inner> : pb::field_list<inner, pb::field_at<decltype( inner::ints ), 1, offsetof( inner, ints )>>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::child ), 1, offsetof( msg, child )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
