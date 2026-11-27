#pragma once

#include <array>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include <micropb/types.hpp>

/// C++ mirrors of `vectors.proto`. The encode, decode and compatibility tests share them.
namespace messages {

enum class color : int32_t
{
	NONE,
	RED,
	BLUE,
	NEG = -1
};

struct scalars
{
	int32_t i32;
	int64_t i64;
	uint32_t u32;
	uint64_t u64;
	int32_t s32;
	int64_t s64;
	bool flag;
	float f;
	double d;
	uint32_t fx32;
	uint64_t fx64;
	int32_t sfx32;
	int64_t sfx64;
	color c;
	int8_t i8;
	uint16_t u16;
};

struct text
{
	std::string_view name;
	std::span<const uint8_t> data;
	std::array<uint8_t, 6> mac;
};

struct inner
{
	int32_t x;
	std::string_view label;
};

struct outer
{
	inner child;
	pb::optional<int32_t> maybe;
	pb::optional<inner> maybe_child;
	int32_t gap;
};

struct lists
{
	pb::repeated<int32_t, 8> ints;
	pb::repeated<float, 8> floats;
	pb::repeated<std::string_view, 8> names;
	pb::repeated<inner, 8> items;
	pb::repeated<int64_t, 8> zz;
};

struct pose
{
	std::array<float, 3> pos;
	std::array<float, 4> rot;
};

struct ping
{
	uint32_t seq;
};

struct envelope
{
	uint32_t id;
	pb::oneof<pose, ping, text> payload;
};

} // namespace messages

template <>
struct pb::fields_of<messages::scalars>
  : pb::field_list<
      messages::scalars,
      pb::field_at<decltype( messages::scalars::i32 ), 1, offsetof( messages::scalars, i32 )>,
      pb::field_at<decltype( messages::scalars::i64 ), 2, offsetof( messages::scalars, i64 )>,
      pb::field_at<decltype( messages::scalars::u32 ), 3, offsetof( messages::scalars, u32 )>,
      pb::field_at<decltype( messages::scalars::u64 ), 4, offsetof( messages::scalars, u64 )>,
      pb::field_at<decltype( messages::scalars::s32 ), 5, offsetof( messages::scalars, s32 ), 0, pb::int_encoding::ZIGZAG>,
      pb::field_at<decltype( messages::scalars::s64 ), 6, offsetof( messages::scalars, s64 ), 0, pb::int_encoding::ZIGZAG>,
      pb::field_at<decltype( messages::scalars::flag ), 7, offsetof( messages::scalars, flag )>,
      pb::field_at<decltype( messages::scalars::f ), 8, offsetof( messages::scalars, f )>,
      pb::field_at<decltype( messages::scalars::d ), 9, offsetof( messages::scalars, d )>,
      pb::field_at<decltype( messages::scalars::fx32 ), 10, offsetof( messages::scalars, fx32 ), 0, pb::int_encoding::FIXED>,
      pb::field_at<decltype( messages::scalars::fx64 ), 11, offsetof( messages::scalars, fx64 ), 0, pb::int_encoding::FIXED>,
      pb::field_at<decltype( messages::scalars::sfx32 ), 12, offsetof( messages::scalars, sfx32 ), 0, pb::int_encoding::FIXED>,
      pb::field_at<decltype( messages::scalars::sfx64 ), 13, offsetof( messages::scalars, sfx64 ), 0, pb::int_encoding::FIXED>,
      pb::field_at<decltype( messages::scalars::c ), 14, offsetof( messages::scalars, c )>,
      pb::field_at<decltype( messages::scalars::i8 ), 15, offsetof( messages::scalars, i8 )>,
      pb::field_at<decltype( messages::scalars::u16 ), 16, offsetof( messages::scalars, u16 )>>
{};

template <>
struct pb::fields_of<messages::text>
  : pb::field_list<messages::text,
                   pb::field_at<decltype( messages::text::name ), 1, offsetof( messages::text, name )>,
                   pb::field_at<decltype( messages::text::data ), 2, offsetof( messages::text, data )>,
                   pb::field_at<decltype( messages::text::mac ), 3, offsetof( messages::text, mac )>>
{};

template <>
struct pb::fields_of<messages::inner>
  : pb::field_list<messages::inner,
                   pb::field_at<decltype( messages::inner::x ), 1, offsetof( messages::inner, x )>,
                   pb::field_at<decltype( messages::inner::label ), 2, offsetof( messages::inner, label )>>
{};

template <>
struct pb::fields_of<messages::outer>
  : pb::field_list<messages::outer,
                   pb::field_at<decltype( messages::outer::child ), 1, offsetof( messages::outer, child )>,
                   pb::field_at<decltype( messages::outer::maybe ), 2, offsetof( messages::outer, maybe )>,
                   pb::field_at<decltype( messages::outer::maybe_child ), 3, offsetof( messages::outer, maybe_child )>,
                   pb::field_at<decltype( messages::outer::gap ), 10, offsetof( messages::outer, gap )>>
{};

template <>
struct pb::fields_of<messages::lists>
  : pb::field_list<messages::lists,
                   pb::field_at<decltype( messages::lists::ints ), 1, offsetof( messages::lists, ints )>,
                   pb::field_at<decltype( messages::lists::floats ), 2, offsetof( messages::lists, floats )>,
                   pb::field_at<decltype( messages::lists::names ), 3, offsetof( messages::lists, names )>,
                   pb::field_at<decltype( messages::lists::items ), 4, offsetof( messages::lists, items )>,
                   pb::field_at<decltype( messages::lists::zz ), 5, offsetof( messages::lists, zz ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<messages::pose>
  : pb::field_list<messages::pose,
                   pb::field_at<decltype( messages::pose::pos ), 1, offsetof( messages::pose, pos )>,
                   pb::field_at<decltype( messages::pose::rot ), 2, offsetof( messages::pose, rot )>>
{};

template <>
struct pb::fields_of<messages::ping>
  : pb::field_list<messages::ping, pb::field_at<decltype( messages::ping::seq ), 1, offsetof( messages::ping, seq )>>
{};

template <>
struct pb::fields_of<messages::envelope>
  : pb::field_list<messages::envelope,
                   pb::field_at<decltype( messages::envelope::id ), 1, offsetof( messages::envelope, id )>,
                   pb::field_at<decltype( messages::envelope::payload ), 2, offsetof( messages::envelope, payload ), 1>,
                   pb::field_at<decltype( messages::envelope::payload ), 3, offsetof( messages::envelope, payload ), 2>,
                   pb::field_at<decltype( messages::envelope::payload ), 4, offsetof( messages::envelope, payload ), 3>>
{};
