// Size report: 32 message types of 8 mixed fields each, in one oneof envelope.
// Do not change the message types: the size measurements in README.md and
// DETAILS.md refer to them. MICROPB_SIZE_TYPES (16 or 32) sets how many of
// them the envelope uses.

#include <expected>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include <micropb/micropb.hpp>

struct m0
{
	bool f0;
	uint32_t f1;
	std::optional<float> f2;
	double f3;
	int32_t f4;
	uint8_t f5;
	pb::repeated<uint8_t, 16> f6;
	std::optional<uint32_t> f7;
};

struct m1
{
	pb::repeated<int32_t, 8> f0;
	std::optional<uint32_t> f1;
	int16_t f2;
	pb::repeated<float, 4> f3;
	float f4;
	int32_t f5;
	uint8_t f6;
	uint32_t f7;
};

struct m2
{
	double f0;
	pb::repeated<float, 4> f1;
	uint32_t f2;
	uint8_t f3;
	uint32_t f4;
	std::optional<uint32_t> f5;
	bool f6;
	float f7;
};

struct m3
{
	float f0;
	pb::repeated<int32_t, 8> f1;
	std::string_view f2;
	uint32_t f3;
	double f4;
	pb::repeated<uint8_t, 16> f5;
	uint64_t f6;
	int16_t f7;
};

struct m4
{
	double f0;
	int16_t f1;
	float f2;
	pb::repeated<float, 4> f3;
	pb::repeated<uint8_t, 16> f4;
	uint32_t f5;
	uint64_t f6;
	std::optional<float> f7;
};

struct m5
{
	pb::repeated<int32_t, 8> f0;
	std::optional<uint32_t> f1;
	uint64_t f2;
	float f3;
	std::string_view f4;
	double f5;
	int16_t f6;
	uint8_t f7;
};

struct m6
{
	uint32_t f0;
	pb::repeated<int32_t, 8> f1;
	uint32_t f2;
	pb::repeated<float, 4> f3;
	uint64_t f4;
	int16_t f5;
	int32_t f6;
	int32_t f7;
};

struct m7
{
	uint32_t f0;
	int32_t f1;
	pb::repeated<uint8_t, 16> f2;
	std::string_view f3;
	std::optional<float> f4;
	uint64_t f5;
	pb::repeated<float, 4> f6;
	int16_t f7;
};

struct m8
{
	pb::repeated<float, 4> f0;
	bool f1;
	pb::repeated<int32_t, 8> f2;
	uint32_t f3;
	std::optional<uint32_t> f4;
	uint64_t f5;
	uint8_t f6;
	uint32_t f7;
};

struct m9
{
	uint8_t f0;
	float f1;
	pb::repeated<uint8_t, 16> f2;
	double f3;
	pb::repeated<float, 4> f4;
	std::optional<float> f5;
	int32_t f6;
	std::string_view f7;
};

struct m10
{
	pb::repeated<uint8_t, 16> f0;
	int32_t f1;
	std::optional<uint32_t> f2;
	int16_t f3;
	uint64_t f4;
	pb::repeated<int32_t, 8> f5;
	int32_t f6;
	uint8_t f7;
};

struct m11
{
	double f0;
	std::string_view f1;
	std::optional<uint32_t> f2;
	pb::repeated<uint8_t, 16> f3;
	uint32_t f4;
	std::optional<float> f5;
	uint8_t f6;
	bool f7;
};

struct m12
{
	double f0;
	int16_t f1;
	int32_t f2;
	std::optional<float> f3;
	uint64_t f4;
	float f5;
	uint32_t f6;
	pb::repeated<int32_t, 8> f7;
};

struct m13
{
	std::optional<uint32_t> f0;
	pb::repeated<float, 4> f1;
	uint64_t f2;
	std::string_view f3;
	uint32_t f4;
	double f5;
	uint8_t f6;
	bool f7;
};

struct m14
{
	uint32_t f0;
	pb::repeated<float, 4> f1;
	double f2;
	pb::repeated<uint8_t, 16> f3;
	uint64_t f4;
	int32_t f5;
	std::optional<float> f6;
	uint32_t f7;
};

struct m15
{
	pb::repeated<float, 4> f0;
	uint8_t f1;
	uint32_t f2;
	std::optional<uint32_t> f3;
	std::string_view f4;
	uint32_t f5;
	uint64_t f6;
	float f7;
};

struct m16
{
	std::optional<float> f0;
	std::optional<uint32_t> f1;
	uint8_t f2;
	std::string_view f3;
	pb::repeated<float, 4> f4;
	double f5;
	uint32_t f6;
	uint64_t f7;
};

struct m17
{
	int16_t f0;
	std::optional<uint32_t> f1;
	uint32_t f2;
	uint32_t f3;
	float f4;
	uint8_t f5;
	int32_t f6;
	uint64_t f7;
};

struct m18
{
	std::string_view f0;
	std::optional<float> f1;
	int32_t f2;
	double f3;
	uint64_t f4;
	bool f5;
	uint32_t f6;
	pb::repeated<int32_t, 8> f7;
};

