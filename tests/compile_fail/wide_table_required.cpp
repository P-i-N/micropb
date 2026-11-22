#include <stdint.h>

#include <micropb/micropb.hpp>

// Field number > 4095 requires MICROPB_WIDE_TABLES.
struct wide_tag
{
	uint32_t x;
};

template <>
struct pb::fields_of<wide_tag>
  : pb::field_list<wide_tag, pb::field_at<decltype( wide_tag::x ), 5000, offsetof( wide_tag, x )>>
{};

size_t trigger( const wide_tag& m )
{
	return pb::encoded_size( m );
}
