#include "generator/detail.pb.hpp"
#include "generator/features.pb.hpp"
#include "vectors/messages.hpp"
#include "vectors/vectors.hpp"
#include "vectors/vectors.pb.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>

#include <micropb/micropb.hpp>

/// The headers that `tools/protoc-gen-micropb.py` wrote from `vectors.proto` and `features.proto`.
/// The expected bytes of the `features` messages come from `protoc --encode` (libprotoc 35.1).
namespace {

namespace gen = micropb::test;

static_assert( sizeof( features::Device_State ) == 1 );
static_assert( std::is_same_v<decltype( gen::scalars::i8 ), int8_t> );
static_assert( std::is_same_v<decltype( gen::scalars::u16 ), uint16_t> );
static_assert( std::is_same_v<decltype( gen::text::mac ), std::array<uint8_t, 6>> );
static_assert( std::is_same_v<decltype( gen::lists::names ), pb::repeated<std::string_view, 8>> );
static_assert(
  std::is_same_v<decltype( gen::envelope::payload ), pb::oneof<gen::pose, gen::ping, gen::text>> );

static_assert( std::is_same_v<decltype( gen::outer::child ), std::optional<gen::inner>> );
static_assert( std::is_same_v<decltype( features::Device::limits ), std::optional<features::limits>> );
static_assert( std::is_same_v<decltype( features::Device_Slot::key ), pb::bytes<16>> );
static_assert( std::is_same_v<decltype( features::Device::token ), std::optional<pb::bytes<8>>> );
static_assert( std::is_same_v<decltype( features::Device::trims ), pb::repeated<int8_t, 4>> );
static_assert( std::is_same_v<decltype( features::Device::trim_delta ), int16_t> );
static_assert( std::is_same_v<decltype( features::Frame::origin ), std::optional<shared::types::Point>> );
static_assert( std::is_same_v<decltype( features::Calibration::gains ), std::array<float, 4>> );
static_assert( std::is_same_v<decltype( features::Batch::slots ), std::span<const features::Device_Slot>> );
static_assert( std::is_same_v<decltype( features::Batch::blob ), std::span<const uint8_t>> );

namespace d = pb::detail;

template <typename L>
struct number_list;

template <typename T,
          typename... M,
          uint32_t... Number,
          size_t... Offset,
          size_t... Alternative,
          pb::int_encoding... Encoding>
struct number_list<pb::field_list<T, pb::field_at<M, Number, Offset, Alternative, Encoding>...>>
{
	static consteval std::array<uint32_t, sizeof...( M )> sorted()
	{
		std::array<uint32_t, sizeof...( M )> numbers{ Number... };
		std::sort( numbers.begin(), numbers.end() );
		return numbers;
	}
};

/// The field numbers that the plugin wrote are those of the struct that a person wrote for the same message.
template <typename Generated, typename Written>
inline constexpr bool same_numbers =
  number_list<d::field_list_of<Generated>>::sorted() == number_list<d::field_list_of<Written>>::sorted();

static_assert( same_numbers<gen::scalars, messages::scalars> && same_numbers<gen::text, messages::text> &&
               same_numbers<gen::inner, messages::inner> && same_numbers<gen::outer, messages::outer> &&
               same_numbers<gen::lists, messages::lists> && same_numbers<gen::pose, messages::pose> &&
               same_numbers<gen::ping, messages::ping> && same_numbers<gen::envelope, messages::envelope> );

template <typename T, size_t N>
bool encodes_to( const T& message, const std::array<uint8_t, N>& wire )
{
	std::array<uint8_t, N> out{};
	const auto size = pb::encode( message, std::span<uint8_t>( out ) );
	return pb::encoded_size( message ) == N && size && *size == N && out == wire;
}

template <typename T, size_t N>
bool round_trips( const std::array<uint8_t, N>& wire )
{
	T message{};
	const auto decoded = pb::decode( std::span<const uint8_t>( wire ), message );
	return decoded && *decoded == N && encodes_to( message, wire );
}

bool test_reference_vectors()
{
	return round_trips<gen::scalars>( vectors::scalars_zero ) && round_trips<gen::scalars>( vectors::scalars_max ) &&
	       round_trips<gen::scalars>( vectors::scalars_negative ) &&
	       round_trips<gen::scalars>( vectors::scalars_enum_neg ) && round_trips<gen::text>( vectors::text_short ) &&
	       round_trips<gen::text>( vectors::text_long ) && round_trips<gen::outer>( vectors::outer_unset ) &&
	       round_trips<gen::outer>( vectors::outer_set ) && round_trips<gen::lists>( vectors::lists_empty ) &&
	       round_trips<gen::lists>( vectors::lists_full ) && round_trips<gen::pose>( vectors::pose_zero ) &&
	       round_trips<gen::envelope>( vectors::envelope_none ) && round_trips<gen::envelope>( vectors::envelope_pose ) &&
	       round_trips<gen::envelope>( vectors::envelope_ping ) && round_trips<gen::envelope>( vectors::envelope_long );
}

/// `envelope.pose.txtpb`: `id: 2 pose { pos: [1, 2, 3] rot: [0, 0, 0, 1] }`.
bool test_reference_values()
{
	gen::envelope message{};
	if ( !pb::decode( std::span<const uint8_t>( vectors::envelope_pose ), message ) )
		return false;

	const auto* pose = pb::get_if<gen::pose>( &message.payload );
	gen::scalars values{};
	return message.id == 2 && pose && pose->pos.size() == 3 && pose->pos[2] == 3 && pose->rot.size() == 4 &&
	       pose->rot[3] == 1 && pb::decode( std::span<const uint8_t>( vectors::scalars_enum_neg ), values ) &&
	       values.c == gen::color::NEG;
}

/// Singular proto3 message fields preserve presence even without `optional`:
/// unset omits the field (0 bytes), engaged empty writes `tag, 0x00`.
bool test_singular_message_presence()
{
	const gen::outer absent{};
	const gen::outer present_empty{ .child = gen::inner{} };
	gen::outer rx_absent{ .child = gen::inner{ 9, "old" } };
	gen::outer rx_empty{};
	const auto a = pb::decode( std::span<const uint8_t>{}, rx_absent );
	const auto b = pb::decode( std::span<const uint8_t>( vectors::outer_unset ), rx_empty );
	return encodes_to( absent, std::array<uint8_t, 0>{} ) && encodes_to( present_empty, vectors::outer_unset ) && a &&
	       *a == 0 && !rx_absent.child.has_value() && b && *b == vectors::outer_unset.size() &&
	       rx_empty.child.has_value() && rx_empty.child->x == 0 && rx_empty.child->label.empty();
}

/// `class: 1 limits {} backup {} serial: 5 text: "hi" ready: true`
bool test_field_numbers()
{
	features::Device device{};
	device.class_ = 1;
	device.limits.emplace();
	device.backup.emplace();
	device.serial = 5;
	device.event = std::string_view( "hi" );
	device.ready = true;

	return encodes_to(
	  device,
	  std::array<uint8_t, 16>{
	    0x08, 0x01, 0x1a, 0x00, 0x22, 0x00, 0x50, 0x05, 0xb2, 0x01, 0x02, 0x68, 0x69, 0xd0, 0x01, 0x01 } );
}

/// `gains: [1, 2, 3, 4] id: "\001\002\003\004\005\006"`
bool test_fixed_arrays()
{
	const features::Calibration calibration{ { 1, 2, 3, 4 }, { 1, 2, 3, 4, 5, 6 } };

	return encodes_to( calibration, std::array<uint8_t, 26>{ 0x0a, 0x10, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00,
	                                                              0x40, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x80, 0x40,
	                                                              0x12, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } );
}

/// `batch { slots { index: 1 key: "ab" } slots {} deltas: [-1, 300] blob: "xyz" }` (libprotoc 36.2)
bool test_views()
{
	features::Device_Slot slots[2]{ { 1, {} }, {} };
	const int16_t deltas[]{ -1, 300 };
	const uint8_t blob[]{ 'x', 'y', 'z' };
	features::Report report{ features::Batch{ slots, deltas, blob } };

	return slots[0].key.push_back( 'a' ) && slots[0].key.push_back( 'b' ) &&
	       encodes_to( report,
	                   std::array<uint8_t, 22>{ 0x12, 0x14, 0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x61, 0x62, 0x0a,
	                                                 0x00, 0x12, 0x03, 0x01, 0xd8, 0x04, 0x1a, 0x03, 0x78, 0x79, 0x7a } );
}

/// Decodes the bytes of `test_views` into the spans of `Batch`. The items are in the arena.
bool test_views_decode()
{
	constexpr std::array<uint8_t, 22> wire{ 0x12, 0x14, 0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x61, 0x62, 0x0a,
	                                             0x00, 0x12, 0x03, 0x01, 0xd8, 0x04, 0x1a, 0x03, 0x78, 0x79, 0x7a };
	std::array<uint8_t, 256> storage{};
	pb::arena memory{ std::span<uint8_t>( storage ) };
	features::Report report{};

	const auto decoded = pb::decode( std::span<const uint8_t>( wire ), report, memory );
	const auto* batch = pb::get_if<features::Batch>( &report.body );
	if ( !decoded || *decoded != wire.size() || !batch || batch->slots.size() != 2 || batch->deltas.size() != 2 ||
	     batch->blob.size() != 3 )
		return false;

	const auto key = batch->slots[0].key;
	return batch->slots[0].index == 1 && key.size() == 2 && key[0] == 'a' && key[1] == 'b' &&
	       batch->slots[1].index == 0 && batch->slots[1].key.empty() && batch->deltas[0] == -1 &&
	       batch->deltas[1] == 300 && batch->blob[0] == 'x' && batch->blob[1] == 'y' && batch->blob[2] == 'z';
}

/// `device { class: -2 state: STATE_BUSY limits { low: 1 high: 2 } backup {} serial: 7 offset: -300 token: ""
/// spare { index: 3 } slots { index: 1 key: "ab" } slots {} history: [STATE_BUSY, STATE_IDLE] trims: [-1, 5]
/// trim_delta: -300 history_delta: [-1, 2, -32768] moved { index: 9 } } slot { key: "k" } origin { x: -4 y: 4 }
/// unit: UNIT_METER mode: RX_MODE_VARIABLE`
constexpr auto frame_wire = std::to_array<uint8_t>(
  { 0x0a, 0x55, 0x08, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x10, 0x01, 0x1a, 0x04,
	  0x08, 0x01, 0x10, 0x02, 0x22, 0x00, 0x50, 0x07, 0x58, 0xd4, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	  0xff, 0x01, 0x62, 0x00, 0x6a, 0x02, 0x08, 0x03, 0x72, 0x06, 0x08, 0x01, 0x12, 0x02, 0x61, 0x62, 0x72,
	  0x00, 0x7a, 0x02, 0x01, 0x00, 0x82, 0x01, 0x0b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	  0x01, 0x05, 0x88, 0x01, 0xd7, 0x04, 0x92, 0x01, 0x05, 0x01, 0x04, 0xff, 0xff, 0x03, 0xca, 0x01, 0x02,
	  0x08, 0x09, 0x12, 0x03, 0x12, 0x01, 0x6b, 0x1a, 0x04, 0x08, 0x07, 0x10, 0x08, 0x20, 0x01, 0x28, 0x01 } );

bool test_frame_encode()
{
	features::Frame frame{};
	features::Device& device = frame.device.emplace();
	device.class_ = -2;
	device.state = features::Device_State::BUSY;
	device.limits = features::limits{ 1, 2 };
	device.backup.emplace();
	device.serial = 7;
	device.offset = -300;
	device.trim_delta = -300;
	device.token.emplace();
	device.spare = features::Device_Slot{ 3, {} };
	device.event = features::Device_Slot{ 9, {} };

	features::Device_Slot first{ 1, {} };
	bool ok = first.key.push_back( 'a' ) && first.key.push_back( 'b' ) && device.slots.push_back( first ) &&
	          device.slots.push_back( {} ) && device.history.push_back( features::Device_State::BUSY ) &&
	          device.history.push_back( features::Device_State::IDLE ) && device.trims.push_back( -1 ) &&
	          device.trims.push_back( 5 ) && device.history_delta.push_back( -1 ) &&
	          device.history_delta.push_back( 2 ) && device.history_delta.push_back( -32768 ) &&
	          frame.slot.emplace().key.push_back( 'k' );
	frame.origin = shared::types::Point{ -4, 4 };
	frame.unit = shared::types::Unit::METER;
	frame.mode = features::RxMode::VARIABLE;

	return ok && encodes_to( frame, frame_wire );
}

bool test_frame_decode()
{
	features::Frame frame{};
	if ( !round_trips<features::Frame>( frame_wire ) ||
	     !pb::decode( std::span<const uint8_t>( frame_wire ), frame ) || !frame.device || !frame.slot || !frame.origin )
		return false;

	const features::Device& device = *frame.device;
	const auto* moved = pb::get_if<features::Device_Slot>( &device.event );
	return device.class_ == -2 && device.state == features::Device_State::BUSY && device.limits &&
	       device.limits->high == 2 && device.backup.has_value() && device.serial == 7 && device.offset == -300 &&
	       device.token && device.token->empty() && device.spare && device.spare->index == 3 &&
	       device.slots.size() == 2 && device.slots[0].key.size() == 2 && device.slots[0].key[1] == 'b' &&
	       device.history.size() == 2 && device.history[0] == features::Device_State::BUSY && device.trims.size() == 2 &&
	       device.trims[0] == -1 && device.trim_delta == -300 && device.history_delta.size() == 3 &&
	       device.history_delta[2] == -32768 && moved && moved->index == 9 && !device.ready &&
	       frame.slot->key.size() == 1 && frame.origin->x == -4 && frame.origin->y == 4 &&
	       frame.unit == shared::types::Unit::METER && frame.mode == features::RxMode::VARIABLE;
}

/// Unbraced declarations (`T msg;`) initialize every generated field to its proto3 default.
bool test_default_initialization()
{
	gen::scalars s;
	gen::text t;
	features::Calibration cal;
	features::Frame f;
	return encodes_to( s, vectors::scalars_zero ) && s.c == gen::color::NONE && t.name.empty() && t.data.empty() &&
	       t.mac == std::array<uint8_t, 6>{} && cal.gains == std::array<float, 4>{} &&
	       cal.id == std::array<uint8_t, 6>{} && !f.device.has_value() && !f.slot.has_value() &&
	       !f.origin.has_value() && f.unit == shared::types::Unit::NONE && f.mode == features::RxMode::FIXED;
}

/// `detail.proto` has names that the namespace `pb` also has. `tag { error: 5 } repeated { error: 1 }`
bool test_names_of_the_library()
{
	::detail::reader message{};
	message.tag = ::detail::tag{ 5 };
	return message.repeated.push_back( { 1 } ) &&
	       encodes_to( message, std::array<uint8_t, 8>{ 0x0a, 0x02, 0x08, 0x05, 0x12, 0x02, 0x08, 0x01 } ) &&
	       round_trips<::detail::reader>( std::array<uint8_t, 8>{ 0x0a, 0x02, 0x08, 0x05, 0x12, 0x02, 0x08, 0x01 } );
}

/// `Log` has three `std::string_view` alternatives in `msg`, addressed by the generated 1-based index constants.
bool test_duplicate_oneof()
{
	constexpr std::array<uint8_t, 7> err_wire{ 0x08, 0x07, 0x12, 0x03, 0x62, 0x61, 0x64 };
	constexpr std::array<uint8_t, 6> info_wire{ 0x08, 0x07, 0x1a, 0x02, 0x6f, 0x6b };
	constexpr std::array<uint8_t, 7> html_wire{ 0x08, 0x07, 0x22, 0x03, 0x3c, 0x62, 0x3e };

	const features::Log err{ .id = 7, .msg{ std::in_place_index<features::Log::msg_err>, "bad" } };
	features::Log info{ .id = 7 };
	info.msg.emplace<features::Log::msg_info>( "ok" );
	features::Log html{ .id = 7 };
	html.msg.emplace<features::Log::msg_html>( "<b>" );

	features::Log rx{};
	if ( !pb::decode( std::span<const uint8_t>( html_wire ), rx ) )
		return false;
	const auto* html_text = pb::get_if<features::Log::msg_html>( &rx.msg );

	return encodes_to( err, err_wire ) && encodes_to( info, info_wire ) && encodes_to( html, html_wire ) &&
	       round_trips<features::Log>( err_wire ) && round_trips<features::Log>( info_wire ) && rx.id == 7 &&
	       rx.msg.index() == features::Log::msg_html && html_text && *html_text == "<b>";
}

} // namespace

int main()
{
	const bool ok = test_default_initialization() && test_reference_vectors() && test_reference_values() &&
	                test_singular_message_presence() && test_field_numbers() && test_fixed_arrays() && test_views() &&
	                test_views_decode() && test_frame_encode() && test_frame_decode() && test_names_of_the_library() &&
	                test_duplicate_oneof();
	return ok ? 0 : 1;
}
