#include <array>
#include <cstdint>

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

////////////////////////////////////////////////////////////////////////////////

static_assert( test_wire_int() );
static_assert( test_repeated() );
static_assert( test_span_buffer() );

} // namespace

int main()
{
	const bool ok = test_wire_int() && test_repeated() && test_span_buffer();
	return ok ? 0 : 1;
}
