#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <micropb/encode.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

using namespace pb;

namespace {

using namespace messages;

constexpr std::uint8_t guard = 0xAA;

/// `encode` writes exactly `expected`, and `encoded_size` agrees.
template <typename T, std::size_t N>
constexpr bool matches(const T& value, const std::array<std::uint8_t, N>& expected) {
    std::array<std::uint8_t, N + 1> buffer{};
    buffer.fill(guard);

    const auto size = encode(value, std::span(buffer).first(N));
    return size && *size == N && encoded_size(value) == N && std::equal(expected.begin(), expected.end(), buffer.begin()) &&
           buffer[N] == guard;
}

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

////////////////////////////////////////////////////////////////////////////////

constexpr bool test_encode_scalars_zero() { return matches(scalars{}, vectors::scalars_zero); }

constexpr bool test_encode_scalars_max() { return matches(scalars_max(), vectors::scalars_max); }

constexpr bool test_encode_scalars_negative() { return matches(scalars_negative(), vectors::scalars_negative); }

constexpr bool test_encode_text_short() { return matches(text{"hi", short_data, mac}, vectors::text_short); }

constexpr bool test_encode_text_long() { return matches(text_long(), vectors::text_long); }

constexpr bool test_encode_outer_set() {
    return matches(outer{.child = {1, {}}, .maybe = 0, .maybe_child = inner{}, .gap = 3}, vectors::outer_set);
}

constexpr bool test_encode_outer_unset() { return matches(outer{}, vectors::outer_unset); }

constexpr bool test_encode_too_small() {
    const text value = text_long();
    const std::size_t size = encoded_size(value);

    std::array<std::uint8_t, 256> buffer{};
    buffer.fill(guard);
    const auto result = encode(value, std::span(buffer).first(size - 1));
    if (result || result.error() != error::BUFFER_TOO_SMALL)
        return false;

    return buffer[size - 1] == guard;
}

struct back {
    [[= pb::tag{2}]] std::int32_t a;
    [[= pb::tag{1}]] std::int32_t b;
};

constexpr bool test_encode_order() {
    constexpr std::array<std::uint8_t, 4> expected{0x08, 0x01, 0x10, 0x01};
    return matches(back{.a = 1, .b = 1}, expected);
}

////////////////////////////////////////////////////////////////////////////////

static_assert(test_encode_scalars_zero());
static_assert(test_encode_scalars_max());
static_assert(test_encode_scalars_negative());
static_assert(test_encode_text_short());
static_assert(test_encode_text_long());
static_assert(test_encode_outer_set());
static_assert(test_encode_outer_unset());
static_assert(test_encode_too_small());
static_assert(test_encode_order());

} // namespace

int main() {
    const bool ok = test_encode_scalars_zero() && test_encode_scalars_max() && test_encode_scalars_negative() &&
                    test_encode_text_short() && test_encode_text_long() && test_encode_outer_set() &&
                    test_encode_outer_unset() && test_encode_too_small() && test_encode_order();
    return ok ? 0 : 1;
}
