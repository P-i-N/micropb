#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <micropb/decode.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

using namespace pb;

namespace {

using namespace cases;
using namespace messages;

template <std::size_t N>
constexpr std::span<const std::uint8_t> bytes_of(const std::array<std::uint8_t, N>& array) {
    return array;
}

template <typename T>
constexpr bool fails(std::span<const std::uint8_t> in, error expected) {
    T message{};
    const auto result = decode(in, message);
    return !result && result.error() == expected;
}

constexpr bool same(const inner& a, const inner& b) { return a.x == b.x && a.label == b.label; }

constexpr bool same(const scalars& a, const scalars& b) {
    return a.i32 == b.i32 && a.i64 == b.i64 && a.u32 == b.u32 && a.u64 == b.u64 && a.s32.value == b.s32.value &&
           a.s64.value == b.s64.value && a.flag == b.flag &&
           std::bit_cast<std::uint32_t>(a.f) == std::bit_cast<std::uint32_t>(b.f) &&
           std::bit_cast<std::uint64_t>(a.d) == std::bit_cast<std::uint64_t>(b.d) && a.fx32.value == b.fx32.value &&
           a.fx64.value == b.fx64.value && a.sfx32.value == b.sfx32.value && a.sfx64.value == b.sfx64.value &&
           a.c == b.c && a.i8 == b.i8 && a.u16 == b.u16;
}

constexpr bool same(const text& a, const text& b) {
    return a.name == b.name && std::equal(a.data.begin(), a.data.end(), b.data.begin(), b.data.end()) &&
           a.mac == b.mac;
}

constexpr bool same(const outer& a, const outer& b) {
    return same(a.child, b.child) && a.maybe == b.maybe && a.maybe_child.has_value() == b.maybe_child.has_value() &&
           (!a.maybe_child || same(*a.maybe_child, *b.maybe_child)) && a.gap == b.gap;
}

constexpr bool same_bits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

template <typename S, typename F>
constexpr bool same_items(const S& a, const S& b, F equal) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), equal);
}

constexpr bool same(const lists& a, const lists& b) {
    return same_items(a.ints, b.ints, std::equal_to<>{}) && same_items(a.floats, b.floats, same_bits) &&
           same_items(a.names, b.names, std::equal_to<>{}) &&
           same_items(a.items, b.items, [](const inner& x, const inner& y) { return same(x, y); }) &&
           same_items(a.zz, b.zz, [](sint64 x, sint64 y) { return x.value == y.value; });
}

/// `decode` reads `in` into a `T` that equals `expected`.
template <typename T, std::size_t N>
constexpr bool decodes(const std::array<std::uint8_t, N>& in, const T& expected) {
    T message{};
    const auto result = decode(bytes_of(in), message);
    return result && *result == N && same(message, expected);
}

struct narrow {
    std::int32_t i32;
    std::int8_t i8;
    std::uint16_t u16;
    bool flag;
    sint32 s32;
};

enum class small : std::uint8_t { ZERO };

struct small_enum {
    small e;
};

struct mac_only {
    std::array<std::uint8_t, 6> mac;
};

struct blob {
    pb::repeated<std::uint8_t, 4> bytes;
};

/// `pose` and `envelope` with the container type that `decode` accepts.
struct pose_owned {
    pb::repeated<float, 3> pos;
    pb::repeated<float, 4> rot;
};

struct envelope_owned {
    std::uint32_t id;
    std::variant<std::monostate, pose_owned, ping, text> payload;
};

struct ints_only {
    pb::repeated<std::int32_t, 8> ints;
};

struct floats_only {
    pb::repeated<float, 4> floats;
};

struct int_buffer {
    pb::span_buffer<std::int32_t> ints;
};

struct sink {
    pb::span_buffer<std::uint8_t> bytes;
};

struct nested_sink {
    inner head;
    sink body;
};

/// Bytes of one varint field.
struct varint_field {
    constexpr varint_field(std::uint8_t key, std::uint64_t value) {
        _data[_size++] = key;
        while (value >= 0x80) {
            _data[_size++] = static_cast<std::uint8_t>(value | 0x80);
            value >>= 7;
        }
        _data[_size++] = static_cast<std::uint8_t>(value);
    }

    constexpr std::span<const std::uint8_t> span() const { return std::span(_data).first(_size); }

private:
    std::array<std::uint8_t, 16> _data{};
    std::size_t _size = 0;
};

////////////////////////////////////////////////////////////////////////////////

