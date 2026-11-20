#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/codec.hpp>

using namespace pb;

namespace {

using namespace cases;
using namespace messages;

template <std::size_t N>
constexpr std::span<const std::uint8_t> bytes_of( const std::array<std::uint8_t, N>& array )
{
	return array;
}

template <typename T>
bool fails( std::span<const std::uint8_t> in, error expected )
{
	T message{};
	const auto result = decode( in, message );
	return !result && result.error() == expected;
}

bool same( const inner& a, const inner& b )
{
	return a.x == b.x && a.label == b.label;
}

bool same( const scalars& a, const scalars& b )
{
	return a.i32 == b.i32 && a.i64 == b.i64 && a.u32 == b.u32 && a.u64 == b.u64 && a.s32.value == b.s32.value &&
	       a.s64.value == b.s64.value && a.flag == b.flag &&
	       std::bit_cast<std::uint32_t>( a.f ) == std::bit_cast<std::uint32_t>( b.f ) &&
	       std::bit_cast<std::uint64_t>( a.d ) == std::bit_cast<std::uint64_t>( b.d ) && a.fx32.value == b.fx32.value &&
	       a.fx64.value == b.fx64.value && a.sfx32.value == b.sfx32.value && a.sfx64.value == b.sfx64.value &&
	       a.c == b.c && a.i8 == b.i8 && a.u16 == b.u16;
}

bool same( const text& a, const text& b )
{
	return a.name == b.name && std::equal( a.data.begin(), a.data.end(), b.data.begin(), b.data.end() ) && a.mac == b.mac;
}

bool same( const outer& a, const outer& b )
{
	return same( a.child, b.child ) && a.maybe == b.maybe && a.maybe_child.has_value() == b.maybe_child.has_value() &&
	       ( !a.maybe_child || same( *a.maybe_child, *b.maybe_child ) ) && a.gap == b.gap;
}

bool same_bits( float a, float b )
{
	return std::bit_cast<std::uint32_t>( a ) == std::bit_cast<std::uint32_t>( b );
}

template <typename S, typename F>
bool same_items( const S& a, const S& b, F equal )
{
	return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(), equal );
}

bool same( const lists& a, const lists& b )
{
	return same_items( a.ints, b.ints, std::equal_to<>{} ) && same_items( a.floats, b.floats, same_bits ) &&
	       same_items( a.names, b.names, std::equal_to<>{} ) &&
	       same_items( a.items, b.items, []( const inner& x, const inner& y ) { return same( x, y ); } ) &&
	       same_items( a.zz, b.zz, []( sint64 x, sint64 y ) { return x.value == y.value; } );
}

/// `decode` reads `in` into a `T` that equals `expected`.
template <typename T, std::size_t N>
bool decodes( const std::array<std::uint8_t, N>& in, const T& expected )
{
	T message{};
	const auto result = decode( bytes_of( in ), message );
	return result && *result == N && same( message, expected );
}

struct narrow
{
	std::int32_t i32;
	std::int8_t i8;
	std::uint16_t u16;
	bool flag;
	sint32 s32;
};

enum class small : std::uint8_t
{
	ZERO
};

struct small_enum
{
	small e;
};

struct mac_only
{
	std::array<std::uint8_t, 6> mac;
};

struct blob
{
	pb::repeated<std::uint8_t, 4> bytes;
};

/// `pose` and `envelope` with the container type that `decode` accepts.
struct pose_owned
{
	pb::repeated<float, 3> pos;
	pb::repeated<float, 4> rot;
};

struct envelope_owned
{
	std::uint32_t id;
	std::variant<std::monostate, pose_owned, ping, text> payload;
};

struct ints_only
{
	pb::repeated<std::int32_t, 8> ints;
};

struct floats_only
{
	pb::repeated<float, 4> floats;
};

/// Repeated fields as spans: `decode` puts their items in a `pb::arena`.
struct arena_lists
{
	std::span<const std::int32_t> ints;
	std::span<const std::string_view> names;
	std::span<const float> floats;
};

struct arena_items
{
	std::span<const arena_lists> items;
};

struct arena_holder
{
	arena_lists child;
};

struct arena_choices
{
	std::optional<arena_lists> maybe;
	std::variant<std::monostate, arena_lists, std::uint32_t> choice;
};

struct arena_aligned
{
	std::span<const std::int8_t> small;
	std::span<const double> wide;
};

struct byte_lists
{
	std::span<const std::span<const std::uint8_t>> blobs;
};

/// Members with bytes that are not all valid: a `bool`, the flag of a `std::optional`, the index of a `std::variant`.
struct fragile
{
	std::optional<std::int32_t> o;
	std::variant<std::monostate, std::uint32_t, float> c;
	bool flag;
};

struct fragile_items
{
	std::span<const fragile> items;
};

} // namespace

template <>
struct pb::fields_of<narrow> : pb::field_list<narrow,
                                              pb::field_at<decltype( narrow::i32 ), 1, offsetof( narrow, i32 )>,
                                              pb::field_at<decltype( narrow::i8 ), 2, offsetof( narrow, i8 )>,
                                              pb::field_at<decltype( narrow::u16 ), 3, offsetof( narrow, u16 )>,
                                              pb::field_at<decltype( narrow::flag ), 4, offsetof( narrow, flag )>,
                                              pb::field_at<decltype( narrow::s32 ), 5, offsetof( narrow, s32 )>>
{};

template <>
struct pb::fields_of<small_enum>
  : pb::field_list<small_enum, pb::field_at<decltype( small_enum::e ), 1, offsetof( small_enum, e )>>
{};

template <>
struct pb::fields_of<mac_only>
  : pb::field_list<mac_only, pb::field_at<decltype( mac_only::mac ), 1, offsetof( mac_only, mac )>>
{};

template <>
struct pb::fields_of<blob>
  : pb::field_list<blob, pb::field_at<decltype( blob::bytes ), 1, offsetof( blob, bytes )>>
{};

template <>
struct pb::fields_of<pose_owned>
  : pb::field_list<pose_owned,
                   pb::field_at<decltype( pose_owned::pos ), 1, offsetof( pose_owned, pos )>,
                   pb::field_at<decltype( pose_owned::rot ), 2, offsetof( pose_owned, rot )>>
{};

template <>
struct pb::fields_of<envelope_owned>
  : pb::field_list<envelope_owned,
                   pb::field_at<decltype( envelope_owned::id ), 1, offsetof( envelope_owned, id )>,
                   pb::field_at<decltype( envelope_owned::payload ), 2, offsetof( envelope_owned, payload ), 1>,
                   pb::field_at<decltype( envelope_owned::payload ), 3, offsetof( envelope_owned, payload ), 2>,
                   pb::field_at<decltype( envelope_owned::payload ), 4, offsetof( envelope_owned, payload ), 3>>
{};

