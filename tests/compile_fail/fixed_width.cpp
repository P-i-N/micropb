#include <cstddef>
#include <cstdint>

#include <micropb/micropb.hpp>

// Fixed-width wire encoding (`fixed32`, `fixed64`, `sfixed32`, `sfixed64`) requires a 4- or 8-byte integer.
struct msg
{
	std::uint16_t a;
};

template <>
struct pb::fields_of<msg>
  : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a ), 0, pb::int_encoding::FIXED>>
{};

std::size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
