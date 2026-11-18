#include <micropb/wire.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

using namespace pb;

namespace {

constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();

template <std::size_t N>
constexpr bool equal(const std::array<std::uint8_t, N>& actual, const std::array<std::uint8_t, N>& expected) {
    return actual == expected;
}

constexpr bool test_varint_size() {
    return varint_size(0) == 1 && varint_size(127) == 1 && varint_size(128) == 2 && varint_size(16383) == 2 &&
           varint_size(16384) == 3 && varint_size(u64_max) == 10;
}

constexpr bool test_zigzag() {
    constexpr auto i32_max = std::numeric_limits<std::int32_t>::max();
    constexpr auto i32_min = std::numeric_limits<std::int32_t>::min();
    constexpr auto i64_min = std::numeric_limits<std::int64_t>::min();

    if (zigzag_encode(std::int32_t{0}) != 0 || zigzag_encode(std::int32_t{-1}) != 1 ||
        zigzag_encode(std::int32_t{1}) != 2 || zigzag_encode(std::int32_t{-2}) != 3)
        return false;
    if (zigzag_encode(i32_max) != 0xFFFFFFFEu || zigzag_encode(i32_min) != 0xFFFFFFFFu)
        return false;
    if (zigzag_encode(i64_min) != u64_max)
        return false;

    for (std::int32_t v : {0, -1, 1, -2, 2, i32_max, i32_min})
        if (zigzag_decode(zigzag_encode(v)) != v)
            return false;
    for (std::int64_t v : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{1}, i64_min,
                           std::numeric_limits<std::int64_t>::max()})
        if (zigzag_decode(zigzag_encode(v)) != v)
            return false;
    return true;
}

constexpr bool test_write_varint() {
    {
        std::array<std::uint8_t, 2> buf{};
        writer w(buf);
        if (!w.write_varint(150) || w.size() != 2 || !equal(buf, {0x96, 0x01}))
            return false;
    }
    {
        std::array<std::uint8_t, 2> buf{};
        writer w(buf);
        if (!w.write_varint(300) || !equal(buf, {0xAC, 0x02}))
            return false;
    }
    {
        std::array<std::uint8_t, 10> buf{};
        writer w(buf);
        if (!w.write_varint(u64_max) || w.size() != 10)
            return false;
        return equal(buf, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01});
    }
}

constexpr bool test_write_fixed() {
    std::array<std::uint8_t, 4> a{};
    writer w32(a);
    if (!w32.write_fixed32(0x12345678) || !equal(a, {0x78, 0x56, 0x34, 0x12}))
        return false;

    std::array<std::uint8_t, 8> b{};
    writer w64(b);
    return w64.write_fixed64(1) && equal(b, {1, 0, 0, 0, 0, 0, 0, 0});
}

constexpr bool test_write_key() {
    std::array<std::uint8_t, 4> buf{};
    writer w(buf);
    if (!w.write_key(1, wire_type::VARINT) || !w.write_key(2, wire_type::LENGTH_DELIMITED) ||
        !w.write_key(16, wire_type::VARINT))
        return false;
    return w.size() == 4 && equal(buf, {0x08, 0x12, 0x80, 0x01});
}

constexpr bool test_writer_full() {
    std::array<std::uint8_t, 2> buf{0, 0xEE};
    writer w(std::span<std::uint8_t>(buf).first(1));
    if (w.write_varint(300))
        return false;
    if (w.size() != 0 || buf[0] != 0 || buf[1] != 0xEE)
        return false;
    if (w.write_fixed32(1) || w.write_fixed64(1) || buf[1] != 0xEE)
        return false;

    constexpr std::array<std::uint8_t, 2> data{1, 2};
    return !w.write_bytes(data) && buf[1] == 0xEE && w.size() == 0;
}

constexpr bool test_write_text() {
    std::array<std::uint8_t, 3> buf{0xEE, 0xEE, 0xEE};
    writer w(buf);
    if (!w.write_bytes(std::string_view("az")) || w.size() != 2)
        return false;
    if (w.write_bytes(std::string_view("bc")) || w.size() != 2)
        return false;
    return buf[0] == 'a' && buf[1] == 'z' && buf[2] == 0xEE;
}

