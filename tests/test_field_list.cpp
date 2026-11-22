#include <array>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>

#include <micropb/micropb.hpp>

namespace manual {

struct inner
{
	int32_t x;
	std::string_view label;
};

struct message
{
	uint32_t id;
	pb::repeated<float, 3> values;
	std::optional<inner> child;
	pb::oneof<inner, uint32_t> choice;
	std::array<uint8_t, 4> key;
	int32_t delta;
};

struct empty
{};

struct shared_offset
{
	[[no_unique_address]] empty e;
	pb::oneof<int32_t, float> choice;
};

/// Encode only: a `std::array` of a type that is not a byte, and a `const` member.
struct fixed
{
	std::array<float, 2> gains;
	const inner label;
};

/// `const` members of each kind: encode only.
struct constants
{
	const uint32_t id;
	const std::optional<int32_t> maybe;
	const pb::oneof<uint32_t, float> choice;
	const pb::repeated<uint8_t, 4> data;
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
	const int32_t a;
};

struct const_items
{
	std::span<const const_item> items;
};

/// A message that contains itself. The codec rejects it (`recursive_message`).
struct tree
{
	int32_t id;
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
	pb::oneof<var_link> choice;
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
                   pb::field_at<decltype( manual::message::delta ), 4, offsetof( manual::message, delta ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<manual::empty> : pb::field_list<manual::empty>
{};

template <>
struct pb::fields_of<manual::shared_offset>
  : pb::field_list<
      manual::shared_offset,
      pb::field_at<decltype( manual::shared_offset::e ), 1, offsetof( manual::shared_offset, e )>,
      pb::field_at<decltype( manual::shared_offset::choice ), 2, offsetof( manual::shared_offset, choice ), 1>,
      pb::field_at<decltype( manual::shared_offset::choice ), 3, offsetof( manual::shared_offset, choice ), 2>>
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
using message_table = d::field_table<d::field_list_of<manual::message>>;
constexpr const auto& layout = message_table::layout;
static_assert( message_table::entry_count == 7 && layout.words_n == 9 );
static_assert( layout.entries[0].number == 1 && layout.entries[1].number == 2 && layout.entries[2].number == 3 &&
               layout.entries[3].number == 4 && layout.entries[4].number == 5 && layout.entries[5].number == 10 &&
               layout.entries[6].number == 11 );
static_assert( layout.entries[2].offset == offsetof( manual::message, key ) && layout.entries[2].alternative == 0 &&
               layout.subs[layout.entries[2].sub_idx] == &d::value_descriptor<std::array<uint8_t, 4>>::value );
static_assert( layout.entries[5].offset == offsetof( manual::message, choice ) && layout.entries[5].alternative == 1 &&
               layout.entries[6].alternative == 2 &&
               layout.subs[layout.entries[6].sub_idx] == &d::value_descriptor<decltype( manual::message::choice )>::value );
static_assert( d::value_descriptor<manual::empty>::value.words == 0 &&
               d::value_descriptor<manual::empty>::value.table == nullptr );
static_assert( d::value_descriptor<manual::shared_offset>::value.words == 5 );

static_assert( !d::is_recursive<manual::inner>() && !d::is_recursive<manual::message>() &&
               !d::is_recursive<manual::empty>() && !d::is_recursive<manual::fixed>() &&
               !d::is_recursive<manual::constants>() );
static_assert( d::is_recursive<manual::tree>() );
static_assert( !d::is_recursive<manual::forest>() );
static_assert( d::is_recursive<manual::opt_node>() && d::is_recursive<manual::opt_link>() );
static_assert( d::is_recursive<manual::var_node>() && d::is_recursive<manual::var_link>() );

static_assert( d::needs_arena<manual::spans>() && !d::needs_arena<manual::message>() &&
               !d::needs_arena<manual::inner>() && !d::needs_arena<manual::empty>() );

template <typename T, size_t N>
bool encodes_to( const T& message, const std::array<uint8_t, N>& wire )
{
	std::array<uint8_t, N> out{};
	const auto size = pb::encode( message, std::span<uint8_t>( out ) );
	return pb::encoded_size( message ) == N && size && *size == N && out == wire;
}

/// `id: 7 values: [1] key: "\001\002\003\004" delta: -1 child { x: 2 } second: 9`, hand-encoded.
constexpr auto message_wire =
  std::to_array<uint8_t>( { 0x08, 0x07, 0x12, 0x04, 0x00, 0x00, 0x80, 0x3f, 0x1a, 0x04, 0x01,
	                               0x02, 0x03, 0x04, 0x20, 0x01, 0x2a, 0x02, 0x08, 0x02, 0x58, 0x09 } );

bool test_encode()
{
	manual::message message{};
	message.id = 7;
	message.child = manual::inner{ 2, {} };
	message.choice = uint32_t{ 9 };
	message.key = { 1, 2, 3, 4 };
	message.delta = -1;
	return message.values.push_back( 1.0f ) && encodes_to( message, message_wire ) &&
	       encodes_to( manual::empty{}, std::array<uint8_t, 0>{} ) &&
	       encodes_to( manual::inner{ 1, "a" }, std::array<uint8_t, 5>{ 0x08, 0x01, 0x12, 0x01, 0x61 } );
}

bool test_decode()
{
	manual::message message{};
	const auto size = pb::decode( std::span<const uint8_t>( message_wire ), message );
	const auto* second = pb::get_if<uint32_t>( &message.choice );
	return size && *size == message_wire.size() && message.id == 7 && message.values.size() == 1 &&
	       message.values[0] == 1.0f && message.child && message.child->x == 2 && second && *second == 9 &&
	       message.key[3] == 4 && message.delta == -1 && encodes_to( message, message_wire );
}

/// A different alternative of the oneof: `first { x: 3 label: "b" }`.
bool test_decode_message_alternative()
{
	const auto wire = std::to_array<uint8_t>( { 0x1a, 0x00, 0x52, 0x05, 0x08, 0x03, 0x12, 0x01, 0x62 } );
	manual::message message{};
	message.choice = uint32_t{ 1 };
	const auto size = pb::decode( std::span<const uint8_t>( wire ), message );
	const auto* first = pb::get_if<manual::inner>( &message.choice );
	return size && first && first->x == 3 && first->label == "b";
}

/// `encode`, `decode` with an arena, and `encode` again give the same bytes.
bool test_decode_spans()
{
	const std::array<manual::inner, 2> items{ manual::inner{ 1, "a" }, { 2, {} } };
	const std::array<float, 3> values{ 1.0f, -2.5f, 0.0f };
	std::array<uint8_t, 64> wire{};
	const auto size = pb::encode( manual::spans{ items, values }, std::span<uint8_t>( wire ) );
	if ( !size )
		return false;

	std::array<uint8_t, 256> storage{};
	pb::arena memory( storage );
	manual::spans message{};
	const auto decoded = pb::decode( std::span<const uint8_t>( wire ).first( *size ), message, memory );
	std::array<uint8_t, 64> again{};
	const auto resized = pb::encode( message, std::span<uint8_t>( again ) );
	return decoded && *decoded == *size && message.items.size() == 2 && message.items[0].x == 1 &&
	       message.items[0].label == "a" && message.items[1].x == 2 && message.items[1].label.empty() &&
	       message.values.size() == 3 && message.values[0] == 1.0f && message.values[1] == -2.5f &&
	       message.values[2] == 0.0f && resized && *resized == *size && again == wire;
}

bool test_encode_only()
{
	const manual::fixed fixed{ { 1.0f, 2.0f }, { 3, {} } };
	return encodes_to( fixed,
	                   std::array<uint8_t, 14>{
	                     0x0a, 0x08, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40, 0x12, 0x02, 0x08, 0x03 } );
}

/// A span of an element with a `const` member encodes. Its descriptor has no constructor.
bool test_const_span_element()
{
	using items = decltype( manual::const_items::items );
	const d::sequence_desc& desc = *d::value_descriptor<items>::value.sequence;
	const manual::const_item values[2]{ { 1 }, { 2 } };
	return desc.span->construct == nullptr &&
	       encodes_to( manual::const_items{ values },
	                   std::array<uint8_t, 8>{ 0x0a, 0x02, 0x08, 0x01, 0x0a, 0x02, 0x08, 0x02 } );
}

bool test_const_members()
{
	const manual::constants constants{ 7, 3, uint32_t{ 9 }, {}, "a" };
	return encodes_to( constants, std::array<uint8_t, 9>{ 0x08, 0x07, 0x10, 0x03, 0x18, 0x09, 0x32, 0x01, 0x61 } );
}

} // namespace

int main()
{
	const bool ok = test_encode() && test_decode() && test_decode_message_alternative() && test_decode_spans() &&
	                test_encode_only() && test_const_members() && test_const_span_element();
	return ok ? 0 : 1;
}
