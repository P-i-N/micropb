#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <micropb/types.hpp>

using namespace pb;

namespace {

constexpr bool test_wire_int()
{
	sint32 s = -5;
	const std::int32_t i = s;
	return i == -5 && sizeof( sint64 ) == 8;
}

constexpr bool test_repeated()
{
	repeated<int, 2> r;
	if ( !r.push_back( 1 ) || !r.push_back( 2 ) || r.push_back( 3 ) )
		return false;
	if ( r.size() != 2 || r[1] != 2 )
		return false;
	r.clear();
	return r.empty();
}

constexpr bool test_span_buffer()
{
	std::array<int, 2> storage{};
	span_buffer<int> b{ storage };
	if ( !b.push_back( 7 ) || storage[0] != 7 || b.capacity() != 2 )
		return false;
	b.clear();
	return b.size() == 0;
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

static_assert( test_wire_int() );
static_assert( test_repeated() );
static_assert( test_span_buffer() );
static_assert( test_arena_empty() );

} // namespace

int main()
{
	const bool ok = test_wire_int() && test_repeated() && test_span_buffer() && test_arena_empty() &&
	                test_arena_allocate() && test_arena_odd_start() && test_arena_full() && test_arena_overflow();
	return ok ? 0 : 1;
}
