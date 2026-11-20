#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

enum class wide : std::int64_t
{
	ZERO
};

struct msg
{
	wide e;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::e ), 1, offsetof( msg, e )>>
{};

static_assert( pb::detail::message_descriptor<msg>::value.count == 1 );