struct m19
{
	int32_t f0;
	std::optional<float> f1;
	uint32_t f2;
	std::optional<uint32_t> f3;
	pb::repeated<int32_t, 8> f4;
	bool f5;
	float f6;
	int16_t f7;
};

struct m20
{
	float f0;
	double f1;
	uint32_t f2;
	int32_t f3;
	std::string_view f4;
	bool f5;
	int32_t f6;
	pb::repeated<int32_t, 8> f7;
};

struct m21
{
	std::string_view f0;
	bool f1;
	uint64_t f2;
	int32_t f3;
	int16_t f4;
	pb::repeated<int32_t, 8> f5;
	pb::repeated<uint8_t, 16> f6;
	std::optional<uint32_t> f7;
};

struct m22
{
	int16_t f0;
	std::optional<uint32_t> f1;
	pb::repeated<int32_t, 8> f2;
	int32_t f3;
	uint32_t f4;
	bool f5;
	pb::repeated<float, 4> f6;
	std::string_view f7;
};

struct m23
{
	std::optional<float> f0;
	double f1;
	float f2;
	bool f3;
	int32_t f4;
	pb::repeated<int32_t, 8> f5;
	uint64_t f6;
	uint8_t f7;
};

struct m24
{
	std::optional<float> f0;
	uint8_t f1;
	uint32_t f2;
	float f3;
	pb::repeated<int32_t, 8> f4;
	pb::repeated<float, 4> f5;
	int32_t f6;
	pb::repeated<uint8_t, 16> f7;
};

struct m25
{
	std::string_view f0;
	std::optional<uint32_t> f1;
	pb::repeated<uint8_t, 16> f2;
	uint64_t f3;
	int16_t f4;
	pb::repeated<float, 4> f5;
	double f6;
	float f7;
};

struct m26
{
	pb::repeated<int32_t, 8> f0;
	float f1;
	uint64_t f2;
	int16_t f3;
	uint32_t f4;
	pb::repeated<float, 4> f5;
	uint32_t f6;
	std::string_view f7;
};

struct m27
{
	std::optional<float> f0;
	uint32_t f1;
	pb::repeated<uint8_t, 16> f2;
	bool f3;
	int32_t f4;
	float f5;
	pb::repeated<int32_t, 8> f6;
	double f7;
};

struct m28
{
	int32_t f0;
	std::optional<float> f1;
	int32_t f2;
	bool f3;
	double f4;
	uint8_t f5;
	pb::repeated<float, 4> f6;
	pb::repeated<uint8_t, 16> f7;
};

struct m29
{
	bool f0;
	uint32_t f1;
	uint64_t f2;
	pb::repeated<int32_t, 8> f3;
	uint32_t f4;
	float f5;
	pb::repeated<uint8_t, 16> f6;
	std::optional<uint32_t> f7;
};

struct m30
{
	std::string_view f0;
	std::optional<float> f1;
	pb::repeated<int32_t, 8> f2;
	double f3;
	pb::repeated<uint8_t, 16> f4;
	uint32_t f5;
	uint64_t f6;
	uint32_t f7;
};

struct m31
{
	double f0;
	float f1;
	std::string_view f2;
	int32_t f3;
	pb::repeated<int32_t, 8> f4;
	uint32_t f5;
	pb::repeated<float, 4> f6;
	pb::repeated<uint8_t, 16> f7;
};

#if MICROPB_SIZE_TYPES == 16
using payload_type = pb::oneof<m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15>;
#else
using payload_type = pb::oneof<
                                  m0,
                                  m1,
                                  m2,
                                  m3,
                                  m4,
                                  m5,
                                  m6,
                                  m7,
                                  m8,
                                  m9,
                                  m10,
                                  m11,
                                  m12,
                                  m13,
                                  m14,
                                  m15,
                                  m16,
                                  m17,
                                  m18,
                                  m19,
                                  m20,
                                  m21,
                                  m22,
                                  m23,
                                  m24,
                                  m25,
                                  m26,
                                  m27,
                                  m28,
                                  m29,
                                  m30,
                                  m31>;
#endif

struct top
{
	uint32_t id;
	payload_type payload;
};

template <>
struct pb::fields_of<m0>
  : pb::field_list<m0,
	               pb::field_at<decltype( m0::f0 ), 1, offsetof( m0, f0 )>,
	               pb::field_at<decltype( m0::f1 ), 2, offsetof( m0, f1 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m0::f2 ), 3, offsetof( m0, f2 )>,
	               pb::field_at<decltype( m0::f3 ), 4, offsetof( m0, f3 )>,
	               pb::field_at<decltype( m0::f4 ), 5, offsetof( m0, f4 )>,
	               pb::field_at<decltype( m0::f5 ), 6, offsetof( m0, f5 )>,
	               pb::field_at<decltype( m0::f6 ), 7, offsetof( m0, f6 )>,
	               pb::field_at<decltype( m0::f7 ), 8, offsetof( m0, f7 )>>
{};