template <>
struct pb::fields_of<ints_only>
  : pb::field_list<ints_only, pb::field_at<decltype( ints_only::ints ), 1, offsetof( ints_only, ints )>>
{};

template <>
struct pb::fields_of<floats_only>
  : pb::field_list<floats_only, pb::field_at<decltype( floats_only::floats ), 1, offsetof( floats_only, floats )>>
{};

template <>
struct pb::fields_of<arena_lists>
  : pb::field_list<arena_lists,
                   pb::field_at<decltype( arena_lists::ints ), 1, offsetof( arena_lists, ints )>,
                   pb::field_at<decltype( arena_lists::names ), 2, offsetof( arena_lists, names )>,
                   pb::field_at<decltype( arena_lists::floats ), 3, offsetof( arena_lists, floats )>>
{};

template <>
struct pb::fields_of<arena_items>
  : pb::field_list<arena_items, pb::field_at<decltype( arena_items::items ), 1, offsetof( arena_items, items )>>
{};

template <>
struct pb::fields_of<arena_holder>
  : pb::field_list<arena_holder, pb::field_at<decltype( arena_holder::child ), 1, offsetof( arena_holder, child )>>
{};

template <>
struct pb::fields_of<arena_choices>
  : pb::field_list<arena_choices,
                   pb::field_at<decltype( arena_choices::maybe ), 1, offsetof( arena_choices, maybe )>,
                   pb::field_at<decltype( arena_choices::choice ), 2, offsetof( arena_choices, choice ), 1>,
                   pb::field_at<decltype( arena_choices::choice ), 3, offsetof( arena_choices, choice ), 2>>
{};

template <>
struct pb::fields_of<arena_aligned>
  : pb::field_list<arena_aligned,
                   pb::field_at<decltype( arena_aligned::small ), 1, offsetof( arena_aligned, small )>,
                   pb::field_at<decltype( arena_aligned::wide ), 2, offsetof( arena_aligned, wide )>>
{};

template <>
struct pb::fields_of<byte_lists>
  : pb::field_list<byte_lists, pb::field_at<decltype( byte_lists::blobs ), 1, offsetof( byte_lists, blobs )>>
{};

template <>
struct pb::fields_of<fragile> : pb::field_list<fragile,
                                               pb::field_at<decltype( fragile::o ), 1, offsetof( fragile, o )>,
                                               pb::field_at<decltype( fragile::c ), 2, offsetof( fragile, c ), 1>,
                                               pb::field_at<decltype( fragile::c ), 3, offsetof( fragile, c ), 2>,
                                               pb::field_at<decltype( fragile::flag ), 4, offsetof( fragile, flag )>>
{};

template <>
struct pb::fields_of<fragile_items>
  : pb::field_list<fragile_items, pb::field_at<decltype( fragile_items::items ), 1, offsetof( fragile_items, items )>>
{};

namespace {

static_assert( !detail::needs_arena<ints_only>() && !detail::needs_arena<lists>() && !detail::needs_arena<text>() &&
               !detail::needs_arena<envelope_owned>() );
static_assert( detail::needs_arena<arena_lists>() && detail::needs_arena<arena_items>() &&
               detail::needs_arena<arena_holder>() && detail::needs_arena<arena_choices>() &&
               detail::needs_arena<byte_lists>() );

inline constexpr std::size_t canary_size = 16;
inline constexpr std::uint8_t canary = 0xA5;

/// The bytes in the arena storage before `decode`. The arena tests run with 0x00 and with 0xA5: `decode` must
/// not read an item that it did not make.
std::uint8_t arena_fill = 0x00;

/// Storage of a `pb::arena` of `size` bytes, `offset` bytes after an address that is aligned for each type. The
/// storage holds `arena_fill`. The 16 canary bytes (0xA5) after the arena storage must not change.
struct guarded
{
	explicit guarded( std::size_t size, std::size_t offset = 0 ) : _first( offset ), _size( size )
	{
		std::fill_n( _bytes.begin() + offset, size, arena_fill );
		std::fill_n( _bytes.begin() + offset + size, canary_size, canary );
	}

	[[nodiscard]] pb::arena memory() { return pb::arena( std::span( _bytes ).subspan( _first, _size ) ); }

	[[nodiscard]] bool intact() const
	{
		const auto* first = _bytes.data() + _first + _size;
		return std::all_of( first, first + canary_size, []( std::uint8_t b ) { return b == canary; } );
	}

	/// `true` when `data` points into the arena storage.
	[[nodiscard]] bool holds( const void* data ) const
	{
		const auto address = reinterpret_cast<std::uintptr_t>( data );
		const auto first = reinterpret_cast<std::uintptr_t>( _bytes.data() + _first );
		return address >= first && address < first + _size;
	}

private:
	alignas( std::max_align_t ) std::array<std::uint8_t, 1024> _bytes{};
	std::size_t _first;
	std::size_t _size;
};

template <typename T, std::size_t N>
bool holds_items( std::span<const T> items, const std::array<T, N>& expected )
{
	return std::equal( items.begin(), items.end(), expected.begin(), expected.end() );
}

/// Items of the caller, with a value before them that looks like a block capacity.
struct caller_items
{
	std::size_t fake_capacity;
	std::array<std::int32_t, 2> items;
	std::array<std::int32_t, 4> after;
};

caller_items caller{ 100, { 10, 11 }, {} };

/// Default member initializers: `decode` resets a value that it makes, so they have no effect.
struct defaulted
{
	std::span<const std::int32_t> v{ caller.items };
	std::int32_t x = 7;
};

struct defaulted_optional
{
	std::optional<defaulted> o;
};

struct defaulted_choice
{
	std::variant<std::monostate, defaulted, std::uint32_t> c;
};

} // namespace

template <>
struct pb::fields_of<defaulted>
  : pb::field_list<defaulted,
                   pb::field_at<decltype( defaulted::v ), 1, offsetof( defaulted, v )>,
                   pb::field_at<decltype( defaulted::x ), 2, offsetof( defaulted, x )>>
{};

template <>
struct pb::fields_of<defaulted_optional>
  : pb::field_list<defaulted_optional,
                   pb::field_at<decltype( defaulted_optional::o ), 1, offsetof( defaulted_optional, o )>>
{};

template <>
struct pb::fields_of<defaulted_choice>
  : pb::field_list<defaulted_choice,
                   pb::field_at<decltype( defaulted_choice::c ), 1, offsetof( defaulted_choice, c ), 1>,
                   pb::field_at<decltype( defaulted_choice::c ), 2, offsetof( defaulted_choice, c ), 2>>
{};

