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
	std::optional<pb::string<15>> label = {};
	pb::repeated<float, 4> channels = {};
	pb::bytes<8> token = {};
	std::array<uint8_t, 6> mac = {};
	static constexpr size_t detail_uptime_s = 1;
	static constexpr size_t detail_fault = 2;
	std::variant<std::monostate, uint32_t, std::string_view> detail = {};
};

} // namespace sensor
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

	const auto* uptime = std::get_if<uint32_t>( &rx.detail );
	return rx.id == 1 && rx.temperature_mc == -250 && rx.label == "ambient" && uptime && *uptime == 3600;
}
```

## Why MicroPB

* **Plain C++23 types, no callbacks:** structs use `int32_t`/`uint32_t`, `enum class`, `std::optional`, `std::variant`, `pb::string<N>`, `pb::bytes<N>`, `pb::repeated<T, N>`, `std::string_view`, `std::span`, and `std::array`, default-initialized to their proto3 defaults. No `has_x` booleans, manual union tags, integer wrapper structs, or stream callbacks.
* **Smaller Flash footprint than NanoPB on Arm Cortex-M:** one non-template interpreter handles all messages from compact `constexpr` word tables in ROM. Measured on Arm Cortex-M4 / Cortex-M33 (`arm-none-eabi-gcc 16.2.0 -Os -mthumb --gc-sections`; x86-64 tables in [DETAILS.md](DETAILS.md#binary-size-comparison)):

  | Configuration (Cortex-M4 / M33) | 1&nbsp;type | 16&nbsp;types | 32&nbsp;types | Extra 8-field type |
  | --- | ---: | ---: | ---: | ---: |
  | **MicroPB** | **4,098 B** | **5,386 B** | **6,706 B** | **82 B** |
  | NanoPB 0.4.9.3 — fixed-size (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,150 B | 6,414 B | 7,774 B | 85 B |
  | NanoPB 0.4.9.3 — `pb_callback_t` (`PB_BUFFER_ONLY` + `PB_NO_ERRMSG`) | 5,210 B | 6,606 B | 8,078 B | 92 B |
  | NanoPB 0.4.9.3 — fixed-size (default flags) | 6,616 B | 7,880 B | 9,240 B | 85 B |
  | NanoPB 0.4.9.3 — `pb_callback_t` (default flags) | 6,676 B | 8,072 B | 9,544 B | 92 B |

* **No heap, no exceptions, no RTTI, no startup code:** compiles with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`. Tables live in `.rodata` with zero static constructors before `main()`.
* **Compile-time schema validation:** invalid tags, unmapped types, recursive messages, and missing scratch buffers fail at compile time with named `static_assert` diagnostics (`pb: <rule>:`).
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
| `optional T` / `oneof` | — | `std::optional<T>` / `std::variant<std::monostate, ...>` |

## API

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

## Integration & code generation

**Requirements:** a C++23 compiler (GCC 12+ for `std::expected`), plus Python 3.8+ and `protoc` to generate headers.

* **CMake:** `add_subdirectory(micropb)` and `target_link_libraries(firmware PRIVATE micropb)`.
* **Header-only:** add `src/` to your include path and compile with `-std=c++23`. Include `<micropb/micropb.hpp>` where you call `pb::encode`/`pb::decode`; generated `.pb.hpp` headers include `<micropb/types.hpp>` only.
* **Generate `.pb.hpp` headers** (use `tools\protoc-gen-micropb.bat` on Windows):

  ```sh
  protoc --plugin=protoc-gen-micropb=tools/protoc-gen-micropb.py \
         --micropb_out=gen --micropb_opt=options_dir=proto \
         -I proto proto/sensor.proto
  ```

## Build & test

```sh
cmake -S . -B .build -G Ninja
cmake --build .build
ctest --test-dir .build --output-on-failure
```

## Further reading

See [DETAILS.md](DETAILS.md) for:

* [API behavior, lifetime rules & `pb::arena`](DETAILS.md#api-behavior)
* [Full type mapping, container APIs & `.options` reference](DETAILS.md#type-mapping-and-rules)
* [Runtime `pb::error` codes & compile-time `pb: <rule>:` checks](DETAILS.md#errors)
* [Side-by-side comparison & benchmarks vs. NanoPB](DETAILS.md#micropb-vs-nanopb)
* [Sanitizers, Windows/MSYS2 & Arm Cortex-M cross-compile tests](DETAILS.md#build-sanitizers-and-cross-compilation)

## License

BSD 3-Clause. See [LICENSE](LICENSE).
