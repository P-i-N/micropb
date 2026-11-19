#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/meta_schema.hpp>

using namespace pb;

namespace {

struct t_auto
{
	std::int32_t a;
	std::int32_t b;
	[[= pb::tag{ 10 }]] std::int32_t c;
	std::int32_t d;
};

struct t_oneof
{
	std::int32_t a;
	std::variant<std::monostate, std::int32_t, float> v;
	std::int32_t b;
};

struct t_tags
{
	[[= pb::oneof_tags<7, 5>{}]] std::variant<std::monostate, std::int32_t, float> v;
	std::int32_t b;
};

struct pose
{
	std::array<float, 3> pos;
	std::array<float, 4> rot;
};

struct nested
{
	std::optional<pose> p;
	std::optional<std::span<const std::uint8_t>> data;
	std::variant<std::monostate, std::string_view, pose, std::array<std::uint8_t, 6>> v;
	pb::repeated<std::array<std::uint8_t, 6>, 2> addresses;
	std::array<std::string_view, 2> names;
	std::span<const pose> poses;
};

////////////////////////////////////////////////////////////////////////////////

template <typename T, std::size_t N>
consteval bool numbers_are( const std::array<std::uint32_t, N>& expected )
{
	constexpr auto table = build_tag_table<T>();
	if ( table.size() != N )
		return false;

	for ( std::size_t i = 0; i < N; ++i )
	{
		if ( table[i].number != expected[i] )
			return false;
	}
	return true;
}

consteval bool test_auto_tags()
{
	constexpr auto table = build_tag_table<t_auto>();
	return numbers_are<t_auto, 4>( { 1, 2, 10, 11 } ) &&
	       table[2].member == ^^t_auto::c&& table[2].alternative == no_alternative;
}

consteval bool test_oneof_tags()
{
	constexpr auto table = build_tag_table<t_oneof>();
	return numbers_are<t_oneof, 4>( { 1, 2, 3, 4 } ) && table[1].member == ^^t_oneof::v&& table[1].alternative == 1 &&
	       table[2].member == ^^t_oneof::v&& table[2].alternative == 2 && table[3].member == ^^t_oneof::b;
}

consteval bool test_explicit()
{
	constexpr auto table = build_tag_table<t_tags>();
	return numbers_are<t_tags, 3>( { 5, 6, 7 } ) && table[0].member == ^^t_tags::v&& table[0].alternative == 2 &&
	       table[1].member == ^^t_tags::b&& table[1].alternative == no_alternative&& table[2].member ==
	         ^^t_tags::v&& table[2].alternative == 1;
}

consteval bool test_nested_allowed()
{
	return numbers_are<nested, 8>( { 1, 2, 3, 4, 5, 6, 7, 8 } );
}

consteval bool test_kinds()
{
	return kind_of<sint32>() == field_kind::SCALAR && kind_of<std::string_view>() == field_kind::STRING &&
	       kind_of<std::span<const std::uint8_t>>() == field_kind::BYTES &&
	       kind_of<std::array<std::uint8_t, 6>>() == field_kind::FIXED_BYTES &&
	       kind_of<repeated<float, 4>>() == field_kind::REPEATED && kind_of<pose>() == field_kind::MESSAGE &&
	       kind_of<std::optional<int>>() == field_kind::OPTIONAL &&
	       kind_of<repeated<std::uint8_t, 4>>() == field_kind::BYTES &&
	       kind_of<span_buffer<std::uint8_t>>() == field_kind::BYTES &&
	       kind_of<std::span<const float>>() == field_kind::REPEATED &&
	       kind_of<span_buffer<pose>>() == field_kind::REPEATED &&
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

static_assert( test_auto_tags() );
static_assert( test_oneof_tags() );
static_assert( test_explicit() );
static_assert( test_nested_allowed() );
static_assert( test_kinds() );
static_assert( test_wire_types() );
static_assert( test_packed() );

} // namespace

// The schema exists only at compile time, so `main` evaluates the tests as a constant.
// A `std::span` of elements that are not `const` is not a sequence type.
static_assert( !pb::detail::sequence_traits<std::span<std::int32_t>>::is_sequence &&
               pb::detail::sequence_traits<std::span<const std::int32_t>>::is_sequence );

int main()
{
	constexpr bool ok = test_auto_tags() && test_oneof_tags() && test_explicit() && test_nested_allowed() &&
	                    test_kinds() && test_wire_types() && test_packed();
	return ok ? 0 : 1;
}
