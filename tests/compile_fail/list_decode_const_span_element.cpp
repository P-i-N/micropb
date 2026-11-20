#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/micropb.hpp>

// The element of a span has a `const` member: the span is encode-only.
struct item
{
	const std::int32_t a;
};

struct msg
{
	std::span<const item> items;
};

template <>
struct pb::fields_of<item> : pb::field_list<item, pb::field_at<decltype( item::a ), 1, offsetof( item, a )>>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::items ), 1, offsetof( msg, items )>>
{};

msg value{};
std::uint8_t storage[16]{};
pb::arena memory( storage );
const auto result = pb::decode( {}, value, memory );