template <>
struct pb::fields_of<m1>
  : pb::field_list<m1,
	               pb::field_at<decltype( m1::f0 ), 1, offsetof( m1, f0 )>,
	               pb::field_at<decltype( m1::f1 ), 2, offsetof( m1, f1 )>,
	               pb::field_at<decltype( m1::f2 ), 3, offsetof( m1, f2 )>,
	               pb::field_at<decltype( m1::f3 ), 4, offsetof( m1, f3 )>,
	               pb::field_at<decltype( m1::f4 ), 5, offsetof( m1, f4 )>,
	               pb::field_at<decltype( m1::f5 ), 6, offsetof( m1, f5 )>,
	               pb::field_at<decltype( m1::f6 ), 7, offsetof( m1, f6 )>,
	               pb::field_at<decltype( m1::f7 ), 8, offsetof( m1, f7 )>>
{};

template <>
struct pb::fields_of<m2>
  : pb::field_list<m2,
	               pb::field_at<decltype( m2::f0 ), 1, offsetof( m2, f0 )>,
	               pb::field_at<decltype( m2::f1 ), 2, offsetof( m2, f1 )>,
	               pb::field_at<decltype( m2::f2 ), 3, offsetof( m2, f2 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m2::f3 ), 4, offsetof( m2, f3 )>,
	               pb::field_at<decltype( m2::f4 ), 5, offsetof( m2, f4 )>,
	               pb::field_at<decltype( m2::f5 ), 6, offsetof( m2, f5 )>,
	               pb::field_at<decltype( m2::f6 ), 7, offsetof( m2, f6 )>,
	               pb::field_at<decltype( m2::f7 ), 8, offsetof( m2, f7 )>>
{};

template <>
struct pb::fields_of<m3>
  : pb::field_list<m3,
	               pb::field_at<decltype( m3::f0 ), 1, offsetof( m3, f0 )>,
	               pb::field_at<decltype( m3::f1 ), 2, offsetof( m3, f1 )>,
	               pb::field_at<decltype( m3::f2 ), 3, offsetof( m3, f2 )>,
	               pb::field_at<decltype( m3::f3 ), 4, offsetof( m3, f3 )>,
	               pb::field_at<decltype( m3::f4 ), 5, offsetof( m3, f4 )>,
	               pb::field_at<decltype( m3::f5 ), 6, offsetof( m3, f5 )>,
	               pb::field_at<decltype( m3::f6 ), 7, offsetof( m3, f6 )>,
	               pb::field_at<decltype( m3::f7 ), 8, offsetof( m3, f7 )>>
{};

template <>
struct pb::fields_of<m4>
  : pb::field_list<m4,
	               pb::field_at<decltype( m4::f0 ), 1, offsetof( m4, f0 )>,
	               pb::field_at<decltype( m4::f1 ), 2, offsetof( m4, f1 )>,
	               pb::field_at<decltype( m4::f2 ), 3, offsetof( m4, f2 )>,
	               pb::field_at<decltype( m4::f3 ), 4, offsetof( m4, f3 )>,
	               pb::field_at<decltype( m4::f4 ), 5, offsetof( m4, f4 )>,
	               pb::field_at<decltype( m4::f5 ), 6, offsetof( m4, f5 )>,
	               pb::field_at<decltype( m4::f6 ), 7, offsetof( m4, f6 )>,
	               pb::field_at<decltype( m4::f7 ), 8, offsetof( m4, f7 )>>
{};

template <>
struct pb::fields_of<m5>
  : pb::field_list<m5,
	               pb::field_at<decltype( m5::f0 ), 1, offsetof( m5, f0 )>,
	               pb::field_at<decltype( m5::f1 ), 2, offsetof( m5, f1 )>,
	               pb::field_at<decltype( m5::f2 ), 3, offsetof( m5, f2 )>,
	               pb::field_at<decltype( m5::f3 ), 4, offsetof( m5, f3 )>,
	               pb::field_at<decltype( m5::f4 ), 5, offsetof( m5, f4 )>,
	               pb::field_at<decltype( m5::f5 ), 6, offsetof( m5, f5 )>,
	               pb::field_at<decltype( m5::f6 ), 7, offsetof( m5, f6 )>,
	               pb::field_at<decltype( m5::f7 ), 8, offsetof( m5, f7 )>>
{};

template <>
struct pb::fields_of<m6>
  : pb::field_list<m6,
	               pb::field_at<decltype( m6::f0 ), 1, offsetof( m6, f0 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m6::f1 ), 2, offsetof( m6, f1 )>,
	               pb::field_at<decltype( m6::f2 ), 3, offsetof( m6, f2 )>,
	               pb::field_at<decltype( m6::f3 ), 4, offsetof( m6, f3 )>,
	               pb::field_at<decltype( m6::f4 ), 5, offsetof( m6, f4 )>,
	               pb::field_at<decltype( m6::f5 ), 6, offsetof( m6, f5 )>,
	               pb::field_at<decltype( m6::f6 ), 7, offsetof( m6, f6 )>,
	               pb::field_at<decltype( m6::f7 ), 8, offsetof( m6, f7 ), 0, pb::int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<m7>
  : pb::field_list<m7,
	               pb::field_at<decltype( m7::f0 ), 1, offsetof( m7, f0 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m7::f1 ), 2, offsetof( m7, f1 )>,
	               pb::field_at<decltype( m7::f2 ), 3, offsetof( m7, f2 )>,
	               pb::field_at<decltype( m7::f3 ), 4, offsetof( m7, f3 )>,
	               pb::field_at<decltype( m7::f4 ), 5, offsetof( m7, f4 )>,
	               pb::field_at<decltype( m7::f5 ), 6, offsetof( m7, f5 )>,
	               pb::field_at<decltype( m7::f6 ), 7, offsetof( m7, f6 )>,
	               pb::field_at<decltype( m7::f7 ), 8, offsetof( m7, f7 )>>
{};

