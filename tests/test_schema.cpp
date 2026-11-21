#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/schema.hpp>

using namespace pb;

namespace {

struct pose
{
	std::array<float, 3> pos;
	std::array<float, 4> rot;
};

////////////////////////////////////////////////////////////////////////////////

consteval bool test_kinds()
{
	return kind_of<sint32>() == field_kind::SCALAR && kind_of<std::string_view>() == field_kind::STRING &&
	       kind_of<std::span<const std::uint8_t>>() == field_kind::BYTES &&
	       kind_of<std::array<std::uint8_t, 6>>() == field_kind::FIXED_BYTES &&
	       kind_of<repeated<float, 4>>() == field_kind::REPEATED && kind_of<pose>() == field_kind::MESSAGE &&
	       kind_of<std::optional<int>>() == field_kind::OPTIONAL &&
	       kind_of<repeated<std::uint8_t, 4>>() == field_kind::BYTES &&
	       kind_of<std::span<const float>>() == field_kind::REPEATED &&
	       kind_of<std::span<const pose>>() == field_kind::REPEATED &&
	       kind_of<std::variant<std::monostate, int>>() == field_kind::ONEOF;
}

consteval bool test_wire_types()
{
	return wire_type_of<float>() == wire_type::FIXED32 && wire_type_of<double>() == wire_type::FIXED64 &&
	       wire_type_of<sfixed64>() == wire_type::FIXED64 && wire_type_of<std::int8_t>() == wire_type::VARINT &&
	       wire_type_of<bool>() == wire_type::VARINT && wire_type_of<sint32>() == wire_type::VARINT &&
	       wire_type_of<fixed32>() == wire_type::FIXED32;
}

consteval bool test_packed()
{
	return is_packed<float>() && is_packed<sint64>() && !is_packed<std::string_view>() && !is_packed<pose>();
}

////////////////////////////////////////////////////////////////////////////////

static_assert( test_kinds() );
static_assert( test_wire_types() );
static_assert( test_packed() );

} // namespace

// A `std::span` of elements that are not `const` is not a sequence type.
static_assert( !pb::detail::sequence_traits<std::span<std::int32_t>>::is_sequence &&
               pb::detail::sequence_traits<std::span<const std::int32_t>>::is_sequence );

int main()
{
	constexpr bool ok = test_kinds() && test_wire_types() && test_packed();
	return ok ? 0 : 1;
}
