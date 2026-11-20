#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include <micropb/micropb.hpp>

// With reflection, the annotations give the field numbers and the library makes the field list.
// Without reflection, the `pb::fields_of` specializations below give the same list.
#if defined( __cpp_impl_reflection )
#define TEST_ANNOTATION( ... ) [[= __VA_ARGS__]]
#else
#define TEST_ANNOTATION( ... )
#endif

namespace manual {

struct inner
{
	std::int32_t x;
	std::string_view label;
};

struct message
{
	std::uint32_t id;
	pb::repeated<float, 3> values;
	TEST_ANNOTATION( pb::tag{ 5 } ) std::optional<inner> child;
	TEST_ANNOTATION( pb::oneof_tags<10, 11>{} ) std::variant<std::monostate, inner, std::uint32_t> choice;
	TEST_ANNOTATION( pb::tag{ 3 } ) std::array<std::uint8_t, 4> key;
	pb::sint32 delta;
};

struct empty
{};

/// Encode only: a `std::array` of a type that is not a byte, and a `const` member.
struct fixed
{
	std::array<float, 2> gains;
	const inner label;
};

/// `const` members of each kind: encode only.
struct constants
{
	const std::uint32_t id;
	const std::optional<std::int32_t> maybe;
	const std::variant<std::monostate, std::uint32_t, float> choice;
	const pb::repeated<std::uint8_t, 4> data;
	const std::string_view name;
};

/// Repeated fields as spans: `decode` puts their items in a `pb::arena`.
struct spans
{
	std::span<const inner> items;
	std::span<const float> values;
};

/// Encode only: the element of a span has a `const` member, so it has no default constructor.
struct const_item
{
	const std::int32_t a;
};

struct const_items
{
	std::span<const const_item> items;
};

/// A message that contains itself. The codec rejects it (`recursive_message`).
struct tree
{
	std::int32_t id;
	std::span<const tree> children;
};

/// Holds a recursive type, but is not on its cycle.
struct forest
{
	tree t;
};

/// A cycle through a `std::optional`: `opt_node`, `opt_link`, `opt_node`.
struct opt_node;

struct opt_link
{
	std::span<const opt_node> nodes;
};

struct opt_node
{
	std::optional<opt_link> link;
};

/// A cycle through a `std::variant` alternative: `var_node`, `var_link`, `var_node`.
struct var_node;

struct var_link
{
	std::span<const var_node> nodes;
};

struct var_node
{
	std::variant<std::monostate, var_link> choice;
};

} // namespace manual

// The lists are in the order of the members, not in the order of the numbers.
template <>
struct pb::fields_of<manual::inner>
  : pb::field_list<manual::inner,
                   pb::field_at<decltype( manual::inner::x ), 1, offsetof( manual::inner, x )>,
                   pb::field_at<decltype( manual::inner::label ), 2, offsetof( manual::inner, label )>>
{};

template <>
struct pb::fields_of<manual::message>
  : pb::field_list<manual::message,
                   pb::field_at<decltype( manual::message::id ), 1, offsetof( manual::message, id )>,
                   pb::field_at<decltype( manual::message::values ), 2, offsetof( manual::message, values )>,
                   pb::field_at<decltype( manual::message::child ), 5, offsetof( manual::message, child )>,
                   pb::field_at<decltype( manual::message::choice ), 10, offsetof( manual::message, choice ), 1>,
                   pb::field_at<decltype( manual::message::choice ), 11, offsetof( manual::message, choice ), 2>,
                   pb::field_at<decltype( manual::message::key ), 3, offsetof( manual::message, key )>,
                   pb::field_at<decltype( manual::message::delta ), 4, offsetof( manual::message, delta )>>
{};

template <>
struct pb::fields_of<manual::empty> : pb::field_list<manual::empty>
{};

template <>
struct pb::fields_of<manual::fixed>
  : pb::field_list<manual::fixed,
                   pb::field_at<decltype( manual::fixed::gains ), 1, offsetof( manual::fixed, gains )>,
                   pb::field_at<decltype( manual::fixed::label ), 2, offsetof( manual::fixed, label )>>
{};

template <>
struct pb::fields_of<manual::constants>
  : pb::field_list<manual::constants,
                   pb::field_at<decltype( manual::constants::id ), 1, offsetof( manual::constants, id )>,
                   pb::field_at<decltype( manual::constants::maybe ), 2, offsetof( manual::constants, maybe )>,
                   pb::field_at<decltype( manual::constants::choice ), 3, offsetof( manual::constants, choice ), 1>,
                   pb::field_at<decltype( manual::constants::choice ), 4, offsetof( manual::constants, choice ), 2>,
                   pb::field_at<decltype( manual::constants::data ), 5, offsetof( manual::constants, data )>,
                   pb::field_at<decltype( manual::constants::name ), 6, offsetof( manual::constants, name )>>
{};

