#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <micropb/micropb.hpp>

// `fixed` is encode-only, so a oneof that has it cannot decode.
struct fixed
{
	std::array<float, 2> gains;
};

struct msg
{
	std::variant<std::monostate, fixed> v;
};

template <>
struct pb::fields_of<fixed> : pb::field_list<fixed, pb::field_at<decltype( fixed::gains ), 1, offsetof( fixed, gains )>>
{};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>>
{};

msg value{};
const auto result = pb::decode( {}, value );