constexpr bool test_read_varint() {
    {
        constexpr std::array<std::uint8_t, 2> in{0x96, 0x01};
        reader r(in);
        const auto v = r.read_varint();
        if (!v || *v != 150 || !r.empty() || r.position() != 2)
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 1> in{0x80};
        reader r(in);
        const auto v = r.read_varint();
        if (v || v.error() != error::TRUNCATED)
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 12> in{0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                                  0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
        reader r(in);
        const auto v = r.read_varint();
        if (v || v.error() != error::INVALID_VARINT)
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 10> in{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
        reader r(in);
        const auto v = r.read_varint();
        return v && *v == u64_max;
    }
}

constexpr bool key_error(std::span<const std::uint8_t> in, error expected) {
    reader r(in);
    const auto k = r.read_key();
    return !k && k.error() == expected;
}

constexpr bool test_read_key() {
    {
        constexpr std::array<std::uint8_t, 1> in{0x08};
        reader r(in);
        const auto k = r.read_key();
        if (!k || k->number != 1 || k->type != wire_type::VARINT)
            return false;
    }
    constexpr std::array<std::uint8_t, 1> zero{0x00};
    constexpr std::array<std::uint8_t, 1> type3{0x0B};
    constexpr std::array<std::uint8_t, 1> type7{0x0F};
    // number 2^29, key 2^32
    constexpr std::array<std::uint8_t, 5> big{0x80, 0x80, 0x80, 0x80, 0x10};
    return key_error(zero, error::INVALID_TAG) && key_error(type3, error::INVALID_WIRE_TYPE) &&
           key_error(type7, error::INVALID_WIRE_TYPE) && key_error(big, error::INVALID_TAG);
}

constexpr bool length_error(std::span<const std::uint8_t> in) {
    reader r(in);
    const auto v = r.read_length_delimited();
    return !v && v.error() == error::TRUNCATED;
}

constexpr bool test_read_length() {
    {
        constexpr std::array<std::uint8_t, 3> in{0x02, 0xAA, 0xBB};
        reader r(in);
        const auto v = r.read_length_delimited();
        if (!v || v->size() != 2 || v->data() != in.data() + 1 || !r.empty())
            return false;
    }
    constexpr std::array<std::uint8_t, 2> short_in{0x05, 0xAA};
    constexpr std::array<std::uint8_t, 5> near32{0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    constexpr std::array<std::uint8_t, 10> near64{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    return length_error(short_in) && length_error(near32) && length_error(near64);
}

constexpr bool test_skip() {
    {
        constexpr std::array<std::uint8_t, 2> in{0x96, 0x01};
        reader r(in);
        if (!r.skip(wire_type::VARINT) || !r.empty())
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 4> in{1, 2, 3, 4};
        reader r(in);
        if (!r.skip(wire_type::FIXED32) || !r.empty())
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 8> in{1, 2, 3, 4, 5, 6, 7, 8};
        reader r(in);
        if (!r.skip(wire_type::FIXED64) || !r.empty())
            return false;
    }
    {
        constexpr std::array<std::uint8_t, 2> in{0x01, 0xAA};
        reader r(in);
        if (!r.skip(wire_type::LENGTH_DELIMITED) || !r.empty())
            return false;
    }
    constexpr std::array<std::uint8_t, 3> in{1, 2, 3};
    reader r(in);
    const auto result = r.skip(wire_type::FIXED64);
    return !result && result.error() == error::TRUNCATED && r.position() == 0;
}

////////////////////////////////////////////////////////////////////////////////

static_assert(test_varint_size());
static_assert(test_zigzag());
static_assert(test_write_varint());
static_assert(test_write_fixed());
static_assert(test_write_key());
static_assert(test_writer_full());
static_assert(test_write_text());
static_assert(test_read_varint());
static_assert(test_read_key());
static_assert(test_read_length());
static_assert(test_skip());

} // namespace

int main() {
    const bool ok = test_varint_size() && test_zigzag() && test_write_varint() && test_write_fixed() &&
                    test_write_key() && test_writer_full() && test_write_text() && test_read_varint() && test_read_key() &&
                    test_read_length() && test_skip();
    return ok ? 0 : 1;
}
