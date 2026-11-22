#define MICROPB_WIDE_TABLES
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include <micropb/codec.hpp>

namespace {

namespace d = pb::detail;
using K = pb::field_kind;

struct plain
{
	uint8_t a;
	uint32_t b;
	double c;
};

struct nothing
{};

struct holder
{
	uint32_t id;
	plain child;
	std::optional<float> maybe;
	pb::repeated<float, 4> floats;
	pb::repeated<uint8_t, 16> blob;
	std::array<uint8_t, 6> mac;
	std::span<const int32_t> view;
	std::string_view name;
	nothing none;
	pb::oneof<plain, float> choice;
};

enum class small : uint8_t
{
	ZERO
};
enum class level : int32_t
{
	ZERO
};

} // namespace

template <>
struct pb::fields_of<plain> : pb::field_list<plain,
                                             pb::field_at<decltype( plain::a ), 1, offsetof( plain, a )>,
                                             pb::field_at<decltype( plain::b ), 2, offsetof( plain, b )>,
                                             pb::field_at<decltype( plain::c ), 3, offsetof( plain, c )>>
{};

template <>
struct pb::fields_of<nothing> : pb::field_list<nothing>
{};

template <>
struct pb::fields_of<holder> : pb::field_list<holder,
                                              pb::field_at<decltype( holder::id ), 1, offsetof( holder, id )>,
                                              pb::field_at<decltype( holder::child ), 5, offsetof( holder, child )>,
                                              pb::field_at<decltype( holder::maybe ), 6, offsetof( holder, maybe )>,
                                              pb::field_at<decltype( holder::floats ), 7, offsetof( holder, floats )>,
                                              pb::field_at<decltype( holder::blob ), 8, offsetof( holder, blob )>,
                                              pb::field_at<decltype( holder::mac ), 9, offsetof( holder, mac )>,
                                              pb::field_at<decltype( holder::view ), 10, offsetof( holder, view )>,
                                              pb::field_at<decltype( holder::name ), 11, offsetof( holder, name )>,
                                              pb::field_at<decltype( holder::none ), 12, offsetof( holder, none )>,
                                              pb::field_at<decltype( holder::choice ), 13, offsetof( holder, choice ), 1>,
                                              pb::field_at<decltype( holder::choice ), 14, offsetof( holder, choice ), 2>>
{};

