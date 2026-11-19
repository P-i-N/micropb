#include <cstddef>
#include <cstdint>

#include <micropb/micropb.hpp>

// ZigZag is for signed values: an unsigned stored type must not compile.
struct msg
{
	pb::wire_int<std::uint16_t, pb::int_encoding::ZIGZAG> a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>>
{};

std::size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
