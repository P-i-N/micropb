// Size report: 32 message types of 8 mixed fields each, in one oneof envelope.
// The message types are those of "Reference: the benchmark" in
// docs/superpowers/specs/2026-11-18-micropb-table-codec-design.md. Do not
// change them: the size targets refer to exactly these types.
// MICROPB_SIZE_TYPES (16 or 32) sets how many of them the envelope uses.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include <micropb/micropb.hpp>

struct m0
{
	bool f0;
	pb::fixed32 f1;
	std::optional<float> f2;
	double f3;
	std::int32_t f4;
	std::uint8_t f5;
	pb::repeated<std::uint8_t, 16> f6;
	std::optional<std::uint32_t> f7;
};

struct m1
{
	pb::repeated<std::int32_t, 8> f0;
	std::optional<std::uint32_t> f1;
	std::int16_t f2;
	pb::repeated<float, 4> f3;
	float f4;
	std::int32_t f5;
	std::uint8_t f6;
	std::uint32_t f7;
};

struct m2
{
	double f0;
	pb::repeated<float, 4> f1;
	pb::fixed32 f2;
	std::uint8_t f3;
	std::uint32_t f4;
	std::optional<std::uint32_t> f5;
	bool f6;
	float f7;
};

struct m3
{
	float f0;
	pb::repeated<std::int32_t, 8> f1;
	std::string_view f2;
	std::uint32_t f3;
	double f4;
	pb::repeated<std::uint8_t, 16> f5;
	std::uint64_t f6;
	std::int16_t f7;
};

struct m4
{
	double f0;
	std::int16_t f1;
	float f2;
	pb::repeated<float, 4> f3;
	pb::repeated<std::uint8_t, 16> f4;
	std::uint32_t f5;
	std::uint64_t f6;
	std::optional<float> f7;
};

struct m5
{
	pb::repeated<std::int32_t, 8> f0;
	std::optional<std::uint32_t> f1;
	std::uint64_t f2;
	float f3;
	std::string_view f4;
	double f5;
	std::int16_t f6;
	std::uint8_t f7;
};

struct m6
{
	pb::fixed32 f0;
	pb::repeated<std::int32_t, 8> f1;
	std::uint32_t f2;
	pb::repeated<float, 4> f3;
	std::uint64_t f4;
	std::int16_t f5;
	std::int32_t f6;
	pb::sint32 f7;
};

struct m7
{
	pb::fixed32 f0;
	std::int32_t f1;
	pb::repeated<std::uint8_t, 16> f2;
	std::string_view f3;
	std::optional<float> f4;
	std::uint64_t f5;
	pb::repeated<float, 4> f6;
	std::int16_t f7;
};

struct m8
{
	pb::repeated<float, 4> f0;
	bool f1;
	pb::repeated<std::int32_t, 8> f2;
	pb::fixed32 f3;
	std::optional<std::uint32_t> f4;
	std::uint64_t f5;
	std::uint8_t f6;
	std::uint32_t f7;
};

struct m9
{
	std::uint8_t f0;
	float f1;
	pb::repeated<std::uint8_t, 16> f2;
	double f3;
	pb::repeated<float, 4> f4;
	std::optional<float> f5;
	pb::sint32 f6;
	std::string_view f7;
};

struct m10
{
	pb::repeated<std::uint8_t, 16> f0;
	std::int32_t f1;
	std::optional<std::uint32_t> f2;
	std::int16_t f3;
	std::uint64_t f4;
	pb::repeated<std::int32_t, 8> f5;
	pb::sint32 f6;
	std::uint8_t f7;
};

struct m11
{
	double f0;
	std::string_view f1;
	std::optional<std::uint32_t> f2;
	pb::repeated<std::uint8_t, 16> f3;
	std::uint32_t f4;
	std::optional<float> f5;
	std::uint8_t f6;
	bool f7;
};

struct m12
{
	double f0;
	std::int16_t f1;
	pb::sint32 f2;
	std::optional<float> f3;
	std::uint64_t f4;
	float f5;
	std::uint32_t f6;
	pb::repeated<std::int32_t, 8> f7;
};

struct m13
{
	std::optional<std::uint32_t> f0;
	pb::repeated<float, 4> f1;
	std::uint64_t f2;
	std::string_view f3;
	pb::fixed32 f4;
	double f5;
	std::uint8_t f6;
	bool f7;
};

struct m14
{
	std::uint32_t f0;
	pb::repeated<float, 4> f1;
	double f2;
	pb::repeated<std::uint8_t, 16> f3;
	std::uint64_t f4;
	pb::sint32 f5;
	std::optional<float> f6;
	pb::fixed32 f7;
};

struct m15
{
	pb::repeated<float, 4> f0;
	std::uint8_t f1;
	std::uint32_t f2;
	std::optional<std::uint32_t> f3;
	std::string_view f4;
	pb::fixed32 f5;
	std::uint64_t f6;
	float f7;
};