namespace {

using plain_table = d::field_table<d::field_list_of<plain>>;
using holder_table = d::field_table<d::field_list_of<holder>>;
constexpr const d::value_desc& p = d::value_descriptor<plain>::value;
constexpr const d::value_desc& h = d::value_descriptor<holder>::value;

template <typename Table>
consteval d::field_entry entry_at( size_t index )
{
	size_t pos = 0;
	uint32_t prev = 0;
	d::field_entry entry{};
	for ( size_t i = 0; i <= index; ++i )
	{
		entry = d::decode_entry( Table::value.words.data(), pos, prev );
		prev = entry.number;
	}
	return entry;
}

template <typename Table>
consteval d::value_desc type_at( size_t index )
{
	const d::value_desc* const* subs = nullptr;
	if constexpr ( Table::layout.subs_n > 0 )
		subs = Table::value.subs.data();
	return d::resolve_type( subs, entry_at<Table>( index ) );
}

////////////////////////////////////////////////////////////////////////////////

// `plain`: three inline scalars at offsets 0, 4, 8, numbers 1, 2, 3, zero compound pointers, 3 halfwords (6 B).
static_assert( p.kind == K::MESSAGE && p.scalar == 0 && p.words == 3 && sizeof( plain_table::value ) == 6 );
static_assert( entry_at<plain_table>( 0 ).offset == 0 && entry_at<plain_table>( 1 ).offset == 4 &&
               entry_at<plain_table>( 2 ).offset == 8 );
static_assert( entry_at<plain_table>( 2 ).number == 3 && type_at<plain_table>( 2 ).kind == K::SCALAR &&
               entry_at<plain_table>( 0 ).alternative == 0 );
static_assert( type_at<plain_table>( 1 ).scalar == d::scalar_code_of<uint32_t>() );

// `holder`: 9 1-halfword entries + 2 2-halfword oneof entries = 13 halfwords, and 8 deduplicated compound pointers.
static_assert( h.kind == K::MESSAGE && h.scalar == 8 && h.words == 13 );
static_assert( entry_at<holder_table>( 1 ).number == 5 && entry_at<holder_table>( 8 ).number == 12 &&
               entry_at<holder_table>( 10 ).number == 14 );
static_assert( type_at<holder_table>( 1 ).kind == K::MESSAGE && type_at<holder_table>( 1 ).table == p.table );
static_assert( type_at<holder_table>( 2 ).kind == K::OPTIONAL &&
               type_at<holder_table>( 2 ).optional->value == &d::value_descriptor<float>::value );
static_assert( type_at<holder_table>( 3 ).kind == K::REPEATED &&
               type_at<holder_table>( 3 ).sequence->storage == d::sequence_storage::REPEATED_8 &&
               type_at<holder_table>( 3 ).sequence->capacity == 4 &&
               type_at<holder_table>( 3 ).sequence->element_size == 4 &&
               type_at<holder_table>( 3 ).sequence->element == &d::value_descriptor<float>::value );
static_assert( type_at<holder_table>( 4 ).kind == K::BYTES && type_at<holder_table>( 4 ).sequence->capacity == 16 );
static_assert( type_at<holder_table>( 5 ).kind == K::FIXED_BYTES && type_at<holder_table>( 5 ).size == 6 );
static_assert( type_at<holder_table>( 6 ).kind == K::REPEATED &&
               type_at<holder_table>( 6 ).sequence->storage == d::sequence_storage::ACCESSOR &&
               type_at<holder_table>( 6 ).sequence->element == &d::value_descriptor<int32_t>::value );
static_assert( type_at<holder_table>( 7 ).kind == K::STRING );
static_assert( type_at<holder_table>( 8 ).kind == K::MESSAGE && type_at<holder_table>( 8 ).words == 0 );
static_assert( entry_at<holder_table>( 9 ).alternative == 1 && entry_at<holder_table>( 10 ).alternative == 2 &&
               entry_at<holder_table>( 9 ).offset == entry_at<holder_table>( 10 ).offset &&
               entry_at<holder_table>( 9 ).sub_idx == 0 && entry_at<holder_table>( 10 ).sub_idx == 0 &&
               type_at<holder_table>( 9 ).kind == K::ONEOF &&
               type_at<holder_table>( 9 ).oneof[0] == &d::value_descriptor<plain>::value &&
               type_at<holder_table>( 9 ).oneof[1] == &d::value_descriptor<float>::value );

static_assert( d::value_descriptor<std::span<const uint8_t>>::value.kind == K::BYTES &&
               d::value_descriptor<std::span<const uint8_t>>::value.sequence == nullptr );
static_assert( d::value_descriptor<std::array<float, 3>>::value.sequence->storage == d::sequence_storage::ARRAY &&
               d::value_descriptor<std::array<float, 3>>::value.sequence->capacity == 3 );

static_assert( d::scalar_code_of<bool>() == ( 0 | d::scalar_bool ) );
static_assert( d::scalar_code_of<int8_t>() == ( 0 | d::scalar_signed ) );
static_assert( d::scalar_code_of<uint16_t>() == 1 && d::scalar_code_of<uint64_t>() == 3 );
static_assert( d::scalar_code_of<int32_t, pb::int_encoding::ZIGZAG>() == ( 2 | d::scalar_signed | d::scalar_zigzag ) );
static_assert( d::scalar_code_of<float>() == ( 2 | d::scalar_fixed ) &&
               d::value_descriptor<int64_t, pb::int_encoding::FIXED>::value.scalar == ( 3 | d::scalar_fixed ) );
static_assert( d::scalar_code_of<small>() == 0 );
static_assert( d::scalar_code_of<level>() == ( 2 | d::scalar_signed ) );

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
	using choice = pb::oneof<plain, float>;
	const d::value_desc& desc = d::value_descriptor<choice>::value;
	choice value;
	if ( d::oneof_index( desc, &value ) != 0 )
		return false;

	value.emplace<plain>().b = 7;
	if ( d::oneof_index( desc, &value ) != 1 || pb::get_if<plain>( &value )->b != 7 )
		return false;

	d::set_oneof_index( desc, &value, 0 );
	return value.index() == 0;
}