template <>
struct pb::fields_of<m8>
  : pb::field_list<m8,
	               pb::field_at<decltype( m8::f0 ), 1, offsetof( m8, f0 )>,
	               pb::field_at<decltype( m8::f1 ), 2, offsetof( m8, f1 )>,
	               pb::field_at<decltype( m8::f2 ), 3, offsetof( m8, f2 )>,
	               pb::field_at<decltype( m8::f3 ), 4, offsetof( m8, f3 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m8::f4 ), 5, offsetof( m8, f4 )>,
	               pb::field_at<decltype( m8::f5 ), 6, offsetof( m8, f5 )>,
	               pb::field_at<decltype( m8::f6 ), 7, offsetof( m8, f6 )>,
	               pb::field_at<decltype( m8::f7 ), 8, offsetof( m8, f7 )>>
{};

template <>
struct pb::fields_of<m9>
  : pb::field_list<m9,
	               pb::field_at<decltype( m9::f0 ), 1, offsetof( m9, f0 )>,
	               pb::field_at<decltype( m9::f1 ), 2, offsetof( m9, f1 )>,
	               pb::field_at<decltype( m9::f2 ), 3, offsetof( m9, f2 )>,
	               pb::field_at<decltype( m9::f3 ), 4, offsetof( m9, f3 )>,
	               pb::field_at<decltype( m9::f4 ), 5, offsetof( m9, f4 )>,
	               pb::field_at<decltype( m9::f5 ), 6, offsetof( m9, f5 )>,
	               pb::field_at<decltype( m9::f6 ), 7, offsetof( m9, f6 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m9::f7 ), 8, offsetof( m9, f7 )>>
{};

template <>
struct pb::fields_of<m10>
  : pb::field_list<m10,
	               pb::field_at<decltype( m10::f0 ), 1, offsetof( m10, f0 )>,
	               pb::field_at<decltype( m10::f1 ), 2, offsetof( m10, f1 )>,
	               pb::field_at<decltype( m10::f2 ), 3, offsetof( m10, f2 )>,
	               pb::field_at<decltype( m10::f3 ), 4, offsetof( m10, f3 )>,
	               pb::field_at<decltype( m10::f4 ), 5, offsetof( m10, f4 )>,
	               pb::field_at<decltype( m10::f5 ), 6, offsetof( m10, f5 )>,
	               pb::field_at<decltype( m10::f6 ), 7, offsetof( m10, f6 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m10::f7 ), 8, offsetof( m10, f7 )>>
{};

template <>
struct pb::fields_of<m11>
  : pb::field_list<m11,
	               pb::field_at<decltype( m11::f0 ), 1, offsetof( m11, f0 )>,
	               pb::field_at<decltype( m11::f1 ), 2, offsetof( m11, f1 )>,
	               pb::field_at<decltype( m11::f2 ), 3, offsetof( m11, f2 )>,
	               pb::field_at<decltype( m11::f3 ), 4, offsetof( m11, f3 )>,
	               pb::field_at<decltype( m11::f4 ), 5, offsetof( m11, f4 )>,
	               pb::field_at<decltype( m11::f5 ), 6, offsetof( m11, f5 )>,
	               pb::field_at<decltype( m11::f6 ), 7, offsetof( m11, f6 )>,
	               pb::field_at<decltype( m11::f7 ), 8, offsetof( m11, f7 )>>
{};

template <>
struct pb::fields_of<m12>
  : pb::field_list<m12,
	               pb::field_at<decltype( m12::f0 ), 1, offsetof( m12, f0 )>,
	               pb::field_at<decltype( m12::f1 ), 2, offsetof( m12, f1 )>,
	               pb::field_at<decltype( m12::f2 ), 3, offsetof( m12, f2 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m12::f3 ), 4, offsetof( m12, f3 )>,
	               pb::field_at<decltype( m12::f4 ), 5, offsetof( m12, f4 )>,
	               pb::field_at<decltype( m12::f5 ), 6, offsetof( m12, f5 )>,
	               pb::field_at<decltype( m12::f6 ), 7, offsetof( m12, f6 )>,
	               pb::field_at<decltype( m12::f7 ), 8, offsetof( m12, f7 )>>
{};

template <>
struct pb::fields_of<m13>
  : pb::field_list<m13,
	               pb::field_at<decltype( m13::f0 ), 1, offsetof( m13, f0 )>,
	               pb::field_at<decltype( m13::f1 ), 2, offsetof( m13, f1 )>,
	               pb::field_at<decltype( m13::f2 ), 3, offsetof( m13, f2 )>,
	               pb::field_at<decltype( m13::f3 ), 4, offsetof( m13, f3 )>,
	               pb::field_at<decltype( m13::f4 ), 5, offsetof( m13, f4 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m13::f5 ), 6, offsetof( m13, f5 )>,
	               pb::field_at<decltype( m13::f6 ), 7, offsetof( m13, f6 )>,
	               pb::field_at<decltype( m13::f7 ), 8, offsetof( m13, f7 )>>
{};