constexpr bool test_decode_scalars_zero() { return decodes(vectors::scalars_zero, scalars{}); }

constexpr bool test_decode_scalars_max() { return decodes(vectors::scalars_max, scalars_max()); }

constexpr bool test_decode_scalars_negative() { return decodes(vectors::scalars_negative, scalars_negative()); }

constexpr bool test_decode_scalars_enum_neg() { return decodes(vectors::scalars_enum_neg, scalars_enum_neg()); }

bool test_decode_text_short() { return decodes(vectors::text_short, text{"hi", short_data, mac}); }

bool test_decode_text_long() { return decodes(vectors::text_long, text_long()); }

bool test_decode_outer_set() {
    return decodes(vectors::outer_set, outer{.child = {1, {}}, .maybe = 0, .maybe_child = inner{}, .gap = 3});
}

constexpr bool test_decode_outer_unset() { return decodes(vectors::outer_unset, outer{}); }

bool test_zero_copy() {
    text message{};
    const auto result = decode(bytes_of(vectors::text_short), message);
    const auto* first = reinterpret_cast<const char*>(vectors::text_short.data());
    return result && message.name.data() == first + 2 && message.data.data() == vectors::text_short.data() + 6;
}

constexpr bool test_reset() {
    scalars message = scalars_max();
    const auto result = decode(bytes_of(vectors::scalars_zero), message);
    return result && *result == 0 && same(message, scalars{});
}

constexpr bool test_last_wins() {
    constexpr std::array<std::uint8_t, 4> in{0x08, 0x01, 0x08, 0x02};
    return decodes(in, inner{2, {}});
}

bool test_merge() {
    constexpr std::array<std::uint8_t, 9> in{0x0a, 0x02, 0x08, 0x01, 0x0a, 0x03, 0x12, 0x01, 'a'};
    constexpr std::array<std::uint8_t, 9> optional_in{0x1a, 0x02, 0x08, 0x01, 0x1a, 0x03, 0x12, 0x01, 'a'};

    outer merged{};
    outer merged_optional{};
    const auto a = decode(bytes_of(in), merged);
    const auto b = decode(bytes_of(optional_in), merged_optional);
    return a && b && merged.child.x == 1 && merged.child.label == "a" && merged_optional.maybe_child &&
           merged_optional.maybe_child->x == 1 && merged_optional.maybe_child->label == "a";
}

constexpr bool test_unknown_skip() {
    constexpr std::array<std::uint8_t, 2 * 21 + 2> in{
        0x28, 0x01,                          // field 5, varint
        0x31, 1,    2,   3,   4,   5, 6, 7, 8, // field 6, fixed64
        0x3a, 0x03, 'a', 'b', 'c',           // field 7, length-delimited
        0x45, 1,    2,   3,   4,             // field 8, fixed32
        0x08, 0x05,                          // x = 5
        0x28, 0x01,                          //
        0x31, 1,    2,   3,   4,   5, 6, 7, 8, //
        0x3a, 0x03, 'a', 'b', 'c',           //
        0x45, 1,    2,   3,   4};
    inner message{};
    const auto result = decode(bytes_of(in), message);
    return result && *result == in.size() && message.x == 5 && message.label.empty();
}

constexpr bool test_wrong_wire_type() {
    constexpr std::array<std::uint8_t, 5> fixed{0x0d, 1, 2, 3, 4};
    constexpr std::array<std::uint8_t, 5> as_string{0x0a, 0x03, 'a', 'b', 'c'};
    return decodes(fixed, inner{}) && decodes(as_string, inner{});
}

constexpr bool test_wrong_wire_type_optional() {
    constexpr std::array<std::uint8_t, 5> in{0x15, 1, 2, 3, 4};
    return decodes(in, outer{});
}

constexpr bool test_narrowing() {
    constexpr std::uint64_t wide = (std::uint64_t{1} << 40) + 7;
    narrow a{};
    narrow b{};
    narrow c{};
    narrow d{};
    narrow e{};
    const bool ok_a = decode(varint_field(0x08, wide).span(), a).has_value();
    const bool ok_b = decode(varint_field(0x10, 300).span(), b).has_value();
    const bool ok_c = decode(varint_field(0x18, 70000).span(), c).has_value();
    const bool ok_d = decode(varint_field(0x20, 2).span(), d).has_value();
    const bool ok_e = decode(varint_field(0x28, (std::uint64_t{3} << 32) + 3).span(), e).has_value();
    return ok_a && a.i32 == 7 && ok_b && b.i8 == 44 && ok_c && c.u16 == 4464 && ok_d && d.flag && ok_e &&
           e.s32.value == -2;
}

