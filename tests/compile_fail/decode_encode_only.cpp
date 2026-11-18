#include <array>
#include <cstdint>

#include <micropb/decode.hpp>

struct msg {
    std::array<std::int32_t, 2> ints;
};

msg value{};
const auto result = pb::decode({}, value);
