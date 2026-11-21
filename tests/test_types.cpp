#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include <micropb/types.hpp>

using namespace pb;

namespace {

constexpr bool test_repeated()
{
	repeated<int, 2> r;
	if ( !r.push_back( 1 ) || !r.push_back( 2 ) || r.push_back( 3 ) )
		return false;
	if ( r.size() != 2 || r[1] != 2 || r.data()[0] != 1 || r.span().size() != 2 )
		return false;

	const repeated<int, 4> same{ { 1, 2 } };
	if ( !( r == same ) )
		return false;

	r.clear();
	return r.empty() && !( r == same );
}

constexpr bool test_bytes()
{
	bytes<4> b;
	if ( !b.empty() || b.capacity() != 4 || !b.push_back( 0x10 ) || !b.push_back( 0x20 ) )
		return false;

	const bytes<8> from_c{ { 0x10, 0x20 } };
	const bytes<4> from_array( std::array<std::uint8_t, 2>{ 0x10, 0x20 } );
	if ( b != from_c || b != from_array || b != std::span<const std::uint8_t>( from_array ) )
		return false;

	const std::array<std::uint8_t, 5> too_long{ 1, 2, 3, 4, 5 };
	if ( b.assign( too_long ) || b.size() != 2 )
		return false;

	const std::array<std::uint8_t, 3> three{ 7, 8, 9 };
	if ( !b.assign( three ) || b.size() != 3 || b[2] != 9 )
		return false;

	b.clear();
	return b.empty();
}

constexpr bool test_string()
{
	string<8> s;
	if ( !s.empty() || s.capacity() != 8 || s.c_str()[0] != '\0' || s != "" )
		return false;

	const string<8> lit = "ambient";
	const string<16> wider = "ambient";
	if ( lit.size() != 7 || lit.length() != 7 || lit != wider || lit != "ambient" || "ambient" != lit ||
	     lit.c_str()[7] != '\0' )
		return false;

	char padded[8] = "hi";
	const string<8> from_buf( padded );
	if ( from_buf.size() != 2 || from_buf != "hi" || from_buf.c_str()[2] != '\0' )
		return false;

	if ( s.assign( "012345678" ) || !s.empty() )
		return false;
	if ( !s.assign( "sensor" ) || !s.push_back( '1' ) || !s.push_back( '2' ) || s.push_back( '3' ) )
		return false;
	if ( s != "sensor12" || s.c_str()[8] != '\0' )
		return false;

	const std::optional<string<8>> opt = "ok";
	const std::variant<std::monostate, bool, string<8>> var = "ok";
	if ( !opt || *opt != "ok" || !std::holds_alternative<string<8>>( var ) || std::get<string<8>>( var ) != "ok" )
		return false;

	const string<0> zero = "";
	s.clear();
	return s.empty() && s.c_str()[0] == '\0' && zero.empty() && zero.c_str()[0] == '\0';
}

constexpr bool test_arena_empty()
{
	std::array<std::uint8_t, 16> storage{};
	arena a{ std::span<std::uint8_t>( storage ) };
	if ( a.used() != 0 || a.capacity() != 16 )
		return false;
	a.clear();
	return a.used() == 0;
}

bool test_arena_allocate()
{
	alignas( 8 ) std::array<std::uint8_t, 64> storage{};
	arena a{ std::span<std::uint8_t>( storage ) };
	if ( a.allocate( 3, 1 ) == nullptr || a.used() != 3 )
		return false;
	void* p = a.allocate( 8, 8 );
	if ( p == nullptr || reinterpret_cast<std::uintptr_t>( p ) % 8 != 0 )
		return false;
	return a.used() >= 3 + 8 && a.used() <= 3 + 7 + 8;
}

bool test_arena_odd_start()
{
	alignas( 8 ) std::array<std::uint8_t, 65> storage{};
	arena a{ std::span<std::uint8_t>( storage ).subspan( 1 ) };
	void* p = a.allocate( 8, 8 );
	if ( p == nullptr || reinterpret_cast<std::uintptr_t>( p ) % 8 != 0 )
		return false;
	const auto address = reinterpret_cast<std::uintptr_t>( p );
	const auto first = reinterpret_cast<std::uintptr_t>( storage.data() );
	return address >= first + 1 && address + 8 <= first + storage.size();
}

bool test_arena_full()
{
	std::array<std::uint8_t, 16> storage{};
	arena a{ std::span<std::uint8_t>( storage ) };
	if ( a.allocate( 16, 1 ) == nullptr )
		return false;
	if ( a.allocate( 1, 1 ) != nullptr || a.used() != 16 )
		return false;
	a.clear();
	return a.allocate( 16, 1 ) != nullptr;
}

bool test_arena_overflow()
{
	alignas( 8 ) std::array<std::uint8_t, 64> storage{};
	arena a{ std::span<std::uint8_t>( storage ) };
	if ( a.allocate( SIZE_MAX, 1 ) != nullptr || a.used() != 0 )
		return false;
	if ( a.allocate( 1, 1 ) == nullptr )
		return false;
	return a.allocate( SIZE_MAX - 2, 8 ) == nullptr && a.used() == 1;
}

////////////////////////////////////////////////////////////////////////////////

static_assert( test_repeated() );
static_assert( test_bytes() );
static_assert( test_string() );
static_assert( test_arena_empty() );
static_assert( std::is_trivially_copyable_v<repeated<int, 4>> && std::is_trivially_destructible_v<repeated<int, 4>> );
static_assert( std::is_trivially_copyable_v<bytes<8>> && std::is_trivially_destructible_v<bytes<8>> );
static_assert( std::is_trivially_copyable_v<string<8>> && std::is_trivially_destructible_v<string<8>> );
static_assert( !std::is_constructible_v<string<4>, const char ( & )[6]> );
static_assert( !std::is_constructible_v<bytes<4>, const std::uint8_t ( & )[5]> );

} // namespace

int main()
{
	pb::string<8> runtime_empty;
	const bool ok = runtime_empty.c_str()[0] == '\0' && std::strlen( runtime_empty.c_str() ) == 0 && test_repeated() &&
	                test_bytes() && test_string() && test_arena_empty() && test_arena_allocate() &&
	                test_arena_odd_start() && test_arena_full() && test_arena_overflow();
	return ok ? 0 : 1;
}
