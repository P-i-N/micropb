#include "vectors/cases.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/micropb.hpp>

using namespace pb;

namespace {

using namespace cases;
using namespace messages;

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

/// `lists` with spans: `decode` puts the items in a `pb::arena`.
struct span_lists
{
	std::span<const std::int32_t> ints;
	std::span<const float> floats;
	std::span<const std::string_view> names;
	std::span<const inner> items;
	std::span<const std::int64_t> zz;
};

} // namespace

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
struct pb::fields_of<span_lists>
  : pb::field_list<span_lists,
                   pb::field_at<decltype( span_lists::ints ), 1, offsetof( span_lists, ints )>,
                   pb::field_at<decltype( span_lists::floats ), 2, offsetof( span_lists, floats )>,
                   pb::field_at<decltype( span_lists::names ), 3, offsetof( span_lists, names )>,
                   pb::field_at<decltype( span_lists::items ), 4, offsetof( span_lists, items )>,
                   pb::field_at<decltype( span_lists::zz ), 5, offsetof( span_lists, zz ), 0, pb::int_encoding::ZIGZAG>>
{};

namespace {

inline constexpr std::size_t canary_size = 16;
inline constexpr std::uint8_t canary = 0xA5;

/// Storage of a `pb::arena`, filled with 0xA5: `decode` must not read an item that it did not make. The 16
/// canary bytes (0xA5) after it must not change.
struct guarded
{
	guarded() { _bytes.fill( canary ); }

	[[nodiscard]] pb::arena memory() { return pb::arena( std::span( _bytes ).first( arena_size ) ); }

	[[nodiscard]] bool intact() const
	{
		return std::all_of( _bytes.begin() + arena_size, _bytes.end(), []( std::uint8_t b ) { return b == canary; } );
	}

private:
	static constexpr std::size_t arena_size = 1024;