template <>
struct pb::fields_of<m14>
  : pb::field_list<m14,
	               pb::field_at<decltype( m14::f0 ), 1, offsetof( m14, f0 )>,
	               pb::field_at<decltype( m14::f1 ), 2, offsetof( m14, f1 )>,
	               pb::field_at<decltype( m14::f2 ), 3, offsetof( m14, f2 )>,
	               pb::field_at<decltype( m14::f3 ), 4, offsetof( m14, f3 )>,
	               pb::field_at<decltype( m14::f4 ), 5, offsetof( m14, f4 )>,
	               pb::field_at<decltype( m14::f5 ), 6, offsetof( m14, f5 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m14::f6 ), 7, offsetof( m14, f6 )>,
	               pb::field_at<decltype( m14::f7 ), 8, offsetof( m14, f7 ), 0, pb::int_encoding::FIXED>>
{};

template <>
struct pb::fields_of<m15>
  : pb::field_list<m15,
	               pb::field_at<decltype( m15::f0 ), 1, offsetof( m15, f0 )>,
	               pb::field_at<decltype( m15::f1 ), 2, offsetof( m15, f1 )>,
	               pb::field_at<decltype( m15::f2 ), 3, offsetof( m15, f2 )>,
	               pb::field_at<decltype( m15::f3 ), 4, offsetof( m15, f3 )>,
	               pb::field_at<decltype( m15::f4 ), 5, offsetof( m15, f4 )>,
	               pb::field_at<decltype( m15::f5 ), 6, offsetof( m15, f5 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m15::f6 ), 7, offsetof( m15, f6 )>,
	               pb::field_at<decltype( m15::f7 ), 8, offsetof( m15, f7 )>>
{};

template <>
struct pb::fields_of<m16>
  : pb::field_list<m16,
	               pb::field_at<decltype( m16::f0 ), 1, offsetof( m16, f0 )>,
	               pb::field_at<decltype( m16::f1 ), 2, offsetof( m16, f1 )>,
	               pb::field_at<decltype( m16::f2 ), 3, offsetof( m16, f2 )>,
	               pb::field_at<decltype( m16::f3 ), 4, offsetof( m16, f3 )>,
	               pb::field_at<decltype( m16::f4 ), 5, offsetof( m16, f4 )>,
	               pb::field_at<decltype( m16::f5 ), 6, offsetof( m16, f5 )>,
	               pb::field_at<decltype( m16::f6 ), 7, offsetof( m16, f6 )>,
	               pb::field_at<decltype( m16::f7 ), 8, offsetof( m16, f7 )>>
{};

template <>
struct pb::fields_of<m17>
  : pb::field_list<m17,
	               pb::field_at<decltype( m17::f0 ), 1, offsetof( m17, f0 )>,
	               pb::field_at<decltype( m17::f1 ), 2, offsetof( m17, f1 )>,
	               pb::field_at<decltype( m17::f2 ), 3, offsetof( m17, f2 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m17::f3 ), 4, offsetof( m17, f3 )>,
	               pb::field_at<decltype( m17::f4 ), 5, offsetof( m17, f4 )>,
	               pb::field_at<decltype( m17::f5 ), 6, offsetof( m17, f5 )>,
	               pb::field_at<decltype( m17::f6 ), 7, offsetof( m17, f6 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m17::f7 ), 8, offsetof( m17, f7 )>>
{};

template <>
struct pb::fields_of<m18>
  : pb::field_list<m18,
	               pb::field_at<decltype( m18::f0 ), 1, offsetof( m18, f0 )>,
	               pb::field_at<decltype( m18::f1 ), 2, offsetof( m18, f1 )>,
	               pb::field_at<decltype( m18::f2 ), 3, offsetof( m18, f2 )>,
	               pb::field_at<decltype( m18::f3 ), 4, offsetof( m18, f3 )>,
	               pb::field_at<decltype( m18::f4 ), 5, offsetof( m18, f4 )>,
	               pb::field_at<decltype( m18::f5 ), 6, offsetof( m18, f5 )>,
	               pb::field_at<decltype( m18::f6 ), 7, offsetof( m18, f6 )>,
	               pb::field_at<decltype( m18::f7 ), 8, offsetof( m18, f7 )>>
{};

template <>
struct pb::fields_of<m19>
  : pb::field_list<m19,
	               pb::field_at<decltype( m19::f0 ), 1, offsetof( m19, f0 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m19::f1 ), 2, offsetof( m19, f1 )>,
	               pb::field_at<decltype( m19::f2 ), 3, offsetof( m19, f2 )>,
	               pb::field_at<decltype( m19::f3 ), 4, offsetof( m19, f3 )>,
	               pb::field_at<decltype( m19::f4 ), 5, offsetof( m19, f4 )>,
	               pb::field_at<decltype( m19::f5 ), 6, offsetof( m19, f5 )>,
	               pb::field_at<decltype( m19::f6 ), 7, offsetof( m19, f6 )>,
	               pb::field_at<decltype( m19::f7 ), 8, offsetof( m19, f7 )>>
{};

