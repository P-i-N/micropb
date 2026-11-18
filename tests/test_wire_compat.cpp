#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <variant>

#include <micropb/micropb.hpp>

using namespace pb;

namespace {

using namespace cases;
using namespace messages;

/// `pose` and `envelope` with the container type that `decode` accepts.
struct pose_owned {
    pb::repeated<float, 3> pos;
    pb::repeated<float, 4> rot;
};

struct envelope_owned {
    std::uint32_t id;
    std::variant<std::monostate, pose_owned, ping, text> payload;
};

template <typename T, std::size_t N>
bool round_trips(const std::array<std::uint8_t, N>& wire) {
    T message{};
    const auto decoded = decode(std::span<const std::uint8_t>(wire), message);
    if (!decoded || *decoded != N)
        return false;

    std::array<std::uint8_t, N> out{};
    const auto encoded = encode(message, std::span<std::uint8_t>(out));
    return encoded && *encoded == N && out == wire;
}

/// A decoded message encodes into a buffer of exactly its encoded size.
template <typename T>
bool reencodes(const T& message) {
    std::array<std::uint8_t, 1024> out{};
    const std::size_t size = encoded_size(message);
    if (size > out.size())
        return false;

    const auto encoded = encode(message, std::span(out).first(size));
    return encoded && *encoded == size;
}

template <typename T, std::size_t N>
bool prefixes_are_safe(const std::array<std::uint8_t, N>& wire) {
    for (std::size_t length = 0; length < N; ++length) {
        T message{};
        const auto result = decode(std::span<const std::uint8_t>(wire).first(length), message);
        if (result ? *result != length || !reencodes(message) : result.error() != error::TRUNCATED)
            return false;
    }
    return true;
}

/// `decode` returns for each copy of `wire` with one changed byte. A sanitizer build finds a bad access.
template <typename T, std::size_t N>
bool corruptions_are_safe(const std::array<std::uint8_t, N>& wire) {
    for (std::size_t i = 0; i < N; ++i) {
        for (const std::uint8_t mask : {0x01, 0x80, 0xFF}) {
            std::array<std::uint8_t, N> in = wire;
            in[i] ^= mask;
            T message{};
            if (decode(std::span<const std::uint8_t>(in), message) && !reencodes(message))
                return false;
        }
    }
    return true;
}

bool test_round_trip_constexpr() {
    return round_trips<scalars>(vectors::scalars_zero) && round_trips<scalars>(vectors::scalars_max) &&
           round_trips<scalars>(vectors::scalars_negative) &&
           round_trips<scalars>(vectors::scalars_enum_neg) && round_trips<outer>(vectors::outer_unset) &&
           round_trips<lists>(vectors::lists_empty) && round_trips<pose_owned>(vectors::pose_zero) &&
           round_trips<envelope_owned>(vectors::envelope_none) &&
           round_trips<envelope_owned>(vectors::envelope_pose) &&
           round_trips<envelope_owned>(vectors::envelope_ping);
}

bool test_round_trip_runtime() {
    return round_trips<text>(vectors::text_short) && round_trips<text>(vectors::text_long) &&
           round_trips<outer>(vectors::outer_set) && round_trips<lists>(vectors::lists_full) &&
           round_trips<envelope_owned>(vectors::envelope_long);
}

bool test_prefix_sweep_constexpr() {
    return prefixes_are_safe<scalars>(vectors::scalars_zero) && prefixes_are_safe<scalars>(vectors::scalars_max) &&
           prefixes_are_safe<scalars>(vectors::scalars_negative) &&
           prefixes_are_safe<scalars>(vectors::scalars_enum_neg) && prefixes_are_safe<outer>(vectors::outer_unset) &&
           prefixes_are_safe<lists>(vectors::lists_empty) && prefixes_are_safe<pose_owned>(vectors::pose_zero) &&
           prefixes_are_safe<envelope_owned>(vectors::envelope_none) &&
           prefixes_are_safe<envelope_owned>(vectors::envelope_pose) &&
           prefixes_are_safe<envelope_owned>(vectors::envelope_ping);
}

bool test_prefix_sweep_runtime() {
    return prefixes_are_safe<text>(vectors::text_short) && prefixes_are_safe<text>(vectors::text_long) &&
           prefixes_are_safe<outer>(vectors::outer_set) && prefixes_are_safe<lists>(vectors::lists_full) &&
           prefixes_are_safe<envelope_owned>(vectors::envelope_long);
}

bool test_corruption_sweep() {
    return corruptions_are_safe<scalars>(vectors::scalars_zero) && corruptions_are_safe<scalars>(vectors::scalars_max) &&
           corruptions_are_safe<scalars>(vectors::scalars_negative) &&
           corruptions_are_safe<scalars>(vectors::scalars_enum_neg) &&
           corruptions_are_safe<outer>(vectors::outer_unset) && corruptions_are_safe<outer>(vectors::outer_set) &&
           corruptions_are_safe<lists>(vectors::lists_empty) && corruptions_are_safe<lists>(vectors::lists_full) &&
           corruptions_are_safe<pose_owned>(vectors::pose_zero) &&
           corruptions_are_safe<text>(vectors::text_short) && corruptions_are_safe<text>(vectors::text_long) &&
           corruptions_are_safe<envelope_owned>(vectors::envelope_none) &&
           corruptions_are_safe<envelope_owned>(vectors::envelope_pose) &&
           corruptions_are_safe<envelope_owned>(vectors::envelope_ping) &&
           corruptions_are_safe<envelope_owned>(vectors::envelope_long);
}

/// The caller frames messages: `decode` reads exactly one message from the span it gets.
bool test_envelope_stream() {
    const envelope sent[3]{{2, pose{{1, 2, 3}, {0, 0, 0, 1}}}, {3, ping{7}}, {4, text{"hi", short_data, mac}}};

    std::array<std::uint8_t, 128> buffer{};
    std::array<std::size_t, 3> sizes{};
    std::size_t used = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto result = encode(sent[i], std::span<std::uint8_t>(buffer).subspan(used));
        if (!result)
            return false;
        sizes[i] = *result;
        used += *result;
    }

    std::size_t offset = 0;
    envelope_owned received[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto result = decode(std::span<const std::uint8_t>(buffer).subspan(offset, sizes[i]), received[i]);
        if (!result || *result != sizes[i])
            return false;
        offset += sizes[i];
    }

    const auto* pos = std::get_if<pose_owned>(&received[0].payload);
    const auto* seq = std::get_if<ping>(&received[1].payload);
    const auto* message = std::get_if<text>(&received[2].payload);
    return received[0].id == 2 && pos && pos->pos.size() == 3 && pos->pos[2] == 3 && pos->rot[3] == 1 &&
           received[1].id == 3 && seq && seq->seq == 7 && received[2].id == 4 && message && message->name == "hi" &&
           message->mac == mac && std::equal(message->data.begin(), message->data.end(), short_data.begin(), short_data.end());
}

////////////////////////////////////////////////////////////////////////////////


} // namespace

int main() {
    const bool ok = test_round_trip_constexpr() && test_round_trip_runtime() && test_prefix_sweep_constexpr() &&
                    test_prefix_sweep_runtime() && test_corruption_sweep() && test_envelope_stream();
    return ok ? 0 : 1;
}
