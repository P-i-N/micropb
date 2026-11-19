#include "generator/features.pb.hpp"
#include "vectors/vectors.hpp"
#include "vectors/vectors.pb.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include <micropb/micropb.hpp>

/// The headers that `tools/protoc-gen-micropb.py` wrote from `vectors.proto` and `features.proto`.
/// The expected bytes of the `features` messages come from `protoc --encode` (libprotoc 35.1).
namespace {

namespace gen = micropb::test;

static_assert(std::is_same_v<decltype(gen::scalars::i8), std::int8_t>);
static_assert(std::is_same_v<decltype(gen::scalars::u16), std::uint16_t>);
static_assert(std::is_same_v<decltype(gen::text::mac), std::array<std::uint8_t, 6>>);
static_assert(std::is_same_v<decltype(gen::lists::names), pb::repeated<std::string_view, 8>>);
static_assert(std::is_same_v<decltype(gen::envelope::payload),
                             std::variant<std::monostate, gen::pose, gen::ping, gen::text>>);

static_assert(std::is_same_v<decltype(features::Device::limits), features::limits>);
static_assert(std::is_same_v<decltype(features::Device::trims), pb::repeated<std::int8_t, 4>>);
static_assert(std::is_same_v<decltype(features::Frame::origin), shared::types::Point>);
static_assert(std::is_same_v<decltype(features::Calibration::gains), std::array<float, 4>>);

template <typename T, std::size_t N>
bool encodes_to(const T& message, const std::array<std::uint8_t, N>& wire) {
    std::array<std::uint8_t, N> out{};
    const auto size = pb::encode(message, std::span<std::uint8_t>(out));
    return pb::encoded_size(message) == N && size && *size == N && out == wire;
}

template <typename T, std::size_t N>
bool round_trips(const std::array<std::uint8_t, N>& wire) {
    T message{};
    const auto decoded = pb::decode(std::span<const std::uint8_t>(wire), message);
    return decoded && *decoded == N && encodes_to(message, wire);
}

bool test_reference_vectors() {
    return round_trips<gen::scalars>(vectors::scalars_zero) && round_trips<gen::scalars>(vectors::scalars_max) &&
           round_trips<gen::scalars>(vectors::scalars_negative) &&
           round_trips<gen::scalars>(vectors::scalars_enum_neg) && round_trips<gen::text>(vectors::text_short) &&
           round_trips<gen::text>(vectors::text_long) && round_trips<gen::outer>(vectors::outer_unset) &&
           round_trips<gen::outer>(vectors::outer_set) && round_trips<gen::lists>(vectors::lists_empty) &&
           round_trips<gen::lists>(vectors::lists_full) && round_trips<gen::pose>(vectors::pose_zero) &&
           round_trips<gen::envelope>(vectors::envelope_none) && round_trips<gen::envelope>(vectors::envelope_pose) &&
           round_trips<gen::envelope>(vectors::envelope_ping) && round_trips<gen::envelope>(vectors::envelope_long);
}

/// `envelope.pose.txtpb`: `id: 2 pose { pos: [1, 2, 3] rot: [0, 0, 0, 1] }`.
bool test_reference_values() {
    gen::envelope message{};
    if (!pb::decode(std::span<const std::uint8_t>(vectors::envelope_pose), message))
        return false;

    const auto* pose = std::get_if<gen::pose>(&message.payload);
    gen::scalars values{};
    return message.id == 2 && pose && pose->pos.size() == 3 && pose->pos[2] == 3 && pose->rot.size() == 4 &&
           pose->rot[3] == 1 && pb::decode(std::span<const std::uint8_t>(vectors::scalars_enum_neg), values) &&
           values.c == gen::color::NEG;
}

/// `class: 1 limits {} backup {} serial: 5 text: "hi" ready: true`
bool test_field_numbers() {
    features::Device device{};
    device.class_ = 1;
    device.serial = 5;
    device.event = std::string_view("hi");
    device.ready = true;

    return encodes_to(device, std::array<std::uint8_t, 16>{0x08, 0x01, 0x1a, 0x00, 0x22, 0x00, 0x50, 0x05,
                                                           0xb2, 0x01, 0x02, 0x68, 0x69, 0xd0, 0x01, 0x01});
}

/// `gains: [1, 2, 3, 4] id: "\001\002\003\004\005\006"`
bool test_fixed_arrays() {
    const features::Calibration calibration{{1, 2, 3, 4}, {1, 2, 3, 4, 5, 6}};

    return encodes_to(calibration,
                      std::array<std::uint8_t, 26>{0x0a, 0x10, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00,
                                                   0x40, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x80, 0x40,
                                                   0x12, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
}

/// `device { class: -2 state: STATE_BUSY limits { low: 1 high: 2 } backup {} serial: 7 offset: -300 token: ""
/// spare { index: 3 } slots { index: 1 key: "ab" } slots {} history: [STATE_BUSY, STATE_IDLE] trims: [-1, 5]
/// moved { index: 9 } } slot { key: "k" } origin { x: -4 y: 4 } unit: UNIT_METER mode: RX_MODE_VARIABLE`
constexpr auto frame_wire = std::to_array<std::uint8_t>({
    0x0a, 0x49, 0x08, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x10, 0x01, 0x1a,
    0x04, 0x08, 0x01, 0x10, 0x02, 0x22, 0x00, 0x50, 0x07, 0x58, 0xd4, 0xfd, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x01, 0x62, 0x00, 0x6a, 0x02, 0x08, 0x03, 0x72, 0x06, 0x08, 0x01, 0x12, 0x02,
    0x61, 0x62, 0x72, 0x00, 0x7a, 0x02, 0x01, 0x00, 0x82, 0x01, 0x0b, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x01, 0x05, 0xca, 0x01, 0x02, 0x08, 0x09, 0x12, 0x03, 0x12, 0x01, 0x6b,
    0x1a, 0x04, 0x08, 0x07, 0x10, 0x08, 0x20, 0x01, 0x28, 0x01});

bool test_frame_encode() {
    features::Frame frame{};
    features::Device& device = frame.device;
    device.class_ = -2;
    device.state = features::Device_State::BUSY;
    device.limits = {1, 2};
    device.serial = 7;
    device.offset = -300;
    device.token.emplace();
    device.spare = features::Device_Slot{3, {}};
    device.event = features::Device_Slot{9, {}};

    features::Device_Slot first{1, {}};
    bool ok = first.key.push_back('a') && first.key.push_back('b') && device.slots.push_back(first) &&
              device.slots.push_back({}) && device.history.push_back(features::Device_State::BUSY) &&
              device.history.push_back(features::Device_State::IDLE) && device.trims.push_back(-1) &&
              device.trims.push_back(5) && frame.slot.key.push_back('k');
    frame.origin = {-4, 4};
    frame.unit = shared::types::Unit::METER;
    frame.mode = features::RxMode::VARIABLE;

    return ok && encodes_to(frame, frame_wire);
}

bool test_frame_decode() {
    features::Frame frame{};
    if (!round_trips<features::Frame>(frame_wire) || !pb::decode(std::span<const std::uint8_t>(frame_wire), frame))
        return false;

    const features::Device& device = frame.device;
    const auto* moved = std::get_if<features::Device_Slot>(&device.event);
    return device.class_ == -2 && device.state == features::Device_State::BUSY && device.limits.high == 2 &&
           device.serial == 7 && device.offset == -300 && device.token && device.token->empty() && device.spare &&
           device.spare->index == 3 && device.slots.size() == 2 && device.slots[0].key.size() == 2 &&
           device.slots[0].key[1] == 'b' && device.history.size() == 2 &&
           device.history[0] == features::Device_State::BUSY && device.trims.size() == 2 && device.trims[0] == -1 &&
           moved && moved->index == 9 && !device.ready && frame.slot.key.size() == 1 && frame.origin.x == -4 &&
           frame.origin.y == 4 && frame.unit == shared::types::Unit::METER && frame.mode == features::RxMode::VARIABLE;
}

} // namespace

int main() {
    const bool ok = test_reference_vectors() && test_reference_values() && test_field_numbers() &&
                    test_fixed_arrays() && test_frame_encode() && test_frame_decode();
    return ok ? 0 : 1;
}
