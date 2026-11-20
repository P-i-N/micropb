#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/codec.hpp>

namespace {

namespace d = pb::detail;
using K = pb::field_kind;

struct plain
{
	std::uint8_t a;
	std::uint32_t b;
	double c;
};

struct nothing
{};

struct holder
{
	std::uint32_t id;
	[[= pb::tag{ 5 }]] plain child;
	std::optional<float> maybe;
	pb::repeated<float, 4> floats;
	pb::repeated<std::uint8_t, 16> blob;
	std::array<std::uint8_t, 6> mac;
	std::span<const std::int32_t> view;
	std::string_view name;
	nothing none;
	std::variant<std::monostate, plain, float> choice;
};

enum class small : std::uint8_t
{
	ZERO
};
enum class level : std::int32_t
{
	ZERO
};

constexpr const d::message_desc& p = d::message_descriptor<plain>::value;
constexpr const d::message_desc& h = d::message_descriptor<holder>::value;

////////////////////////////////////////////////////////////////////////////////

// `plain`: three scalars at the offsets 0, 4, 8, with the numbers 1, 2, 3.
static_assert( p.count == 3 && p.fields[0].offset == 0 && p.fields[1].offset == 4 && p.fields[2].offset == 8 );
static_assert( p.fields[2].number == 3 && p.fields[2].type->kind == K::SCALAR && p.fields[0].alternative == 0 );
static_assert( p.fields[1].type == &d::value_descriptor<std::uint32_t>::value );

// `holder`: 11 entries, with the numbers 1 5 6 7 8 9 10 11 12 13 14.
static_assert( h.count == 11 && h.fields[1].number == 5 && h.fields[8].number == 12 && h.fields[10].number == 14 );
static_assert( h.fields[1].type->kind == K::MESSAGE && h.fields[1].type->message == &p );
static_assert( h.fields[2].type->kind == K::OPTIONAL &&
               h.fields[2].type->optional->value == &d::value_descriptor<float>::value );
static_assert( h.fields[3].type->kind == K::REPEATED &&
               h.fields[3].type->sequence->storage == d::sequence_storage::REPEATED &&
               h.fields[3].type->sequence->capacity == 4 && h.fields[3].type->sequence->element_size == 4 &&
               h.fields[3].type->sequence->element == &d::value_descriptor<float>::value );
static_assert( h.fields[4].type->kind == K::BYTES && h.fields[4].type->sequence->capacity == 16 );
static_assert( h.fields[5].type->kind == K::FIXED_BYTES && h.fields[5].type->size == 6 );
static_assert( h.fields[6].type->kind == K::REPEATED &&
               h.fields[6].type->sequence->storage == d::sequence_storage::ACCESSOR &&
               h.fields[6].type->sequence->element == &d::value_descriptor<std::int32_t>::value );
static_assert( h.fields[7].type->kind == K::STRING );
static_assert( h.fields[8].type->kind == K::MESSAGE && h.fields[8].type->message->count == 0 );
static_assert( h.fields[9].alternative == 1 && h.fields[10].alternative == 2 &&
               h.fields[9].offset == h.fields[10].offset && h.fields[9].type == h.fields[10].type &&
               h.fields[9].type->kind == K::ONEOF &&
               h.fields[9].type->variant->alternatives[1] == &d::value_descriptor<plain>::value &&
               h.fields[9].type->variant->alternatives[2] == &d::value_descriptor<float>::value );

static_assert( d::value_descriptor<std::span<const std::uint8_t>>::value.kind == K::BYTES &&
               d::value_descriptor<std::span<const std::uint8_t>>::value.sequence == nullptr );
static_assert( d::value_descriptor<std::array<float, 3>>::value.sequence->storage == d::sequence_storage::ARRAY &&
               d::value_descriptor<std::array<float, 3>>::value.sequence->capacity == 3 );

static_assert( d::scalar_code_of<bool>() == ( 0 | d::scalar_bool ) );
static_assert( d::scalar_code_of<std::int8_t>() == ( 0 | d::scalar_signed ) );
static_assert( d::scalar_code_of<std::uint16_t>() == 1 && d::scalar_code_of<std::uint64_t>() == 3 );
static_assert( d::scalar_code_of<pb::sint32>() == ( 2 | d::scalar_signed | d::scalar_zigzag ) );
static_assert( d::scalar_code_of<float>() == ( 2 | d::scalar_fixed ) &&
               d::scalar_code_of<pb::sfixed64>() == ( 3 | d::scalar_fixed ) );
static_assert( d::scalar_code_of<small>() == ( 0 | d::scalar_enum ) );
static_assert( d::scalar_code_of<level>() == ( 2 | d::scalar_enum | d::scalar_signed ) );

static_assert( sizeof( d::field_desc ) <= ( sizeof( void* ) == 4 ? 12 : 16 ) );
static_assert( sizeof( d::value_desc ) <= 2 * sizeof( void* ) );

////////////////////////////////////////////////////////////////////////////////

bool test_optional_accessor()
{
	const d::optional_desc& desc = *d::value_descriptor<std::optional<plain>>::value.optional;
	std::optional<plain> value;
	if ( desc.access( &value, d::optional_op::GET ) != nullptr )
		return false;

	auto* first = static_cast<plain*>( desc.access( &value, d::optional_op::ENGAGE ) );
	if ( first == nullptr || !value || value->b != 0 )
		return false;

	first->b = 7;
	if ( desc.access( &value, d::optional_op::ENGAGE ) != first || value->b != 7 )
		return false;
	if ( desc.access( &value, d::optional_op::GET ) != first )
		return false;

	return desc.access( &value, d::optional_op::RESET ) == nullptr && !value;
}

bool test_variant_accessor()
{
	using choice = std::variant<std::monostate, plain, float>;
	const d::variant_desc& desc = *d::value_descriptor<choice>::value.variant;
	choice value;
	if ( desc.access( &value, 1, false ) != nullptr )
		return false;

	auto* first = static_cast<plain*>( desc.access( &value, 1, true ) );
	if ( first == nullptr || value.index() != 1 || first->b != 0 )
		return false;

	first->b = 7;
	if ( desc.access( &value, 1, true ) != first || std::get<plain>( value ).b != 7 )
		return false;
	if ( desc.access( &value, 2, false ) != nullptr || value.index() != 1 )
		return false;

	return desc.access( &value, 0, true ) != nullptr && value.index() == 0;
}

bool test_sequence_accessor()
{
	constexpr std::array<std::int32_t, 3> items{ 1, 2, 3 };
	std::span<const std::int32_t> view( items );
	const d::sequence_desc& view_desc = *d::value_descriptor<std::span<const std::int32_t>>::value.sequence;
	const d::sequence_view a = view_desc.access( &view );
	if ( a.count != 3 || a.capacity != 3 || a.data != items.data() || a.stored_count != nullptr )
		return false;

	std::array<std::int32_t, 4> storage{};
	pb::span_buffer<std::int32_t> buffer( storage );
	static_cast<void>( buffer.push_back( 1 ) );
	static_cast<void>( buffer.push_back( 2 ) );
	const d::sequence_desc& buffer_desc = *d::value_descriptor<pb::span_buffer<std::int32_t>>::value.sequence;
	const d::sequence_view b = buffer_desc.access( &buffer );
	if ( b.count != 2 || b.capacity != 4 || b.data != storage.data() || b.stored_count == nullptr ||
	     *b.stored_count != 2 )
		return false;

	*b.stored_count = 3;
	if ( buffer.size() != 3 || view_desc.element_size != 4 || buffer_desc.storage != d::sequence_storage::ACCESSOR )
		return false;

	// Only a `std::span<const T>` has a binder: `decode` sets it to a block in the arena.
	std::array<std::int32_t, 2> block{ 5, 6 };
	view_desc.bind( &view, block.data(), 2 );
	return view.data() == block.data() && view.size() == 2 && view_desc.element_align == alignof( std::int32_t ) &&
	       view_desc.construct == &d::construct_item<std::int32_t> && buffer_desc.bind == nullptr &&
	       buffer_desc.construct == nullptr &&
	       d::value_descriptor<pb::repeated<float, 4>>::value.sequence->bind == nullptr &&
	       view_desc.reserve == &d::reserve && buffer_desc.reserve == nullptr &&
	       d::value_descriptor<pb::repeated<float, 4>>::value.sequence->reserve == nullptr &&
	       d::value_descriptor<std::array<float, 2>>::value.sequence->reserve == nullptr;
}

/// A span with more items than the capacity of its block fails closed: the count takes no block
/// (`ARENA_FULL`), and the fill adds no item (`TRUNCATED`).
bool test_block_bound()
{
	// A block with the capacity 1 and 2 items: a span that `decode` did not bind.
	alignas( std::size_t ) std::array<unsigned char, sizeof( std::size_t ) + 2 * sizeof( std::int32_t )> block{};
	const std::size_t capacity = 1;
	std::memcpy( block.data(), &capacity, sizeof capacity );
	const auto* first = reinterpret_cast<const std::int32_t*>( block.data() + sizeof( std::size_t ) );
	std::span<const std::int32_t> view( first, 2 );

	const d::sequence_desc& desc = *d::value_descriptor<std::span<const std::int32_t>>::value.sequence;
	std::array<std::uint8_t, 64> storage{};
	pb::arena memory( storage );
	constexpr std::array<std::uint8_t, 1> value{ 0x05 };
	const pb::field_key key{ 1, pb::wire_type::VARINT };

	pb::reader record( value );
	d::decode_repeated( record, key, desc, &view, &memory );
	pb::reader element( value );
	d::decode_element( element, key.type, desc, &view, &memory );
	return record.failed() && record.status().error() == pb::error::ARENA_FULL && element.failed() &&
	       element.status().error() == pb::error::TRUNCATED && memory.used() == 0 && view.data() == first &&
	       view.size() == 2;
}

////////////////////////////////////////////////////////////////////////////////

enum class mode : std::uint8_t
{
	OFF,
	ON,
	MAX = 200
};
enum class grade : std::int32_t
{
	LOW = -5,
	ZERO = 0,
	HIGH = 5
};

struct inner
{
	std::int32_t x;
	std::string_view label;
	pb::sint64 z;
};

struct pose
{
	pb::repeated<float, 3> pos;
	pb::repeated<float, 4> rot;
};

struct ping
{
	std::uint32_t seq;
};

using choice = std::variant<std::monostate, pose, ping, std::uint32_t>;

/// One member of each kind, with the storage types that `decode` accepts.
struct all
{
	pb::span_buffer<std::int32_t> buffered;
	bool flag;
	std::int8_t i8;
	std::int16_t i16;
	std::int32_t i32;
	std::int64_t i64;
	std::uint8_t u8;
	std::uint16_t u16;
	std::uint32_t u32;
	std::uint64_t u64;
	pb::sint32 s32;
	pb::sint64 s64;
	pb::fixed32 fx32;
	pb::fixed64 fx64;
	pb::sfixed32 sfx32;
	pb::sfixed64 sfx64;
	float f;
	double d;
	mode m;
	grade g;
	std::string_view name;
	std::span<const std::uint8_t> view;
	std::array<std::uint8_t, 6> mac;
	pb::repeated<std::uint8_t, 16> blob;
	inner child;
	std::optional<std::int32_t> maybe;
	std::optional<inner> maybe_child;
	pb::repeated<std::int32_t, 8> ints;
	pb::repeated<pb::sint32, 4> zz;
	pb::repeated<double, 2> ds;
	pb::repeated<inner, 3> items;
	pb::repeated<std::string_view, 3> names;
	[[= pb::oneof_tags<60, 61, 62>{}]] choice payload;
	[[= pb::tag{ 100 }]] std::int16_t tail;
};

/// The same message, with the storage types that only `encode` accepts in the
/// place of the sequences of scalars.
struct all_views
{
	std::span<const std::int32_t> buffered;
	bool flag;
	std::int8_t i8;
	std::int16_t i16;
	std::int32_t i32;
	std::int64_t i64;
	std::uint8_t u8;
	std::uint16_t u16;
	std::uint32_t u32;
	std::uint64_t u64;
	pb::sint32 s32;
	pb::sint64 s64;
	pb::fixed32 fx32;
	pb::fixed64 fx64;
	pb::sfixed32 sfx32;
	pb::sfixed64 sfx64;
	float f;
	double d;
	mode m;
	grade g;
	std::string_view name;
	std::span<const std::uint8_t> view;
	std::array<std::uint8_t, 6> mac;
	pb::repeated<std::uint8_t, 16> blob;
	inner child;
	std::optional<std::int32_t> maybe;
	std::optional<inner> maybe_child;
	std::span<const std::int32_t> ints;
	std::span<const pb::sint32> zz;
	std::array<double, 2> ds;
	pb::repeated<inner, 3> items;
	pb::repeated<std::string_view, 3> names;
	[[= pb::oneof_tags<60, 61, 62>{}]] choice payload;
	[[= pb::tag{ 100 }]] std::int16_t tail;
};

constexpr std::array<std::uint8_t, 3> raw{ 9, 8, 7 };
constexpr std::array<std::int32_t, 3> buffered_values{ 4, 5, 6 };
constexpr std::array<std::int32_t, 3> int_values{ 1, -1, 100000 };
constexpr std::array<pb::sint32, 2> zz_values{ pb::sint32( -1 ), pb::sint32( 2 ) };

template <typename R, typename V>
void append_all( R& sequence, const V& values )
{
	for ( const auto& value : values )
		static_cast<void>( sequence.push_back( value ) );
}

/// Sets the members that `all` and `all_views` have in the same form.
template <typename T>
void fill_common( T& v )
{
	v.flag = true;
	v.i8 = -100;
	v.i16 = -3;
	v.i32 = -2;
	v.i64 = -3;
	v.u8 = 200;
	v.u16 = 65535;
	v.u32 = 300;
	v.u64 = ~std::uint64_t{ 0 };
	v.s32 = -4;
	v.s64 = std::int64_t{ 1 } << 40;
	v.fx32 = 7;
	v.fx64 = std::uint64_t{ 1 } << 40;
	v.sfx32 = -8;
	v.sfx64 = -9;
	v.f = -0.0f;
	v.d = 2.5;
	v.m = mode::MAX;
	v.g = grade::LOW;
	v.name = "hello";
	v.view = raw;
	v.mac = { 1, 2, 3, 4, 5, 6 };
	append_all( v.blob, std::array<std::uint8_t, 4>{ 1, 2, 3, 4 } );
	v.child = { 5, "c", -6 };
	v.maybe = 0;
	v.maybe_child = inner{};
	append_all( v.items, std::array<inner, 3>{ inner{ 1, "a", 2 }, inner{}, inner{ 3, "", -4 } } );
	append_all( v.names, std::array<std::string_view, 2>{ "x", "" } );
	v.tail = -7;
}

using buffer = std::array<std::uint8_t, 512>;

/// An `all` with all members in their initial state. `all` has no default
/// constructor, because a `pb::span_buffer` must get its storage.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
all make_all( std::span<std::int32_t> storage )
{
	return { .buffered = pb::span_buffer<std::int32_t>( storage ) };
}
#pragma GCC diagnostic pop

/// `encode`, `decode` and `encode` again give the same bytes, and `encoded_size` agrees.
bool round_trips( const all& value )
{
	buffer first{};
	const auto size = pb::encode( value, first );
	if ( !size || pb::encoded_size( value ) != *size )
		return false;

	std::array<std::int32_t, 4> storage{};
	all back = make_all( storage );
	const auto used = pb::decode( std::span<const std::uint8_t>( first ).first( *size ), back );
	if ( !used || *used != *size )
		return false;

	buffer second{};
	const auto again = pb::encode( back, second );
	return again && *again == *size && pb::encoded_size( back ) == *size && first == second;
}

template <typename A, typename B>
bool same_bytes( const A& a, const B& b )
{
	buffer first{};
	buffer second{};
	const auto size_a = pb::encode( a, first );
	const auto size_b = pb::encode( b, second );
	return size_a && size_b && *size_a == *size_b && first == second;
}

bool test_all_kinds()
{
	std::array<std::int32_t, 4> storage{};
	const all empty = make_all( storage );
	// A nested message (key 2, length 1) and a fixed-length bytes field (key 2, length 1, 6 bytes) are
	// always on the wire.
	if ( !round_trips( empty ) || pb::encoded_size( empty ) != 12 )
		return false;

	all value = make_all( storage );
	fill_common( value );
	append_all( value.buffered, buffered_values );
	append_all( value.ints, int_values );
	append_all( value.zz, zz_values );
	append_all( value.ds, std::array<double, 2>{ 1.0, -2.0 } );

	all_views views{};
	fill_common( views );
	views.buffered = buffered_values;
	views.ints = int_values;
	views.zz = zz_values;
	views.ds = { 1.0, -2.0 };

	pose where{};
	append_all( where.pos, std::array<float, 3>{ 1, 2, 3 } );
	append_all( where.rot, std::array<float, 4>{ 0, 0, 0, 1 } );

	const choice payloads[]{ std::monostate{}, where, ping{ 0 }, std::uint32_t{ 0 } };
	for ( const choice& payload : payloads )
	{
		value.payload = payload;
		views.payload = payload;
		if ( !round_trips( value ) || !same_bytes( value, views ) )
			return false;
	}
	return true;
}

} // namespace

int main()
{
	const bool ok = test_optional_accessor() && test_variant_accessor() && test_sequence_accessor() &&
	                test_block_bound() && test_all_kinds();
	return ok ? 0 : 1;
}
