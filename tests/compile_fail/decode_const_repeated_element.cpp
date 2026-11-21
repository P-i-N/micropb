#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

// The elements of a `pb::repeated` are `const`: encode-only.
struct msg
{
	pb::repeated<const int32_t, 2> ints;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::ints ), 1, offsetof( msg, ints )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
