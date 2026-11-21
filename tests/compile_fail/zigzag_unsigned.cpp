#include <stddef.h>
#include <stdint.h>

#include <micropb/micropb.hpp>

// ZigZag is for signed values: an unsigned integer field must not compile.
struct msg
{
	uint16_t a;
};

template <>
struct pb::fields_of<msg>
  : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a ), 0, pb::int_encoding::ZIGZAG>>
{};

size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