/// An enum is `int32` on the wire. The decoder truncates the varint to `std::int32_t`, then converts it to the
/// enum. 300 becomes 44 in an enum with `std::uint8_t` as its underlying type.
constexpr bool test_narrow_enum() {
    small_enum message{};
    const bool ok = decode(varint_field(0x08, 300).span(), message).has_value();
    return ok && static_cast<std::uint8_t>(message.e) == 44;
}

constexpr bool test_fixed_bytes() {
    constexpr std::array<std::uint8_t, 6> four{0x0a, 0x04, 1, 2, 3, 4};
    constexpr std::array<std::uint8_t, 9> seven{0x0a, 0x07, 1, 2, 3, 4, 5, 6, 7};
    constexpr std::array<std::uint8_t, 7> five{0x0a, 0x05, 1, 2, 3, 4, 5};
    constexpr std::array<std::uint8_t, 6> padded{1, 2, 3, 4, 0, 0};

    mac_only message{{9, 9, 9, 9, 9, 9}};
    const auto result = decode(bytes_of(four), message);
    return result && message.mac == padded && fails<mac_only>(bytes_of(seven), error::CAPACITY_EXCEEDED) &&
           fails<blob>(bytes_of(five), error::CAPACITY_EXCEEDED);
}

constexpr bool test_repeated_bytes() {
    constexpr std::array<std::uint8_t, 6> in{0x0a, 0x04, 1, 2, 3, 4};
    blob message{};
    const auto result = decode(bytes_of(in), message);
    return result && message.bytes.size() == 4 && message.bytes[3] == 4;
}

constexpr bool test_span_buffer() {
    constexpr std::array<std::uint8_t, 5> in{0x0a, 0x03, 7, 8, 9};
    constexpr std::array<std::uint8_t, 6> big{0x0a, 0x04, 1, 2, 3, 4};
    std::array<std::uint8_t, 3> storage{};

    sink message{pb::span_buffer<std::uint8_t>(storage)};
    const auto first = decode(bytes_of(in), message);
    const bool filled = first && message.bytes.size() == 3 && message.bytes[2] == 9 && storage[0] == 7;

    const auto second = decode(std::span<const std::uint8_t>(), message);
    const bool cleared = second && message.bytes.empty() && message.bytes.capacity() == 3;

    const auto third = decode(bytes_of(big), message);
    return filled && cleared && !third && third.error() == error::CAPACITY_EXCEEDED;
}

constexpr bool test_span_buffer_nested() {
    constexpr std::array<std::uint8_t, 7> in{0x12, 0x05, 0x0a, 0x03, 7, 8, 9};
    std::array<std::uint8_t, 3> storage{};

    nested_sink message{{}, sink{pb::span_buffer<std::uint8_t>(storage)}};
    const auto first = decode(bytes_of(in), message);
    const bool filled = first && message.body.bytes.size() == 3 && message.body.bytes[1] == 8;

    const auto second = decode(std::span<const std::uint8_t>(), message);
    return filled && second && message.body.bytes.empty() && message.body.bytes.capacity() == 3;
}

