#include <stdint.h>
#include <string_view>

#include <micropb/schema.hpp>

struct unbounded_msg
{
	std::string_view text;
};

template <>
struct pb::fields_of<unbounded_msg>
  : pb::field_list<unbounded_msg, pb::field_at<decltype( unbounded_msg::text ), 1, offsetof( unbounded_msg, text )>>
{};

constexpr size_t trigger = pb::max_encoded_size<unbounded_msg>;
