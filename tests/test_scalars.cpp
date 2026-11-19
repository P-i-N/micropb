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

using zz8 = pb::wire_int<std::int8_t, pb::int_encoding::ZIGZAG>;
using zz16 = pb::wire_int<std::int16_t, pb::int_encoding::ZIGZAG>;
using zz_int = pb::wire_int<int, pb::int_encoding::ZIGZAG>;
using zz64 = pb::wire_int<long long, pb::int_encoding::ZIGZAG>;
static_assert(pb::kind_of<zz8>() == pb::field_kind::SCALAR);
static_assert(pb::wire_type_of<zz16>() == pb::wire_type::VARINT);
static_assert(pb::kind_of<pb::repeated<zz8, 4>>() == pb::field_kind::REPEATED); // not bytes
static_assert(pb::is_packed<zz8>());                                            // the element type

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

struct narrow {
    zz8 a;
    zz16 b;
    zz_int c;
    zz64 d;
    pb::repeated<zz16, 3> e;
};

struct wide {
    pb::sint32 a;
    pb::sint32 b;
    pb::sint32 c;
    pb::sint64 d;
    pb::repeated<pb::sint32, 3> e;
};

} // namespace

template <>
struct pb::fields_of<narrow> : pb::field_list<narrow,
    pb::field_at<decltype(narrow::a), 1, offsetof(narrow, a)>,
    pb::field_at<decltype(narrow::b), 2, offsetof(narrow, b)>,
    pb::field_at<decltype(narrow::c), 3, offsetof(narrow, c)>,
    pb::field_at<decltype(narrow::d), 4, offsetof(narrow, d)>,
    pb::field_at<decltype(narrow::e), 5, offsetof(narrow, e)>> {};

template <>
struct pb::fields_of<wide> : pb::field_list<wide,
    pb::field_at<decltype(wide::a), 1, offsetof(wide, a)>,
    pb::field_at<decltype(wide::b), 2, offsetof(wide, b)>,
    pb::field_at<decltype(wide::c), 3, offsetof(wide, c)>,
    pb::field_at<decltype(wide::d), 4, offsetof(wide, d)>,
    pb::field_at<decltype(wide::e), 5, offsetof(wide, e)>> {};

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

/// A narrow ZigZag member writes the same bytes as `sint32` and `sint64`.
bool test_narrow_zigzag() {
    narrow n{};
    n.a = std::int8_t{-128};
    n.b = std::int16_t{-32768};
    n.c = -1;
    n.d = -5'000'000'000ll;
    (void)n.e.push_back(std::int16_t{-1});
    (void)n.e.push_back(std::int16_t{0});
    (void)n.e.push_back(std::int16_t{32767});

    wide w{};
    w.a = -128;
    w.b = -32768;
    w.c = -1;
    w.d = std::int64_t{-5'000'000'000ll};
    (void)w.e.push_back(-1);
    (void)w.e.push_back(0);
    (void)w.e.push_back(32767);

    std::array<std::uint8_t, 64> narrow_bytes{};
    std::array<std::uint8_t, 64> wide_bytes{};
    const auto narrow_size = pb::encode(n, std::span<std::uint8_t>(narrow_bytes));
    const auto wide_size = pb::encode(w, std::span<std::uint8_t>(wide_bytes));
    if (!narrow_size || !wide_size || *narrow_size != *wide_size || narrow_bytes != wide_bytes)
        return false;
    if (pb::encoded_size(n) != *narrow_size || pb::encoded_size(w) != *wide_size)
        return false;

    narrow decoded{};
    const auto used = pb::decode(std::span<const std::uint8_t>(narrow_bytes.data(), *narrow_size), decoded);
    if (!used || *used != *narrow_size || decoded.a != n.a || decoded.b != n.b || decoded.c != n.c ||
        decoded.d != n.d || decoded.e.size() != 3 || decoded.e[0] != -1 || decoded.e[1] != 0 ||
        decoded.e[2] != 32767)
        return false;

    // A value that is too wide for the member loses its upper bits, as for `int32` into 16 bits.
    wide big{};
    big.b = 100000;
    std::array<std::uint8_t, 64> big_bytes{};
    const auto big_size = pb::encode(big, std::span<std::uint8_t>(big_bytes));
    if (!big_size)
        return false;
    narrow truncated{};
    const auto truncated_used =
        pb::decode(std::span<const std::uint8_t>(big_bytes.data(), *big_size), truncated);
    return truncated_used && truncated.b == static_cast<std::int16_t>(100000);
}

/// A `sint64` value that does not fit the member. The two decode rules differ here (see the specification):
/// a 4-byte member truncates the varint to 32 bits and then decodes ZigZag, as Google does for `sint32`;
/// a 1-, 2- or 8-byte member decodes 64-bit ZigZag and then truncates.
/// 2^31 is the ZigZag varint 2^32. 4 bytes: the low 32 bits of 2^32 are 0, and ZigZag(0) is 0.
/// 2 bytes: ZigZag-decoding 2^32 gives 2^31, whose low 16 bits are 0.
struct one_sint64 {
    pb::sint64 a;
};
struct one_sint32 {
    pb::sint32 a;
};
struct one_zz16 {
    zz16 a;
};

} // namespace

template <>
struct pb::fields_of<one_sint64> : pb::field_list<one_sint64, pb::field_at<decltype(one_sint64::a), 1, offsetof(one_sint64, a)>> {};
template <>
struct pb::fields_of<one_sint32> : pb::field_list<one_sint32, pb::field_at<decltype(one_sint32::a), 1, offsetof(one_sint32, a)>> {};
template <>
struct pb::fields_of<one_zz16> : pb::field_list<one_zz16, pb::field_at<decltype(one_zz16::a), 1, offsetof(one_zz16, a)>> {};

namespace {

bool test_out_of_range_sint64() {
    one_sint64 source{};
    source.a = std::int64_t{2147483648};
    std::array<std::uint8_t, 16> bytes{};
    const auto size = pb::encode(source, std::span<std::uint8_t>(bytes));
    if (!size)
        return false;
    const std::span<const std::uint8_t> input(bytes.data(), *size);

    one_sint32 four{};
    four.a = 7;
    const auto four_used = pb::decode(input, four);
    if (!four_used || four.a != 0)
        return false;

    one_zz16 two{};
    two.a = std::int16_t{7};
    const auto two_used = pb::decode(input, two);
    return two_used && two.a == 0;
}

} // namespace

int main() {
    const bool ok = test_standard_integers() && test_narrow_zigzag() && test_out_of_range_sint64();
    return ok ? 0 : 1;
}
