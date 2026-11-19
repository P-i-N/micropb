#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/micropb.hpp>

// The member `a` is in the list two times.
struct msg {
    std::int32_t a;
    std::int32_t b;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg,
    pb::field_at<decltype(msg::a), 1, offsetof(msg, a)>,
    pb::field_at<decltype(msg::a), 2, offsetof(msg, a)>> {};

static_assert(pb::detail::message_descriptor<msg>::value.count == 2);
