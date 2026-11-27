#include <array>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include <micropb/schema.hpp>

using namespace pb;

namespace {

enum class signed_e8 : int8_t
{
	A = 0,
};

enum class unsigned_e8 : uint8_t
{
	A = 0,
};

enum class unsigned_e16 : uint16_t
{
	A = 0,
};

enum class unsigned_e32 : uint32_t
{
	A = 0,
};

enum class trait_small : int32_t
{
	RED = 0,
	GREEN = 1,
	BLUE = 2,
};

enum class trait_wide : int32_t
{
	ZERO = 0,
	BIG = 128,
};

enum class trait_neg : int32_t
{
	ZERO = 0,
	NEG = -1,
};

struct trait_enum_msg
{
	trait_small one;
	trait_wide wide;
	trait_neg neg;
	pb::repeated<trait_small, 4> many;
};

struct pose
{
	std::array<float, 3> pos;
	std::array<float, 4> rot;
};

struct empty_msg
{};

struct bounded_scalars
{
	bool b;
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int8_t s8;
	int16_t s16;
	int32_t s32;
	int64_t s64;
	uint32_t fx32;
	uint64_t fx64;
	float f;
	double d;
	signed_e8 e_s8;
	unsigned_e8 e_u8;
	unsigned_e16 e_u16;
	unsigned_e32 e_u32;
};

struct bounded_containers
{
	pb::string<15> label;
	pb::bytes<8> token;
	std::array<uint8_t, 6> mac;
	std::span<const uint8_t, 4> fixed_view;
	pb::repeated<int32_t, 20> packed_ints; // 20 * 10 = 200 B payload -> 2-byte length prefix
	pb::repeated<pb::string<5>, 3> names;
	pb::map<pb::string<4>, uint32_t, 2> table;
	pb::string<0> zero_plain;
	std::optional<pb::string<0>> zero_opt;
	std::optional<empty_msg> empty_child;
	const empty_msg* ptr_child;
};

struct multi_oneof_msg
{
	pb::oneof<bool, pb::string<10>, uint32_t> first;
	pb::oneof<double, int8_t> second;
};

struct unbounded_sv
{
	std::string_view s;
};

struct unbounded_bytes
{
	std::span<const uint8_t> b;
};

struct unbounded_repeated
{
	std::span<const int32_t> xs;
};

struct unbounded_oneof
{
	pb::oneof<uint32_t, std::string_view> v;
};

struct unbounded_nested
{
	std::optional<unbounded_sv> child;
};

struct rec_node
{
	uint32_t id;
	const rec_node* next;
};

struct mut_b;

struct mut_a
{
	const mut_b* b;
};

struct mut_b
{
	const mut_a* a;
};

} // namespace

template <>
struct pb::enum_traits<trait_small>
{
	static constexpr trait_small min = trait_small::RED;
	static constexpr trait_small max = trait_small::BLUE;
};

template <>
struct pb::enum_traits<trait_wide>
{
	static constexpr trait_wide min = trait_wide::ZERO;
	static constexpr trait_wide max = trait_wide::BIG;
};

template <>
struct pb::enum_traits<trait_neg>
{
	static constexpr trait_neg min = trait_neg::NEG;
	static constexpr trait_neg max = trait_neg::ZERO;
};

template <>
struct pb::fields_of<trait_enum_msg>
  : pb::field_list<trait_enum_msg,
                   pb::field_at<decltype( trait_enum_msg::one ), 1, offsetof( trait_enum_msg, one )>,
                   pb::field_at<decltype( trait_enum_msg::wide ), 2, offsetof( trait_enum_msg, wide )>,
                   pb::field_at<decltype( trait_enum_msg::neg ), 3, offsetof( trait_enum_msg, neg )>,
                   pb::field_at<decltype( trait_enum_msg::many ), 4, offsetof( trait_enum_msg, many )>>
{};

template <>
struct pb::fields_of<empty_msg> : pb::field_list<empty_msg>
{};

