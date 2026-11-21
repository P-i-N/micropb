#include <cstddef>
#include <cstdint>

#include <micropb/micropb.hpp>

// ZigZag is for signed values: an unsigned integer field must not compile.
struct msg
{
	std::uint16_t a;
};

template <>
struct pb::fields_of<msg>
  : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a ), 0, pb::int_encoding::ZIGZAG>>
{};

std::size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
