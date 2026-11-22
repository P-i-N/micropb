# MicroPB Implementation & Reference Details

This document covers the API rules, proto3-to-C++23 type mapping, `.options` reference, diagnostics, NanoPB comparison, and build options for MicroPB. For an overview and quick start, see [README.md](README.md).

## Contents

* [API behavior](#api-behavior)
* [Type mapping and rules](#type-mapping-and-rules)
* [Code generator (`protoc-gen-micropb`)](#code-generator-protoc-gen-micropb)
* [Errors](#errors)
* [Compile-time checks](#compile-time-checks)
* [Code size](#code-size)
* [MicroPB vs. NanoPB](#micropb-vs-nanopb)
* [Build, sanitizers, and cross-compilation](#build-sanitizers-and-cross-compilation)
* [Status and limits](#status-and-limits)

## API behavior

```cpp
template <typename T>
[[nodiscard]] size_t pb::encoded_size( const T& msg );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::encode( const T& msg, std::span<uint8_t> out );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in, T& msg );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in,
                                                           T& msg,
                                                           std::span<uint8_t> memory );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in, T& msg, pb::arena& memory );
```

### Encode rules

* `encode` writes one proto3 message into `out` in ascending field-number order and returns the byte count written.
* It never writes past `out`. If `out` is too short, it returns `pb::error::BUFFER_TOO_SMALL`, and the partial contents of `out` are not valid.
* `encoded_size` computes the exact byte count using the same code path without writing bytes.

### Decode rules

* `decode` reads one message from `in` and returns `in.size()` on success.
* It resets `msg` before reading, so you can reuse one struct across messages (`pb::repeated` and `pb::bytes` reset size to 0; `pb::string` resets size to 0 and writes `'\0'` at index 0; `std::span<const T>` becomes empty).
* New `std::optional` values and switched `std::variant` alternatives also start from the proto3 zero/empty state; C++ default member initializers inside them do not persist.
* Unknown field numbers and known fields with unexpected wire types are skipped, preserving forward compatibility.
* Duplicate scalar fields keep the last value; duplicate nested messages merge; duplicate `oneof` fields keep the last alternative; repeated scalar fields accept packed, unpacked, or mixed encodings.
* Varints wider than a narrow integer member truncate to the member width, matching the Google C++ runtime. Any non-zero varint decodes as `true` for `bool`. Unnamed enum values keep their integer value.
* Strings are not checked for valid UTF-8.
* On error, `msg` retains fields decoded before the failure, and arena blocks allocated during that call remain used until `memory.clear()`.
* For single-message decode with `std::span<const T>` repeated fields, pass a `uint8_t[N]` C array, `std::array<uint8_t, N>`, or `std::span<uint8_t>` directly as `memory` (temporary rvalue `std::array`s do not compile, preventing dangling spans). Pass `pb::arena&` when sharing one scratch buffer across multiple `decode` calls or inspecting `memory.used()`.

> [!WARNING]
> Decoded `std::string_view` and `std::span<const uint8_t>` fields point into `in`. Decoded `std::span<const T>` repeated fields point into `memory`. Keep `in` and `memory` alive and unchanged for as long as you read `msg`. Fields stored in `pb::string<N>`, `pb::bytes<N>`, `pb::repeated<T, N>`, or `std::array` own their data inside `msg`.

## Type mapping and rules

### Proto3 to C++23 mapping

| C++ type | `pb::field_at` encoding | proto3 type | Notes |
| --- | --- | --- | --- |
| `bool` | `VARINT` | `bool` | |
| `int32_t`, `int64_t` | `VARINT` | `int32`, `int64` | Negative values encode as 10-byte varints. |
| `uint32_t`, `uint64_t` | `VARINT` | `uint32`, `uint64` | |
| `int8_t`, `int16_t` | `VARINT` | `int32` | Narrow storage in RAM; decode truncates to member width. |
| `uint8_t`, `uint16_t` | `VARINT` | `uint32` | Narrow storage in RAM; decode truncates to member width. |
| Standard integers (`short`, `int`, `long`, `long long`, signed/unsigned) | `VARINT` | `int32`/`int64`/`uint32`/`uint64` by size and sign | Prefer `<stdint.h>` types across targets (`long` is 32-bit on Windows/Arm and 64-bit on Linux x86-64). |
| Signed 1-, 2-, 4-, or 8-byte integer (`int8_t`..`int64_t`) | `pb::int_encoding::ZIGZAG` | `sint32`, `sint64` | ZigZag varint; compact for negative numbers. |
| 4- or 8-byte integer (`uint32_t`, `uint64_t`, `int32_t`, `int64_t`) | `pb::int_encoding::FIXED` | `fixed32`, `fixed64`, `sfixed32`, `sfixed64` | Fixed 4- or 8-byte little-endian wire encoding. |
| `float`, `double` | `VARINT` *(default)* | `float`, `double` | 4- and 8-byte IEEE-754 fixed wire encoding. |
| `enum`, `enum class` (up to 32 bits) | `VARINT` | `enum` | `int32` on the wire. |
| `std::string_view` | — | `string` | Zero-copy view into input on decode. |
| `pb::string<N>` | — | `string` | Copies up to `N` bytes into the struct plus a trailing `'\0'` (`c_str()`, `view()`, `assign()`, implicit `std::string_view`). |
| `std::span<const uint8_t>` | — | `bytes` | Zero-copy view into input on decode. |
| `pb::bytes<N>` | — | `bytes` | Copies up to `N` bytes into the struct (`data()`, `span()`, `assign()`, `push_back()`, implicit `std::span<const uint8_t>`). |
| `std::array<uint8_t, N>` | — | `bytes` | Fixed length: encodes all `N` bytes; shorter input zero-pads; longer input fails with `CAPACITY_EXCEEDED`. |
| `std::span<const uint8_t, N>` | — | `bytes` | Encode-only fixed-extent view. |
| Aggregate `struct` | — | `message` | Nested message. |
| `std::optional<T>` | *(element encoding)* | `optional T` | Explicit presence. |
| `std::variant<std::monostate, A, B, ...>` | *(per-alternative encoding)* | `oneof` | Index `0` is `std::monostate` (unset); alternatives `1..K` may share a C++ type (see [`oneof` fields](#oneof-fields-and-duplicate-alternative-types)). |
| `pb::repeated<T, N>` | *(element encoding)* | `repeated T` | Fixed capacity `N` inside the struct (`push_back()`, `data()`, `span()`, range-`for`, `==`). |
| `std::span<const T>` | *(element encoding)* | `repeated T` | Variable length; decodes into caller scratch memory / `pb::arena`. |
| `std::array<T, N>` (`T` not `uint8_t`) | *(element encoding)* | `repeated T` | Encode-only fixed array of `N` elements. |

Plain `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`, `__int128`, pointers, and `volatile` types do not compile (`pb: unmapped_type`). `const` members compile for `encode` and `encoded_size`, and fail at compile time in `decode` (`pb: decode_const_member`).

### Presence on encode

| Member | Written on the wire when |
| --- | --- |
| Scalar, `string` (`std::string_view`, `pb::string<N>`), `bytes` (`std::span`, `pb::bytes<N>`) | Value is non-zero or non-empty (`-0.0` is non-zero) |
| `std::array<uint8_t, N>` (`FIXED_BYTES`) | `N > 0` (writes all `N` bytes) |
| Nested `struct` | Always |
| `std::optional<T>` | Engaged (including zero or empty values) |
| `std::variant` | An alternative other than `std::monostate` is active |
| Repeated field | Element count is at least 1 (scalars are always packed) |

### `oneof` fields and duplicate alternative types

A `oneof` maps to `std::variant<std::monostate, A, B, ...>`, where index `0` is `std::monostate` (no field set) and `1..K` correspond to the `oneof` alternatives in declaration order. `protoc-gen-micropb` emits 1-based `static constexpr size_t <oneof>_<field>` constants above each `std::variant` member (zero struct size or Flash cost).

* **Distinct alternative types** (`oneof detail { uint32 uptime_s = 8; string fault = 9; }`): you can use either type-based access (`tx.detail = uint32_t{ 3600 }`, `std::get_if<uint32_t>( &rx.detail )`) or index-based access.
* **Repeated alternative types** (`oneof msg { string err = 2; string info = 3; string html = 4; }`, or `int32` and `sint32` in the same `oneof`): standard C++ allows repeated types in `std::variant`, and the codec dispatches strictly by 1-based index. Type-based assignment and `std::get_if<T>` do not compile when `T` appears more than once; use `emplace<Index>`, `std::in_place_index<Index>`, `index()`, and `std::get_if<Index>`:

```cpp
// message Log {
//   uint32 id = 1;
//   oneof msg { string err = 2; string info = 3; string html = 4; }
// }
struct Log
{
	uint32_t id = 0;
	static constexpr size_t msg_err = 1;
	static constexpr size_t msg_info = 2;
	static constexpr size_t msg_html = 3;
	std::variant<std::monostate, std::string_view, std::string_view, std::string_view> msg = {};
};

void handle_log( std::span<uint8_t> wire )
{
	// Construct in place (const-friendly) or assign with emplace<Index>:
	const Log tx{
		.id = 7,
		.msg{ std::in_place_index<Log::msg_err>, "timeout" },
	};
	const auto written = pb::encode( tx, wire );
	if ( !written )
		return;

	Log rx;
	if ( !pb::decode( wire.first( *written ), rx ) )
		return;

	if ( const auto* err = std::get_if<Log::msg_err>( &rx.msg ) )
	{
		// *err is "timeout" (rx.msg.index() == Log::msg_err)
	}
}
```

### `pb::arena` and scratch buffer details

* Before allocating, `decode` scans the remaining message bytes to count all elements of that `std::span<const T>` field, then allocates one aligned contiguous block.
* Multiple `decode` calls can share one `pb::arena`. `memory.clear()` resets usage to 0 and invalidates previously decoded spans.
* If space is insufficient, `decode` returns `pb::error::ARENA_FULL` without writing out of bounds.
* If a non-canonical sender splits one nested submessage across multiple wire records and appends repeated elements in both, `decode` allocates a larger block and copies prior elements; the earlier block stays used until `clear()`.
* Element types in `std::span<const T>` must be trivially copyable and trivially destructible (`pb: span_element_trivial`). `pb::string<N>`, `pb::bytes<N>`, and `pb::repeated<T, N>` (for trivially copyable `T`) satisfy this requirement.

## Code generator (`protoc-gen-micropb`)

`tools/protoc-gen-micropb.py` requires Python 3.8+ and `protoc` (no `protobuf` Python package). On Windows, pass `tools\protoc-gen-micropb.bat` to `--plugin`.

### Naming and layout rules

* Every generated struct member has a proto3 default initializer (`= 0` for integers, `= 0.0f` / `= 0.0` for floats, `= false` for `bool`, `= Enum::ZERO` for enums, and `= {}` for views, messages, optionals, `oneof` variants, bounded containers, and fixed `std::array`s), so unbraced declarations (`T msg;`) start at proto3 defaults.
* Generated headers include `<stdint.h>` and `<stddef.h>` (only when needed) and emit unqualified `int8_t`..`int64_t` and `uint8_t`..`uint64_t` types.
* `sint32`/`sint64` and `fixed32`/`fixed64`/`sfixed32`/`sfixed64` emit plain `int32_t`/`int64_t`/`uint32_t`/`uint64_t` members and record `pb::int_encoding::ZIGZAG` or `pb::int_encoding::FIXED` in `pb::field_at`.
* Each `oneof` emits `static constexpr size_t <oneof>_<field> = 1..K;` constants for its alternatives above the `std::variant` member.
* `package a.b;` maps to `namespace a::b`.
* Nested definitions flatten with underscores: `Outer.Inner` becomes `Outer_Inner`.
* C++ keywords, `pb`, `std`, `<stdint.h>`/`<stddef.h>` typedefs (`uint32_t`, `size_t`, etc.), and common C macros (`NULL`, `EOF`, `errno`, `stdin`, `stdout`, `stderr`, `assert`, `offsetof`) receive a trailing `_`.
* Enumerator prefixes matching `<ENUM_NAME>_` (case-insensitive) are stripped when every value in the enum has the prefix and the remainder starts with a letter (`MODE_IDLE` -> `Mode::IDLE`).
* Structs are emitted in dependency order. Unused `import` statements emit no `#include`.

### `.options` reference

The plugin searches for `<stem>.options` relative to the current directory first, then in each `--micropb_opt=options_dir=<dir>`. Wildcards `*` and `?` are supported (`* max_count:8`); later matching lines override earlier ones.

| Option | Target | Generated C++ type |
| --- | --- | --- |
| `max_count:N` | `repeated` | `pb::repeated<T, N>` |
| `max_count:N fixed_count:true` | `repeated` | `std::array<T, N>` (encode-only) |
| `max_length:N` | `string` (including `repeated string`) | `pb::string<N>` (up to `N` bytes + `'\0'`) |
| `max_size:N` | `bytes` / `string` (including `repeated`) | `pb::bytes<N>` on `bytes`; `pb::string<N - 1>` on `string` (matches NanoPB buffer size including `'\0'`) |
| `max_size:N fixed_length:true` | `bytes` | `std::array<uint8_t, N>` |
| `type:FT_POINTER` | `repeated`, `bytes`, or `string` | `std::span<const T>`, `std::span<const uint8_t>`, or `std::string_view` (overrides wildcard `max_count`/`max_size`/`max_length`) |
| `type:FT_STATIC`, `type:FT_DEFAULT` | `repeated`, `bytes`, `string` | Uses the other options on the field |
| `int_size:IS_8`, `IS_16`, `IS_32`, `IS_64` | `int32`, `int64`, `uint32`, `uint64`, `sint32`, `sint64` | Narrow or wide `intN_t` / `uintN_t` |
| `enum_intsize:IS_8`, `IS_16`, `IS_32` | `enum` | `enum class E : intN_t` (fails generation if a `.proto` enumerator does not fit) |

Inline `.proto` annotations such as `[(nanopb).max_size = 6]` are not read. Unknown options and unmatched patterns print warnings to `stderr`. The plugin rejects proto2/editions files, `map` fields, recursive messages, colliding C++ member names inside a struct, `int_size:IS_8` on repeated unsigned integers, and `enum_intsize:IS_64`. Services, `extend` blocks, and `[packed = false]` are ignored.

## Errors

| `pb::error` | Returned by | Cause |
| --- | --- | --- |
| `BUFFER_TOO_SMALL` | `encode` | Output span cannot hold the encoded message. |
| `TRUNCATED` | `decode` | Input ends mid-field, or a length prefix exceeds remaining bytes. |
| `INVALID_VARINT` | `decode` | Varint exceeds 10 bytes or 64 bits. |
| `INVALID_WIRE_TYPE` | `decode` | Wire type is 3, 4, 6, or 7. |
| `INVALID_TAG` | `decode` | Field number is 0 or exceeds 2^29 - 1. |
| `CAPACITY_EXCEEDED` | `decode` | Input has more elements or bytes than `pb::repeated`, `pb::bytes`, `pb::string`, or `std::array` holds. |
| `ARENA_FULL` | `decode` | Scratch memory / `pb::arena` lacks space for a `std::span<const T>` repeated field. |

## Compile-time checks

Invalid schemas fail compilation with `static_assert` diagnostics prefixed by `pb: <rule>:`.

| Category | Rules |
| --- | --- |
| Field numbers | `tag_zero` (0), `tag_too_large` (> 2^29 - 1), `tag_reserved` (19000–19999), `duplicate_tag` |
| Message & member types | `not_aggregate`, `member_offset_too_large` (> 65535), `unmapped_type`, `zigzag_unsigned`, `fixed_width`, `enum_too_wide` (> 32 bits), `recursive_message`, `span_element_trivial` |
| Invalid nesting | `monostate_member`, `variant_no_monostate`, `variant_repeated_alternative`, `variant_optional_alternative`, `variant_variant_alternative`, `optional_repeated`, `optional_optional`, `optional_variant`, `repeated_repeated`, `repeated_optional`, `repeated_variant` |
| Field lists | `no_field_list`, `field_list_type`, `field_list_offset`, `field_list_alternative`, `field_list_overlap` |
| Decode-only constraints | `decode_encode_only` (`std::array` repeated), `decode_fixed_span` (static-extent `std::span`), `decode_needs_arena`, `decode_const_member` |

## Code size

`tests/size/size_report.cpp` compiles one translation unit (`-Os`, GCC 16.2) that encodes and decodes an envelope `oneof` containing 16 or 32 distinct 8-field message types (`cmake --build .build --target size_report` runs the host measurement):

| Configuration | Arm Cortex-M4 / M33 (`-mthumb`, B) | x86-64 without `.eh_frame` (B) | x86-64 with `.eh_frame` (B) |
| --- | ---: | ---: | ---: |
| 16 message types | **5,340** | 8,101 | 9,645 |
| 32 message types | **6,580** | 9,765 | 11,309 |
| Each additional 8-field type | **77** | 104 | 104 |

For side-by-side measurements against NanoPB, see [Binary size comparison](#binary-size-comparison).

## MicroPB vs. NanoPB

Both libraries target microcontrollers without heap allocation and read the same `.options` file format. NanoPB is written in C99 and supports streaming I/O (`pb_istream_t` / `pb_ostream_t`) and custom `pb_callback_t` hooks; MicroPB is written for C++23, requires contiguous byte spans, and has no streaming or custom callbacks.

### 1. Variable-length fields: no `pb_callback_t`

Consider a message with a variable-length `string` and `repeated int32` (no `max_size` or `max_count`):

```proto
syntax = "proto3";

message Batch {
  string device = 1;
  repeated int32 values = 2;
}
```

**NanoPB** generates `pb_callback_t` members (`typedef struct _Batch { pb_callback_t device; pb_callback_t values; } Batch;`). To encode and decode `Batch`, you write callback functions, context structs, and `void*` casts, and wire function pointers before each call:

```cpp
// NanoPB: context structs + 4 callbacks + per-call pointer setup
struct string_ctx
{
	char* data;
	size_t len;
	size_t cap;
};

struct values_ctx
{
	int32_t* data;
	size_t count;
	size_t cap;
};

bool encode_string( pb_ostream_t* s, const pb_field_iter_t* f, void* const* arg )
{
	const auto* ctx = static_cast<const string_ctx*>( *arg );
	return pb_encode_tag_for_field( s, f ) &&
	       pb_encode_string( s, reinterpret_cast<const pb_byte_t*>( ctx->data ), ctx->len );
}

bool encode_values( pb_ostream_t* s, const pb_field_iter_t* f, void* const* arg )
{
	const auto* ctx = static_cast<const values_ctx*>( *arg );
	for ( size_t i = 0; i < ctx->count; ++i )
	{
		if ( !pb_encode_tag_for_field( s, f ) ||
		     !pb_encode_varint( s, static_cast<uint64_t>( static_cast<int64_t>( ctx->data[i] ) ) ) )
			return false; // writes unpacked tags unless a sizing pass is added
	}
	return true;
}

bool decode_string( pb_istream_t* s, const pb_field_iter_t*, void** arg )
{
	auto* ctx = static_cast<string_ctx*>( *arg );
	if ( s->bytes_left > ctx->cap )
		return false;
	ctx->len = s->bytes_left;
	return pb_read( s, reinterpret_cast<pb_byte_t*>( ctx->data ), ctx->len );
}

bool decode_values( pb_istream_t* s, const pb_field_iter_t*, void** arg )
{
	auto* ctx = static_cast<values_ctx*>( *arg );
	while ( s->bytes_left > 0 )
	{ // assumes packed input; unpacked calls once per item
		uint64_t v = 0;
		if ( ctx->count == ctx->cap || !pb_decode_varint( s, &v ) )
			return false;
		ctx->data[ctx->count++] = static_cast<int32_t>( v );
	}
	return true;
}

// At each encode/decode site: populate string_ctx/values_ctx and assign
// msg.device.funcs, msg.device.arg, msg.values.funcs, msg.values.arg.
// If Batch is inside a `oneof`, pb_decode clears the union with memset and erases
// funcs.decode unless `(nanopb_msgopt).submsg_callback = true` is also configured.
```

**MicroPB** generates standard C++23 view types (`std::string_view` and `std::span<const int32_t>`). There are no callbacks to write or attach, packed and unpacked inputs both work, and `Batch` works unchanged inside `std::optional` or `std::variant` (`oneof`):

```cpp
// Generated by protoc-gen-micropb:
// struct Batch
// {
// 	std::string_view device = {};
// 	std::span<const int32_t> values = {};
// };

// Encode (packed varints written automatically)
const int32_t items[]{ 10, 20, 30 };
const auto written = pb::encode( Batch{ .device = "imu0", .values = items }, wire );

// Decode (`device` views `packet`; `values` is contiguous in `scratch`)
uint8_t scratch[128];
Batch rx;
const auto read = pb::decode( packet, rx, scratch );
```

### 2. Fixed-size fields (no callbacks): no manual `has_x`, `x_count`, or `which_x`

Even when every capacity is fixed in `.options` so NanoPB uses static structs instead of `pb_callback_t`, C struct conventions leave room for silent bugs:

```proto
syntax = "proto3";
package app;

message Sample {
  repeated float values = 1; // .options: max_count:8
  optional sint32 offset = 2;
  optional string label = 3; // .options: max_length:31
}

message Envelope {
  oneof msg {
    Sample sample = 1;
    uint32 ping = 2;
  }
}
```

**NanoPB (static mode)** generates separate `_count`, `has_`, and `which_` fields alongside raw arrays, `char[32]` buffers, and a `union`, plus a `.pb.c` file that must be compiled and linked:

```cpp
// Generated by nanopb:
// typedef struct _app_Sample
// {
// 	pb_size_t values_count;
// 	float values[8];
// 	bool has_offset;
// 	int32_t offset;
// 	bool has_label;
// 	char label[32];
// } app_Sample;
// typedef struct _app_Envelope
// {
// 	pb_size_t which_msg;
// 	union
// 	{
// 		app_Sample sample;
// 		uint32_t ping;
// 	} msg;
// } app_Envelope;

app_Envelope tx = app_Envelope_init_zero;
tx.which_msg = app_Envelope_sample_tag; // forgetting this drops or corrupts the oneof

app_Sample& s = tx.msg.sample;
s.values[0] = 1.5f;
s.values_count = 1;                                   // manual count; no bounds check on values[i]
s.has_offset = true;                                  // forgetting `has_offset = true` silently drops offset
s.offset = -12;
s.has_label = true;
std::strncpy( s.label, "ch0", sizeof( s.label ) - 1 ); // manual strncpy + null-termination care

pb_ostream_t out = pb_ostream_from_buffer( wire.data(), wire.size() );
if ( !pb_encode( &out, app_Envelope_fields, &tx ) )
	return;

app_Envelope rx = app_Envelope_init_zero;
pb_istream_t in = pb_istream_from_buffer( wire.data(), out.bytes_written );
if ( pb_decode( &in, app_Envelope_fields, &rx ) && rx.which_msg == app_Envelope_sample_tag )
{
	// must check rx.msg.sample.has_offset before reading rx.msg.sample.offset
}
```

**MicroPB** encodes presence, union state, and bounds in C++23 types (`pb::repeated`, `std::optional`, `pb::string` or zero-copy `std::string_view`, `std::variant`) and needs no `.pb.c` translation unit:

```cpp
// Generated by protoc-gen-micropb:
// struct Sample
// {
// 	pb::repeated<float, 8> values = {};
// 	std::optional<int32_t> offset = {};
// 	std::optional<pb::string<31>> label = {};
// };
// struct Envelope
// {
// 	static constexpr size_t msg_sample = 1;
// 	static constexpr size_t msg_ping = 2;
// 	std::variant<std::monostate, Sample, uint32_t> msg = {};
// };

const app::Sample s{
	.values = { { 1.5f } },
	.offset = -12,  // sets presence automatically; ZigZag-encoded via pb::fields_of
	.label = "ch0", // compile-time length-checked and null-terminated
};

const auto n = pb::encode( app::Envelope{ .msg = s }, wire ); // sets active variant alternative
if ( !n )
	return;

app::Envelope rx;
if ( pb::decode( std::span( wire ).first( *n ), rx ) )
{
	if ( const auto* sample = std::get_if<app::Sample>( &rx.msg ) )
	{
		// sample->offset is std::optional<int32_t>; sample->label->c_str() is "ch0"
	}
}
```

### Feature comparison

| Area | MicroPB | NanoPB |
| --- | --- | --- |
| Language & integration | Header-only C++23 (5 headers, no `.c` files) | C99 (`pb_common.c`, `pb_encode.c`, `pb_decode.c` + `.pb.c` per `.proto`) |
| Integer scalar members | Plain `int32_t`, `uint32_t`, `int64_t`, `uint64_t` (`int8_t`/`int16_t` with `int_size`) | Plain `int32_t`, `uint32_t`, etc. |
| Variable `string` / `bytes` | `std::string_view` / `std::span<const uint8_t>` (zero-copy view) | `pb_callback_t` (`encode`/`decode` callbacks + `void* arg`) |
| Bounded `string` / `bytes` | `pb::string<N>` (null-terminated) / `pb::bytes<N>` | `char x[N + 1]` / `PB_BYTES_ARRAY_T(N)` |
| Variable `repeated T` | `std::span<const T>` + scratch buffer / `pb::arena` (works inside `oneof`) | `pb_callback_t` (needs `submsg_callback` inside `oneof`) or `FT_POINTER` (`malloc`) |
| Bounded `repeated T` | `pb::repeated<T, N>` (`push_back`, `size`, `span`, range-`for`) | `pb_size_t x_count; T x[N];` |
| `optional T` | `std::optional<T>` | `bool has_x; T x;` |
| `oneof` | `std::variant<std::monostate, A, B>` | `pb_size_t which_x; union { ... } x;` |
| Error handling | `std::expected<size_t, pb::error>` | `bool` + `stream.errmsg` string |
| `.options` file | Same syntax (`max_count`, `max_size`, `max_length`, `fixed_length`, `int_size`) | Native format |
| Generator dependencies | Python 3 stdlib only + `protoc` | Python 3 + `protobuf` Python package + `protoc` |
| Streaming I/O & custom callbacks | No (contiguous spans only, no callbacks) | Yes (`pb_istream_t` / `pb_ostream_t`, `pb_callback_t`) |
| C99, proto2 | No (C++23, proto3) | Yes |

### Binary size comparison

Measured on the `size_report` schema (an envelope `oneof` holding 1, 16, or 32 distinct message types of 8 mixed fields each, both `encode` and `decode` compiled with `-Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables` and linked with `--gc-sections`):

#### Arm Cortex-M4 & Cortex-M33 (`arm-none-eabi-gcc` / `arm-none-eabi-g++` 16.2.0, `-mthumb`)

| Configuration (`-mcpu=cortex-m4 -mthumb`) | 1&nbsp;type | 16&nbsp;types | 32&nbsp;types | Each extra 8-field type |
| --- | ---: | ---: | ---: | ---: |
| **MicroPB** | **4,146 B** | **5,340 B** | **6,580 B** | **77 B** |
| **NanoPB 0.4.9.3** — fixed-size (`max_length:31`, `PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,150 B | 6,414 B | 7,774 B | 85 B |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,210 B | 6,606 B | 8,078 B | 92 B |
| **NanoPB 0.4.9.3** — fixed-size (`max_length:31`, default flags) | 6,616 B | 7,880 B | 9,240 B | 85 B |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (default flags) | 6,676 B | 8,072 B | 9,544 B | 92 B |

*(With `-mcpu=cortex-m33 -mthumb`, MicroPB is identical at `4,146 / 5,340 / 6,580 B` (`77 B/type`); NanoPB with `PB_BUFFER_ONLY` + `PB_NO_ERRMSG` is `2 B` smaller at `5,148 / 6,412 / 7,772 B` fixed and `5,208 / 6,604 / 8,076 B` callback; NanoPB default flags are identical.)*

* **32-bit Arm scaling:** on 32-bit Arm, 4-byte pointers halve MicroPB's subdescriptor and variant tables and Thumb-2 shrinks `std::variant` accessors, so MicroPB is smaller than every NanoPB configuration on both base runtime (`1.0–2.5 KB` smaller at 1 type, `1.2–2.9 KB` smaller at 32 types) and per-message growth (`77 B` vs `85–92 B` per 8-field message type).
* **External symbols:** `arm-none-eabi-nm -u` shows that MicroPB references only `memcpy`, `memmove`, and `memset` (no 64-bit division helpers or C++ runtime symbols).

#### Linux x86-64 (`GCC 16.2.1 -Os`)

| Configuration (no `.eh_frame`) | 1&nbsp;type | 16&nbsp;types | 32&nbsp;types | Each extra 8-field type |
| --- | ---: | ---: | ---: | ---: |
| **MicroPB** | **6,446 B** | **8,101 B** | **9,765 B** | **104 B** |
| **NanoPB 0.4.9.3** — fixed-size (`max_length:31`, `PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 8,405 B | 10,029 B | 11,773 B | 109 B |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 8,489 B | 10,366 B | 12,318 B | 122 B |
| **NanoPB 0.4.9.3** — fixed-size (`max_length:31`, default flags) | 10,413 B | 12,037 B | 13,781 B | 109 B |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (default flags) | 10,497 B | 12,374 B | 14,326 B | 122 B |

*(With Linux default `.eh_frame` unwind tables included, x86-64 totals at 1 / 16 / 32 types are `7,990 / 9,645 / 11,309 B` for MicroPB, `12,469 / 14,093 / 15,837 B` for NanoPB fixed default, and `10,189 / 11,813 / 13,557 B` for NanoPB fixed with `PB_BUFFER_ONLY` + `PB_NO_ERRMSG`.)*

### Encode and decode speed comparison

Measured on Linux x86-64 (`GCC 16.2.1`, AMD Custom CPU 1772, no LTO; not measured on Cortex-M hardware). MicroPB is compiled as header-only C++23 (`-fno-exceptions -fno-rtti`); NanoPB 0.4.9.3 is compiled as C99 (`pb_common.c`, `pb_encode.c`, `pb_decode.c`, plus descriptor `.c`) with the same optimization flag. Every workload is verified to produce byte-for-byte identical wire output before timing. Values are medians of 7 trials (`300k–1M` iterations each) in nanoseconds per operation; speedup is `NanoPB / MicroPB`.

#### `-O2` vs. NanoPB 0.4.9.3 (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`)

| Workload | Wire | MicroPB enc | NanoPB enc | Enc speedup | MicroPB dec | NanoPB dec | Dec speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 mixed scalars | 76 B | **57.2 ns** | 162.7 ns | **2.84x** | **77.3 ns** | 226.3 ns | **2.93x** |
| Sensor `Reading` (mixed) | 73 B | **47.4 ns** | 121.6 ns | **2.57x** | **82.1 ns** | 180.4 ns | **2.20x** |
| 32 packed `int32` (fixed) | 143 B | **177.5 ns** | 369.0 ns | **2.08x** | **127.6 ns** | 208.4 ns | **1.63x** |
| 32 packed `int32` (`span` / `pb_callback_t`) | 143 B | **174.8 ns** | 286.3 ns | **1.64x** | 175.5 ns | **140.8 ns** | 0.80x |
| 32 packed `float` (fixed) | 131 B | **10.8 ns** | 51.7 ns | **4.79x** | **29.9 ns** | 111.0 ns | **3.71x** |
| 32 packed `float` (`span` vs fixed) | 131 B | **11.8 ns** | 53.1 ns | **4.51x** | **33.0 ns** | 110.6 ns | **3.36x** |
| 2 strings + 2 bytes (copied) | 184 B | **31.9 ns** | 83.8 ns | **2.63x** | **48.8 ns** | 85.4 ns | **1.75x** |
| 2 strings + 2 bytes (zero-copy view vs copied) | 184 B | **27.0 ns** | 83.6 ns | **3.10x** | **37.9 ns** | 84.5 ns | **2.23x** |
| `oneof` envelope (16 submessages) | 44 B | **92.3 ns** | 296.2 ns | **3.21x** | **73.8 ns** | 230.3 ns | **3.12x** |
| Sparse 16 scalars (2 non-zero) | 7 B | **38.1 ns** | 142.6 ns | **3.74x** | **32.5 ns** | 158.9 ns | **4.88x** |

*(Against NanoPB with default flags at `-O2`, MicroPB is faster on all 10 workloads: `1.89x–13.92x` on encode and `1.10x–4.79x` on decode.)*

#### `-Os` vs. NanoPB 0.4.9.3 (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`)

| Workload | Wire | MicroPB enc | NanoPB enc | Enc speedup | MicroPB dec | NanoPB dec | Dec speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 mixed scalars | 76 B | **129.8 ns** | 238.6 ns | **1.84x** | **219.0 ns** | 321.2 ns | **1.47x** |
| Sensor `Reading` (mixed) | 73 B | **108.5 ns** | 193.1 ns | **1.78x** | 240.7 ns | 240.7 ns | 1.00x |
| 32 packed `int32` (fixed) | 143 B | **366.1 ns** | 406.6 ns | **1.11x** | **214.1 ns** | 267.3 ns | **1.25x** |
| 32 packed `int32` (`span` / `pb_callback_t`) | 143 B | 358.2 ns | **327.9 ns** | 0.92x | 320.8 ns | **159.6 ns** | 0.50x |
| 32 packed `float` (fixed) | 131 B | **23.4 ns** | 134.5 ns | **5.76x** | **56.8 ns** | 207.5 ns | **3.65x** |
| 32 packed `float` (`span` vs fixed) | 131 B | **22.8 ns** | 135.4 ns | **5.93x** | **56.7 ns** | 208.3 ns | **3.67x** |
| 2 strings + 2 bytes (copied) | 184 B | **74.9 ns** | 120.6 ns | **1.61x** | 142.8 ns | **112.7 ns** | 0.79x |
| 2 strings + 2 bytes (zero-copy view vs copied) | 184 B | **71.1 ns** | 122.1 ns | **1.72x** | 118.7 ns | **115.0 ns** | 0.97x |
| `oneof` envelope (16 submessages) | 44 B | **308.8 ns** | 380.2 ns | **1.23x** | **179.4 ns** | 287.2 ns | **1.60x** |
| Sparse 16 scalars (2 non-zero) | 7 B | **47.3 ns** | 181.7 ns | **3.84x** | **60.2 ns** | 205.3 ns | **3.41x** |

*(Against NanoPB with default flags at `-Os`, MicroPB is `0.99x–7.07x` on encode and `1.11x–4.36x` on decode. Decoding `std::span<const int32_t>` is slower than a single-pass NanoPB `pb_callback_t` because MicroPB scans the input first to count elements and allocate one contiguous block in scratch memory.)*

## Build, sanitizers, and cross-compilation

```sh
cmake -S . -B .build -G Ninja
cmake --build .build
ctest --test-dir .build --output-on-failure
```

* **Windows (MSYS2 UCRT64):** pass `-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe` and put `C:\msys64\ucrt64\bin` first in `PATH`.
* **Sanitizers:** configure with `-DMICROPB_SANITIZE=ON` to run tests under AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize-undefined-trap-on-error` on MinGW).
* **Arm cross-compile checks:** when `arm-none-eabi-g++` is in `PATH` (or installed under `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin` on Windows, or passed via `-DMICROPB_ARM_COMPILERS`), CTest compiles all unit tests for `-mcpu=cortex-m4 -mthumb` (`-DMICROPB_ARM_CPU` overrides the CPU), verifies `no_startup_code` with `arm-none-eabi-nm`, and builds `tests/consumer`.
* **Regenerate checked-in `.pb.hpp` test headers:**
  ```sh
  MICROPB_UPDATE_GENERATED=1 python3 tests/generator/test_plugin.py CheckedInTest
  ```
* **Regenerate `protoc` reference byte vectors (`tests/vectors/vectors.hpp`):**
  ```sh
  cmake -DPROTOC=protoc -P tests/vectors/generate.cmake
  ```

## Status and limits

* **Tested toolchains:** GCC 16.2 on Linux and Windows (MSYS2 UCRT64); compile-tested with Arm GNU Toolchain 13.3, 14.2, and 16.2 (`-mcpu=cortex-m4 -mthumb`, not yet executed on hardware); `protoc` 35.1 and 36.2 with Python 3.13 and 3.14.
* **Out of scope:** proto2, editions, `map` fields, groups, well-known types (`Any`, `Timestamp`), JSON, unknown-field preservation, and streaming I/O.
* **Runtime-only codec:** `encode`, `decode`, and `encoded_size` read members via byte offsets and cannot run in `constexpr` evaluation.
* **Limits:** member offsets must be 65535 bytes or less; recursive message definitions are rejected so stack depth stays bounded.