template <>
struct pb::fields_of<bounded_scalars>
  : pb::field_list<bounded_scalars,
                   pb::field_at<decltype( bounded_scalars::b ), 1, offsetof( bounded_scalars, b )>,
                   pb::field_at<decltype( bounded_scalars::i8 ), 2, offsetof( bounded_scalars, i8 )>,
                   pb::field_at<decltype( bounded_scalars::i16 ), 3, offsetof( bounded_scalars, i16 )>,
                   pb::field_at<decltype( bounded_scalars::i32 ), 4, offsetof( bounded_scalars, i32 )>,
                   pb::field_at<decltype( bounded_scalars::i64 ), 5, offsetof( bounded_scalars, i64 )>,
                   pb::field_at<decltype( bounded_scalars::u8 ), 6, offsetof( bounded_scalars, u8 )>,
                   pb::field_at<decltype( bounded_scalars::u16 ), 7, offsetof( bounded_scalars, u16 )>,
                   pb::field_at<decltype( bounded_scalars::u32 ), 8, offsetof( bounded_scalars, u32 )>,
                   pb::field_at<decltype( bounded_scalars::u64 ), 9, offsetof( bounded_scalars, u64 )>,
                   pb::field_at<decltype( bounded_scalars::s8 ), 10, offsetof( bounded_scalars, s8 ), 0, int_encoding::ZIGZAG>,
                   pb::field_at<decltype( bounded_scalars::s16 ), 11, offsetof( bounded_scalars, s16 ), 0, int_encoding::ZIGZAG>,
                   pb::field_at<decltype( bounded_scalars::s32 ), 12, offsetof( bounded_scalars, s32 ), 0, int_encoding::ZIGZAG>,
                   pb::field_at<decltype( bounded_scalars::s64 ), 13, offsetof( bounded_scalars, s64 ), 0, int_encoding::ZIGZAG>,
                   pb::field_at<decltype( bounded_scalars::fx32 ), 14, offsetof( bounded_scalars, fx32 ), 0, int_encoding::FIXED>,
                   pb::field_at<decltype( bounded_scalars::fx64 ), 15, offsetof( bounded_scalars, fx64 ), 0, int_encoding::FIXED>,
                   pb::field_at<decltype( bounded_scalars::f ), 16, offsetof( bounded_scalars, f )>,
                   pb::field_at<decltype( bounded_scalars::d ), 17, offsetof( bounded_scalars, d )>,
                   pb::field_at<decltype( bounded_scalars::e_s8 ), 18, offsetof( bounded_scalars, e_s8 )>,
                   pb::field_at<decltype( bounded_scalars::e_u8 ), 19, offsetof( bounded_scalars, e_u8 )>,
                   pb::field_at<decltype( bounded_scalars::e_u16 ), 20, offsetof( bounded_scalars, e_u16 )>,
                   pb::field_at<decltype( bounded_scalars::e_u32 ),
                                detail::max_field_number,
                                offsetof( bounded_scalars, e_u32 )>>
{};

template <>
struct pb::fields_of<bounded_containers>
  : pb::field_list<bounded_containers,
                   pb::field_at<decltype( bounded_containers::label ), 1, offsetof( bounded_containers, label )>,
                   pb::field_at<decltype( bounded_containers::token ), 2, offsetof( bounded_containers, token )>,
                   pb::field_at<decltype( bounded_containers::mac ), 3, offsetof( bounded_containers, mac )>,
                   pb::field_at<decltype( bounded_containers::fixed_view ), 4, offsetof( bounded_containers, fixed_view )>,
                   pb::field_at<decltype( bounded_containers::packed_ints ), 5, offsetof( bounded_containers, packed_ints )>,
                   pb::field_at<decltype( bounded_containers::names ), 6, offsetof( bounded_containers, names )>,
                   pb::field_at<decltype( bounded_containers::table ), 7, offsetof( bounded_containers, table )>,
                   pb::field_at<decltype( bounded_containers::zero_plain ), 8, offsetof( bounded_containers, zero_plain )>,
                   pb::field_at<decltype( bounded_containers::zero_opt ), 9, offsetof( bounded_containers, zero_opt )>,
                   pb::field_at<decltype( bounded_containers::empty_child ), 10, offsetof( bounded_containers, empty_child )>,
                   pb::field_at<decltype( bounded_containers::ptr_child ), 11, offsetof( bounded_containers, ptr_child )>>
{};