template <>
struct pb::fields_of<m20>
  : pb::field_list<m20,
	               pb::field_at<decltype( m20::f0 ), 1, offsetof( m20, f0 )>,
	               pb::field_at<decltype( m20::f1 ), 2, offsetof( m20, f1 )>,
	               pb::field_at<decltype( m20::f2 ), 3, offsetof( m20, f2 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m20::f3 ), 4, offsetof( m20, f3 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m20::f4 ), 5, offsetof( m20, f4 )>,
	               pb::field_at<decltype( m20::f5 ), 6, offsetof( m20, f5 )>,
	               pb::field_at<decltype( m20::f6 ), 7, offsetof( m20, f6 )>,
	               pb::field_at<decltype( m20::f7 ), 8, offsetof( m20, f7 )>>
{};

template <>
struct pb::fields_of<m21>
  : pb::field_list<m21,
	               pb::field_at<decltype( m21::f0 ), 1, offsetof( m21, f0 )>,
	               pb::field_at<decltype( m21::f1 ), 2, offsetof( m21, f1 )>,
	               pb::field_at<decltype( m21::f2 ), 3, offsetof( m21, f2 )>,
	               pb::field_at<decltype( m21::f3 ), 4, offsetof( m21, f3 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m21::f4 ), 5, offsetof( m21, f4 )>,
	               pb::field_at<decltype( m21::f5 ), 6, offsetof( m21, f5 )>,
	               pb::field_at<decltype( m21::f6 ), 7, offsetof( m21, f6 )>,
	               pb::field_at<decltype( m21::f7 ), 8, offsetof( m21, f7 )>>
{};

template <>
struct pb::fields_of<m22>
  : pb::field_list<m22,
	               pb::field_at<decltype( m22::f0 ), 1, offsetof( m22, f0 )>,
	               pb::field_at<decltype( m22::f1 ), 2, offsetof( m22, f1 )>,
	               pb::field_at<decltype( m22::f2 ), 3, offsetof( m22, f2 )>,
	               pb::field_at<decltype( m22::f3 ), 4, offsetof( m22, f3 )>,
	               pb::field_at<decltype( m22::f4 ), 5, offsetof( m22, f4 )>,
	               pb::field_at<decltype( m22::f5 ), 6, offsetof( m22, f5 )>,
	               pb::field_at<decltype( m22::f6 ), 7, offsetof( m22, f6 )>,
	               pb::field_at<decltype( m22::f7 ), 8, offsetof( m22, f7 )>>
{};

template <>
struct pb::fields_of<m23>
  : pb::field_list<m23,
	               pb::field_at<decltype( m23::f0 ), 1, offsetof( m23, f0 )>,
	               pb::field_at<decltype( m23::f1 ), 2, offsetof( m23, f1 )>,
	               pb::field_at<decltype( m23::f2 ), 3, offsetof( m23, f2 )>,
	               pb::field_at<decltype( m23::f3 ), 4, offsetof( m23, f3 )>,
	               pb::field_at<decltype( m23::f4 ), 5, offsetof( m23, f4 )>,
	               pb::field_at<decltype( m23::f5 ), 6, offsetof( m23, f5 )>,
	               pb::field_at<decltype( m23::f6 ), 7, offsetof( m23, f6 )>,
	               pb::field_at<decltype( m23::f7 ), 8, offsetof( m23, f7 )>>
{};

template <>
struct pb::fields_of<m24>
  : pb::field_list<m24,
	               pb::field_at<decltype( m24::f0 ), 1, offsetof( m24, f0 )>,
	               pb::field_at<decltype( m24::f1 ), 2, offsetof( m24, f1 )>,
	               pb::field_at<decltype( m24::f2 ), 3, offsetof( m24, f2 )>,
	               pb::field_at<decltype( m24::f3 ), 4, offsetof( m24, f3 )>,
	               pb::field_at<decltype( m24::f4 ), 5, offsetof( m24, f4 )>,
	               pb::field_at<decltype( m24::f5 ), 6, offsetof( m24, f5 )>,
	               pb::field_at<decltype( m24::f6 ), 7, offsetof( m24, f6 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m24::f7 ), 8, offsetof( m24, f7 )>>
{};

template <>
struct pb::fields_of<m25>
  : pb::field_list<m25,
	               pb::field_at<decltype( m25::f0 ), 1, offsetof( m25, f0 )>,
	               pb::field_at<decltype( m25::f1 ), 2, offsetof( m25, f1 )>,
	               pb::field_at<decltype( m25::f2 ), 3, offsetof( m25, f2 )>,
	               pb::field_at<decltype( m25::f3 ), 4, offsetof( m25, f3 )>,
	               pb::field_at<decltype( m25::f4 ), 5, offsetof( m25, f4 )>,
	               pb::field_at<decltype( m25::f5 ), 6, offsetof( m25, f5 )>,
	               pb::field_at<decltype( m25::f6 ), 7, offsetof( m25, f6 )>,
	               pb::field_at<decltype( m25::f7 ), 8, offsetof( m25, f7 )>>
{};