namespace {

/// Bytes of one varint field.
struct varint_field
{
	constexpr varint_field( std::uint8_t key, std::uint64_t value )
	{
		_data[_size++] = key;
		while ( value >= 0x80 )
		{
			_data[_size++] = static_cast<std::uint8_t>( value | 0x80 );
			value >>= 7;
		}
		_data[_size++] = static_cast<std::uint8_t>( value );
	}

	constexpr std::span<const std::uint8_t> span() const { return std::span( _data ).first( _size ); }

private:
	std::array<std::uint8_t, 16> _data{};
	std::size_t _size = 0;
};

////////////////////////////////////////////////////////////////////////////////

bool test_decode_scalars_zero()
{
	return decodes( vectors::scalars_zero, scalars{} );
}

bool test_decode_scalars_max()
{
	return decodes( vectors::scalars_max, scalars_max() );
}

bool test_decode_scalars_negative()
{
	return decodes( vectors::scalars_negative, scalars_negative() );
}

bool test_decode_scalars_enum_neg()
{
	return decodes( vectors::scalars_enum_neg, scalars_enum_neg() );
}

bool test_decode_text_short()
{
	return decodes( vectors::text_short, text{ "hi", short_data, mac } );
}

bool test_decode_text_long()
{
	return decodes( vectors::text_long, text_long() );
}

bool test_decode_outer_set()
{
	return decodes( vectors::outer_set, outer{ .child = { 1, {} }, .maybe = 0, .maybe_child = inner{}, .gap = 3 } );
}

bool test_decode_outer_unset()
{
	return decodes( vectors::outer_unset, outer{} );
}

bool test_zero_copy()
{
	text message{};
	const auto result = decode( bytes_of( vectors::text_short ), message );
	const auto* first = reinterpret_cast<const char*>( vectors::text_short.data() );
	return result && message.name.data() == first + 2 && message.data.data() == vectors::text_short.data() + 6;
}

bool test_reset()
{
	scalars message = scalars_max();
	const auto result = decode( bytes_of( vectors::scalars_zero ), message );
	return result && *result == 0 && same( message, scalars{} );
}

bool test_last_wins()
{
	constexpr std::array<std::uint8_t, 4> in{ 0x08, 0x01, 0x08, 0x02 };
	return decodes( in, inner{ 2, {} } );
}

bool test_merge()
{
	constexpr std::array<std::uint8_t, 9> in{ 0x0a, 0x02, 0x08, 0x01, 0x0a, 0x03, 0x12, 0x01, 'a' };
	constexpr std::array<std::uint8_t, 9> optional_in{ 0x1a, 0x02, 0x08, 0x01, 0x1a, 0x03, 0x12, 0x01, 'a' };

	outer merged{};
	outer merged_optional{};
	const auto a = decode( bytes_of( in ), merged );
	const auto b = decode( bytes_of( optional_in ), merged_optional );
	return a && b && merged.child.x == 1 && merged.child.label == "a" && merged_optional.maybe_child &&
	       merged_optional.maybe_child->x == 1 && merged_optional.maybe_child->label == "a";
}

bool test_unknown_skip()
{
	constexpr std::array<std::uint8_t, 2 * 21 + 2> in{ 0x28, 0x01,                            // field 5, varint
	                                                   0x31, 1,    2,   3,   4,   5, 6, 7, 8, // field 6, fixed64
	                                                   0x3a, 0x03, 'a', 'b', 'c',             // field 7, length-delimited
	                                                   0x45, 1,    2,   3,   4,               // field 8, fixed32
	                                                   0x08, 0x05,                            // x = 5
	                                                   0x28, 0x01,                            //
	                                                   0x31, 1,    2,   3,   4,   5, 6, 7, 8, //
	                                                   0x3a, 0x03, 'a', 'b', 'c',             //
	                                                   0x45, 1,    2,   3,   4 };
	inner message{};
	const auto result = decode( bytes_of( in ), message );
	return result && *result == in.size() && message.x == 5 && message.label.empty();
}

bool test_wrong_wire_type()
{
	constexpr std::array<std::uint8_t, 5> fixed{ 0x0d, 1, 2, 3, 4 };
	constexpr std::array<std::uint8_t, 5> as_string{ 0x0a, 0x03, 'a', 'b', 'c' };
	return decodes( fixed, inner{} ) && decodes( as_string, inner{} );
}

bool test_wrong_wire_type_optional()
{
	constexpr std::array<std::uint8_t, 5> in{ 0x15, 1, 2, 3, 4 };
	return decodes( in, outer{} );
}

bool test_narrowing()
{
	constexpr std::uint64_t wide = ( std::uint64_t{ 1 } << 40 ) + 7;
	narrow a{};
	narrow b{};
	narrow c{};
	narrow d{};
	narrow e{};
	const bool ok_a = decode( varint_field( 0x08, wide ).span(), a ).has_value();
	const bool ok_b = decode( varint_field( 0x10, 300 ).span(), b ).has_value();
	const bool ok_c = decode( varint_field( 0x18, 70000 ).span(), c ).has_value();
	const bool ok_d = decode( varint_field( 0x20, 2 ).span(), d ).has_value();
	const bool ok_e = decode( varint_field( 0x28, ( std::uint64_t{ 3 } << 32 ) + 3 ).span(), e ).has_value();
	return ok_a && a.i32 == 7 && ok_b && b.i8 == 44 && ok_c && c.u16 == 4464 && ok_d && d.flag && ok_e &&
	       e.s32.value == -2;
}

/// An enum is `int32` on the wire. The decoder truncates the varint to `std::int32_t`, then converts it to the
/// enum. 300 becomes 44 in an enum with `std::uint8_t` as its underlying type.
bool test_narrow_enum()
{
	small_enum message{};
	const bool ok = decode( varint_field( 0x08, 300 ).span(), message ).has_value();
	return ok && static_cast<std::uint8_t>( message.e ) == 44;
}

bool test_fixed_bytes()
{
	constexpr std::array<std::uint8_t, 6> four{ 0x0a, 0x04, 1, 2, 3, 4 };
	constexpr std::array<std::uint8_t, 9> seven{ 0x0a, 0x07, 1, 2, 3, 4, 5, 6, 7 };
	constexpr std::array<std::uint8_t, 7> five{ 0x0a, 0x05, 1, 2, 3, 4, 5 };
	constexpr std::array<std::uint8_t, 6> padded{ 1, 2, 3, 4, 0, 0 };

	mac_only message{ { 9, 9, 9, 9, 9, 9 } };
	const auto result = decode( bytes_of( four ), message );
	return result && message.mac == padded && fails<mac_only>( bytes_of( seven ), error::CAPACITY_EXCEEDED ) &&
	       fails<blob>( bytes_of( five ), error::CAPACITY_EXCEEDED );
}

bool test_repeated_bytes()
{
	constexpr std::array<std::uint8_t, 6> in{ 0x0a, 0x04, 1, 2, 3, 4 };
	blob message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.bytes.size() == 4 && message.bytes[3] == 4;
}

bool test_errors()
{
	constexpr std::array<std::uint8_t, 1> truncated{ 0x08 };
	constexpr std::array<std::uint8_t, 1> wire_type{ 0x0B };
	constexpr std::array<std::uint8_t, 1> tag{ 0x00 };
	constexpr std::array<std::uint8_t, 12> varint{
		0x08, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01
	};
	constexpr std::array<std::uint8_t, 3> submessage{ 0x0a, 0x01, 0x08 };
	return fails<inner>( bytes_of( truncated ), error::TRUNCATED ) &&
	       fails<inner>( bytes_of( wire_type ), error::INVALID_WIRE_TYPE ) &&
	       fails<inner>( bytes_of( tag ), error::INVALID_TAG ) &&
	       fails<inner>( bytes_of( varint ), error::INVALID_VARINT ) &&
	       fails<outer>( bytes_of( submessage ), error::TRUNCATED );
}

bool same( const pose_owned& a, const pose_owned& b )
{
	return same_items( a.pos, b.pos, same_bits ) && same_items( a.rot, b.rot, same_bits );
}

bool same( const envelope_owned& a, const envelope_owned& b )
{
	if ( a.id != b.id || a.payload.index() != b.payload.index() )
		return false;
	if ( const auto* pose = std::get_if<pose_owned>( &a.payload ) )
		return same( *pose, std::get<pose_owned>( b.payload ) );
	if ( const auto* message = std::get_if<text>( &a.payload ) )
		return same( *message, std::get<text>( b.payload ) );
	if ( const auto* message = std::get_if<ping>( &a.payload ) )
		return message->seq == std::get<ping>( b.payload ).seq;
	return true;
}

template <std::size_t N>
constexpr pb::repeated<float, N> floats_of( const std::array<float, N>& values )
{
	return repeated_of<pb::repeated<float, N>>( values );
}

constexpr envelope_owned envelope_pose_owned()
{
	return { 2, pose_owned{ floats_of<3>( { 1, 2, 3 } ), floats_of<4>( { 0, 0, 0, 1 } ) } };
}

bool test_decode_lists_empty()
{
	return decodes( vectors::lists_empty, lists{} );
}

bool test_decode_lists_full()
{
	return decodes( vectors::lists_full, lists_full() );
}

bool test_decode_pose_zero()
{
	return decodes( vectors::pose_zero, pose_owned{ floats_of<3>( {} ), floats_of<4>( {} ) } );
}

bool test_decode_envelope_none()
{
	return decodes( vectors::envelope_none, envelope_owned{ 1, {} } );
}

bool test_decode_envelope_pose()
{
	return decodes( vectors::envelope_pose, envelope_pose_owned() );
}

bool test_decode_envelope_ping()
{
	return decodes( vectors::envelope_ping, envelope_owned{ 3, ping{} } );
}

bool test_decode_envelope_long()
{
	return decodes( vectors::envelope_long, envelope_owned{ 4, text_long() } );
}

bool test_unpacked_scalars()
{
	constexpr std::array<std::uint8_t, 6> in{ 0x08, 10, 0x08, 20, 0x08, 30 };
	ints_only message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.ints.size() == 3 && message.ints[0] == 10 && message.ints[1] == 20 && message.ints[2] == 30;
}

bool test_mixed_packing()
{
	constexpr std::array<std::uint8_t, 6> in{ 0x0a, 0x02, 1, 2, 0x08, 3 };
	ints_only message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.ints.size() == 3 && message.ints[0] == 1 && message.ints[1] == 2 && message.ints[2] == 3;
}

bool test_capacity()
{
	constexpr std::array<std::uint8_t, 11> packed{ 0x0a, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	constexpr std::array<std::uint8_t, 18> unpacked{ 0x08, 1,    0x08, 2,    0x08, 3,    0x08, 4,    0x08,
	                                                 5,    0x08, 6,    0x08, 7,    0x08, 8,    0x08, 9 };
	constexpr std::array<std::uint8_t, 10> eight{ 0x0a, 8, 1, 2, 3, 4, 5, 6, 7, 8 };
	ints_only message{};
	const auto result = decode( bytes_of( eight ), message );
	return fails<ints_only>( bytes_of( packed ), error::CAPACITY_EXCEEDED ) &&
	       fails<ints_only>( bytes_of( unpacked ), error::CAPACITY_EXCEEDED ) && result && message.ints.size() == 8;
}

bool test_packed_bad_length()
{
	constexpr std::array<std::uint8_t, 7> in{ 0x0a, 0x05, 0, 0, 0x80, 0x3f, 0 };
	return fails<floats_only>( bytes_of( in ), error::TRUNCATED );
}

bool test_repeated_wrong_wire_type()
{
	constexpr std::array<std::uint8_t, 5> in{ 0x0d, 1, 2, 3, 4 };
	ints_only message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.ints.empty();
}

bool test_repeated_message_no_merge()
{
	constexpr std::array<std::uint8_t, 6> in{ 0x22, 0x02, 0x08, 0x01, 0x22, 0x00 };
	lists message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.items.size() == 2 && message.items[0].x == 1 && message.items[1].x == 0;
}

bool test_oneof_last_wins()
{
	std::array<std::uint8_t, 2 + vectors::envelope_pose.size()> in{ 0x1a, 0x00 };
	std::copy( vectors::envelope_pose.begin(), vectors::envelope_pose.end(), in.begin() + 2 );

	envelope_owned message{};
	const auto result = decode( bytes_of( in ), message );
	return result && same( message, envelope_pose_owned() );
}

bool test_oneof_merge()
{
	constexpr std::array<std::uint8_t, 8> in{ 0x1a, 0x02, 0x08, 0x01, 0x1a, 0x00, 0x08, 0x05 };
	envelope_owned message{};
	const auto result = decode( bytes_of( in ), message );
	const auto* active = std::get_if<ping>( &message.payload );
	return result && active && active->seq == 1 && message.id == 5;
}

bool test_oneof_replace()
{
	constexpr std::array<std::uint8_t, 10> in{ 0x1a, 0x02, 0x08, 0x07, 0x12, 0x00, 0x1a, 0x00, 0x08, 0x01 };
	envelope_owned message{};
	const auto result = decode( bytes_of( in ), message );
	const auto* active = std::get_if<ping>( &message.payload );
	return result && active && active->seq == 0 && message.id == 1;
}

bool test_oneof_wrong_wire_type()
{
	constexpr std::array<std::uint8_t, 7> in{ 0x1a, 0x00, 0x15, 1, 2, 3, 4 };
	envelope_owned message{};
	const auto result = decode( bytes_of( in ), message );
	return result && std::holds_alternative<ping>( message.payload );
}

/// The reset before `decode` only sets the count of a sequence to 0. The decoder must reset each new element.
bool test_reuse_repeated_message()
{
	constexpr std::array<std::uint8_t, 7> first{ 0x22, 0x05, 0x08, 0x01, 0x12, 0x01, 'a' };
	constexpr std::array<std::uint8_t, 2> second{ 0x22, 0x00 };
	lists message{};
	const auto a = decode( bytes_of( first ), message );
	const bool filled = a && message.items.size() == 1 && message.items[0].x == 1 && message.items[0].label == "a";

	const auto b = decode( bytes_of( second ), message );
	return filled && b && message.items.size() == 1 && message.items[0].x == 0 && message.items[0].label.empty();
}

/// A second `decode` into the same object does not keep the alternative or its values.
bool test_reuse_oneof()
{
	constexpr std::array<std::uint8_t, 4> first{ 0x1a, 0x02, 0x08, 0x07 };
	constexpr std::array<std::uint8_t, 2> second{ 0x1a, 0x00 };
	envelope_owned message{};
	const auto a = decode( bytes_of( first ), message );
	const auto* seven = std::get_if<ping>( &message.payload );
	const bool filled = a && seven && seven->seq == 7;

	const auto b = decode( bytes_of( second ), message );
	const auto* zero = std::get_if<ping>( &message.payload );
	const bool replaced = b && zero && zero->seq == 0;

	const auto c = decode( std::span<const std::uint8_t>(), message );
	return filled && replaced && c && std::holds_alternative<std::monostate>( message.payload );
}

struct nothing
{};

struct holds_nothing
{
	nothing n;
	std::uint32_t x;
};

} // namespace

template <>
struct pb::fields_of<nothing> : pb::field_list<nothing>
{};

template <>
struct pb::fields_of<holds_nothing>
  : pb::field_list<holds_nothing,
                   pb::field_at<decltype( holds_nothing::n ), 1, offsetof( holds_nothing, n )>,
                   pb::field_at<decltype( holds_nothing::x ), 2, offsetof( holds_nothing, x )>>
{};

namespace {

/// A message type without fields skips all its input.
bool test_decode_empty_message()
{
	constexpr std::array<std::uint8_t, 6> in{ 0x0a, 0x02, 0x08, 0x05, 0x10, 0x01 };
	holds_nothing message{};
	nothing alone{};
	return decode( bytes_of( in ), message ) && message.x == 1 && decode( std::span<const std::uint8_t>(), alone );
}

/// The decoder examines the capacity before it reads an element.
bool test_error_order()
{
	constexpr std::array<std::uint8_t, 3> bad{ 0x22, 0x01, 0x08 };
	std::array<std::uint8_t, 8 * 2 + 3> full{};
	for ( std::size_t i = 0; i < 8; ++i )
		full[2 * i] = 0x22;
	std::copy( bad.begin(), bad.end(), full.begin() + 16 );
	return fails<lists>( bytes_of( bad ), error::TRUNCATED ) &&
	       fails<lists>( bytes_of( full ), error::CAPACITY_EXCEEDED );
}

/// A second `decode` into the same object resets each kind of member: the object can be a receive buffer
/// that the application uses again.
bool test_reuse_resets_each_kind()
{
	text words{};
	const auto a = decode( bytes_of( vectors::text_short ), words );
	const bool filled = a && words.name == "hi" && !words.data.empty() && words.mac == mac;
	const auto b = decode( std::span<const std::uint8_t>(), words );
	const bool cleared = b && words.name.empty() && words.data.empty() && words.data.data() == nullptr &&
	                     words.mac == std::array<std::uint8_t, 6>{};

	outer nested{};
	const auto c = decode( bytes_of( vectors::outer_set ), nested );
	const bool set = c && nested.child.x == 1 && nested.maybe && nested.maybe_child && nested.gap == 3;
	const auto d = decode( std::span<const std::uint8_t>(), nested );
	return filled && cleared && set && d && nested.child.x == 0 && !nested.maybe && !nested.maybe_child &&
	       nested.gap == 0;
}

/// On an error, the message holds the fields that were read before the error, and nothing of the field
/// that failed.
bool test_partial_result()
{
	constexpr std::array<std::uint8_t, 4> scalar{ 0x08, 0x05, 0x08, 0x80 };
	inner one{};
	const auto a = decode( bytes_of( scalar ), one );
	const bool kept = !a && a.error() == error::TRUNCATED && one.x == 5;

	constexpr std::array<std::uint8_t, 4> packed{ 0x0a, 0x02, 0x01, 0x80 };
	ints_only numbers{};
	const auto b = decode( bytes_of( packed ), numbers );
	const bool one_item = !b && b.error() == error::TRUNCATED && numbers.ints.size() == 1 && numbers.ints[0] == 1;

	constexpr std::array<std::uint8_t, 3> element{ 0x22, 0x01, 0x08 };
	lists messages{};
	const auto c = decode( bytes_of( element ), messages );
	return kept && one_item && !c && messages.items.empty();
}

/// A second occurrence of a fixed-length bytes field replaces the first. A shorter one leaves no old bytes.
bool test_fixed_bytes_last_wins()
{
	constexpr std::array<std::uint8_t, 12> in{ 0x0a, 0x06, 1, 2, 3, 4, 5, 6, 0x0a, 0x02, 9, 9 };
	constexpr std::array<std::uint8_t, 6> expected{ 9, 9, 0, 0, 0, 0 };
	mac_only message{};
	return decode( bytes_of( in ), message ) && message.mac == expected;
}

////////////////////////////////////////////////////////////////////////////////

/// `ints: [1, 2] names: "a" floats: [1]`
constexpr auto arena_wire =
  std::to_array<std::uint8_t>( { 0x0a, 0x02, 0x01, 0x02, 0x12, 0x01, 'a', 0x1a, 0x04, 0x00, 0x00, 0x80, 0x3f } );

/// An arena of exactly `used()` bytes decodes the message. One byte less is `ARENA_FULL`.
bool test_arena_exact()
{
	guarded measure_storage( 512 );
	pb::arena measure = measure_storage.memory();
	arena_lists a{};
	if ( !decode( bytes_of( arena_wire ), a, measure ) )
		return false;

	const std::size_t used = measure.used();
	guarded exact_storage( used );
	pb::arena exact = exact_storage.memory();
	arena_lists b{};
	const auto fits = decode( bytes_of( arena_wire ), b, exact );

	guarded small_storage( used - 1 );
	pb::arena small = small_storage.memory();
	arena_lists c{};
	const auto full = decode( bytes_of( arena_wire ), c, small );
	return fits && *fits == arena_wire.size() && exact.used() == used &&
	       holds_items( b.ints, std::to_array<std::int32_t>( { 1, 2 } ) ) &&
	       holds_items( b.names, std::to_array<std::string_view>( { "a" } ) ) &&
	       holds_items( b.floats, std::to_array<float>( { 1.0f } ) ) && !full && full.error() == error::ARENA_FULL &&
	       measure_storage.intact() && exact_storage.intact() && small_storage.intact();
}

/// The records of two span fields mixed, and packed and unpacked records of one field mixed. Each field gets
/// one block: the arena use is that of the same message with one record for each field.
bool test_arena_mixed_records()
{
	// ints: [1, 2] names: "a" ints: 3 names: "" ints: [4] names: "bc" ints: 5
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0x02, 0x01, 0x02, 0x12, 0x01, 'a', 0x08, 0x03, 0x12,
	                                                   0x00, 0x0a, 0x01, 0x04, 0x12, 0x02, 'b', 'c',  0x08, 0x05 } );
	// ints: [1, 2, 3, 4, 5] names: "a" names: "" names: "bc"
	constexpr auto sorted = std::to_array<std::uint8_t>(
	  { 0x0a, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x12, 0x01, 'a', 0x12, 0x00, 0x12, 0x02, 'b', 'c' } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_lists message{};
	const auto result = decode( bytes_of( in ), message, memory );

	guarded sorted_storage( 512 );
	pb::arena sorted_memory = sorted_storage.memory();
	arena_lists one_record{};
	const auto again = decode( bytes_of( sorted ), one_record, sorted_memory );
	return result && *result == in.size() &&
	       holds_items( message.ints, std::to_array<std::int32_t>( { 1, 2, 3, 4, 5 } ) ) &&
	       holds_items( message.names, std::to_array<std::string_view>( { "a", "", "bc" } ) ) && message.floats.empty() &&
	       again && memory.used() == sorted_memory.used() && storage.intact() && sorted_storage.intact();
}

/// A span of messages whose elements have span fields.
bool test_arena_nested_repeated()
{
	// items { ints: [1, 2] names: "a" } items { ints: [3] } items { }
	constexpr auto in = std::to_array<std::uint8_t>(
	  { 0x0a, 0x07, 0x0a, 0x02, 0x01, 0x02, 0x12, 0x01, 'a', 0x0a, 0x03, 0x0a, 0x01, 0x03, 0x0a, 0x00 } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_items message{};
	const auto result = decode( bytes_of( in ), message, memory );
	if ( !result || message.items.size() != 3 )
		return false;

	std::array<std::uint8_t, in.size()> out{};
	const auto encoded = encode( message, std::span<std::uint8_t>( out ) );
	const arena_lists& first = message.items[0];
	return holds_items( first.ints, std::to_array<std::int32_t>( { 1, 2 } ) ) &&
	       holds_items( first.names, std::to_array<std::string_view>( { "a" } ) ) &&
	       holds_items( message.items[1].ints, std::to_array<std::int32_t>( { 3 } ) ) && message.items[1].names.empty() &&
	       message.items[2].ints.empty() && storage.holds( first.ints.data() ) && encoded && *encoded == in.size() &&
	       out == in && storage.intact();
}

/// A nested message that occurs two times: its span field has the items of both, in order.
bool test_arena_merge()
{
	// child { ints: [1, 2] } child { ints: [3] names: "x" }
	constexpr auto in = std::to_array<std::uint8_t>(
	  { 0x0a, 0x04, 0x0a, 0x02, 0x01, 0x02, 0x0a, 0x06, 0x0a, 0x01, 0x03, 0x12, 0x01, 'x' } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_holder message{};
	const auto result = decode( bytes_of( in ), message, memory );
	return result && holds_items( message.child.ints, std::to_array<std::int32_t>( { 1, 2, 3 } ) ) &&
	       holds_items( message.child.names, std::to_array<std::string_view>( { "x" } ) ) && storage.intact();
}

/// Span fields in the message of a `std::optional` and of a oneof alternative.
bool test_arena_oneof_and_optional()
{
	// maybe { ints: [7] } choice { ints: [8, 9] names: "c" }
	constexpr auto in = std::to_array<std::uint8_t>(
	  { 0x0a, 0x03, 0x0a, 0x01, 0x07, 0x12, 0x07, 0x0a, 0x02, 0x08, 0x09, 0x12, 0x01, 'c' } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_choices message{};
	const auto result = decode( bytes_of( in ), message, memory );
	const auto* chosen = std::get_if<arena_lists>( &message.choice );
	return result && message.maybe && holds_items( message.maybe->ints, std::to_array<std::int32_t>( { 7 } ) ) &&
	       chosen && holds_items( chosen->ints, std::to_array<std::int32_t>( { 8, 9 } ) ) &&
	       holds_items( chosen->names, std::to_array<std::string_view>( { "c" } ) ) && storage.intact();
}

/// Elements with different alignment in one arena, whose storage is at an odd address.
bool test_arena_alignment()
{
	// small: [1, 2, 3] wide: [1.5, -2]
	constexpr auto in =
	  std::to_array<std::uint8_t>( { 0x0a, 0x03, 0x01, 0x02, 0x03, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
		                               0x00, 0xf8, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0 } );

	guarded storage( 512, 1 );
	pb::arena memory = storage.memory();
	arena_aligned message{};
	const auto result = decode( bytes_of( in ), message, memory );
	const auto wide = reinterpret_cast<std::uintptr_t>( message.wide.data() );
	return result && holds_items( message.small, std::to_array<std::int8_t>( { 1, 2, 3 } ) ) &&
	       holds_items( message.wide, std::to_array<double>( { 1.5, -2.0 } ) ) && wide % alignof( double ) == 0 &&
	       storage.holds( message.small.data() ) && storage.holds( message.wide.data() ) && storage.intact();
}

/// `repeated bytes`: the block holds the views, and each view points into the input.
bool test_arena_byte_lists()
{
	// blobs: "abc" blobs: "" blobs: ""
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0x03, 'a', 'b', 'c', 0x0a, 0x00, 0x0a, 0x02, 0x01, 0x02 } );

	guarded storage( 256 );
	pb::arena memory = storage.memory();
	byte_lists message{};
	const auto result = decode( bytes_of( in ), message, memory );
	if ( !result || message.blobs.size() != 3 )
		return false;

	const auto* first = in.data();
	const auto* last = in.data() + in.size();
	const auto in_input = [&]( std::span<const std::uint8_t> blob ) {
		return blob.data() >= first && blob.data() <= last;
	};
	const auto& blobs = message.blobs;
	return holds_items( blobs[0], std::to_array<std::uint8_t>( { 'a', 'b', 'c' } ) ) && blobs[1].empty() &&
	       holds_items( blobs[2], std::to_array<std::uint8_t>( { 0x01, 0x02 } ) ) && blobs[0].data() == in.data() + 2 &&
	       blobs[2].data() == in.data() + 9 && in_input( blobs[1] ) && storage.holds( blobs.data() ) && storage.intact();
}

/// Packed records that are not valid: the count is too small for the reads. The block stays full, `decode`
/// writes no item after its end.
bool test_arena_bad_packed()
{
	// A block: the capacity, then the items.
	constexpr auto no_end = std::to_array<std::uint8_t>( { 0x0a, 0x03, 0x01, 0x02, 0x80 } );
	guarded varint_storage( sizeof( std::size_t ) + 2 * sizeof( std::int32_t ) );
	pb::arena varint_memory = varint_storage.memory();
	arena_lists varints{};
	const auto a = decode( bytes_of( no_end ), varints, varint_memory );

	constexpr auto six = std::to_array<std::uint8_t>( { 0x1a, 0x06, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00 } );
	guarded fixed_storage( sizeof( std::size_t ) + sizeof( float ) );
	pb::arena fixed_memory = fixed_storage.memory();
	arena_lists fixed{};
	const auto b = decode( bytes_of( six ), fixed, fixed_memory );

	return !a && a.error() == error::TRUNCATED && holds_items( varints.ints, std::to_array<std::int32_t>( { 1, 2 } ) ) &&
	       varint_storage.intact() && !b && b.error() == error::TRUNCATED &&
	       holds_items( fixed.floats, std::to_array<float>( { 1.0f } ) ) && fixed_storage.intact();
}

/// A packed record whose length is larger than the remaining input is `TRUNCATED`. `decode` takes no block for it.
bool test_arena_huge_length()
{
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0xff, 0xff, 0xff, 0x0f, 0x01, 0x02, 0x03 } );
	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_lists message{};
	const auto result = decode( bytes_of( in ), message, memory );
	return !result && result.error() == error::TRUNCATED && memory.used() == 0 && message.ints.empty() &&
	       storage.intact();
}

