#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/micropb.hpp>

// Only a `std::variant` member has an alternative index.
struct msg {
    std::int32_t a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype(msg::a), 1, offsetof(msg, a), 1>> {};

static_assert(pb::detail::message_descriptor<msg>::value.count == 1);
