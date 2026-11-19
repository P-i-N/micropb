#include <cstddef>

#include <micropb/micropb.hpp>

// Plain `char` has no proto3 mapping: it is not an integer by size and sign.
struct msg
{
	char a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>>
{};

std::size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