struct m16
{
	std::optional<float> f0;
	std::optional<std::uint32_t> f1;
	std::uint8_t f2;
	std::string_view f3;
	pb::repeated<float, 4> f4;
	double f5;
	std::uint32_t f6;
	std::uint64_t f7;
};

struct m17
{
	std::int16_t f0;
	std::optional<std::uint32_t> f1;
	pb::fixed32 f2;
	std::uint32_t f3;
	float f4;
	std::uint8_t f5;
	pb::sint32 f6;
	std::uint64_t f7;
};

struct m18
{
	std::string_view f0;
	std::optional<float> f1;
	std::int32_t f2;
	double f3;
	std::uint64_t f4;
	bool f5;
	std::uint32_t f6;
	pb::repeated<std::int32_t, 8> f7;
};

struct m19
{
	pb::sint32 f0;
	std::optional<float> f1;
	std::uint32_t f2;
	std::optional<std::uint32_t> f3;
	pb::repeated<std::int32_t, 8> f4;
	bool f5;
	float f6;
	std::int16_t f7;
};

struct m20
{
	float f0;
	double f1;
	pb::fixed32 f2;
	pb::sint32 f3;
	std::string_view f4;
	bool f5;
	std::int32_t f6;
	pb::repeated<std::int32_t, 8> f7;
};

struct m21
{
	std::string_view f0;
	bool f1;
	std::uint64_t f2;
	pb::sint32 f3;
	std::int16_t f4;
	pb::repeated<std::int32_t, 8> f5;
	pb::repeated<std::uint8_t, 16> f6;
	std::optional<std::uint32_t> f7;
};

struct m22
{
	std::int16_t f0;
	std::optional<std::uint32_t> f1;
	pb::repeated<std::int32_t, 8> f2;
	std::int32_t f3;
	std::uint32_t f4;
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
	std::int32_t f4;
	pb::repeated<std::int32_t, 8> f5;
	std::uint64_t f6;
	std::uint8_t f7;
};

struct m24
{
	std::optional<float> f0;
	std::uint8_t f1;
	std::uint32_t f2;
	float f3;
	pb::repeated<std::int32_t, 8> f4;
	pb::repeated<float, 4> f5;
	pb::sint32 f6;
	pb::repeated<std::uint8_t, 16> f7;
};

struct m25
{
	std::string_view f0;
	std::optional<std::uint32_t> f1;
	pb::repeated<std::uint8_t, 16> f2;
	std::uint64_t f3;
	std::int16_t f4;
	pb::repeated<float, 4> f5;
	double f6;
	float f7;
};

struct m26
{
	pb::repeated<std::int32_t, 8> f0;
	float f1;
	std::uint64_t f2;
	std::int16_t f3;
	std::uint32_t f4;
	pb::repeated<float, 4> f5;
	pb::fixed32 f6;
	std::string_view f7;
};

struct m27
{
	std::optional<float> f0;
	std::uint32_t f1;
	pb::repeated<std::uint8_t, 16> f2;
	bool f3;
	pb::sint32 f4;
	float f5;
	pb::repeated<std::int32_t, 8> f6;
	double f7;
};

struct m28
{
	pb::sint32 f0;
	std::optional<float> f1;
	std::int32_t f2;
	bool f3;
	double f4;
	std::uint8_t f5;
	pb::repeated<float, 4> f6;
	pb::repeated<std::uint8_t, 16> f7;
};

struct m29
{
	bool f0;
	std::uint32_t f1;
	std::uint64_t f2;
	pb::repeated<std::int32_t, 8> f3;
	pb::fixed32 f4;
	float f5;
	pb::repeated<std::uint8_t, 16> f6;
	std::optional<std::uint32_t> f7;
};

struct m30
{
	std::string_view f0;
	std::optional<float> f1;
	pb::repeated<std::int32_t, 8> f2;
	double f3;
	pb::repeated<std::uint8_t, 16> f4;
	pb::fixed32 f5;
	std::uint64_t f6;
	std::uint32_t f7;
};

struct m31
{
	double f0;
	float f1;
	std::string_view f2;
	std::int32_t f3;
	pb::repeated<std::int32_t, 8> f4;
	pb::fixed32 f5;
	pb::repeated<float, 4> f6;
	pb::repeated<std::uint8_t, 16> f7;
};

#if MICROPB_SIZE_TYPES == 16
using payload_type = std::variant<std::monostate, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15>;
#else
using payload_type = std::variant<std::monostate,
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
	std::uint32_t id;
	payload_type payload;
};

std::expected<std::size_t, pb::error> size_report_encode( const top& m, std::span<std::uint8_t> out )
{
	return pb::encode( m, out );
}

std::expected<std::size_t, pb::error> size_report_decode( std::span<const std::uint8_t> in, top& m )
{
	return pb::decode( in, m );
}