	alignas( std::max_align_t ) std::array<std::uint8_t, arena_size + canary_size> _bytes{};
};

/// Decodes `in` into a new `T`, with a guarded arena when `T` needs one, and gives the result and the message
/// to `check`. `false` when `check` gives `false` or `decode` changed a canary.
template <typename T, typename F>
bool decode_then( std::span<const std::uint8_t> in, F check )
{
	T message{};
	if constexpr ( !detail::needs_arena<T>() )
	{
		return check( decode( in, message ), message );
	}
	else
	{
		guarded storage;
		pb::arena memory = storage.memory();
		const auto result = decode( in, message, memory );
		return check( result, message ) && storage.intact();
	}
}

template <typename T, std::size_t N>
bool round_trips( const std::array<std::uint8_t, N>& wire )
{
	T message{};
	const auto decoded = decode( std::span<const std::uint8_t>( wire ), message );
	if ( !decoded || *decoded != N )
		return false;

	std::array<std::uint8_t, N> out{};
	const auto encoded = encode( message, std::span<std::uint8_t>( out ) );
	return encoded && *encoded == N && out == wire;
}

/// A decoded message encodes into a buffer of exactly its encoded size.
template <typename T>
bool reencodes( const T& message )
{
	std::array<std::uint8_t, 1024> out{};
	const std::size_t size = encoded_size( message );
	if ( size > out.size() )
		return false;

	const auto encoded = encode( message, std::span( out ).first( size ) );
	return encoded && *encoded == size;
}

template <typename T, std::size_t N>
bool prefixes_are_safe( const std::array<std::uint8_t, N>& wire )
{
	for ( std::size_t length = 0; length < N; ++length )
	{
		const auto valid = [&]( const auto& result, const T& message ) {
			return result ? *result == length && reencodes( message ) : result.error() == error::TRUNCATED;
		};
		if ( !decode_then<T>( std::span<const std::uint8_t>( wire ).first( length ), valid ) )
			return false;
	}
	return true;
}

/// `decode` returns for each copy of `wire` with one changed byte. A sanitizer build finds a bad access.
template <typename T, std::size_t N>
bool corruptions_are_safe( const std::array<std::uint8_t, N>& wire )
{
	for ( std::size_t i = 0; i < N; ++i )
	{
		for ( const std::uint8_t mask : { 0x01, 0x80, 0xFF } )
		{
			std::array<std::uint8_t, N> in = wire;
			in[i] ^= mask;
			const auto valid = []( const auto& result, const T& message ) { return !result || reencodes( message ); };
			if ( !decode_then<T>( std::span<const std::uint8_t>( in ), valid ) )
				return false;
		}
	}
	return true;
}

bool test_round_trip_constexpr()
{
	return round_trips<scalars>( vectors::scalars_zero ) && round_trips<scalars>( vectors::scalars_max ) &&
	       round_trips<scalars>( vectors::scalars_negative ) && round_trips<scalars>( vectors::scalars_enum_neg ) &&
	       round_trips<outer>( vectors::outer_unset ) && round_trips<lists>( vectors::lists_empty ) &&
	       round_trips<pose_owned>( vectors::pose_zero ) && round_trips<envelope_owned>( vectors::envelope_none ) &&
	       round_trips<envelope_owned>( vectors::envelope_pose ) && round_trips<envelope_owned>( vectors::envelope_ping );
}

bool test_round_trip_runtime()
{
	return round_trips<text>( vectors::text_short ) && round_trips<text>( vectors::text_long ) &&
	       round_trips<outer>( vectors::outer_set ) && round_trips<lists>( vectors::lists_full ) &&
	       round_trips<envelope_owned>( vectors::envelope_long );
}

bool test_prefix_sweep_constexpr()
{
	return prefixes_are_safe<scalars>( vectors::scalars_zero ) && prefixes_are_safe<scalars>( vectors::scalars_max ) &&
	       prefixes_are_safe<scalars>( vectors::scalars_negative ) &&
	       prefixes_are_safe<scalars>( vectors::scalars_enum_neg ) && prefixes_are_safe<outer>( vectors::outer_unset ) &&
	       prefixes_are_safe<lists>( vectors::lists_empty ) && prefixes_are_safe<pose_owned>( vectors::pose_zero ) &&
	       prefixes_are_safe<envelope_owned>( vectors::envelope_none ) &&
	       prefixes_are_safe<envelope_owned>( vectors::envelope_pose ) &&
	       prefixes_are_safe<envelope_owned>( vectors::envelope_ping );
}

bool test_prefix_sweep_runtime()
{
	return prefixes_are_safe<text>( vectors::text_short ) && prefixes_are_safe<text>( vectors::text_long ) &&
	       prefixes_are_safe<outer>( vectors::outer_set ) && prefixes_are_safe<lists>( vectors::lists_full ) &&
	       prefixes_are_safe<envelope_owned>( vectors::envelope_long );
}

bool test_corruption_sweep()
{
	return corruptions_are_safe<scalars>( vectors::scalars_zero ) &&
	       corruptions_are_safe<scalars>( vectors::scalars_max ) &&
	       corruptions_are_safe<scalars>( vectors::scalars_negative ) &&
	       corruptions_are_safe<scalars>( vectors::scalars_enum_neg ) &&
	       corruptions_are_safe<outer>( vectors::outer_unset ) && corruptions_are_safe<outer>( vectors::outer_set ) &&
	       corruptions_are_safe<lists>( vectors::lists_empty ) && corruptions_are_safe<lists>( vectors::lists_full ) &&
	       corruptions_are_safe<pose_owned>( vectors::pose_zero ) && corruptions_are_safe<text>( vectors::text_short ) &&
	       corruptions_are_safe<text>( vectors::text_long ) &&
	       corruptions_are_safe<envelope_owned>( vectors::envelope_none ) &&
	       corruptions_are_safe<envelope_owned>( vectors::envelope_pose ) &&
	       corruptions_are_safe<envelope_owned>( vectors::envelope_ping ) &&
	       corruptions_are_safe<envelope_owned>( vectors::envelope_long );
}

/// The caller frames messages: `decode` reads exactly one message from the span it gets.
bool test_envelope_stream()
{
	const envelope sent[3]{ { 2, pose{ { 1, 2, 3 }, { 0, 0, 0, 1 } } },
	                        { 3, ping{ 7 } },
	                        { 4, text{ "hi", short_data, mac } } };

	std::array<std::uint8_t, 128> buffer{};
	std::array<std::size_t, 3> sizes{};
	std::size_t used = 0;
	for ( std::size_t i = 0; i < 3; ++i )
	{
		const auto result = encode( sent[i], std::span<std::uint8_t>( buffer ).subspan( used ) );
		if ( !result )
			return false;
		sizes[i] = *result;
		used += *result;
	}

	std::size_t offset = 0;
	envelope_owned received[3]{};
	for ( std::size_t i = 0; i < 3; ++i )
	{
		const auto result = decode( std::span<const std::uint8_t>( buffer ).subspan( offset, sizes[i] ), received[i] );
		if ( !result || *result != sizes[i] )
			return false;
		offset += sizes[i];
	}

	const auto* pos = std::get_if<pose_owned>( &received[0].payload );
	const auto* seq = std::get_if<ping>( &received[1].payload );
	const auto* message = std::get_if<text>( &received[2].payload );
	return received[0].id == 2 && pos && pos->pos.size() == 3 && pos->pos[2] == 3 && pos->rot[3] == 1 &&
	       received[1].id == 3 && seq && seq->seq == 7 && received[2].id == 4 && message && message->name == "hi" &&
	       message->mac == mac &&
	       std::equal( message->data.begin(), message->data.end(), short_data.begin(), short_data.end() );
}

template <typename A, typename B, typename F>
bool same_items( const A& a, const B& b, F equal )
{
	return std::equal( a.begin(), a.end(), b.begin(), b.end(), equal );
}

/// `span_lists` with an arena decodes the items of the `pb::repeated` decode, and encodes to the reference bytes.
template <std::size_t N>
bool spans_match( const std::array<std::uint8_t, N>& wire )
{
	lists owned{};
	const auto a = decode( std::span<const std::uint8_t>( wire ), owned );

	guarded storage;
	pb::arena memory = storage.memory();
	span_lists views{};
	const auto b = decode( std::span<const std::uint8_t>( wire ), views, memory );

	std::array<std::uint8_t, N> out{};
	const auto encoded = encode( views, std::span<std::uint8_t>( out ) );
	const auto same_bits = []( float x, float y ) {
		return std::bit_cast<std::uint32_t>( x ) == std::bit_cast<std::uint32_t>( y );
	};
	return a && b && *b == N && same_items( owned.ints, views.ints, std::equal_to<>{} ) &&
	       same_items( owned.floats, views.floats, same_bits ) &&
	       same_items( owned.names, views.names, std::equal_to<>{} ) &&
	       same_items( owned.items,
	                   views.items,
	                   []( const inner& x, const inner& y ) { return x.x == y.x && x.label == y.label; } ) &&
	       same_items( owned.zz, views.zz, std::equal_to<>{} ) && encoded &&
	       *encoded == N && out == wire && storage.intact();
}

bool test_span_lists()
{
	return spans_match( vectors::lists_full ) && spans_match( vectors::lists_empty ) &&
	       prefixes_are_safe<span_lists>( vectors::lists_full ) &&
	       corruptions_are_safe<span_lists>( vectors::lists_full ) &&
	       corruptions_are_safe<span_lists>( vectors::lists_empty );
}

////////////////////////////////////////////////////////////////////////////////

} // namespace

int main()
{
	const bool ok = test_round_trip_constexpr() && test_round_trip_runtime() && test_prefix_sweep_constexpr() &&
	                test_prefix_sweep_runtime() && test_corruption_sweep() && test_envelope_stream() && test_span_lists();
	return ok ? 0 : 1;
}
