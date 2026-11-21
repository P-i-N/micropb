#include <span>
#include <stddef.h>
#include <stdint.h>

#include <micropb/micropb.hpp>

// A repeated field in a std::span with a static extent is encode-only.
constexpr int32_t storage[2]{};

struct msg
{
	std::span<const int32_t, 2> ints{ storage };
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::ints ), 1, offsetof( msg, ints )>>
{};

msg value{};
const auto result = pb::decode( {}, value );
