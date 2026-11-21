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
	plain child;
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
consteval d::field_entry entry_at( std::size_t index )
{
	std::size_t pos = 0;
	d::field_entry entry{};
	for ( std::size_t i = 0; i <= index; ++i )
		entry = d::decode_entry( Table::value.words.data(), pos );
	return entry;
}

template <typename Table>
consteval d::value_desc type_at( std::size_t index )
{
	const d::value_desc* const* subs = nullptr;
	if constexpr ( Table::layout.subs_n > 0 )
		subs = Table::value.subs.data();
	return d::resolve_type( subs, entry_at<Table>( index ) );
}

////////////////////////////////////////////////////////////////////////////////

// `plain`: three inline scalars at offsets 0, 4, 8, numbers 1, 2, 3, zero compound pointers, 3 words (12 B).
static_assert( p.kind == K::MESSAGE && p.scalar == 0 && p.words == 3 && sizeof( plain_table::value ) == 12 );
static_assert( entry_at<plain_table>( 0 ).offset == 0 && entry_at<plain_table>( 1 ).offset == 4 &&
               entry_at<plain_table>( 2 ).offset == 8 );
static_assert( entry_at<plain_table>( 2 ).number == 3 && type_at<plain_table>( 2 ).kind == K::SCALAR &&
               entry_at<plain_table>( 0 ).alternative == 0 );
static_assert( type_at<plain_table>( 1 ).scalar == d::scalar_code_of<std::uint32_t>() );

// `holder`: 11 1-word entries, with the numbers 1 5 6 7 8 9 10 11 12 13 14, and 8 deduplicated compound pointers.
static_assert( h.kind == K::MESSAGE && h.scalar == 8 && h.words == 11 );
static_assert( entry_at<holder_table>( 1 ).number == 5 && entry_at<holder_table>( 8 ).number == 12 &&
               entry_at<holder_table>( 10 ).number == 14 );
static_assert( type_at<holder_table>( 1 ).kind == K::MESSAGE && type_at<holder_table>( 1 ).table == p.table );
static_assert( type_at<holder_table>( 2 ).kind == K::OPTIONAL &&
               type_at<holder_table>( 2 ).optional->value == &d::value_descriptor<float>::value );
static_assert( type_at<holder_table>( 3 ).kind == K::REPEATED &&
               type_at<holder_table>( 3 ).sequence->storage == d::sequence_storage::REPEATED &&
               type_at<holder_table>( 3 ).sequence->capacity == 4 &&
               type_at<holder_table>( 3 ).sequence->element_size == 4 &&
               type_at<holder_table>( 3 ).sequence->element == &d::value_descriptor<float>::value );
static_assert( type_at<holder_table>( 4 ).kind == K::BYTES && type_at<holder_table>( 4 ).sequence->capacity == 16 );
static_assert( type_at<holder_table>( 5 ).kind == K::FIXED_BYTES && type_at<holder_table>( 5 ).size == 6 );
static_assert( type_at<holder_table>( 6 ).kind == K::REPEATED &&
               type_at<holder_table>( 6 ).sequence->storage == d::sequence_storage::ACCESSOR &&
               type_at<holder_table>( 6 ).sequence->element == &d::value_descriptor<std::int32_t>::value );
static_assert( type_at<holder_table>( 7 ).kind == K::STRING );
static_assert( type_at<holder_table>( 8 ).kind == K::MESSAGE && type_at<holder_table>( 8 ).words == 0 );
static_assert( entry_at<holder_table>( 9 ).alternative == 1 && entry_at<holder_table>( 10 ).alternative == 2 &&
               entry_at<holder_table>( 9 ).offset == entry_at<holder_table>( 10 ).offset &&
               entry_at<holder_table>( 9 ).sub_idx == 0 && entry_at<holder_table>( 10 ).sub_idx == 0 &&
               type_at<holder_table>( 9 ).kind == K::ONEOF &&
               type_at<holder_table>( 9 ).variant->alternatives[1] == &d::value_descriptor<plain>::value &&
               type_at<holder_table>( 9 ).variant->alternatives[2] == &d::value_descriptor<float>::value );