template <>
struct pb::fields_of<multi_oneof_msg>
  : pb::field_list<multi_oneof_msg,
                   pb::field_at<decltype( multi_oneof_msg::first ), 1, offsetof( multi_oneof_msg, first ), 1>,
                   pb::field_at<decltype( multi_oneof_msg::first ), 16, offsetof( multi_oneof_msg, first ), 2>,
                   pb::field_at<decltype( multi_oneof_msg::first ), 3, offsetof( multi_oneof_msg, first ), 3>,
                   pb::field_at<decltype( multi_oneof_msg::second ), 4, offsetof( multi_oneof_msg, second ), 1>,
                   pb::field_at<decltype( multi_oneof_msg::second ), 5, offsetof( multi_oneof_msg, second ), 2, int_encoding::ZIGZAG>>
{};

template <>
struct pb::fields_of<unbounded_sv>
  : pb::field_list<unbounded_sv, pb::field_at<decltype( unbounded_sv::s ), 1, offsetof( unbounded_sv, s )>>
{};

template <>
struct pb::fields_of<unbounded_bytes>
  : pb::field_list<unbounded_bytes, pb::field_at<decltype( unbounded_bytes::b ), 1, offsetof( unbounded_bytes, b )>>
{};

template <>
struct pb::fields_of<unbounded_repeated>
  : pb::field_list<unbounded_repeated,
                   pb::field_at<decltype( unbounded_repeated::xs ), 1, offsetof( unbounded_repeated, xs )>>
{};

template <>
struct pb::fields_of<unbounded_oneof>
  : pb::field_list<unbounded_oneof,
                   pb::field_at<decltype( unbounded_oneof::v ), 1, offsetof( unbounded_oneof, v ), 1>,
                   pb::field_at<decltype( unbounded_oneof::v ), 2, offsetof( unbounded_oneof, v ), 2>>
{};

template <>
struct pb::fields_of<unbounded_nested>
  : pb::field_list<unbounded_nested,
                   pb::field_at<decltype( unbounded_nested::child ), 1, offsetof( unbounded_nested, child )>>
{};

template <>
struct pb::fields_of<rec_node>
  : pb::field_list<rec_node,
                   pb::field_at<decltype( rec_node::id ), 1, offsetof( rec_node, id )>,
                   pb::field_at<decltype( rec_node::next ), 2, offsetof( rec_node, next )>>
{};

template <>
struct pb::fields_of<mut_a> : pb::field_list<mut_a, pb::field_at<decltype( mut_a::b ), 1, offsetof( mut_a, b )>>
{};

template <>
struct pb::fields_of<mut_b> : pb::field_list<mut_b, pb::field_at<decltype( mut_b::a ), 1, offsetof( mut_b, a )>>
{};

