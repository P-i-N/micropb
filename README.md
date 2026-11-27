# MicroPB

A header-only proto3 encoder and decoder for embedded C++23.

## Quick start

Define your schema in `.proto` and bound any fixed-capacity fields in a NanoPB-compatible `.options` sidecar file:

```proto
// sensor.proto
syntax = "proto3";
package sensor;

enum Mode {
  MODE_IDLE = 0;
  MODE_RUN = 1;
}

message Reading {
  uint32 id = 1;
  sint32 temperature_mc = 2;
  Mode mode = 3;
  optional string label = 4;   // sensor.options: Reading.label    max_length:15
  repeated float channels = 5; // sensor.options: Reading.channels max_count:4
  bytes token = 6;             // sensor.options: Reading.token    max_size:8
  bytes mac = 7;               // sensor.options: Reading.mac      max_size:6 fixed_length:true
  oneof detail {
    uint32 uptime_s = 8;
    string fault = 9;
  }
}
```

`protoc-gen-micropb` generates plain C++23 aggregates — using standard `<stdint.h>` types, Protobuf default member initializers, and no wrapper classes — plus a compile-time `pb::fields_of` table:

```cpp
// sensor.pb.hpp (generated)
namespace sensor {

enum class Mode : int32_t
{
	IDLE = 0,
	RUN = 1,
};

struct Reading
{
	uint32_t id = 0;
	int32_t temperature_mc = 0;
	Mode mode = Mode::IDLE;
	pb::optional<pb::string<15>> label = {};
	pb::repeated<float, 4> channels = {};
	pb::bytes<8> token = {};
	std::array<uint8_t, 6> mac = {};
	pb::oneof<uint32_t, std::string_view> detail = {};
};

} // namespace sensor

// Generated compile-time traits (field tags, oneof indices, enum min/max, max_encoded_size):
// pb::traits<sensor::Mode>::min / ::max
// pb::traits<sensor::Reading>::id_tag, ::detail_uptime_s, ::has_max_encoded_size, ::max_encoded_size
```

Encode, size, and decode any message with three functions (`pb::encode`, `pb::decode`, `pb::encoded_size`):

```cpp
#include "sensor.pb.hpp"

#include <array>
#include <span>
#include <stdint.h>

#include <micropb/micropb.hpp>

bool round_trip()
{
	uint8_t wire[64]{};

	const sensor::Reading tx{
		.id = 1,
		.temperature_mc = -250,
		.mode = sensor::Mode::RUN,
		.label = "ambient",
		.channels = { { 3.31f, 3.29f } },
		.token = { { 0xAA, 0xBB } },
		.mac = { 0x02, 0x00, 0x5E, 0x10, 0x00, 0x01 },
		.detail = uint32_t{ 3600 },
	};

	const auto written = pb::encode( tx, wire ); // std::expected<size_t, pb::error>
	if ( !written )
		return false;

	sensor::Reading rx;
	if ( !pb::decode( std::span( wire ).first( *written ), rx ) )
		return false;

	const auto* uptime = pb::get_if<uint32_t>( &rx.detail );
	return rx.id == 1 && rx.temperature_mc == -250 && rx.label == "ambient" && uptime && *uptime == 3600;
}
```

## Why MicroPB