constexpr bool test_errors() {
    constexpr std::array<std::uint8_t, 1> truncated{0x08};
    constexpr std::array<std::uint8_t, 1> wire_type{0x0B};
    constexpr std::array<std::uint8_t, 1> tag{0x00};
    constexpr std::array<std::uint8_t, 12> varint{0x08, 0x80, 0x80, 0x80, 0x80, 0x80,
                                                  0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    constexpr std::array<std::uint8_t, 3> submessage{0x0a, 0x01, 0x08};
    return fails<inner>(bytes_of(truncated), error::TRUNCATED) &&
           fails<inner>(bytes_of(wire_type), error::INVALID_WIRE_TYPE) &&
           fails<inner>(bytes_of(tag), error::INVALID_TAG) &&
           fails<inner>(bytes_of(varint), error::INVALID_VARINT) &&
           fails<outer>(bytes_of(submessage), error::TRUNCATED);
}

constexpr bool same(const pose_owned& a, const pose_owned& b) {
    return same_items(a.pos, b.pos, same_bits) && same_items(a.rot, b.rot, same_bits);
}

constexpr bool same(const envelope_owned& a, const envelope_owned& b) {
    if (a.id != b.id || a.payload.index() != b.payload.index())
        return false;
    if (const auto* pose = std::get_if<pose_owned>(&a.payload))
        return same(*pose, std::get<pose_owned>(b.payload));
    if (const auto* message = std::get_if<text>(&a.payload))
        return same(*message, std::get<text>(b.payload));
    if (const auto* message = std::get_if<ping>(&a.payload))
        return message->seq == std::get<ping>(b.payload).seq;
    return true;
}

template <std::size_t N>
constexpr pb::repeated<float, N> floats_of(const std::array<float, N>& values) {
    return repeated_of<pb::repeated<float, N>>(values);
}

constexpr envelope_owned envelope_pose_owned() {
    return {2, pose_owned{floats_of<3>({1, 2, 3}), floats_of<4>({0, 0, 0, 1})}};
}

constexpr bool test_decode_lists_empty() { return decodes(vectors::lists_empty, lists{}); }

bool test_decode_lists_full() { return decodes(vectors::lists_full, lists_full()); }

constexpr bool test_decode_pose_zero() {
    return decodes(vectors::pose_zero, pose_owned{floats_of<3>({}), floats_of<4>({})});
}

constexpr bool test_decode_envelope_none() { return decodes(vectors::envelope_none, envelope_owned{1, {}}); }

constexpr bool test_decode_envelope_pose() { return decodes(vectors::envelope_pose, envelope_pose_owned()); }

constexpr bool test_decode_envelope_ping() { return decodes(vectors::envelope_ping, envelope_owned{3, ping{}}); }

bool test_decode_envelope_long() { return decodes(vectors::envelope_long, envelope_owned{4, text_long()}); }

constexpr bool test_unpacked_scalars() {
    constexpr std::array<std::uint8_t, 6> in{0x08, 10, 0x08, 20, 0x08, 30};
    ints_only message{};
    const auto result = decode(bytes_of(in), message);
    return result && message.ints.size() == 3 && message.ints[0] == 10 && message.ints[1] == 20 &&
           message.ints[2] == 30;
}

constexpr bool test_mixed_packing() {
    constexpr std::array<std::uint8_t, 6> in{0x0a, 0x02, 1, 2, 0x08, 3};
    ints_only message{};
    const auto result = decode(bytes_of(in), message);
    return result && message.ints.size() == 3 && message.ints[0] == 1 && message.ints[1] == 2 &&
           message.ints[2] == 3;
}

constexpr bool test_capacity() {
    constexpr std::array<std::uint8_t, 11> packed{0x0a, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    constexpr std::array<std::uint8_t, 18> unpacked{0x08, 1, 0x08, 2, 0x08, 3, 0x08, 4, 0x08,
                                                    5,    0x08, 6, 0x08, 7, 0x08, 8, 0x08, 9};
    constexpr std::array<std::uint8_t, 10> eight{0x0a, 8, 1, 2, 3, 4, 5, 6, 7, 8};
    ints_only message{};
    const auto result = decode(bytes_of(eight), message);
    return fails<ints_only>(bytes_of(packed), error::CAPACITY_EXCEEDED) &&
           fails<ints_only>(bytes_of(unpacked), error::CAPACITY_EXCEEDED) && result && message.ints.size() == 8;
}

constexpr bool test_packed_bad_length() {
    constexpr std::array<std::uint8_t, 7> in{0x0a, 0x05, 0, 0, 0x80, 0x3f, 0};
    return fails<floats_only>(bytes_of(in), error::TRUNCATED);
}

constexpr bool test_repeated_wrong_wire_type() {
    constexpr std::array<std::uint8_t, 5> in{0x0d, 1, 2, 3, 4};
    ints_only message{};
    const auto result = decode(bytes_of(in), message);
    return result && message.ints.empty();
}

constexpr bool test_repeated_message_no_merge() {
    constexpr std::array<std::uint8_t, 6> in{0x22, 0x02, 0x08, 0x01, 0x22, 0x00};
    lists message{};
    const auto result = decode(bytes_of(in), message);
    return result && message.items.size() == 2 && message.items[0].x == 1 && message.items[1].x == 0;
}

constexpr bool test_span_buffer_repeated() {
    constexpr std::array<std::uint8_t, 4> in{0x0a, 0x02, 1, 2};
    std::array<std::int32_t, 4> storage{};

    int_buffer message{pb::span_buffer<std::int32_t>(storage)};
    const auto first = decode(bytes_of(in), message);
    const bool filled = first && message.ints.size() == 2 && storage[0] == 1 && storage[1] == 2;

    const auto second = decode(std::span<const std::uint8_t>(), message);
    return filled && second && message.ints.empty() && message.ints.capacity() == 4 && storage[0] == 1;
}

constexpr bool test_oneof_last_wins() {
    std::array<std::uint8_t, 2 + vectors::envelope_pose.size()> in{0x1a, 0x00};
    std::copy(vectors::envelope_pose.begin(), vectors::envelope_pose.end(), in.begin() + 2);

    envelope_owned message{};
    const auto result = decode(bytes_of(in), message);
    return result && same(message, envelope_pose_owned());
}

constexpr bool test_oneof_merge() {
    constexpr std::array<std::uint8_t, 8> in{0x1a, 0x02, 0x08, 0x01, 0x1a, 0x00, 0x08, 0x05};
    envelope_owned message{};
    const auto result = decode(bytes_of(in), message);
    const auto* active = std::get_if<ping>(&message.payload);
    return result && active && active->seq == 1 && message.id == 5;
}

constexpr bool test_oneof_replace() {
    constexpr std::array<std::uint8_t, 10> in{0x1a, 0x02, 0x08, 0x07, 0x12, 0x00, 0x1a, 0x00, 0x08, 0x01};
    envelope_owned message{};
    const auto result = decode(bytes_of(in), message);
    const auto* active = std::get_if<ping>(&message.payload);
    return result && active && active->seq == 0 && message.id == 1;
}

constexpr bool test_oneof_wrong_wire_type() {
    constexpr std::array<std::uint8_t, 7> in{0x1a, 0x00, 0x15, 1, 2, 3, 4};
    envelope_owned message{};
    const auto result = decode(bytes_of(in), message);
    return result && std::holds_alternative<ping>(message.payload);
}

////////////////////////////////////////////////////////////////////////////////

static_assert(test_decode_scalars_zero());
static_assert(test_decode_scalars_max());
static_assert(test_decode_scalars_negative());
static_assert(test_decode_scalars_enum_neg());
static_assert(test_decode_outer_unset());
static_assert(test_reset());
static_assert(test_last_wins());
static_assert(test_unknown_skip());
static_assert(test_wrong_wire_type());
static_assert(test_wrong_wire_type_optional());
static_assert(test_narrowing());
static_assert(test_narrow_enum());
static_assert(test_fixed_bytes());
static_assert(test_repeated_bytes());
static_assert(test_span_buffer());
static_assert(test_span_buffer_nested());
static_assert(test_errors());
static_assert(test_decode_lists_empty());
static_assert(test_decode_pose_zero());
static_assert(test_decode_envelope_none());
static_assert(test_decode_envelope_pose());
static_assert(test_decode_envelope_ping());
static_assert(test_unpacked_scalars());
static_assert(test_mixed_packing());
static_assert(test_capacity());
static_assert(test_packed_bad_length());
static_assert(test_repeated_wrong_wire_type());
static_assert(test_repeated_message_no_merge());
static_assert(test_span_buffer_repeated());
static_assert(test_oneof_last_wins());
static_assert(test_oneof_merge());
static_assert(test_oneof_replace());
static_assert(test_oneof_wrong_wire_type());

} // namespace

int main() {
    const bool ok = test_decode_scalars_zero() && test_decode_scalars_max() && test_decode_scalars_negative() &&
                    test_decode_scalars_enum_neg() && test_decode_text_short() && test_decode_text_long() &&
                    test_decode_outer_set() &&
                    test_decode_outer_unset() && test_zero_copy() && test_reset() && test_last_wins() &&
                    test_merge() && test_unknown_skip() && test_wrong_wire_type() &&
                    test_wrong_wire_type_optional() && test_narrowing() && test_narrow_enum() &&
                    test_fixed_bytes() &&
                    test_repeated_bytes() && test_span_buffer() && test_span_buffer_nested() && test_errors() &&
                    test_decode_lists_empty() &&
                    test_decode_pose_zero() &&
                    test_decode_envelope_none() &&
                    test_decode_envelope_pose() &&
                    test_decode_envelope_ping() &&
                    test_unpacked_scalars() &&
                    test_mixed_packing() &&
                    test_capacity() &&
                    test_packed_bad_length() &&
                    test_repeated_wrong_wire_type() &&
                    test_repeated_message_no_merge() &&
                    test_span_buffer_repeated() &&
                    test_oneof_last_wins() &&
                    test_oneof_merge() &&
                    test_oneof_replace() &&
                    test_oneof_wrong_wire_type() &&
                    test_decode_lists_full() &&
                    test_decode_envelope_long();
    return ok ? 0 : 1;
}
