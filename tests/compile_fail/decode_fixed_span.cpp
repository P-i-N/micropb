#include <span>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

constexpr uint8_t storage[6]{};

struct msg
{
	std::span<const uint8_t, 6> mac{ storage };
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::mac ), 1, offsetof( msg, mac )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