template <>
struct pb::fields_of<manual::spans>
  : pb::field_list<manual::spans,
                   pb::field_at<decltype( manual::spans::items ), 1, offsetof( manual::spans, items )>,
                   pb::field_at<decltype( manual::spans::values ), 2, offsetof( manual::spans, values )>>
{};

template <>
struct pb::fields_of<manual::const_item>
  : pb::field_list<manual::const_item,
                   pb::field_at<decltype( manual::const_item::a ), 1, offsetof( manual::const_item, a )>>
{};

template <>
struct pb::fields_of<manual::const_items>
  : pb::field_list<manual::const_items,
                   pb::field_at<decltype( manual::const_items::items ), 1, offsetof( manual::const_items, items )>>
{};

template <>
struct pb::fields_of<manual::tree>
  : pb::field_list<manual::tree,
                   pb::field_at<decltype( manual::tree::id ), 1, offsetof( manual::tree, id )>,
                   pb::field_at<decltype( manual::tree::children ), 2, offsetof( manual::tree, children )>>
{};

template <>
struct pb::fields_of<manual::forest>
  : pb::field_list<manual::forest, pb::field_at<decltype( manual::forest::t ), 1, offsetof( manual::forest, t )>>
{};

template <>
struct pb::fields_of<manual::opt_link>
  : pb::field_list<manual::opt_link,
                   pb::field_at<decltype( manual::opt_link::nodes ), 1, offsetof( manual::opt_link, nodes )>>
{};

template <>
struct pb::fields_of<manual::opt_node>
  : pb::field_list<manual::opt_node,
                   pb::field_at<decltype( manual::opt_node::link ), 1, offsetof( manual::opt_node, link )>>
{};

template <>
struct pb::fields_of<manual::var_link>
  : pb::field_list<manual::var_link,
                   pb::field_at<decltype( manual::var_link::nodes ), 1, offsetof( manual::var_link, nodes )>>
{};

template <>
struct pb::fields_of<manual::var_node>
  : pb::field_list<manual::var_node,
                   pb::field_at<decltype( manual::var_node::choice ), 1, offsetof( manual::var_node, choice ), 1>>
{};