* **Plain C++23 types, no callbacks:** structs use `int32_t`/`uint32_t`, `enum class`, `pb::optional`, `pb::oneof`, `pb::string<N>`, `pb::bytes<N>`, `pb::repeated<T, N>`, `pb::map<K, V, N>`, `std::string_view`, `std::span`, and `std::array`, default-initialized to their proto3 defaults. No `has_x` booleans, manual union tags, integer wrapper structs, or stream callbacks.
* **Smaller Flash footprint than NanoPB on Arm Cortex-M:** one non-template interpreter handles all messages from compact `constexpr` halfword tables in ROM. By default, MicroPB optimizes for minimum ROM size; define `MICROPB_FAST` to enable speed-oriented fast paths. Measured on Arm Cortex-M4 / Cortex-M33 (`arm-none-eabi-gcc 16.2.0 -Os -mthumb --gc-sections`; x86-64 tables in [DETAILS.md](DETAILS.md#binary-size-comparison)):

  | Configuration (Cortex-M4 / M33) | 1&nbsp;type | 16&nbsp;types | 32&nbsp;types | Extra 8-field type |
  | --- | ---: | ---: | ---: | ---: |
  | **MicroPB (default)** | **3,456&nbsp;B** | **4,144&nbsp;B** | **4,824&nbsp;B** | **42&nbsp;B** |
  | **MicroPB (`MICROPB_FAST`)** | **4,142&nbsp;B** | **4,830&nbsp;B** | **5,510&nbsp;B** | **42&nbsp;B** |
  | NanoPB 0.4.9.3 — fixed-size (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,150&nbsp;B | 6,414&nbsp;B | 7,774&nbsp;B | 85&nbsp;B |
  | NanoPB 0.4.9.3 — `pb_callback_t` (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,210&nbsp;B | 6,606&nbsp;B | 8,078&nbsp;B | 92&nbsp;B |
  | NanoPB 0.4.9.3 — fixed-size (default flags) | 6,616&nbsp;B | 7,880&nbsp;B | 9,240&nbsp;B | 85&nbsp;B |
  | NanoPB 0.4.9.3 — `pb_callback_t` (default flags) | 6,676&nbsp;B | 8,072&nbsp;B | 9,544&nbsp;B | 92&nbsp;B |

* **Faster encode and decode than NanoPB:** measured on Linux x86-64 (`GCC 16.2.1 -O2`, median ns/op, byte-identical wire output) with `MICROPB_FAST` against NanoPB 0.4.9.3 (`PB_BUFFER_ONLY + PB_NO_ERRMSG`; default-mode, `-Os`, and NanoPB default-flag tables in [DETAILS.md](DETAILS.md#encode-and-decode-speed-comparison)):

  | Workload | Wire | MicroPB (`MICROPB_FAST`) enc | NanoPB enc | MicroPB (`MICROPB_FAST`) dec | NanoPB dec |
  | --- | ---: | ---: | ---: | ---: | ---: |
  | 12 mixed scalars | 76 B | **49 ns** (3.4x) | 165 ns | **87 ns** (2.6x) | 222 ns |
  | Sensor `Reading` (above) | 73 B | **43 ns** (2.9x) | 124 ns | **79 ns** (2.2x) | 175 ns |
  | 32 packed `int32` (fixed) | 143 B | **183 ns** (2.0x) | 370 ns | **112 ns** (2.1x) | 230 ns |
  | 32 packed `float` (fixed) | 131 B | **12 ns** (4.5x) | 53 ns | **36 ns** (3.2x) | 115 ns |
  | `oneof` envelope (16 submsgs) | 44 B | **77 ns** (3.8x) | 287 ns | **80 ns** (2.8x) | 223 ns |

* **No heap, no exceptions, no RTTI, no startup code:** compiles with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`. Tables live in `.rodata` with zero static constructors before `main()`.
* **Compile-time safety checks:** encode-only `std::array` decodes, missing scratch buffers, and table/offset limits fail at compile time with named `static_assert` diagnostics (`pb: <rule>:`).
* **Wire-identical to `protoc` & zero external dependencies:** verified byte-for-byte against `protoc`, using 5 standard-library-only C++23 headers and a single-file Python 3 plugin (stdlib only).

> [!NOTE]
> **Contiguous buffers only:** MicroPB reads from and writes to contiguous byte spans. Streaming I/O (`pb_istream_t` / `pb_ostream_t`) and user callbacks (`pb_callback_t`) are not supported.

## Zero-copy views & scratch buffers

When a field has no `max_length`, `max_size`, or `max_count` in `.options`, `protoc-gen-micropb` emits standard C++23 views:

* `string` / `bytes` become `std::string_view` and `std::span<const uint8_t>`, pointing directly into the input buffer on decode.
* `repeated T` becomes `std::span<const T>`, allocated as a single contiguous block in a caller-owned scratch buffer (`uint8_t[N]`, `std::array<uint8_t, N>`, `std::span<uint8_t>`, or `pb::arena` when sharing across multiple `decode` calls):

```cpp
struct Batch
{
	std::string_view device = {};          // views `packet` directly
	std::span<const int32_t> samples = {}; // allocated in `scratch` on decode
};

void on_packet( std::span<const uint8_t> packet )
{
	uint8_t scratch[256]; // or std::array<uint8_t, 256>, std::span<uint8_t>, or pb::arena

	Batch rx;
	if ( pb::decode( packet, rx, scratch ) )
	{
		// `rx` is valid while `packet` and `scratch` remain alive
	}
}
```

## Proto3 to C++23 mapping

| `.proto` field | `.options` | Generated C++23 type |
| --- | --- | --- |
| `int32`, `sint32`, `sfixed32` | *(optional `int_size:IS_8/16/32/64`)* | `int32_t` (`int8_t`, `int16_t`, `int64_t`) |
| `uint32`, `fixed32` | *(optional `int_size:IS_8/16/32/64`)* | `uint32_t` (`uint8_t`, `uint16_t`, `uint64_t`) |
| `int64`, `sint64`, `sfixed64` / `uint64`, `fixed64` | *(optional `int_size`)* | `int64_t` / `uint64_t` |
| `bool`, `float`, `double` | — | `bool`, `float`, `double` |
| `enum E` | *(optional `enum_intsize:IS_8/16/32`)* | `enum class E : int32_t` |
| `string` | *(none)* / `max_length:N` (`max_size:N+1`) | `std::string_view` / `pb::string<N>` (null-terminated) |
| `bytes` | *(none)* / `max_size:N` / `max_size:N fixed_length:true` | `std::span<const uint8_t>` / `pb::bytes<N>` / `std::array<uint8_t, N>` |
| `repeated T` | *(none)* / `max_count:N` / `max_count:N fixed_count:true` | `std::span<const T>` / `pb::repeated<T, N>` / `std::array<T, N>` |
| `map<K, V>` | *(none)* / `max_count:N` / `max_count:N fixed_count:true` | `std::span<const pb::kv_pair<K, V>>` / `pb::map<K, V, N>` / `std::array<pb::kv_pair<K, V>, N>` |
| `message T` | *(none)* / `type:FT_POINTER` (auto on cycles) | `pb::optional<T>` / `const T*` (allocated in scratch/arena) |
| `optional T` / `oneof` | — | `pb::optional<T>` / `pb::oneof<...>` |

## API

```cpp
template <typename T>
struct pb::traits; // messages: <field>_tag, <oneof>_<field>, has_max_encoded_size, max_encoded_size; enums: min, max

template <typename T>
[[nodiscard]] size_t pb::encoded_size( const T& msg );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::encode( const T& msg, std::span<uint8_t> out );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::encode( const T& msg, pb::writer& sink );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in, T& msg );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in,
                                                           T& msg,
                                                           std::span<uint8_t> memory );

template <typename T>
[[nodiscard]] std::expected<size_t, pb::error> pb::decode( std::span<const uint8_t> in, T& msg, pb::arena& memory );
```

## Integration & code generation

**Requirements:** a C++23 compiler (GCC 12+ for `std::expected`), plus Python 3.8+ and `protoc` to generate headers.

* **CMake:** `add_subdirectory(micropb)` and `target_link_libraries(firmware PRIVATE micropb)`.
* **Header-only:** add `src/` to your include path and compile with `-std=c++23`. Include `<micropb/micropb.hpp>` where you call `pb::encode`/`pb::decode`; generated `.pb.hpp` headers include `<micropb/schema.hpp>` (types and `pb::traits`, no codec).
* **Generate `.pb.hpp` headers** (use `tools\protoc-gen-micropb.bat` on Windows):

  ```sh
  protoc --plugin=protoc-gen-micropb=tools/protoc-gen-micropb.py \
         --micropb_out=gen --micropb_opt=options_dir=proto \
         -I proto proto/sensor.proto
  ```
  Pass `--micropb_opt=namespace_prefix=foo::bar` to prepend a C++ namespace prefix to generated headers.

## Build & test

```sh
cmake -S . -B .build -G Ninja
cmake --build .build
ctest --test-dir .build --output-on-failure
```

## Limitations & trade-offs

* **Contiguous buffers only:** MicroPB encodes into and decodes from contiguous `std::span` buffers. Streaming I/O and user callbacks (`pb_callback_t`) are not supported.
* **Proto3 subset:** no proto2, editions, groups, well-known types (`Any`, `Timestamp`), JSON, or unknown-field preservation (unknown fields are skipped on decode).
* **Bounded recursion depth (`MICROPB_MAX_DEPTH`):** recursive messages are supported automatically (`std::span<const T>` for unbounded `repeated`; `const T*` for singular and `map`-value cycle fields, or explicit `type:FT_POINTER`) up to `MICROPB_MAX_DEPTH` nested submessages (default `32`; deeper inputs or cyclic pointer graphs return `pb::error::MAX_DEPTH_EXCEEDED`).
* **C++23 and runtime-only codec:** requires C++23 (`std::expected`). `pb::encode`, `pb::decode`, and `pb::encoded_size` access members by byte offset and cannot run in `constexpr` evaluation.
* **Compact table limits vs. `MICROPB_WIDE_TABLES`:** default compact tables support field numbers up to `4095`, member offsets up to `4095` bytes, up to `73` distinct compound types per message, and one `pb::oneof` of up to `32` alternatives per message. Define `MICROPB_WIDE_TABLES` to support field numbers up to `2^29 - 1`, offsets up to `65535` bytes, `255` compound types, multiple `pb::oneof` members, and `255` alternatives per `oneof`, at the cost of larger ROM tables.
* **ROM size vs. speed (`MICROPB_FAST`):** the default build minimizes Flash/ROM (`3,456 B` base on Cortex-M4) with linear field lookup and element-by-element packed loops. Define `MICROPB_FAST` (`+686 B` on Cortex-M4) to enable inline scalar dispatch, cursor field lookup, 1-byte varint fast path, and bulk packed scalar encode/decode.
* **Two-pass `std::span<const T>` decode:** decoding a dynamic `repeated` span scans the input first to count elements and allocate one contiguous block in scratch memory, which is slower than a single-pass callback.

## Further reading

See [DETAILS.md](DETAILS.md) for:

* [API behavior, lifetime rules & `pb::arena`](DETAILS.md#api-behavior)
* [Full type mapping, container APIs & `.options` reference](DETAILS.md#type-mapping-and-rules)
* [Runtime `pb::error` codes & compile-time `pb: <rule>:` checks](DETAILS.md#errors)
* [Side-by-side comparison & benchmarks vs. NanoPB](DETAILS.md#micropb-vs-nanopb)
* [Sanitizers, Windows/MSYS2 & Arm Cortex-M cross-compile tests](DETAILS.md#build-sanitizers-and-cross-compilation)

## License

BSD 3-Clause. See [LICENSE](LICENSE).