static_assert( d::value_descriptor<std::span<const std::uint8_t>>::value.kind == K::BYTES &&
               d::value_descriptor<std::span<const std::uint8_t>>::value.sequence == nullptr );
static_assert( d::value_descriptor<std::array<float, 3>>::value.sequence->storage == d::sequence_storage::ARRAY &&
               d::value_descriptor<std::array<float, 3>>::value.sequence->capacity == 3 );

static_assert( d::scalar_code_of<bool>() == ( 0 | d::scalar_bool ) );
static_assert( d::scalar_code_of<std::int8_t>() == ( 0 | d::scalar_signed ) );
static_assert( d::scalar_code_of<std::uint16_t>() == 1 && d::scalar_code_of<std::uint64_t>() == 3 );
static_assert( d::scalar_code_of<pb::sint32>() == ( 2 | d::scalar_signed | d::scalar_zigzag ) );
static_assert( d::scalar_code_of<float>() == ( 2 | d::scalar_fixed ) &&
               d::value_descriptor<pb::sfixed64>::value.scalar == ( 3 | d::scalar_fixed ) );
static_assert( d::scalar_code_of<small>() == ( 0 | d::scalar_enum ) );
static_assert( d::scalar_code_of<level>() == ( 2 | d::scalar_enum | d::scalar_signed ) );

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
	const d::span_desc& span = *view_desc.span;
	const d::sequence_view a = span.access( &view );
	if ( a.count != 3 || a.data != items.data() )
		return false;

	if ( view_desc.element_size != 4 || view_desc.storage != d::sequence_storage::ACCESSOR )
		return false;

	// Only a `std::span<const T>` has a binder: `decode` sets it to a block in the arena.
	std::array<std::int32_t, 2> block{ 5, 6 };
	span.bind( &view, block.data(), 2 );
	return view.data() == block.data() && view.size() == 2 && span.element_align == alignof( std::int32_t ) &&
	       span.construct == &d::construct_item<std::int32_t> && span.reserve == &d::reserve &&
	       d::value_descriptor<pb::repeated<float, 4>>::value.sequence->span == nullptr &&
	       d::value_descriptor<std::array<float, 2>>::value.sequence->span == nullptr;
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
	choice payload;
	std::int16_t tail;
};

/// The same message, with the storage types that only `encode` accepts in the
/// place of the sequences of scalars.
struct all_views
{
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
	choice payload;
	std::int16_t tail;
};

} // namespace

template <>
struct pb::fields_of<inner> : pb::field_list<inner,
                                             pb::field_at<decltype( inner::x ), 1, offsetof( inner, x )>,
                                             pb::field_at<decltype( inner::label ), 2, offsetof( inner, label )>,
                                             pb::field_at<decltype( inner::z ), 3, offsetof( inner, z )>>
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
                   pb::field_at<decltype( all::s32 ), 10, offsetof( all, s32 )>,
                   pb::field_at<decltype( all::s64 ), 11, offsetof( all, s64 )>,
                   pb::field_at<decltype( all::fx32 ), 12, offsetof( all, fx32 )>,
                   pb::field_at<decltype( all::fx64 ), 13, offsetof( all, fx64 )>,
                   pb::field_at<decltype( all::sfx32 ), 14, offsetof( all, sfx32 )>,
                   pb::field_at<decltype( all::sfx64 ), 15, offsetof( all, sfx64 )>,
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
                   pb::field_at<decltype( all::zz ), 28, offsetof( all, zz )>,
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
                   pb::field_at<decltype( all_views::s32 ), 10, offsetof( all_views, s32 )>,
                   pb::field_at<decltype( all_views::s64 ), 11, offsetof( all_views, s64 )>,
                   pb::field_at<decltype( all_views::fx32 ), 12, offsetof( all_views, fx32 )>,
                   pb::field_at<decltype( all_views::fx64 ), 13, offsetof( all_views, fx64 )>,
                   pb::field_at<decltype( all_views::sfx32 ), 14, offsetof( all_views, sfx32 )>,
                   pb::field_at<decltype( all_views::sfx64 ), 15, offsetof( all_views, sfx64 )>,
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
                   pb::field_at<decltype( all_views::zz ), 28, offsetof( all_views, zz )>,
                   pb::field_at<decltype( all_views::ds ), 29, offsetof( all_views, ds )>,
                   pb::field_at<decltype( all_views::items ), 30, offsetof( all_views, items )>,
                   pb::field_at<decltype( all_views::names ), 31, offsetof( all_views, names )>,
                   pb::field_at<decltype( all_views::payload ), 60, offsetof( all_views, payload ), 1>,
                   pb::field_at<decltype( all_views::payload ), 61, offsetof( all_views, payload ), 2>,
                   pb::field_at<decltype( all_views::payload ), 62, offsetof( all_views, payload ), 3>,
                   pb::field_at<decltype( all_views::tail ), 100, offsetof( all_views, tail )>>
{};

