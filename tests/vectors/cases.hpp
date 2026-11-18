#pragma once

#include "messages.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

/// C++ values of the `vectors.hpp` cases that the encode and decode tests share.
namespace cases {

using namespace messages;

constexpr scalars scalars_max() {
    scalars v{};
    v.i32 = std::numeric_limits<std::int32_t>::max();
    v.i64 = std::numeric_limits<std::int64_t>::max();
    v.u32 = std::numeric_limits<std::uint32_t>::max();
    v.u64 = std::numeric_limits<std::uint64_t>::max();
    v.s32 = std::numeric_limits<std::int32_t>::max();
    v.s64 = std::numeric_limits<std::int64_t>::max();
    v.flag = true;
    v.f = std::numeric_limits<float>::max();
    v.d = std::numeric_limits<double>::max();
    v.fx32 = std::numeric_limits<std::uint32_t>::max();
    v.fx64 = std::numeric_limits<std::uint64_t>::max();
    v.sfx32 = std::numeric_limits<std::int32_t>::max();
    v.sfx64 = std::numeric_limits<std::int64_t>::max();
    v.c = color::BLUE;
    v.i8 = std::numeric_limits<std::int8_t>::max();
    v.u16 = std::numeric_limits<std::uint16_t>::max();
    return v;
}

constexpr scalars scalars_negative() {
    scalars v{};
    v.i32 = -1;
    v.i64 = -1;
    v.s32 = -1;
    v.s64 = -1;
    v.f = -0.0f;
    v.sfx32 = -1;
    v.sfx64 = -1;
    v.c = color::BLUE;
    v.i8 = -1;
    return v;
}

constexpr std::array<std::uint8_t, 6> mac{1, 2, 3, 4, 5, 6};
constexpr std::array<std::uint8_t, 3> short_data{1, 2, 3};

constexpr std::array<char, 200> long_name = [] {
    std::array<char, 200> name{};
    name.fill('a');
    return name;
}();

constexpr text text_long() { return {std::string_view(long_name.data(), long_name.size()), {}, mac}; }

} // namespace cases
