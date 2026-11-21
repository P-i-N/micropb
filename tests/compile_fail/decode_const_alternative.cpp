#include <cstddef>
#include <cstdint>
#include <variant>

#include <micropb/codec.hpp>

// An alternative of a `std::variant` is `const`: encode-only.
struct msg
{
	std::variant<std::monostate, const std::int32_t> v;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::v ), 1, offsetof( msg, v ), 1>>
{};

msg value{};
const auto result = pb::decode( {}, value );