namespace {

constexpr std::array<std::uint8_t, 3> raw{ 9, 8, 7 };
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

/// `encode`, `decode` and `encode` again give the same bytes, and `encoded_size` agrees.
bool round_trips( const all& value )
{
	buffer first{};
	const auto size = pb::encode( value, first );
	if ( !size || pb::encoded_size( value ) != *size )
		return false;

	all back{};
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

namespace {

template <std::size_t I>
struct alt_tag
{
	std::uint32_t v;
};

template <typename Seq>
struct wide_variant_msg;

template <std::size_t... I>
struct wide_variant_msg<std::index_sequence<I...>>
{
	using variant_t = std::variant<std::monostate, alt_tag<I>...>;
	variant_t first;
	std::variant<std::monostate, std::int32_t, double> second;
	pb::repeated<std::uint8_t, 4100> pad;
	std::uint32_t far_scalar;
	std::uint32_t max_tag_scalar;
};

using wide_msg = wide_variant_msg<std::make_index_sequence<33>>;

} // namespace

template <std::size_t I>
struct pb::fields_of<alt_tag<I>> : pb::field_list<alt_tag<I>, pb::field_at<decltype( alt_tag<I>::v ), 1, 0>>
{};

template <std::size_t... I>
pb::field_list<
  wide_msg,
  pb::field_at<decltype( wide_msg::first ), static_cast<std::uint32_t>( I + 1 ), offsetof( wide_msg, first ), I + 1>...,
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
	static_assert( wt::entry_count == 38 && wt::layout.words_n == 33 + 5 * 3 );

	wide_msg tx{};
	tx.first.emplace<33>( alt_tag<32>{ 42 } );
	tx.second = 3.5;
	tx.far_scalar = 777;
	tx.max_tag_scalar = 999;

	std::array<std::uint8_t, 128> wire{};
	const auto n = pb::encode( tx, wire );
	if ( !n || pb::encoded_size( tx ) != *n )
		return false;

	wide_msg rx{};
	rx.first.emplace<1>( alt_tag<0>{ 9 } );
	rx.second = std::int32_t{ 5 };
	if ( !pb::decode( std::span<const std::uint8_t>( wire ).first( *n ), rx ) )
		return false;

	const auto* a33 = std::get_if<33>( &rx.first );
	const auto* s2 = std::get_if<double>( &rx.second );
	return a33 != nullptr && a33->v == 42 && s2 != nullptr && *s2 == 3.5 && rx.far_scalar == 777 &&
	       rx.max_tag_scalar == 999 && rx.pad.empty();
}

} // namespace

int main()
{
	const bool ok = test_optional_accessor() && test_variant_accessor() && test_sequence_accessor() &&
	                test_block_bound() && test_all_kinds() && test_wide_entries();
	return ok ? 0 : 1;
}