bool test_sequence_accessor()
{
	constexpr std::array<int32_t, 3> items{ 1, 2, 3 };
	std::span<const int32_t> view( items );
	const d::sequence_desc& view_desc = *d::value_descriptor<std::span<const int32_t>>::value.sequence;
	const d::span_desc& span = *view_desc.span;
	const d::sequence_view a = span.access( &view );
	if ( a.count != 3 || a.data != items.data() )
		return false;

	if ( view_desc.element_size != 4 || view_desc.storage != d::sequence_storage::ACCESSOR )
		return false;

	// Only a `std::span<const T>` has a binder: `decode` sets it to a block in the arena.
	std::array<int32_t, 2> block{ 5, 6 };
	span.bind( &view, block.data(), 2 );
	return view.data() == block.data() && view.size() == 2 && span.element_align == alignof( int32_t ) &&
	       span.construct == &d::construct_item<int32_t> && span.reserve == &d::reserve &&
	       d::value_descriptor<pb::repeated<float, 4>>::value.sequence->span == nullptr &&
	       d::value_descriptor<std::array<float, 2>>::value.sequence->span == nullptr;
}

/// A span with more items than the capacity of its block fails closed: the count takes no block
/// (`ARENA_FULL`), and the fill adds no item (`TRUNCATED`).
bool test_block_bound()
{
	// A block with the capacity 1 and 2 items: a span that `decode` did not bind.
	alignas( size_t ) std::array<unsigned char, sizeof( size_t ) + 2 * sizeof( int32_t )> block{};
	const size_t capacity = 1;
	std::memcpy( block.data(), &capacity, sizeof capacity );
	const auto* first = reinterpret_cast<const int32_t*>( block.data() + sizeof( size_t ) );
	std::span<const int32_t> view( first, 2 );

	const d::sequence_desc& desc = *d::value_descriptor<std::span<const int32_t>>::value.sequence;
	std::array<uint8_t, 64> storage{};
	pb::arena memory( storage );
	constexpr std::array<uint8_t, 1> value{ 0x05 };
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

enum class mode : uint8_t
{
	OFF,
	ON,
	MAX = 200
};
enum class grade : int32_t
{
	LOW = -5,
	ZERO = 0,
	HIGH = 5
};

struct inner
{
	int32_t x;
	std::string_view label;
	int64_t z;
};

struct pose
{
	pb::repeated<float, 3> pos;
	pb::repeated<float, 4> rot;
};

struct ping
{
	uint32_t seq;
};

using choice = pb::oneof<pose, ping, uint32_t>;

/// One member of each kind, with the storage types that `decode` accepts.
struct all
{
	bool flag;
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int32_t s32;
	int64_t s64;
	uint32_t fx32;
	uint64_t fx64;
	int32_t sfx32;
	int64_t sfx64;
	float f;
	double d;
	mode m;
	grade g;
	std::string_view name;
	std::span<const uint8_t> view;
	std::array<uint8_t, 6> mac;
	pb::repeated<uint8_t, 16> blob;
	inner child;
	std::optional<int32_t> maybe;
	std::optional<inner> maybe_child;
	pb::repeated<int32_t, 8> ints;
	pb::repeated<int32_t, 4> zz;
	pb::repeated<double, 2> ds;
	pb::repeated<inner, 3> items;
	pb::repeated<std::string_view, 3> names;
	choice payload;
	int16_t tail;
};

/// The same message, with the storage types that only `encode` accepts in the
/// place of the sequences of scalars.
struct all_views
{
	bool flag;
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int32_t s32;
	int64_t s64;
	uint32_t fx32;
	uint64_t fx64;
	int32_t sfx32;
	int64_t sfx64;
	float f;
	double d;
	mode m;
	grade g;
	std::string_view name;
	std::span<const uint8_t> view;
	std::array<uint8_t, 6> mac;
	pb::repeated<uint8_t, 16> blob;
	inner child;
	std::optional<int32_t> maybe;
	std::optional<inner> maybe_child;
	std::span<const int32_t> ints;
	std::span<const int32_t> zz;
	std::array<double, 2> ds;
	pb::repeated<inner, 3> items;
	pb::repeated<std::string_view, 3> names;
	choice payload;
	int16_t tail;
};

} // namespace

