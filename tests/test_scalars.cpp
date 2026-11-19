#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <micropb/micropb.hpp>

// The standard integer types are accepted by size and sign, so a message with `int` or `long`
// members works on each target, whatever `std::int32_t` is there.

static_assert(pb::kind_of<signed char>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<short>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<int>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<long long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned short>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned int>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned long long>() == pb::field_kind::SCALAR);
static_assert(pb::wire_type_of<long>() == pb::wire_type::VARINT);
static_assert(pb::kind_of<pb::repeated<signed char, 4>>() == pb::field_kind::REPEATED); // not bytes
static_assert(pb::kind_of<pb::repeated<unsigned char, 4>>() == pb::field_kind::BYTES);
static_assert(pb::is_packed<signed char>()); // the element type of a repeated field

namespace {

/// The fixed-width type of the same size and sign as `long` and `unsigned long`.
using long_equivalent = std::conditional_t<sizeof(long) == 4, std::int32_t, std::int64_t>;
using ulong_equivalent = std::conditional_t<sizeof(unsigned long) == 4, std::uint32_t, std::uint64_t>;

struct standard {
    int a;
    long b;
    unsigned long long c;
    short d;
    signed char e;
    unsigned int f;
    unsigned long g;
};

struct fixed_width {
    std::int32_t a;
    long_equivalent b;
    std::uint64_t c;
    std::int16_t d;
    std::int8_t e;
    std::uint32_t f;
    ulong_equivalent g;
};

} // namespace

template <>
struct pb::fields_of<standard> : pb::field_list<standard,
    pb::field_at<decltype(standard::a), 1, offsetof(standard, a)>,
    pb::field_at<decltype(standard::b), 2, offsetof(standard, b)>,
    pb::field_at<decltype(standard::c), 3, offsetof(standard, c)>,
    pb::field_at<decltype(standard::d), 4, offsetof(standard, d)>,
    pb::field_at<decltype(standard::e), 5, offsetof(standard, e)>,
    pb::field_at<decltype(standard::f), 6, offsetof(standard, f)>,
    pb::field_at<decltype(standard::g), 7, offsetof(standard, g)>> {};

template <>
struct pb::fields_of<fixed_width> : pb::field_list<fixed_width,
    pb::field_at<decltype(fixed_width::a), 1, offsetof(fixed_width, a)>,
    pb::field_at<decltype(fixed_width::b), 2, offsetof(fixed_width, b)>,
    pb::field_at<decltype(fixed_width::c), 3, offsetof(fixed_width, c)>,
    pb::field_at<decltype(fixed_width::d), 4, offsetof(fixed_width, d)>,
    pb::field_at<decltype(fixed_width::e), 5, offsetof(fixed_width, e)>,
    pb::field_at<decltype(fixed_width::f), 6, offsetof(fixed_width, f)>,
    pb::field_at<decltype(fixed_width::g), 7, offsetof(fixed_width, g)>> {};

namespace {

/// The standard integer types encode and decode like the fixed-width types of the same size and sign.
bool test_standard_integers() {
    const standard value{-1, -123456, 0xFFFF'FFFF'FFFF'FFFFull, -300, -5, 4000000000u, 4000000000ul};
    const fixed_width twin{-1, -123456, 0xFFFF'FFFF'FFFF'FFFFull, -300, -5, 4000000000u, 4000000000ul};

    std::array<std::uint8_t, 96> bytes{};
    std::array<std::uint8_t, 96> twin_bytes{};
    const auto size = pb::encode(value, std::span<std::uint8_t>(bytes));
    const auto twin_size = pb::encode(twin, std::span<std::uint8_t>(twin_bytes));
    if (!size || !twin_size || *size != *twin_size || bytes != twin_bytes)
        return false;
    if (pb::encoded_size(value) != *size || pb::encoded_size(twin) != *size)
        return false;

    standard decoded{};
    const auto used = pb::decode(std::span<const std::uint8_t>(bytes.data(), *size), decoded);
    return used && *used == *size && decoded.a == value.a && decoded.b == value.b && decoded.c == value.c &&
           decoded.d == value.d && decoded.e == value.e && decoded.f == value.f && decoded.g == value.g;
}

} // namespace

int main() {
    const bool ok = test_standard_integers();
    return ok ? 0 : 1;
}
