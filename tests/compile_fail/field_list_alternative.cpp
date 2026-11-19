#include <cstddef>
#include <cstdint>
#include <variant>

#include <micropb/codec.hpp>

struct msg {
    std::variant<std::monostate, std::int32_t, float> v;
};

// A `std::variant` member needs one entry for each alternative, with the index 1 or more.
template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype(msg::v), 1, offsetof(msg, v)>> {};

static_assert(pb::detail::message_descriptor<msg>::value.count == 1);