namespace {

////////////////////////////////////////////////////////////////////////////////

consteval bool test_kinds()
{
	return kind_of<int32_t>() == field_kind::SCALAR && kind_of<std::string_view>() == field_kind::STRING &&
	       kind_of<string<16>>() == field_kind::STRING &&
	       kind_of<std::span<const uint8_t>>() == field_kind::BYTES &&
	       kind_of<bytes<16>>() == field_kind::BYTES &&
	       kind_of<std::array<uint8_t, 6>>() == field_kind::FIXED_BYTES &&
	       kind_of<repeated<float, 4>>() == field_kind::REPEATED && kind_of<pose>() == field_kind::MESSAGE &&
	       kind_of<std::optional<int>>() == field_kind::OPTIONAL &&
	       kind_of<repeated<uint8_t, 4>>() == field_kind::BYTES &&
	       kind_of<repeated<bytes<8>, 4>>() == field_kind::REPEATED &&
	       kind_of<repeated<string<8>, 4>>() == field_kind::REPEATED &&
	       kind_of<std::span<const float>>() == field_kind::REPEATED &&
	       kind_of<std::span<const pose>>() == field_kind::REPEATED &&
	       kind_of<pb::oneof<int>>() == field_kind::ONEOF &&
	       kind_of<pb::oneof<std::string_view, std::string_view>>() == field_kind::ONEOF &&
	       kind_of<pb::kv_pair<std::string_view, int32_t>>() == field_kind::MESSAGE &&
	       kind_of<pb::map<pb::string<8>, int32_t, 4>>() == field_kind::REPEATED &&
	       kind_of<std::span<const pb::kv_pair<uint32_t, pose>>>() == field_kind::REPEATED &&
	       kind_of<const pose*>() == field_kind::POINTER && kind_of<pose*>() == field_kind::MESSAGE &&
	       kind_of<const int32_t*>() == field_kind::MESSAGE && kind_of<const void*>() == field_kind::MESSAGE;
}

consteval bool test_wire_types()
{
	return wire_type_of<float>() == wire_type::FIXED32 && wire_type_of<double>() == wire_type::FIXED64 &&
	       wire_type_of<int64_t, int_encoding::FIXED>() == wire_type::FIXED64 &&
	       wire_type_of<int8_t>() == wire_type::VARINT && wire_type_of<bool>() == wire_type::VARINT &&
	       wire_type_of<int32_t, int_encoding::ZIGZAG>() == wire_type::VARINT &&
	       wire_type_of<uint32_t, int_encoding::FIXED>() == wire_type::FIXED32 &&
	       wire_type_of<const pose*>() == wire_type::LENGTH_DELIMITED;
}

consteval bool test_packed()
{
	return is_packed<float>() && is_packed<int64_t>() && !is_packed<std::string_view>() && !is_packed<pose>() &&
	       !is_packed<const pose*>();
}

consteval bool test_max_encoded_size()
{
	// bounded_scalars:
	// tags 1..15 (1 B key): b(1)+i8(10)+i16(10)+i32(10)+i64(10)+u8(2)+u16(3)+u32(5)+u64(10)
	//                       +s8(2)+s16(3)+s32(5)+s64(10)+fx32(4)+fx64(8) = 15 + 93 = 108
	// tags 16..20 (2 B key): f(4)+d(8)+e_s8(10)+e_u8(2)+e_u16(3) = 10 + 27 = 37
	// tag 2^29-1 (5 B key): e_u32(10) = 15
	// total = 160
	constexpr size_t expected_scalars = 160;

	// bounded_containers (tags 1..11, 1 B key each):
	// label: 1+1+15=17, token: 1+1+8=10, mac: 1+1+6=8, fixed_view: 1+1+4=6,
	// packed_ints: 1+2+200=203, names: 3*(1+1+5)=21,
	// table: 2*(1+1+((1+1+4)+(1+5))) = 2*(2+12) = 28,
	// zero_plain: 0, zero_opt: 1+1+0=2, empty_child: 1+1+0=2, ptr_child: 1+1+0=2
	// total = 299
	constexpr size_t expected_containers = 299;

	// multi_oneof_msg:
	// first: max(1+1=2, 2+1+10=13, 1+5=6) = 13
	// second: max(1+8=9, 1+2=3) = 9
	// total = 22
	constexpr size_t expected_oneof = 22;

	// trait_enum_msg (tags 1..4, 1 B key each):
	// one (0..2): 1+1=2, wide (0..128): 1+2=3, neg (-1..0): 1+10=11, many (4 packed 0..2): 1+1+4*1=6
	// total = 22
	constexpr size_t expected_trait_enums = 22;

	return has_max_encoded_size<empty_msg> && max_encoded_size<empty_msg> == 0 &&
	       has_max_encoded_size<const empty_msg> && max_encoded_size<const empty_msg> == 0 &&
	       has_max_encoded_size<bounded_scalars> && max_encoded_size<bounded_scalars> == expected_scalars &&
	       has_max_encoded_size<bounded_containers> && max_encoded_size<bounded_containers> == expected_containers &&
	       has_max_encoded_size<multi_oneof_msg> && max_encoded_size<multi_oneof_msg> == expected_oneof &&
	       has_max_encoded_size<trait_enum_msg> && max_encoded_size<trait_enum_msg> == expected_trait_enums &&
	       !has_max_encoded_size<unbounded_sv> && !has_max_encoded_size<unbounded_bytes> &&
	       !has_max_encoded_size<unbounded_repeated> && !has_max_encoded_size<unbounded_oneof> &&
	       !has_max_encoded_size<unbounded_nested> && !has_max_encoded_size<rec_node> &&
	       !has_max_encoded_size<mut_a> && !has_max_encoded_size<mut_b>;
}

////////////////////////////////////////////////////////////////////////////////

static_assert( test_kinds() );
static_assert( test_wire_types() );
static_assert( test_packed() );
static_assert( test_max_encoded_size() );

} // namespace

// A `std::span` of elements that are not `const` is not a sequence type.
static_assert( !pb::detail::sequence_traits<std::span<int32_t>>::is_sequence &&
               pb::detail::sequence_traits<std::span<const int32_t>>::is_sequence );

int main()
{
	constexpr bool ok = test_kinds() && test_wire_types() && test_packed() && test_max_encoded_size();
	return ok ? 0 : 1;
}
