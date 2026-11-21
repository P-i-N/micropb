#include <array>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

// The elements of a fixed-length bytes array are `const`: encode-only.
struct msg
{
	std::array<const uint8_t, 6> mac{};
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::mac ), 1, offsetof( msg, mac )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