namespace {

namespace d = pb::detail;

// The table is in the order of the field numbers.
constexpr const d::message_desc& table = d::message_descriptor<manual::message>::value;
static_assert( table.count == 7 );
static_assert( table.fields[0].number == 1 && table.fields[1].number == 2 && table.fields[2].number == 3 &&
               table.fields[3].number == 4 && table.fields[4].number == 5 && table.fields[5].number == 10 &&
               table.fields[6].number == 11 );
static_assert( table.fields[2].offset == offsetof( manual::message, key ) && table.fields[2].alternative == 0 &&
               table.fields[2].type == &d::value_descriptor<std::array<std::uint8_t, 4>>::value );
static_assert( table.fields[5].offset == offsetof( manual::message, choice ) && table.fields[5].alternative == 1 &&
               table.fields[6].alternative == 2 &&
               table.fields[6].type == &d::value_descriptor<decltype( manual::message::choice )>::value );
static_assert( d::message_descriptor<manual::empty>::value.count == 0 );

static_assert( !d::is_recursive<manual::inner>() && !d::is_recursive<manual::message>() &&
               !d::is_recursive<manual::empty>() && !d::is_recursive<manual::fixed>() &&
               !d::is_recursive<manual::constants>() );
static_assert( d::is_recursive<manual::tree>() );
static_assert( !d::is_recursive<manual::forest>() );
static_assert( d::is_recursive<manual::opt_node>() && d::is_recursive<manual::opt_link>() );
static_assert( d::is_recursive<manual::var_node>() && d::is_recursive<manual::var_link>() );

static_assert( d::needs_arena<manual::spans>() && !d::needs_arena<manual::message>() &&
               !d::needs_arena<manual::inner>() && !d::needs_arena<manual::empty>() );

#if defined( __cpp_impl_reflection )
// The list that reflection makes from the struct is the same type as the list above.
static_assert( std::is_same_v<d::reflected_fields<manual::inner>, d::field_list_of<manual::inner>> );
static_assert( std::is_same_v<d::reflected_fields<manual::message>, d::field_list_of<manual::message>> );
static_assert( std::is_same_v<d::reflected_fields<manual::empty>, d::field_list_of<manual::empty>> );
static_assert( std::is_same_v<d::reflected_fields<manual::fixed>, d::field_list_of<manual::fixed>> );
static_assert( std::is_same_v<d::reflected_fields<manual::constants>, d::field_list_of<manual::constants>> );
static_assert( std::is_same_v<d::reflected_fields<manual::spans>, d::field_list_of<manual::spans>> );
#endif

template <typename T, std::size_t N>
bool encodes_to( const T& message, const std::array<std::uint8_t, N>& wire )
{
	std::array<std::uint8_t, N> out{};
	const auto size = pb::encode( message, std::span<std::uint8_t>( out ) );
	return pb::encoded_size( message ) == N && size && *size == N && out == wire;
}

/// `id: 7 values: [1] key: "\001\002\003\004" delta: -1 child { x: 2 } second: 9`, hand-encoded.
constexpr auto message_wire =
  std::to_array<std::uint8_t>( { 0x08, 0x07, 0x12, 0x04, 0x00, 0x00, 0x80, 0x3f, 0x1a, 0x04, 0x01,
	                               0x02, 0x03, 0x04, 0x20, 0x01, 0x2a, 0x02, 0x08, 0x02, 0x58, 0x09 } );

bool test_encode()
{
	manual::message message{};
	message.id = 7;
	message.child = manual::inner{ 2, {} };
	message.choice = std::uint32_t{ 9 };
	message.key = { 1, 2, 3, 4 };
	message.delta = -1;
	return message.values.push_back( 1.0f ) && encodes_to( message, message_wire ) &&
	       encodes_to( manual::empty{}, std::array<std::uint8_t, 0>{} ) &&
	       encodes_to( manual::inner{ 1, "a" }, std::array<std::uint8_t, 5>{ 0x08, 0x01, 0x12, 0x01, 0x61 } );
}

bool test_decode()
{
	manual::message message{};
	const auto size = pb::decode( std::span<const std::uint8_t>( message_wire ), message );
	const auto* second = std::get_if<std::uint32_t>( &message.choice );
	return size && *size == message_wire.size() && message.id == 7 && message.values.size() == 1 &&
	       message.values[0] == 1.0f && message.child && message.child->x == 2 && second && *second == 9 &&
	       message.key[3] == 4 && message.delta == -1 && encodes_to( message, message_wire );
}

/// A different alternative of the oneof: `first { x: 3 label: "b" }`.
bool test_decode_message_alternative()
{
	const auto wire = std::to_array<std::uint8_t>( { 0x1a, 0x00, 0x52, 0x05, 0x08, 0x03, 0x12, 0x01, 0x62 } );
	manual::message message{};
	message.choice = std::uint32_t{ 1 };
	const auto size = pb::decode( std::span<const std::uint8_t>( wire ), message );
	const auto* first = std::get_if<manual::inner>( &message.choice );
	return size && first && first->x == 3 && first->label == "b";
}

/// `encode`, `decode` with an arena, and `encode` again give the same bytes.
bool test_decode_spans()
{
	const std::array<manual::inner, 2> items{ manual::inner{ 1, "a" }, { 2, {} } };
	const std::array<float, 3> values{ 1.0f, -2.5f, 0.0f };
	std::array<std::uint8_t, 64> wire{};
	const auto size = pb::encode( manual::spans{ items, values }, std::span<std::uint8_t>( wire ) );
	if ( !size )
		return false;

	std::array<std::uint8_t, 256> storage{};
	pb::arena memory( storage );
	manual::spans message{};
	const auto decoded = pb::decode( std::span<const std::uint8_t>( wire ).first( *size ), message, memory );
	std::array<std::uint8_t, 64> again{};
	const auto resized = pb::encode( message, std::span<std::uint8_t>( again ) );
	return decoded && *decoded == *size && message.items.size() == 2 && message.items[0].x == 1 &&
	       message.items[0].label == "a" && message.items[1].x == 2 && message.items[1].label.empty() &&
	       message.values.size() == 3 && message.values[0] == 1.0f && message.values[1] == -2.5f &&
	       message.values[2] == 0.0f && resized && *resized == *size && again == wire;
}

bool test_encode_only()
{
	const manual::fixed fixed{ { 1.0f, 2.0f }, { 3, {} } };
	return encodes_to( fixed,
	                   std::array<std::uint8_t, 14>{
	                     0x0a, 0x08, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40, 0x12, 0x02, 0x08, 0x03 } );
}

/// A span of an element with a `const` member encodes. Its descriptor has no constructor.
bool test_const_span_element()
{
	using items = decltype( manual::const_items::items );
	const d::sequence_desc& desc = *d::value_descriptor<items>::value.sequence;
	const manual::const_item values[2]{ { 1 }, { 2 } };
	return desc.construct == nullptr &&
	       encodes_to( manual::const_items{ values },
	                   std::array<std::uint8_t, 8>{ 0x0a, 0x02, 0x08, 0x01, 0x0a, 0x02, 0x08, 0x02 } );
}

bool test_const_members()
{
	const manual::constants constants{ 7, 3, std::uint32_t{ 9 }, {}, "a" };
	return encodes_to( constants, std::array<std::uint8_t, 9>{ 0x08, 0x07, 0x10, 0x03, 0x18, 0x09, 0x32, 0x01, 0x61 } );
}

} // namespace

int main()
{
	const bool ok = test_encode() && test_decode() && test_decode_message_alternative() && test_decode_spans() &&
	                test_encode_only() && test_const_members() && test_const_span_element();
	return ok ? 0 : 1;
}
