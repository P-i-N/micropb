#include <span>
#include <stddef.h>
#include <stdint.h>

#include <micropb/codec.hpp>

// The arena never calls a destructor, and a merge copies the items with `memcpy`.
struct item
{
	int32_t x;
	~item() {}
};

struct msg
{
	std::span<const item> items;
};

template <>
struct pb::fields_of<item> : pb::field_list<item, pb::field_at<decltype( item::x ), 1, offsetof( item, x )>>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::items ), 1, offsetof( msg, items )>>
{};

const msg value{};
const size_t size = pb::encoded_size( value );
