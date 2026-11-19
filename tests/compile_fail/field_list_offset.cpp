#include <cstddef>
#include <cstdint>

#include <micropb/codec.hpp>

struct msg {
    std::int32_t a;
};

// The offset 64 is not in `msg`.
template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype(msg::a), 1, 64>> {};

static_assert(pb::detail::message_descriptor<msg>::value.count == 1);
