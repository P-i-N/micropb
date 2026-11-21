# micropb Implementation & Reference Details

This document covers the API rules, proto3-to-C++23 type mapping, `.options` reference, diagnostics, and build options for `micropb`. For an overview and quick-start examples, see [README.md](README.md).

## Contents

* [API behavior](#api-behavior)
* [Type mapping and rules](#type-mapping-and-rules)
* [Code generator (`protoc-gen-micropb`)](#code-generator-protoc-gen-micropb)
* [Errors](#errors)
* [Compile-time checks](#compile-time-checks)
* [Code size](#code-size)
* [micropb vs. NanoPB](#micropb-vs-nanopb)
* [Build, sanitizers, and cross-compilation](#build-sanitizers-and-cross-compilation)
* [Status and limits](#status-and-limits)

## API behavior

```cpp
template <typename T>
[[nodiscard]] std::size_t pb::encoded_size(const T& msg);

template <typename T>
[[nodiscard]] std::expected<std::size_t, pb::error> pb::encode(const T& msg, std::span<std::uint8_t> out);

template <typename T>
[[nodiscard]] std::expected<std::size_t, pb::error> pb::decode(std::span<const std::uint8_t> in, T& msg);

template <typename T>
[[nodiscard]] std::expected<std::size_t, pb::error> pb::decode(std::span<const std::uint8_t> in, T& msg, pb::arena& memory);
```

### Encode rules

* `encode` writes one proto3 message into `out` in ascending field-number order and returns the byte count written.
* It never writes past `out`. If `out` is too short, it returns `pb::error::BUFFER_TOO_SMALL`, and the partial contents of `out` are not valid.
* `encoded_size` computes the exact byte count using the same code path without writing bytes.

### Decode rules

* `decode` reads one message from `in` and returns `in.size()` on success.
* It resets `msg` before reading, so you can reuse one struct across messages (`pb::repeated` resets its size to 0; `std::span<const T>` becomes empty).
* New `std::optional` values and switched `std::variant` alternatives also start from the proto3 zero/empty state; C++ default member initializers inside them do not persist.
* Unknown field numbers and known fields with unexpected wire types are skipped, preserving forward compatibility.
* Duplicate scalar fields keep the last value; duplicate nested messages merge; duplicate `oneof` fields keep the last alternative; repeated scalar fields accept packed, unpacked, or mixed encodings.
* Varints wider than a narrow integer member truncate to the member width, matching the Google C++ runtime. Any non-zero varint decodes as `true` for `bool`. Unnamed enum values keep their integer value.
* Strings are not checked for valid UTF-8.
* On error, `msg` retains fields decoded before the failure, and arena blocks allocated during that call remain used until `memory.clear()`.

> [!WARNING]
> Decoded `std::string_view` and `std::span<const std::uint8_t>` fields point into `in`. Decoded `std::span<const T>` repeated fields point into `memory`. Keep `in` and `memory` alive and unchanged for as long as you read `msg`.

## Type mapping and rules

### Proto3 to C++23 mapping

| C++ type | proto3 type | Notes |
| --- | --- | --- |
| `bool` | `bool` | |
| `std::int32_t`, `std::int64_t` | `int32`, `int64` | Negative values encode as 10-byte varints. |
| `std::uint32_t`, `std::uint64_t` | `uint32`, `uint64` | |
| `std::int8_t`, `std::int16_t` | `int32` | Narrow storage in RAM; decode truncates to member width. |
| `std::uint8_t`, `std::uint16_t` | `uint32` | Narrow storage in RAM; decode truncates to member width. |
| Standard integer types (`short`, `int`, `long`, `long long`, signed/unsigned) | `int32`/`int64`/`uint32`/`uint64` by size and sign | Prefer `<cstdint>` types across targets (`long` is 32-bit on Windows/Arm and 64-bit on Linux x86-64). |
| `pb::sint32`, `pb::sint64`, `pb::wire_int<T, pb::int_encoding::ZIGZAG>` | `sint32` (1/2/4-byte signed `T`), `sint64` (8-byte signed `T`) | ZigZag varint; compact for negative numbers. Converts implicitly to and from `T`. |
| `pb::fixed32`, `pb::fixed64`, `pb::sfixed32`, `pb::sfixed64` | `fixed32`, `fixed64`, `sfixed32`, `sfixed64` | Fixed 4- or 8-byte little-endian wire encoding. |
| `float`, `double` | `float`, `double` | |
| `enum`, `enum class` (up to 32 bits) | `enum` | `int32` on the wire. |
| `std::string_view` | `string` | Zero-copy view into input on decode. |
| `std::span<const std::uint8_t>` | `bytes` | Zero-copy view into input on decode. |
| `pb::repeated<std::uint8_t, N>` | `bytes` | Copies up to `N` bytes into the struct. |
| `std::array<std::uint8_t, N>` | `bytes` | Fixed length: encodes all `N` bytes; shorter input zero-pads; longer input fails with `CAPACITY_EXCEEDED`. |
| `std::span<const std::uint8_t, N>` | `bytes` | Encode-only fixed-extent view. |
| Aggregate `struct` | `message` | Nested message. |
| `std::optional<T>` | `optional T` | Explicit presence. |
| `std::variant<std::monostate, A, B, ...>` | `oneof` | Each alternative must have a distinct type. |
| `pb::repeated<T, N>` | `repeated T` | Fixed capacity `N` inside the struct. |
| `std::span<const T>` | `repeated T` | Variable length; decodes into `pb::arena`. |
| `std::array<T, N>` (`T` not `uint8_t`) | `repeated T` | Encode-only fixed array of `N` elements. |

Plain `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`, `__int128`, pointers, and `volatile` types do not compile (`pb: unmapped_type`). `const` members compile for `encode` and `encoded_size`, and fail at compile time in `decode` (`pb: decode_const_member`).

### Presence on encode

| Member | Written on the wire when |
| --- | --- |
| Scalar, `string`, `bytes` (`span` or `pb::repeated`) | Value is non-zero or non-empty (`-0.0` is non-zero) |
| `std::array<std::uint8_t, N>` (`FIXED_BYTES`) | `N > 0` (writes all `N` bytes) |
| Nested `struct` | Always |
| `std::optional<T>` | Engaged (including zero or empty values) |
| `std::variant` | An alternative other than `std::monostate` is active |
| Repeated field | Element count is at least 1 (scalars are always packed) |

### `pb::arena` details

* Before allocating, `decode` scans the remaining message bytes to count all elements of that `std::span<const T>` field, then allocates one aligned contiguous block.
* Multiple `decode` calls can share one `pb::arena`. `memory.clear()` resets usage to 0 and invalidates previously decoded spans.
* If space is insufficient, `decode` returns `pb::error::ARENA_FULL` without writing out of bounds.
* If a non-canonical sender splits one nested submessage across multiple wire records and appends repeated elements in both, `decode` allocates a larger block and copies prior elements; the earlier block stays used until `clear()`.
* Element types in `std::span<const T>` must be trivially copyable and trivially destructible (`pb: span_element_trivial`).

## Code generator (`protoc-gen-micropb`)

`tools/protoc-gen-micropb.py` requires Python 3.8+ and `protoc` (no `protobuf` Python package). On Windows, pass `tools\protoc-gen-micropb.bat` to `--plugin`.

### Naming and layout rules

* `package a.b;` maps to `namespace a::b`.
* Nested definitions flatten with underscores: `Outer.Inner` becomes `Outer_Inner`.
* C++ keywords, `pb`, `std`, and common C macros (`NULL`, `EOF`, `errno`, `stdin`, `stdout`, `stderr`, `assert`, `offsetof`) receive a trailing `_`.
* Enumerator prefixes matching `<ENUM_NAME>_` (case-insensitive) are stripped when every value in the enum has the prefix and the remainder starts with a letter (`MODE_IDLE` -> `Mode::IDLE`).
* Structs are emitted in dependency order. Unused `import` statements emit no `#include`.

### `.options` reference

The plugin searches for `<stem>.options` relative to the current directory first, then in each `--micropb_opt=options_dir=<dir>`. Wildcards `*` and `?` are supported (`* max_count:8`); later matching lines override earlier ones.

| Option | Target | Generated C++ type |
| --- | --- | --- |
| `max_count:N` | `repeated` | `pb::repeated<T, N>` |
| `max_count:N fixed_count:true` | `repeated` | `std::array<T, N>` (encode-only) |
| `max_size:N` | `bytes` | `pb::repeated<std::uint8_t, N>` |
| `max_size:N fixed_length:true` | `bytes` | `std::array<std::uint8_t, N>` |
| `type:FT_POINTER` | `repeated` or `bytes` | `std::span<const T>` or `std::span<const std::uint8_t>` (overrides wildcard `max_count`/`max_size`) |
| `type:FT_STATIC`, `type:FT_DEFAULT` | `repeated`, `bytes`, `string` | Uses the other options on the field |
| `int_size:IS_8`, `IS_16`, `IS_32`, `IS_64` | `int32`, `int64`, `uint32`, `uint64`, `sint32`, `sint64` | Narrow/wide `<cstdint>` integer or `pb::wire_int<std::intN_t, pb::int_encoding::ZIGZAG>` |
| `enum_intsize:IS_8`, `IS_16`, `IS_32` | `enum` | `enum class E : std::intN_t` (fails generation if a `.proto` enumerator does not fit) |

Inline `.proto` annotations such as `[(nanopb).max_size = 6]` are not read. Unknown options and unmatched patterns print warnings to `stderr`. The plugin rejects proto2/editions files, `map` fields, recursive messages, duplicate C++ types inside one `oneof`, `int_size:IS_8` on repeated unsigned integers, and `enum_intsize:IS_64`. Services, `extend` blocks, and `[packed = false]` are ignored.

## Errors

| `pb::error` | Returned by | Cause |
| --- | --- | --- |
| `BUFFER_TOO_SMALL` | `encode` | Output span cannot hold the encoded message. |
| `TRUNCATED` | `decode` | Input ends mid-field, or a length prefix exceeds remaining bytes. |
| `INVALID_VARINT` | `decode` | Varint exceeds 10 bytes or 64 bits. |
| `INVALID_WIRE_TYPE` | `decode` | Wire type is 3, 4, 6, or 7. |
| `INVALID_TAG` | `decode` | Field number is 0 or exceeds 2^29 - 1. |
| `CAPACITY_EXCEEDED` | `decode` | Input has more elements or bytes than `pb::repeated` or `std::array` holds. |
| `ARENA_FULL` | `decode` | `pb::arena` lacks space for a `std::span<const T>` repeated field. |

## Compile-time checks

Invalid schemas fail compilation with `static_assert` diagnostics prefixed by `pb: <rule>:`.

| Category | Rules |
| --- | --- |
| Field numbers | `tag_zero` (0), `tag_too_large` (> 2^29 - 1), `tag_reserved` (19000–19999), `duplicate_tag` |
| Message & member types | `not_aggregate`, `member_offset_too_large` (> 65535), `unmapped_type`, `zigzag_unsigned`, `enum_too_wide` (> 32 bits), `recursive_message`, `span_element_trivial` |
| Invalid nesting | `monostate_member`, `variant_no_monostate`, `variant_duplicate_alternative`, `variant_repeated_alternative`, `variant_optional_alternative`, `variant_variant_alternative`, `optional_repeated`, `optional_optional`, `optional_variant`, `repeated_repeated`, `repeated_optional`, `repeated_variant` |
| Field lists | `no_field_list`, `field_list_type`, `field_list_offset`, `field_list_alternative`, `field_list_overlap` |
| Decode-only constraints | `decode_encode_only` (`std::array` repeated), `decode_fixed_span` (static-extent `std::span`), `decode_needs_arena`, `decode_const_member` |

## Code size

`cmake --build .build --target size_report` compiles one translation unit (`-Os`, GCC 16.2.1, Linux x86-64) that encodes and decodes an envelope `oneof` containing 16 or 32 distinct 8-field message types:

| Configuration | Without `.eh_frame` (bytes) | With `.eh_frame` (bytes) |
| --- | ---: | ---: |
| 16 message types | 7,308 | 8,716 |
| 32 message types | 9,229 | 10,637 |
| Each additional 8-field type | 120 | 120 |

For side-by-side measurements against NanoPB, see [Binary size comparison](#binary-size-comparison). Arm hardware size measurements are not yet recorded.

## micropb vs. NanoPB

Both libraries target microcontrollers without heap allocation and read the same `.options` file format. NanoPB is written in C99 and supports streaming I/O (`pb_istream_t` / `pb_ostream_t`) and custom `pb_callback_t` hooks; `micropb` is written for C++23, requires contiguous byte spans, and has no streaming or custom callbacks.

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
struct string_ctx { char* data; size_t len; size_t cap; };
struct values_ctx { int32_t* data; size_t count; size_t cap; };

bool encode_string(pb_ostream_t* s, const pb_field_iter_t* f, void* const* arg) {
    const auto* ctx = static_cast<const string_ctx*>(*arg);
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_string(s, reinterpret_cast<const pb_byte_t*>(ctx->data), ctx->len);
}

bool encode_values(pb_ostream_t* s, const pb_field_iter_t* f, void* const* arg) {
    const auto* ctx = static_cast<const values_ctx*>(*arg);
    for (size_t i = 0; i < ctx->count; ++i) {
        if (!pb_encode_tag_for_field(s, f) ||
            !pb_encode_varint(s, static_cast<uint64_t>(static_cast<int64_t>(ctx->data[i]))))
            return false; // writes unpacked tags unless a sizing pass is added
    }
    return true;
}

bool decode_string(pb_istream_t* s, const pb_field_iter_t*, void** arg) {
    auto* ctx = static_cast<string_ctx*>(*arg);
    if (s->bytes_left > ctx->cap)
        return false;
    ctx->len = s->bytes_left;
    return pb_read(s, reinterpret_cast<pb_byte_t*>(ctx->data), ctx->len);
}

bool decode_values(pb_istream_t* s, const pb_field_iter_t*, void** arg) {
    auto* ctx = static_cast<values_ctx*>(*arg);
    while (s->bytes_left > 0) { // assumes packed input; unpacked calls once per item
        uint64_t v = 0;
        if (ctx->count == ctx->cap || !pb_decode_varint(s, &v))
            return false;
        ctx->data[ctx->count++] = static_cast<int32_t>(v);
    }
    return true;
}

// At each encode/decode site: populate string_ctx/values_ctx and assign
// msg.device.funcs, msg.device.arg, msg.values.funcs, msg.values.arg.
// If Batch is inside a `oneof`, pb_decode clears the union with memset and erases
// funcs.decode unless `(nanopb_msgopt).submsg_callback = true` is also configured.
```

**`micropb`** generates standard C++23 view types (`std::string_view` and `std::span<const std::int32_t>`). There are no callbacks to write or attach, packed and unpacked inputs both work, and `Batch` works unchanged inside `std::optional` or `std::variant` (`oneof`):

```cpp
// Generated by protoc-gen-micropb:
// struct Batch {
//     std::string_view device;
//     std::span<const std::int32_t> values;
// };

// Encode (packed varints written automatically)
const std::array<std::int32_t, 3> items{10, 20, 30};
const auto written = pb::encode(Batch{.device = "imu0", .values = items}, wire);

// Decode (`device` views `packet`; `values` is contiguous in `memory`)
std::array<std::uint8_t, 128> scratch{};
pb::arena memory(scratch);
Batch rx{};
const auto read = pb::decode(packet, rx, memory);
```

### 2. Fixed-size fields (no callbacks): no manual `has_x`, `x_count`, or `which_x`

Even when every capacity is fixed in `.options` so NanoPB uses static structs instead of `pb_callback_t`, C struct conventions leave room for silent bugs:

```proto
syntax = "proto3";
package app;

message Sample {
  repeated float values = 1; // .options: max_count:8
  optional sint32 offset = 2;
  optional string label = 3; // NanoPB .options: max_length:31
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
// typedef struct _app_Sample {
//     pb_size_t values_count; float values[8];
//     bool has_offset; int32_t offset;
//     bool has_label; char label[32];
// } app_Sample;
// typedef struct _app_Envelope {
//     pb_size_t which_msg;
//     union { app_Sample sample; uint32_t ping; } msg;
// } app_Envelope;

app_Envelope tx = app_Envelope_init_zero;
tx.which_msg = app_Envelope_sample_tag;            // forgetting this drops or corrupts the oneof

app_Sample& s = tx.msg.sample;
s.values[0] = 1.5f;
s.values_count = 1;                                // manual count; no bounds check on values[i]
s.has_offset = true;                               // forgetting `has_offset = true` silently drops offset
s.offset = -12;
s.has_label = true;
std::strncpy(s.label, "ch0", sizeof(s.label) - 1); // copies into 32-byte buffer in every Sample

pb_ostream_t out = pb_ostream_from_buffer(wire.data(), wire.size());
if (!pb_encode(&out, app_Envelope_fields, &tx))
    return;

app_Envelope rx = app_Envelope_init_zero;
pb_istream_t in = pb_istream_from_buffer(wire.data(), out.bytes_written);
if (pb_decode(&in, app_Envelope_fields, &rx) && rx.which_msg == app_Envelope_sample_tag) {
    // must check rx.msg.sample.has_offset before reading rx.msg.sample.offset
}
```

**`micropb`** encodes presence, union state, and bounds in standard C++23 types, needs no `max_length` buffer for `string`, and needs no `.pb.c` translation unit:

```cpp
// Generated by protoc-gen-micropb:
// struct Sample {
//     pb::repeated<float, 8> values;
//     std::optional<pb::sint32> offset;
//     std::optional<std::string_view> label;
// };
// struct Envelope {
//     std::variant<std::monostate, Sample, std::uint32_t> msg;
// };

app::Sample s{};
if (!s.values.push_back(1.5f)) // bounds-checked
    return;
s.offset = -12;                // sets presence automatically
s.label = "ch0";               // zero-copy view; no char[32] buffer in struct

const auto n = pb::encode(app::Envelope{.msg = s}, wire); // sets active variant alternative
if (!n)
    return;

app::Envelope rx{};
if (pb::decode(std::span<const std::uint8_t>(wire).first(*n), rx)) {
    if (const auto* sample = std::get_if<app::Sample>(&rx.msg)) {
        // sample->offset is std::optional<pb::sint32>; sample->label views `wire`
    }
}
```

### Feature comparison

| Area | micropb | NanoPB |
| --- | --- | --- |
| Language & integration | Header-only C++23 (5 headers, no `.c` files) | C99 (`pb_common.c`, `pb_encode.c`, `pb_decode.c` + `.pb.c` per `.proto`) |
| Variable `string` / `bytes` | `std::string_view` / `std::span<const std::uint8_t>` (zero-copy view) | `pb_callback_t` (`encode`/`decode` callbacks + `void* arg`) |
| Fixed `string` | Not needed (`std::string_view` has no length cap) | `char x[N + 1]` in every struct (`max_length:N`) |
| Variable `repeated T` | `std::span<const T>` + `pb::arena` (works inside `oneof`) | `pb_callback_t` (needs `submsg_callback` inside `oneof`) or `FT_POINTER` (`malloc`) |
| Fixed `repeated T` | `pb::repeated<T, N>` (`push_back`, `size`, range-`for`) | `pb_size_t x_count; T x[N];` |
| `optional T` | `std::optional<T>` | `bool has_x; T x;` |
| `oneof` | `std::variant<std::monostate, A, B>` | `pb_size_t which_x; union { ... } x;` |
| Error handling | `std::expected<std::size_t, pb::error>` | `bool` + `stream.errmsg` string |
| `.options` file | Same syntax (`max_count`, `max_size`, `fixed_length`, `int_size`) | Native format |
| Generator dependencies | Python 3 stdlib only + `protoc` | Python 3 + `protobuf` Python package + `protoc` |
| Streaming I/O & custom callbacks | No (contiguous spans only, no callbacks) | Yes (`pb_istream_t` / `pb_ostream_t`, `pb_callback_t`) |
| C99, proto2 | No (C++23, proto3) | Yes |

### Binary size comparison

Measured on the `size_report` schema (an envelope `oneof` holding 1, 16, or 32 distinct message types of 8 mixed fields each, both `encode` and `decode` linked with `--gc-sections`, GCC 16.2.1 `-Os`, Linux x86-64):

**Code + read-only data (`-fno-asynchronous-unwind-tables -fno-unwind-tables`, no `.eh_frame`):**

| Configuration | 1&nbsp;type | 16&nbsp;types | 32&nbsp;types | Each extra 8-field type |
| --- | ---: | ---: | ---: | ---: |
| **micropb** | **5,497** | **7,308** | **9,229** | 120 |
| **NanoPB 0.4.9.3** — fixed-size (`max_length:31`, default flags) | 10,413 | 12,037 | 13,781 | **109** |
| **NanoPB 0.4.9.3** — fixed-size (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 8,405 | 10,029 | 11,773 | **109** |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (default flags) | 10,497 | 12,374 | 14,326 | 122 |
| **NanoPB 0.4.9.3** — `pb_callback_t` strings (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 8,489 | 10,366 | 12,318 | 122 |

*(With Linux default `.eh_frame` unwind tables included, totals at 1 / 16 / 32 types are `6,901 / 8,712 / 10,633 B` for `micropb`, `12,469 / 14,093 / 15,837 B` for NanoPB fixed default, and `10,189 / 11,813 / 13,557 B` for NanoPB fixed with `PB_BUFFER_ONLY` + `PB_NO_ERRMSG`.)*

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

* **Tested toolchains:** GCC 16.2 on Linux and Windows (MSYS2 UCRT64); compile-tested with Arm GNU Toolchain 13.3, 14.2, and 16.2 (`-mcpu=cortex-m4 -mthumb`, not yet executed on hardware); `protoc` 35.1 and 36.2 with Python 3.13 and 3.14. Clang currently warns on `[[gnu::noclone]]` in `codec.hpp`.
* **Out of scope:** proto2, editions, `map` fields, groups, well-known types (`Any`, `Timestamp`), JSON, unknown-field preservation, and streaming I/O.
* **Runtime-only codec:** `encode`, `decode`, and `encoded_size` read members via byte offsets and cannot run in `constexpr` evaluation.
* **Limits:** member offsets must be 65535 bytes or less; recursive message definitions are rejected so stack depth stays bounded.
