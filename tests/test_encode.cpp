#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <micropb/encode.hpp>

using namespace pb;

namespace {

using namespace cases;
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

/// `lists` with the container types that only the encoder accepts.
struct lists_views {
    std::span<const std::int32_t> ints;
    std::array<float, 2> floats;
};

struct lists_owned {
    pb::repeated<std::int32_t, 8> ints;
    pb::repeated<float, 8> floats;
};

////////////////////////////////////////////////////////////////////////////////

constexpr bool test_encode_scalars_zero() { return matches(scalars{}, vectors::scalars_zero); }

constexpr bool test_encode_scalars_max() { return matches(scalars_max(), vectors::scalars_max); }

constexpr bool test_encode_scalars_negative() { return matches(scalars_negative(), vectors::scalars_negative); }

constexpr bool test_encode_scalars_enum_neg() { return matches(scalars_enum_neg(), vectors::scalars_enum_neg); }

constexpr bool test_encode_text_short() { return matches(text{"hi", short_data, mac}, vectors::text_short); }

constexpr bool test_encode_text_long() { return matches(text_long(), vectors::text_long); }

constexpr bool test_encode_outer_set() {
    return matches(outer{.child = {1, {}}, .maybe = 0, .maybe_child = inner{}, .gap = 3}, vectors::outer_set);
}

constexpr bool test_encode_outer_unset() { return matches(outer{}, vectors::outer_unset); }

constexpr bool test_encode_lists_empty() { return matches(lists{}, vectors::lists_empty); }

constexpr bool test_encode_lists_full() { return matches(lists_full(), vectors::lists_full); }

constexpr bool test_encode_pose_zero() { return matches(pose{}, vectors::pose_zero); }

constexpr bool test_encode_envelope_none() { return matches(envelope{1, {}}, vectors::envelope_none); }

constexpr bool test_encode_envelope_pose() {
    return matches(envelope{2, pose{{1, 2, 3}, {0, 0, 0, 1}}}, vectors::envelope_pose);
}

constexpr bool test_encode_envelope_ping() { return matches(envelope{3, ping{}}, vectors::envelope_ping); }

constexpr bool test_encode_envelope_long() { return matches(envelope{4, text_long()}, vectors::envelope_long); }

constexpr bool test_encode_spans() {
    constexpr std::array<std::int32_t, 3> three{1, -2, 3};
    lists_owned owned;
    for (const std::int32_t item : three)
        static_cast<void>(owned.ints.push_back(item));
    static_cast<void>(owned.floats.push_back(1.5f));
    static_cast<void>(owned.floats.push_back(-2.5f));

    const lists_views views{std::span<const std::int32_t>(three), {1.5f, -2.5f}};
    std::array<std::uint8_t, 64> a{};
    std::array<std::uint8_t, 64> b{};
    const auto size_a = encode(owned, a);
    const auto size_b = encode(views, b);

    const struct {
        std::array<std::int32_t, 3> ints;
    } array_only{three};
    const struct {
        pb::repeated<std::int32_t, 3> ints;
    } repeated_only{repeated_of<pb::repeated<std::int32_t, 3>>(three)};

    std::array<std::uint8_t, 16> c{};
    std::array<std::uint8_t, 16> d{};
    const auto size_c = encode(array_only, c);
    const auto size_d = encode(repeated_only, d);

    return size_a && size_b && *size_a == *size_b && std::equal(a.begin(), a.begin() + *size_a, b.begin()) &&
           size_c && size_d && *size_c == *size_d && *size_c == encoded_size(array_only) &&
           std::equal(c.begin(), c.begin() + *size_c, d.begin());
}

constexpr bool test_known_size_used() {
    constexpr std::size_t known = encoded_size(pose{});
    static_assert(known == 32);
    return known == *pb::known_size<pose>() && known == vectors::pose_zero.size() && !pb::known_size<envelope>();
}

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

enum class wide_unsigned : std::uint32_t { ZERO, MAX = 0xFFFFFFFF };

struct unsigned_enum {
    wide_unsigned e;
};

/// An enum is `int32` on the wire: an unsigned value of 2^31 or more is a negative `int32`.
constexpr bool test_encode_unsigned_enum() {
    constexpr std::array<std::uint8_t, 11> expected{0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    return matches(unsigned_enum{wide_unsigned::MAX}, expected);
}

constexpr bool test_encode_order() {
    constexpr std::array<std::uint8_t, 4> expected{0x08, 0x01, 0x10, 0x01};
    return matches(back{.a = 1, .b = 1}, expected);
}

////////////////////////////////////////////////////////////////////////////////

static_assert(test_encode_scalars_zero());
static_assert(test_encode_scalars_max());
static_assert(test_encode_scalars_negative());
static_assert(test_encode_scalars_enum_neg());
static_assert(test_encode_text_short());
static_assert(test_encode_text_long());
static_assert(test_encode_outer_set());
static_assert(test_encode_outer_unset());
static_assert(test_encode_lists_empty());
static_assert(test_encode_lists_full());
static_assert(test_encode_pose_zero());
static_assert(test_encode_envelope_none());
static_assert(test_encode_envelope_pose());
static_assert(test_encode_envelope_ping());
static_assert(test_encode_envelope_long());
static_assert(test_encode_spans());
static_assert(test_known_size_used());
static_assert(test_encode_too_small());
static_assert(test_encode_order());
static_assert(test_encode_unsigned_enum());

} // namespace

int main() {
    const bool ok = test_encode_scalars_zero() && test_encode_scalars_max() && test_encode_scalars_negative() &&
                    test_encode_scalars_enum_neg() && test_encode_text_short() && test_encode_text_long() &&
                    test_encode_outer_set() &&
                    test_encode_outer_unset() && test_encode_lists_empty() && test_encode_lists_full() &&
                    test_encode_pose_zero() && test_encode_envelope_none() && test_encode_envelope_pose() &&
                    test_encode_envelope_ping() && test_encode_envelope_long() && test_encode_spans() &&
                    test_known_size_used() && test_encode_too_small() && test_encode_order() &&
                    test_encode_unsigned_enum();
    return ok ? 0 : 1;
}