template <>
struct pb::fields_of<m26>
  : pb::field_list<m26,
	               pb::field_at<decltype( m26::f0 ), 1, offsetof( m26, f0 )>,
	               pb::field_at<decltype( m26::f1 ), 2, offsetof( m26, f1 )>,
	               pb::field_at<decltype( m26::f2 ), 3, offsetof( m26, f2 )>,
	               pb::field_at<decltype( m26::f3 ), 4, offsetof( m26, f3 )>,
	               pb::field_at<decltype( m26::f4 ), 5, offsetof( m26, f4 )>,
	               pb::field_at<decltype( m26::f5 ), 6, offsetof( m26, f5 )>,
	               pb::field_at<decltype( m26::f6 ), 7, offsetof( m26, f6 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m26::f7 ), 8, offsetof( m26, f7 )>>
{};

template <>
struct pb::fields_of<m27>
  : pb::field_list<m27,
	               pb::field_at<decltype( m27::f0 ), 1, offsetof( m27, f0 )>,
	               pb::field_at<decltype( m27::f1 ), 2, offsetof( m27, f1 )>,
	               pb::field_at<decltype( m27::f2 ), 3, offsetof( m27, f2 )>,
	               pb::field_at<decltype( m27::f3 ), 4, offsetof( m27, f3 )>,
	               pb::field_at<decltype( m27::f4 ), 5, offsetof( m27, f4 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m27::f5 ), 6, offsetof( m27, f5 )>,
	               pb::field_at<decltype( m27::f6 ), 7, offsetof( m27, f6 )>,
	               pb::field_at<decltype( m27::f7 ), 8, offsetof( m27, f7 )>>
{};

template <>
struct pb::fields_of<m28>
  : pb::field_list<m28,
	               pb::field_at<decltype( m28::f0 ), 1, offsetof( m28, f0 ), 0, pb::int_encoding::ZIGZAG>,
	               pb::field_at<decltype( m28::f1 ), 2, offsetof( m28, f1 )>,
	               pb::field_at<decltype( m28::f2 ), 3, offsetof( m28, f2 )>,
	               pb::field_at<decltype( m28::f3 ), 4, offsetof( m28, f3 )>,
	               pb::field_at<decltype( m28::f4 ), 5, offsetof( m28, f4 )>,
	               pb::field_at<decltype( m28::f5 ), 6, offsetof( m28, f5 )>,
	               pb::field_at<decltype( m28::f6 ), 7, offsetof( m28, f6 )>,
	               pb::field_at<decltype( m28::f7 ), 8, offsetof( m28, f7 )>>
{};

template <>
struct pb::fields_of<m29>
  : pb::field_list<m29,
	               pb::field_at<decltype( m29::f0 ), 1, offsetof( m29, f0 )>,
	               pb::field_at<decltype( m29::f1 ), 2, offsetof( m29, f1 )>,
	               pb::field_at<decltype( m29::f2 ), 3, offsetof( m29, f2 )>,
	               pb::field_at<decltype( m29::f3 ), 4, offsetof( m29, f3 )>,
	               pb::field_at<decltype( m29::f4 ), 5, offsetof( m29, f4 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m29::f5 ), 6, offsetof( m29, f5 )>,
	               pb::field_at<decltype( m29::f6 ), 7, offsetof( m29, f6 )>,
	               pb::field_at<decltype( m29::f7 ), 8, offsetof( m29, f7 )>>
{};

template <>
struct pb::fields_of<m30>
  : pb::field_list<m30,
	               pb::field_at<decltype( m30::f0 ), 1, offsetof( m30, f0 )>,
	               pb::field_at<decltype( m30::f1 ), 2, offsetof( m30, f1 )>,
	               pb::field_at<decltype( m30::f2 ), 3, offsetof( m30, f2 )>,
	               pb::field_at<decltype( m30::f3 ), 4, offsetof( m30, f3 )>,
	               pb::field_at<decltype( m30::f4 ), 5, offsetof( m30, f4 )>,
	               pb::field_at<decltype( m30::f5 ), 6, offsetof( m30, f5 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m30::f6 ), 7, offsetof( m30, f6 )>,
	               pb::field_at<decltype( m30::f7 ), 8, offsetof( m30, f7 )>>
{};

template <>
struct pb::fields_of<m31>
  : pb::field_list<m31,
	               pb::field_at<decltype( m31::f0 ), 1, offsetof( m31, f0 )>,
	               pb::field_at<decltype( m31::f1 ), 2, offsetof( m31, f1 )>,
	               pb::field_at<decltype( m31::f2 ), 3, offsetof( m31, f2 )>,
	               pb::field_at<decltype( m31::f3 ), 4, offsetof( m31, f3 )>,
	               pb::field_at<decltype( m31::f4 ), 5, offsetof( m31, f4 )>,
	               pb::field_at<decltype( m31::f5 ), 6, offsetof( m31, f5 ), 0, pb::int_encoding::FIXED>,
	               pb::field_at<decltype( m31::f6 ), 7, offsetof( m31, f6 )>,
	               pb::field_at<decltype( m31::f7 ), 8, offsetof( m31, f7 )>>
{};