/// An error in the third element of a span field: the span has the two items before it, in the arena.
bool test_arena_error_mid_field()
{
	constexpr auto in = std::to_array<std::uint8_t>( { 0x08, 0x01, 0x08, 0x02, 0x08, 0x80 } );
	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_lists message{};
	const auto result = decode( bytes_of( in ), message, memory );
	return !result && result.error() == error::TRUNCATED &&
	       holds_items( message.ints, std::to_array<std::int32_t>( { 1, 2 } ) ) && storage.holds( message.ints.data() ) &&
	       storage.intact();
}

/// A second message in the same arena does not change the first.
bool test_arena_two_messages()
{
	// ints: [1, 2]
	constexpr auto first = std::to_array<std::uint8_t>( { 0x0a, 0x02, 0x01, 0x02 } );
	// ints: [3, 4] names: "z"
	constexpr auto second = std::to_array<std::uint8_t>( { 0x0a, 0x02, 0x03, 0x04, 0x12, 0x01, 'z' } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_lists a{};
	arena_lists b{};
	const auto ra = decode( bytes_of( first ), a, memory );
	const std::size_t used = memory.used();
	const auto rb = decode( bytes_of( second ), b, memory );
	return ra && rb && memory.used() > used && holds_items( a.ints, std::to_array<std::int32_t>( { 1, 2 } ) ) &&
	       holds_items( b.ints, std::to_array<std::int32_t>( { 3, 4 } ) ) &&
	       holds_items( b.names, std::to_array<std::string_view>( { "z" } ) ) && storage.intact();
}

/// The order of the fields on the wire changes only the padding between the blocks.
bool test_arena_order()
{
	// ints: [1, 2, 3] names: "a" names: "b" floats: [1]
	constexpr auto in_order = std::to_array<std::uint8_t>(
	  { 0x0a, 0x03, 0x01, 0x02, 0x03, 0x12, 0x01, 'a', 0x12, 0x01, 'b', 0x1a, 0x04, 0x00, 0x00, 0x80, 0x3f } );
	// floats: [1] names: "a" ints: [1, 2, 3] names: "b"
	constexpr auto reversed = std::to_array<std::uint8_t>(
	  { 0x1a, 0x04, 0x00, 0x00, 0x80, 0x3f, 0x12, 0x01, 'a', 0x0a, 0x03, 0x01, 0x02, 0x03, 0x12, 0x01, 'b' } );
	constexpr std::size_t blocks = 3;

	guarded storage_a( 512 );
	pb::arena memory_a = storage_a.memory();
	arena_lists a{};
	guarded storage_b( 512 );
	pb::arena memory_b = storage_b.memory();
	arena_lists b{};
	const auto ra = decode( bytes_of( in_order ), a, memory_a );
	const auto rb = decode( bytes_of( reversed ), b, memory_b );
	const std::size_t difference =
	  memory_a.used() > memory_b.used() ? memory_a.used() - memory_b.used() : memory_b.used() - memory_a.used();
	return ra && rb && difference <= blocks * ( alignof( std::max_align_t ) - 1 ) &&
	       holds_items( b.ints, std::to_array<std::int32_t>( { 1, 2, 3 } ) ) &&
	       holds_items( b.names, std::to_array<std::string_view>( { "a", "b" } ) ) &&
	       holds_items( b.floats, std::to_array<float>( { 1.0f } ) ) && storage_a.intact() && storage_b.intact();
}

/// A record with the wrong wire type for a span field is skipped and not counted.
bool test_arena_wrong_wire_type()
{
	// ints: 5, field 1 as fixed32, ints: 6
	constexpr auto in = std::to_array<std::uint8_t>( { 0x08, 0x05, 0x0d, 0x01, 0x02, 0x03, 0x04, 0x08, 0x06 } );
	// ints: 5 ints: 6
	constexpr auto without = std::to_array<std::uint8_t>( { 0x08, 0x05, 0x08, 0x06 } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_lists message{};
	const auto result = decode( bytes_of( in ), message, memory );

	guarded without_storage( 512 );
	pb::arena without_memory = without_storage.memory();
	arena_lists expected{};
	const auto again = decode( bytes_of( without ), expected, without_memory );
	return result && again && holds_items( message.ints, std::to_array<std::int32_t>( { 5, 6 } ) ) &&
	       memory.used() == without_memory.used() && storage.intact() && without_storage.intact();
}

/// A value that `decode` makes for a `std::optional` or a oneof alternative starts from its reset state: a span
/// with a default member initializer gets only the items of the wire, in the arena, and the caller items do not
/// change. A scalar with a default member initializer is 0 when the field is absent.
bool test_arena_new_value_reset()
{
	// o { v: [1, 2, 3] }, and c { v: [1, 2, 3] }
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0x05, 0x0a, 0x03, 0x01, 0x02, 0x03 } );
	guarded storage( 512 );
	pb::arena memory = storage.memory();
	defaulted_optional a{};
	const auto ra = decode( bytes_of( in ), a, memory );
	defaulted_choice b{};
	const auto rb = decode( bytes_of( in ), b, memory );
	const auto* chosen = std::get_if<defaulted>( &b.c );

	const bool caller_unchanged = caller.fake_capacity == 100 && caller.items == std::array<std::int32_t, 2>{ 10, 11 } &&
	                              caller.after == std::array<std::int32_t, 4>{};
	return ra && a.o && holds_items( a.o->v, std::to_array<std::int32_t>( { 1, 2, 3 } ) ) &&
	       storage.holds( a.o->v.data() ) && a.o->x == 0 && rb && chosen &&
	       holds_items( chosen->v, std::to_array<std::int32_t>( { 1, 2, 3 } ) ) && storage.holds( chosen->v.data() ) &&
	       chosen->x == 0 && caller_unchanged && storage.intact();
}

