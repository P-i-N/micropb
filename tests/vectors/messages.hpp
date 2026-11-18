#pragma once

#include <micropb/types.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

/// C++ mirrors of `vectors.proto`. The encode, decode and compatibility tests share them.
namespace messages {

enum class color : std::int32_t { NONE, RED, BLUE };

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

} // namespace messages
