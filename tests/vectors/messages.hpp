#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/types.hpp>

/// C++ mirrors of `vectors.proto`. The encode, decode and compatibility tests share them.
namespace messages {

enum class color : std::int32_t { NONE, RED, BLUE, NEG = -1 };

struct scalars {
    std::int32_t i32;
    std::int64_t i64;
    std::uint32_t u32;
    std::uint64_t u64;
    pb::sint32 s32;
    pb::sint64 s64;
    bool flag;
    float f;
    double d;
    pb::fixed32 fx32;
    pb::fixed64 fx64;
    pb::sfixed32 sfx32;
    pb::sfixed64 sfx64;
    color c;
    std::int8_t i8;
    std::uint16_t u16;
};

struct text {
    std::string_view name;
    std::span<const std::uint8_t> data;
    std::array<std::uint8_t, 6> mac;
};

struct inner {
    std::int32_t x;
    std::string_view label;
};

struct outer {
    inner child;
    std::optional<std::int32_t> maybe;
    std::optional<inner> maybe_child;
    [[= pb::tag{10}]] std::int32_t gap;
};

struct lists {
    pb::repeated<std::int32_t, 8> ints;
    pb::repeated<float, 8> floats;
    pb::repeated<std::string_view, 8> names;
    pb::repeated<inner, 8> items;
    pb::repeated<pb::sint64, 8> zz;
};

struct pose {
    std::array<float, 3> pos;
    std::array<float, 4> rot;
};

struct ping {
    std::uint32_t seq;
};

struct envelope {
    std::uint32_t id;
    std::variant<std::monostate, pose, ping, text> payload;
};

} // namespace messages