/// A new item in the arena is a constructed object before `decode` resets it and reads into it.
bool test_arena_new_items()
{
	// items { } items { o: 5 } items { c: 7 flag: true }
	constexpr auto in =
	  std::to_array<std::uint8_t>( { 0x0a, 0x00, 0x0a, 0x02, 0x08, 0x05, 0x0a, 0x04, 0x10, 0x07, 0x20, 0x01 } );
	guarded storage( 512 );
	pb::arena memory = storage.memory();
	fragile_items message{};
	const auto result = decode( bytes_of( in ), message, memory );
	if ( !result || message.items.size() != 3 )
		return false;

	const fragile& empty = message.items[0];
	const fragile& with_o = message.items[1];
	const auto* chosen = std::get_if<std::uint32_t>( &message.items[2].c );
	return !empty.o && empty.c.index() == 0 && !empty.flag && with_o.o == 5 && with_o.c.index() == 0 && !with_o.flag &&
	       !message.items[2].o && chosen && *chosen == 7 && message.items[2].flag && storage.intact();
}

/// A second `decode` into the same message resets each span field, also in a nested message. The arena does
/// not free the first blocks.
bool test_arena_reset()
{
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0x04, 0x0a, 0x02, 0x01, 0x02 } );

	guarded storage( 512 );
	pb::arena memory = storage.memory();
	arena_holder message{};
	const auto first = decode( bytes_of( in ), message, memory );
	const bool filled = first && holds_items( message.child.ints, std::to_array<std::int32_t>( { 1, 2 } ) );
	const std::size_t used = memory.used();

	const auto second = decode( std::span<const std::uint8_t>(), message, memory );
	return filled && second && message.child.ints.empty() && memory.used() == used && storage.intact();
}