template <>
struct pb::fields_of<inner>
  : pb::field_list<inner,
                   pb::field_at<decltype( inner::x ), 1, offsetof( inner, x )>,
                   pb::field_at<decltype( inner::label ), 2, offsetof( inner, label )>,
                   pb::field_at<decltype( inner::z ), 3, offsetof( inner, z ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<pose> : pb::field_list<pose,
                                            pb::field_at<decltype( pose::pos ), 1, offsetof( pose, pos )>,
                                            pb::field_at<decltype( pose::rot ), 2, offsetof( pose, rot )>>
{};

template <>
struct pb::fields_of<ping> : pb::field_list<ping, pb::field_at<decltype( ping::seq ), 1, offsetof( ping, seq )>>
{};

template <>
struct pb::fields_of<all>
  : pb::field_list<all,
                   pb::field_at<decltype( all::flag ), 1, offsetof( all, flag )>,
                   pb::field_at<decltype( all::i8 ), 2, offsetof( all, i8 )>,
                   pb::field_at<decltype( all::i16 ), 3, offsetof( all, i16 )>,
                   pb::field_at<decltype( all::i32 ), 4, offsetof( all, i32 )>,
                   pb::field_at<decltype( all::i64 ), 5, offsetof( all, i64 )>,
                   pb::field_at<decltype( all::u8 ), 6, offsetof( all, u8 )>,
                   pb::field_at<decltype( all::u16 ), 7, offsetof( all, u16 )>,
                   pb::field_at<decltype( all::u32 ), 8, offsetof( all, u32 )>,
                   pb::field_at<decltype( all::u64 ), 9, offsetof( all, u64 )>,
                   pb::field_at<decltype( all::s32 ), 10, offsetof( all, s32 ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all::s64 ), 11, offsetof( all, s64 ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all::fx32 ), 12, offsetof( all, fx32 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all::fx64 ), 13, offsetof( all, fx64 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all::sfx32 ), 14, offsetof( all, sfx32 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all::sfx64 ), 15, offsetof( all, sfx64 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all::f ), 16, offsetof( all, f )>,
                   pb::field_at<decltype( all::d ), 17, offsetof( all, d )>,
                   pb::field_at<decltype( all::m ), 18, offsetof( all, m )>,
                   pb::field_at<decltype( all::g ), 19, offsetof( all, g )>,
                   pb::field_at<decltype( all::name ), 20, offsetof( all, name )>,
                   pb::field_at<decltype( all::view ), 21, offsetof( all, view )>,
                   pb::field_at<decltype( all::mac ), 22, offsetof( all, mac )>,
                   pb::field_at<decltype( all::blob ), 23, offsetof( all, blob )>,
                   pb::field_at<decltype( all::child ), 24, offsetof( all, child )>,
                   pb::field_at<decltype( all::maybe ), 25, offsetof( all, maybe )>,
                   pb::field_at<decltype( all::maybe_child ), 26, offsetof( all, maybe_child )>,
                   pb::field_at<decltype( all::ints ), 27, offsetof( all, ints )>,
                   pb::field_at<decltype( all::zz ), 28, offsetof( all, zz ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all::ds ), 29, offsetof( all, ds )>,
                   pb::field_at<decltype( all::items ), 30, offsetof( all, items )>,
                   pb::field_at<decltype( all::names ), 31, offsetof( all, names )>,
                   pb::field_at<decltype( all::payload ), 60, offsetof( all, payload ), 1>,
                   pb::field_at<decltype( all::payload ), 61, offsetof( all, payload ), 2>,
                   pb::field_at<decltype( all::payload ), 62, offsetof( all, payload ), 3>,
                   pb::field_at<decltype( all::tail ), 100, offsetof( all, tail )>>
{};

template <>
struct pb::fields_of<all_views>
  : pb::field_list<all_views,
                   pb::field_at<decltype( all_views::flag ), 1, offsetof( all_views, flag )>,
                   pb::field_at<decltype( all_views::i8 ), 2, offsetof( all_views, i8 )>,
                   pb::field_at<decltype( all_views::i16 ), 3, offsetof( all_views, i16 )>,
                   pb::field_at<decltype( all_views::i32 ), 4, offsetof( all_views, i32 )>,
                   pb::field_at<decltype( all_views::i64 ), 5, offsetof( all_views, i64 )>,
                   pb::field_at<decltype( all_views::u8 ), 6, offsetof( all_views, u8 )>,
                   pb::field_at<decltype( all_views::u16 ), 7, offsetof( all_views, u16 )>,
                   pb::field_at<decltype( all_views::u32 ), 8, offsetof( all_views, u32 )>,
                   pb::field_at<decltype( all_views::u64 ), 9, offsetof( all_views, u64 )>,
                   pb::field_at<decltype( all_views::s32 ), 10, offsetof( all_views, s32 ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all_views::s64 ), 11, offsetof( all_views, s64 ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all_views::fx32 ), 12, offsetof( all_views, fx32 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all_views::fx64 ), 13, offsetof( all_views, fx64 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all_views::sfx32 ), 14, offsetof( all_views, sfx32 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all_views::sfx64 ), 15, offsetof( all_views, sfx64 ), 0, pb::int_encoding::FIXED>,
                   pb::field_at<decltype( all_views::f ), 16, offsetof( all_views, f )>,
                   pb::field_at<decltype( all_views::d ), 17, offsetof( all_views, d )>,
                   pb::field_at<decltype( all_views::m ), 18, offsetof( all_views, m )>,
                   pb::field_at<decltype( all_views::g ), 19, offsetof( all_views, g )>,
                   pb::field_at<decltype( all_views::name ), 20, offsetof( all_views, name )>,
                   pb::field_at<decltype( all_views::view ), 21, offsetof( all_views, view )>,
                   pb::field_at<decltype( all_views::mac ), 22, offsetof( all_views, mac )>,
                   pb::field_at<decltype( all_views::blob ), 23, offsetof( all_views, blob )>,
                   pb::field_at<decltype( all_views::child ), 24, offsetof( all_views, child )>,
                   pb::field_at<decltype( all_views::maybe ), 25, offsetof( all_views, maybe )>,
                   pb::field_at<decltype( all_views::maybe_child ), 26, offsetof( all_views, maybe_child )>,
                   pb::field_at<decltype( all_views::ints ), 27, offsetof( all_views, ints )>,
                   pb::field_at<decltype( all_views::zz ), 28, offsetof( all_views, zz ), 0, pb::int_encoding::ZIGZAG>,
                   pb::field_at<decltype( all_views::ds ), 29, offsetof( all_views, ds )>,
                   pb::field_at<decltype( all_views::items ), 30, offsetof( all_views, items )>,
                   pb::field_at<decltype( all_views::names ), 31, offsetof( all_views, names )>,
                   pb::field_at<decltype( all_views::payload ), 60, offsetof( all_views, payload ), 1>,
                   pb::field_at<decltype( all_views::payload ), 61, offsetof( all_views, payload ), 2>,
                   pb::field_at<decltype( all_views::payload ), 62, offsetof( all_views, payload ), 3>,
                   pb::field_at<decltype( all_views::tail ), 100, offsetof( all_views, tail )>>
{};

namespace {

constexpr std::array<uint8_t, 3> raw{ 9, 8, 7 };
constexpr std::array<int32_t, 3> int_values{ 1, -1, 100000 };
constexpr std::array<int32_t, 2> zz_values{ -1, 2 };

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
	v.u64 = ~uint64_t{ 0 };
	v.s32 = -4;
	v.s64 = int64_t{ 1 } << 40;
	v.fx32 = 7;
	v.fx64 = uint64_t{ 1 } << 40;
	v.sfx32 = -8;
	v.sfx64 = -9;
	v.f = -0.0f;
	v.d = 2.5;
	v.m = mode::MAX;
	v.g = grade::LOW;
	v.name = "hello";
	v.view = raw;
	v.mac = { 1, 2, 3, 4, 5, 6 };
	append_all( v.blob, std::array<uint8_t, 4>{ 1, 2, 3, 4 } );
	v.child = { 5, "c", -6 };
	v.maybe = 0;
	v.maybe_child = inner{};
	append_all( v.items, std::array<inner, 3>{ inner{ 1, "a", 2 }, inner{}, inner{ 3, "", -4 } } );
	append_all( v.names, std::array<std::string_view, 2>{ "x", "" } );
	v.tail = -7;
}

using buffer = std::array<uint8_t, 512>;

/// `encode`, `decode` and `encode` again give the same bytes, and `encoded_size` agrees.
bool round_trips( const all& value )
{
	buffer first{};
	const auto size = pb::encode( value, first );
	if ( !size || pb::encoded_size( value ) != *size )
		return false;

	all back{};
	const auto used = pb::decode( std::span<const uint8_t>( first ).first( *size ), back );
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
	const all empty{};
	// A nested message (key 2, length 1) and a fixed-length bytes field (key 2, length 1, 6 bytes) are
	// always on the wire.
	if ( !round_trips( empty ) || pb::encoded_size( empty ) != 12 )
		return false;

	all value{};
	fill_common( value );
	append_all( value.ints, int_values );
	append_all( value.zz, zz_values );
	append_all( value.ds, std::array<double, 2>{ 1.0, -2.0 } );

	all_views views{};
	fill_common( views );
	views.ints = int_values;
	views.zz = zz_values;
	views.ds = { 1.0, -2.0 };

	pose where{};
	append_all( where.pos, std::array<float, 3>{ 1, 2, 3 } );
	append_all( where.rot, std::array<float, 4>{ 0, 0, 0, 1 } );

	const choice payloads[]{ choice{}, where, ping{ 0 }, uint32_t{ 0 } };
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

namespace {

template <size_t I>
struct alt_tag
{
	uint32_t v;
};

template <typename Seq>
struct wide_variant_msg;

template <size_t... I>
struct wide_variant_msg<std::index_sequence<I...>>
{
	using variant_t = pb::oneof<alt_tag<I>...>;
	variant_t first;
	pb::oneof<int32_t, double> second;
	pb::repeated<uint8_t, 4100> pad;
	uint32_t far_scalar;
	uint32_t max_tag_scalar;
};

using wide_msg = wide_variant_msg<std::make_index_sequence<33>>;

} // namespace

template <size_t I>
struct pb::fields_of<alt_tag<I>> : pb::field_list<alt_tag<I>, pb::field_at<decltype( alt_tag<I>::v ), 1, 0>>
{};

template <size_t... I>
pb::field_list<
  wide_msg,
  pb::field_at<decltype( wide_msg::first ), static_cast<uint32_t>( I + 1 ), offsetof( wide_msg, first ), I + 1>...,
  pb::field_at<decltype( wide_msg::second ), 100, offsetof( wide_msg, second ), 1>,
  pb::field_at<decltype( wide_msg::second ), 101, offsetof( wide_msg, second ), 2>,
  pb::field_at<decltype( wide_msg::pad ), 200, offsetof( wide_msg, pad )>,
  pb::field_at<decltype( wide_msg::far_scalar ), 4096, offsetof( wide_msg, far_scalar )>,
  pb::field_at<decltype( wide_msg::max_tag_scalar ),
               pb::detail::max_field_number,
               offsetof( wide_msg, max_tag_scalar )>>
make_wide_list( std::index_sequence<I...> );

template <>
struct pb::fields_of<wide_msg> : decltype( make_wide_list( std::make_index_sequence<33>{} ) )
{};

namespace {

/// Exercises 3-word field entries: alternative 33 (> 32), a second `std::variant`
/// (`sub_idx == 1`), member offsets > 4095, and field numbers 4096 and 2^29 - 1.
bool test_wide_entries()
{
	using wt = d::field_table<d::field_list_of<wide_msg>>;
	// 32 1-word entries (`first` 1..32) + 1 (`pad`) = 33 1-word entries, plus 5 3-word entries = 48 words.
	static_assert( wt::entry_count == 38 && wt::layout.words_n == 32 * 2 + 1 + 5 * 5 + 1 );

	wide_msg tx{};
	tx.first.emplace<33>( alt_tag<32>{ 42 } );
	tx.second = 3.5;
	tx.far_scalar = 777;
	tx.max_tag_scalar = 999;

	std::array<uint8_t, 128> wire{};
	const auto n = pb::encode( tx, wire );
	if ( !n || pb::encoded_size( tx ) != *n )
		return false;

	wide_msg rx{};
	rx.first.emplace<1>( alt_tag<0>{ 9 } );
	rx.second = int32_t{ 5 };
	if ( !pb::decode( std::span<const uint8_t>( wire ).first( *n ), rx ) )
		return false;

	const auto* a33 = pb::get_if<33>( &rx.first );
	const auto* s2 = pb::get_if<double>( &rx.second );
	return a33 != nullptr && a33->v == 42 && s2 != nullptr && *s2 == 3.5 && rx.far_scalar == 777 &&
	       rx.max_tag_scalar == 999 && rx.pad.empty();
}

struct encoded_compounds
{
	std::optional<int32_t> opt_zz;
	std::optional<uint32_t> opt_fx;
	pb::oneof<int32_t, uint32_t> oneof_enc;
};

} // namespace

template <>
struct pb::fields_of<encoded_compounds>
  : pb::field_list<
      encoded_compounds,
      pb::field_at<decltype( encoded_compounds::opt_zz ), 1, offsetof( encoded_compounds, opt_zz ), 0, pb::int_encoding::ZIGZAG>,
      pb::field_at<decltype( encoded_compounds::opt_fx ), 2, offsetof( encoded_compounds, opt_fx ), 0, pb::int_encoding::FIXED>,
      pb::field_at<decltype( encoded_compounds::oneof_enc ), 3, offsetof( encoded_compounds, oneof_enc ), 1, pb::int_encoding::ZIGZAG>,
      pb::field_at<decltype( encoded_compounds::oneof_enc ), 4, offsetof( encoded_compounds, oneof_enc ), 2, pb::int_encoding::FIXED>>
{};

namespace {

/// `pb::int_encoding` works on `std::optional` values and `std::variant` (`oneof`) alternatives.
bool test_encoded_compounds()
{
	// field 1 (varint zigzag -1 -> 0x01): 0x08 0x01
	// field 2 (fixed32 0x04030201):       0x15 0x01 0x02 0x03 0x04
	// field 3 (varint zigzag -2 -> 0x03): 0x18 0x03
	constexpr auto expected_a = std::to_array<uint8_t>( { 0x08, 0x01, 0x15, 0x01, 0x02, 0x03, 0x04, 0x18, 0x03 } );
	encoded_compounds tx_a{ .opt_zz = -1, .opt_fx = 0x04030201u, .oneof_enc = int32_t{ -2 } };
	std::array<uint8_t, 16> out_a{};
	const auto n_a = pb::encode( tx_a, out_a );
	if ( !n_a || *n_a != expected_a.size() || !std::equal( expected_a.begin(), expected_a.end(), out_a.begin() ) )
		return false;

	encoded_compounds rx_a{};
	if ( !pb::decode( expected_a, rx_a ) || rx_a.opt_zz != -1 || rx_a.opt_fx != 0x04030201u ||
	     !pb::holds_alternative<int32_t>( rx_a.oneof_enc ) || *pb::get_if<int32_t>( &rx_a.oneof_enc ) != -2 )
		return false;

	// field 4 (fixed32 0x89ABCDEF): 0x25 0xEF 0xCD 0xAB 0x89
	constexpr auto expected_b = std::to_array<uint8_t>( { 0x25, 0xEF, 0xCD, 0xAB, 0x89 } );
	encoded_compounds tx_b{ .opt_zz = std::nullopt, .opt_fx = std::nullopt, .oneof_enc = uint32_t{ 0x89ABCDEFu } };
	std::array<uint8_t, 16> out_b{};
	const auto n_b = pb::encode( tx_b, out_b );
	if ( !n_b || *n_b != expected_b.size() || !std::equal( expected_b.begin(), expected_b.end(), out_b.begin() ) )
		return false;

	if ( !pb::decode( expected_b, rx_a ) || rx_a.opt_zz.has_value() || rx_a.opt_fx.has_value() ||
	     !pb::holds_alternative<uint32_t>( rx_a.oneof_enc ) ||
	     *pb::get_if<uint32_t>( &rx_a.oneof_enc ) != 0x89ABCDEFu )
		return false;
	return true;
}

} // namespace

int main()
{
	const bool ok = test_optional_accessor() && test_variant_accessor() && test_sequence_accessor() &&
	                test_block_bound() && test_all_kinds() && test_wide_entries() && test_encoded_compounds();
	return ok ? 0 : 1;
}
