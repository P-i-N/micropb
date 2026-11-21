#include <stddef.h>

#include <micropb/micropb.hpp>

// This target is built with GNU extensions (CXX_EXTENSIONS ON): only then is `__int128` an integer type
// for `std::is_integral`. It is 16 bytes wide and has no proto3 mapping, so it must not compile.
struct msg
{
	__int128 a;
};

template <>
struct pb::fields_of<msg> : pb::field_list<msg, pb::field_at<decltype( msg::a ), 1, offsetof( msg, a )>>
{};

size_t size_of_message( const msg& m )
{
	return pb::encoded_size( m );
}