/// An arena of capacity 0 gives `ARENA_FULL` for a span field with one item. A message without items needs no block.
bool test_arena_no_storage()
{
	constexpr auto one = std::to_array<std::uint8_t>( { 0x0a, 0x01, 0x05 } );

	guarded storage( 0 );
	pb::arena memory = storage.memory();
	arena_lists message{};
	const auto full = decode( bytes_of( one ), message, memory );
	const auto none = decode( std::span<const std::uint8_t>(), message, memory );
	return !full && full.error() == error::ARENA_FULL && none && message.ints.empty() && memory.used() == 0 &&
	       storage.intact();
}

/// The arena tests, with the arena storage filled with `fill` before each `decode`.
bool test_arena( std::uint8_t fill )
{
	arena_fill = fill;
	return test_arena_exact() && test_arena_mixed_records() && test_arena_nested_repeated() && test_arena_merge() &&
	       test_arena_oneof_and_optional() && test_arena_alignment() && test_arena_bad_packed() &&
	       test_arena_byte_lists() && test_arena_huge_length() && test_arena_error_mid_field() &&
	       test_arena_two_messages() && test_arena_order() && test_arena_wrong_wire_type() &&
	       test_arena_new_value_reset() && test_arena_new_items() && test_arena_reset() && test_arena_no_storage();
}