#if MICROPB_SIZE_TYPES == 16
template <>
struct pb::fields_of<top>
  : pb::field_list<top,
	               pb::field_at<decltype( top::id ), 1, offsetof( top, id )>,
	               pb::field_at<decltype( top::payload ), 2, offsetof( top, payload ), 1>,
	               pb::field_at<decltype( top::payload ), 3, offsetof( top, payload ), 2>,
	               pb::field_at<decltype( top::payload ), 4, offsetof( top, payload ), 3>,
	               pb::field_at<decltype( top::payload ), 5, offsetof( top, payload ), 4>,
	               pb::field_at<decltype( top::payload ), 6, offsetof( top, payload ), 5>,
	               pb::field_at<decltype( top::payload ), 7, offsetof( top, payload ), 6>,
	               pb::field_at<decltype( top::payload ), 8, offsetof( top, payload ), 7>,
	               pb::field_at<decltype( top::payload ), 9, offsetof( top, payload ), 8>,
	               pb::field_at<decltype( top::payload ), 10, offsetof( top, payload ), 9>,
	               pb::field_at<decltype( top::payload ), 11, offsetof( top, payload ), 10>,
	               pb::field_at<decltype( top::payload ), 12, offsetof( top, payload ), 11>,
	               pb::field_at<decltype( top::payload ), 13, offsetof( top, payload ), 12>,
	               pb::field_at<decltype( top::payload ), 14, offsetof( top, payload ), 13>,
	               pb::field_at<decltype( top::payload ), 15, offsetof( top, payload ), 14>,
	               pb::field_at<decltype( top::payload ), 16, offsetof( top, payload ), 15>,
	               pb::field_at<decltype( top::payload ), 17, offsetof( top, payload ), 16>>
{};
#else
template <>
struct pb::fields_of<top>
  : pb::field_list<top,
	               pb::field_at<decltype( top::id ), 1, offsetof( top, id )>,
	               pb::field_at<decltype( top::payload ), 2, offsetof( top, payload ), 1>,
	               pb::field_at<decltype( top::payload ), 3, offsetof( top, payload ), 2>,
	               pb::field_at<decltype( top::payload ), 4, offsetof( top, payload ), 3>,
	               pb::field_at<decltype( top::payload ), 5, offsetof( top, payload ), 4>,
	               pb::field_at<decltype( top::payload ), 6, offsetof( top, payload ), 5>,
	               pb::field_at<decltype( top::payload ), 7, offsetof( top, payload ), 6>,
	               pb::field_at<decltype( top::payload ), 8, offsetof( top, payload ), 7>,
	               pb::field_at<decltype( top::payload ), 9, offsetof( top, payload ), 8>,
	               pb::field_at<decltype( top::payload ), 10, offsetof( top, payload ), 9>,
	               pb::field_at<decltype( top::payload ), 11, offsetof( top, payload ), 10>,
	               pb::field_at<decltype( top::payload ), 12, offsetof( top, payload ), 11>,
	               pb::field_at<decltype( top::payload ), 13, offsetof( top, payload ), 12>,
	               pb::field_at<decltype( top::payload ), 14, offsetof( top, payload ), 13>,
	               pb::field_at<decltype( top::payload ), 15, offsetof( top, payload ), 14>,
	               pb::field_at<decltype( top::payload ), 16, offsetof( top, payload ), 15>,
	               pb::field_at<decltype( top::payload ), 17, offsetof( top, payload ), 16>,
	               pb::field_at<decltype( top::payload ), 18, offsetof( top, payload ), 17>,
	               pb::field_at<decltype( top::payload ), 19, offsetof( top, payload ), 18>,
	               pb::field_at<decltype( top::payload ), 20, offsetof( top, payload ), 19>,
	               pb::field_at<decltype( top::payload ), 21, offsetof( top, payload ), 20>,
	               pb::field_at<decltype( top::payload ), 22, offsetof( top, payload ), 21>,
	               pb::field_at<decltype( top::payload ), 23, offsetof( top, payload ), 22>,
	               pb::field_at<decltype( top::payload ), 24, offsetof( top, payload ), 23>,
	               pb::field_at<decltype( top::payload ), 25, offsetof( top, payload ), 24>,
	               pb::field_at<decltype( top::payload ), 26, offsetof( top, payload ), 25>,
	               pb::field_at<decltype( top::payload ), 27, offsetof( top, payload ), 26>,
	               pb::field_at<decltype( top::payload ), 28, offsetof( top, payload ), 27>,
	               pb::field_at<decltype( top::payload ), 29, offsetof( top, payload ), 28>,
	               pb::field_at<decltype( top::payload ), 30, offsetof( top, payload ), 29>,
	               pb::field_at<decltype( top::payload ), 31, offsetof( top, payload ), 30>,
	               pb::field_at<decltype( top::payload ), 32, offsetof( top, payload ), 31>,
	               pb::field_at<decltype( top::payload ), 33, offsetof( top, payload ), 32>>
{};
#endif

std::expected<size_t, pb::error> size_report_encode( const top& m, std::span<uint8_t> out )
{
	return pb::encode( m, out );
}

std::expected<size_t, pb::error> size_report_decode( std::span<const uint8_t> in, top& m )
{
	return pb::decode( in, m );
}