/// A message with only `pb::repeated` fields decodes without an arena.
bool test_no_arena_needed()
{
	constexpr auto in = std::to_array<std::uint8_t>( { 0x0a, 0x02, 0x01, 0x02 } );
	ints_only message{};
	const auto result = decode( bytes_of( in ), message );
	return result && message.ints.size() == 2 && message.ints[1] == 2;
}

} // namespace

int main()
{
	const bool ok =
	  test_decode_scalars_zero() && test_decode_scalars_max() && test_decode_scalars_negative() &&
	  test_decode_scalars_enum_neg() && test_decode_text_short() && test_decode_text_long() && test_decode_outer_set() &&
	  test_decode_outer_unset() && test_zero_copy() && test_reset() && test_last_wins() && test_merge() &&
	  test_unknown_skip() && test_wrong_wire_type() && test_wrong_wire_type_optional() && test_narrowing() &&
	  test_narrow_enum() && test_fixed_bytes() && test_repeated_bytes() && test_errors() && test_decode_lists_empty() &&
	  test_decode_pose_zero() && test_decode_envelope_none() && test_decode_envelope_pose() &&
	  test_decode_envelope_ping() && test_unpacked_scalars() && test_mixed_packing() && test_capacity() &&
	  test_packed_bad_length() && test_repeated_wrong_wire_type() && test_repeated_message_no_merge() &&
	  test_oneof_last_wins() && test_oneof_merge() && test_oneof_replace() && test_oneof_wrong_wire_type() &&
	  test_decode_lists_full() && test_decode_envelope_long() && test_reuse_repeated_message() && test_reuse_oneof() &&
	  test_decode_empty_message() && test_error_order() && test_reuse_resets_each_kind() && test_partial_result() &&
	  test_fixed_bytes_last_wins() && test_arena( 0x00 ) && test_arena( 0xA5 ) && test_no_arena_needed();
	return ok ? 0 : 1;
}
