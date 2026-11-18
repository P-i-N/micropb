# micropb design

## Intent

`micropb` is a header-only proto3 codec. It sends messages between embedded devices and host PC tools over a USB COM serial port. The C++ structs are the schema. C++26 static reflection gives the field tags and the wire types. There is no `protoc` step for the C++ side and no descriptor tables in ROM.

The first user is the engine on the host PC. The device side is freestanding, so the core must also be correct there from the start.

## Goals

* Encode and decode proto3 wire format from plain C++ structs, byte-identical to `protoc` output for the same message.
* No heap, no exceptions, no RTTI in the core.
* All of encode, decode and `encoded_size` are `constexpr`. Tests can run them in `static_assert`.
* Decoding from a contiguous buffer is zero-copy for `std::string_view` and `std::span<const std::uint8_t>`.
* Generate a `.proto` file from the C++ structs. The PC tools can use it with Google protobuf (Python, C#). This file is the contract between the two sides.

## Non-goals

* Framing (length prefix, COBS, CRC). A different layer does it. `micropb` decodes one complete message from a contiguous buffer.
* Streaming encode or decode, and partial messages.
* `map<K, V>`, proto2, editions, `Any`, well-known types, JSON.
* Retention of unknown fields. The decoder skips them.
* Code size measurement and ARM cross-compilation. These come later.

## Anti-goals

* Any output that is not byte-identical to `protoc` for the same message and values.
* A dependency on a code generator or a runtime library for the C++ side.
* A silent loss of data. Decoding a full container returns an error. It does not drop values.

## Constraints

* Compiler: GCC 16.2 (MSYS2 ucrt64), `-std=c++26 -freflection -fno-exceptions -fno-rtti`. A probe in this session showed that `nonstatic_data_members_of`, `annotations_of`, `extract`, `template for` and splices work. The type of an annotation from `type_of` is cv-qualified (`const pb::tag`). Compare it after `remove_cvref`.
* Standard library only. No `core` library. A new dependency needs approval.
* House style (`C:\GitHub\CLAUDE.md`): snake_case types and functions, SCREAMING_CASE enumerators in `enum class`, `_` prefix on members, `[[nodiscard]]`, `#pragma once`, comments in ASD-STE100.
* Headers go in `src/micropb/`. Namespace `pb`.
* Errors are values: `std::expected<std::size_t, pb::error>`.

## Approaches

The open choice was how the encoder writes the varint length prefix of submessages and packed fields.

* **A. Reserve 1 byte, backpatch with `memmove`.** One pass. Payloads of 128 bytes or more move once per nesting level.
* **B. Compute sizes first.** `encoded_size` walks the message, then the writer writes each prefix with its final value. No `memmove`. The output is in strict order. `encoded_size` is also a public function for buffer sizing.
* **C. `consteval max_wire_size<T>()`.** Works only for bounded types. `std::string_view` and `std::span` have no bound, so it cannot be the general mechanism.

**Decision: B** (the user's choice). Extension (also the user's choice): when a nested message type has a size known at compile time, `encoded_size` uses the constant and does not walk into it.

A message type has a known size only when its minimum and maximum encoded sizes are equal. With proto3 implicit presence, a field with a zero value is not written, so most types do not qualify. The types that qualify:

* a non-empty `std::array<T, N>` of fixed-width scalars (`float`, `double`, `pb::fixed32`, `pb::fixed64`, `pb::sfixed32`, `pb::sfixed64`), written as one packed field;
* a non-empty `std::array<std::uint8_t, N>`, written as `bytes`;
* a nested message whose type has a known size.

A rejected option: an annotation that forces the encoder to write zero fields. It would give more known-size types, but the output would not be byte-identical to `protoc`.

## Public API

All in namespace `pb`. `uint8_t` means `std::uint8_t`.

```cpp
enum class error {
    BUFFER_TOO_SMALL,     // encode: output span too short
    TRUNCATED,            // decode: input ends inside a field
    INVALID_VARINT,       // decode: varint longer than 10 bytes, or overflows its target
    INVALID_WIRE_TYPE,    // decode: wire type 3, 4, 6, 7, or wrong wire type for a known field
    INVALID_TAG,          // decode: field number 0 or above 2^29 - 1
    CAPACITY_EXCEEDED,    // decode: more elements than a repeated container holds
};

template <typename T>
[[nodiscard]] constexpr std::size_t encoded_size(const T& msg);

template <typename T>
[[nodiscard]] constexpr std::expected<std::size_t, error> encode(const T& msg, std::span<uint8_t> out);

template <typename T>
[[nodiscard]] constexpr std::expected<std::size_t, error> decode(std::span<const uint8_t> in, T& msg);
```

`encode` returns the number of bytes written. `decode` returns the number of bytes read, which is `in.size()` on success.

## Type mapping

| C++ type | Proto3 type | Wire type |
| --- | --- | --- |
| `bool` | `bool` | varint |
| `int32_t`, `uint32_t`, `int64_t`, `uint64_t` | `int32`, `uint32`, `int64`, `uint64` | varint |
| `int8_t`, `int16_t` | `int32` | varint |
| `uint8_t`, `uint16_t` | `uint32` | varint |
| `std::array<uint8_t, N>` | `bytes` | length-delimited |
| `enum`, `enum class` | `enum` | varint |
| `pb::sint32`, `pb::sint64` | `sint32`, `sint64` | varint, ZigZag |
| `float`, `pb::fixed32`, `pb::sfixed32` | `float`, `fixed32`, `sfixed32` | fixed32 |
| `double`, `pb::fixed64`, `pb::sfixed64` | `double`, `fixed64`, `sfixed64` | fixed64 |
| `std::string_view` | `string` | length-delimited |
| `std::span<const uint8_t>` | `bytes` | length-delimited |
| other class type (aggregate) | nested `message` | length-delimited |
| `std::optional<T>` | `optional T` | as `T` |
| `std::variant<std::monostate, A, B, ...>` | `oneof` | as each alternative |
| `std::array<T, N>`, `std::span<const T>`, `pb::repeated<T, N>`, `pb::span_buffer<T>` | `repeated T` | packed for scalars, unpacked otherwise |

`pb::sint32` and the other wire wrappers are zero-cost wrappers with implicit conversion to and from the underlying integer.

Negative `int32_t` values encode as 10-byte varints, as `protoc` does. The 8- and 16-bit types encode as their 32-bit proto type. The decoder truncates wider values to the target width, by the same rule as for `int64` -> `int32`.

`std::array<uint8_t, N>` is `bytes` with a fixed length. The encoder omits it only when `N == 0`. On decode, a length other than `N` is `error::CAPACITY_EXCEEDED` when it is longer. When it is shorter, the remaining bytes are 0. (Builder's call.) It has a known size, so it counts for the known-size rule. This also applies to `std::uint8_t` elements in `std::span`, `pb::repeated` and `pb::span_buffer`: these are `bytes` too, not `repeated uint32`.

### Presence

* Plain scalar, string, bytes, repeated: implicit presence. The encoder omits the field when the value is zero or empty.
* Nested message (plain struct member): the encoder always writes it, also when all its fields are zero. `protoc` omits a message field only when it is unset, and a plain struct member is always set. Use `std::optional<msg>` to omit it.
* `std::optional<T>`: explicit presence. The encoder omits it only when it is `std::nullopt`.
* `std::variant`: the encoder writes only the active alternative. `std::monostate` means no alternative.

### Repeated fields

* Encode: `std::array`, `std::span<const T>`, `pb::repeated`, `pb::span_buffer`. Scalars are packed. Strings, bytes and messages are unpacked.
* Decode: `pb::repeated<T, N>` (internal `std::array<T, N>` and a count) and `pb::span_buffer<T>` (a user-supplied `std::span<T>` and a count). The decoder accepts packed and unpacked input for scalars, also mixed in one message, as proto3 requires. `std::array` and `std::span<const T>` are encode-only. A struct that is used for both directions uses `pb::repeated` or `pb::span_buffer`.
* More elements than the capacity: `error::CAPACITY_EXCEEDED`.

## Tag assignment

`build_tag_table<T>()` is `consteval`. It reads `nonstatic_data_members_of(^^T, access_context::current())`.

1. The first field gets tag 1. Each field without an annotation gets the previous tag + 1.
2. `[[=pb::tag{N}]]` sets the tag to `N`. The fields after it continue from `N + 1`.
3. A `std::variant<std::monostate, A, B, ...>` uses one tag for each alternative that is not `std::monostate`, in order. `[[=pb::oneof_tags<TA, TB, ...>{}]]` sets them explicitly. The next field continues from the last tag + 1.

Compile errors (`static_assert` or a `consteval` failure) for: duplicate tags, tag 0, tags above 2^29 - 1, tags 19000-19999 (reserved by protobuf), the wrong number of `oneof_tags`, a `std::variant` without `std::monostate` as its first alternative, and a member type that has no mapping.

The same table is the one source for encode, decode, `encoded_size` and the `.proto` generator. It also stores the member identifiers, which the generator needs.

The encoder writes fields in ascending tag order, as the Google C++ runtime does, also when explicit tags are not in declaration order. (Builder's call.)

## Reference for wire compatibility

The reference is the Google C++ runtime. It decides the edge cases, for example `-0.0` with implicit presence: the C++ runtime writes it, because it compares bit patterns. (Builder's call.)

The reference vectors come from `protoc --encode` (MSYS2 `mingw-w64-ucrt-x86_64-protobuf`, libprotoc 36.2, approved). It uses the C++ runtime, so no Python is necessary.

* `tests/vectors/` holds each `.proto` and text-format input.
* A script in `tests/vectors/` runs `protoc --encode` and writes the bytes into a generated header.
* The generated header is checked in. The build and the tests do not need `protoc`.
* A check: in this session, `protoc --encode` gave `08 ff ff ff ff ff ff ff ff ff 01 10 01 19 00 00 00 00 00 00 00 80` for `int32 a = -1; sint32 b = -1; double c = -0.0;`.

The installed Python `protobuf` package is 6.31.1 and `protoc` is 36.2 (Python gencode 6.36.2). Python code that `protoc` generates needs a runtime that is not older than the generator, so stage 3 must update the Python package or write the plugin without generated `_pb2` modules.

## Generated `.proto` naming

The `.proto` file is the contract with the PC tools. A change to these rules breaks their generated code. (All builder's calls.)

* `package`: the argument to `generate_proto3_schema`.
* Message name: the identifier of the struct, without its C++ namespace. Two structs with the same identifier: compile error.
* Field name: the member identifier.
* `oneof` name: the identifier of the `std::variant` member. Its field names: the identifier of each alternative's type in snake_case. Two alternatives of the same type: compile error.
* Enum value names: `ENUM_NAME_VALUE` in SCREAMING_CASE, because proto3 enum values share the package scope. Example: `rx_mode::FIXED_LENGTH` becomes `RX_MODE_FIXED_LENGTH`.
* An enum without an enumerator of value 0: compile error in the generator, because proto3 requires one.

## Freestanding

The core uses only `<cstdint>`, `<cstddef>`, `<span>`, `<string_view>`, `<array>`, `<optional>`, `<variant>`, `<expected>`, `<bit>`, `<type_traits>`, `<meta>`. No build in this project shows that it compiles freestanding. This stays unverified until the ARM stage. (Builder's call.)

## Decode semantics

* `decode` resets `msg` before it reads: each field gets its value-initialized state, except that each `pb::span_buffer` keeps its storage span and only sets its count to 0. (Builder's call.)
* `pb::span_buffer` is allowed only where the caller can bind storage before `decode`: a member of the top-level message, or of a plain nested struct member. Inside `std::variant`, `std::optional` or an element of a repeated field it is a compile error. Use `pb::repeated<T, N>` there. (Builder's call.)
* The decoder accepts all input that the Google C++ runtime accepts, so the PC tools and the device can change field widths independently. (Builder's call.)
    * A known field with the wrong wire type: skipped as an unknown field.
    * A varint that is too wide for its target: truncated, as protobuf does for `int64` -> `int32`.
    * A `bool` varint other than 0: `true`.
    * Errors stay only for malformed wire data: truncation, varints longer than 10 bytes, invalid wire types, tag 0, capacity.
* Scalar field that occurs more than once: the last value wins.
* Nested message that occurs more than once: the occurrences merge, as protobuf requires.
* `oneof`: the last alternative wins.
* Unknown field numbers: the decoder skips them by wire type.
* Unknown enum values: the decoder keeps the integer value in the enum, which C++ allows. This is proto3 open-enum behavior.
* Recursion depth has a bound at compile time, because a struct cannot contain itself by value. There is no runtime depth limit.

## Files

```text
CMakeLists.txt
src/micropb/
    types.hpp        wire wrappers, repeated, span_buffer, tag, oneof_tags, error
    wire.hpp         constexpr reader and writer, varint, ZigZag, skip_field
    meta_schema.hpp  build_tag_table, type classification, known-size check
    encode.hpp       encoded_size, encode
    decode.hpp       decode
    schema_gen.hpp   host only: C++ structs -> .proto text (uses std::string)
    micropb.hpp      umbrella header, without schema_gen.hpp
tools/
    protoc-gen-micropb.py
tests/
    test_constexpr.cpp
    test_wire_compat.cpp
    test_schema_gen.cpp
```

## Stages

Each stage is a separate, reviewable change.

1. Core: `types.hpp`, `wire.hpp`, `meta_schema.hpp`, `encode.hpp`, `decode.hpp`, CMake, `test_constexpr.cpp`, `test_wire_compat.cpp`. The tests include an envelope message with a `oneof` of all message types. This is the reference use case for the serial link.
2. `.proto` generator: `schema_gen.hpp`, `test_schema_gen.cpp`.
3. `protoc-gen-micropb.py`: a `protoc` plugin that reads an existing `.proto` file and writes C++26 structs. It writes `[[=pb::tag{N}]]` only where field numbers have gaps. Its purpose is to bring existing `.proto` schemas into `micropb`. It needs Python, the `protobuf` Python package and `protoc`. This needs approval as a dependency.

## Left to the builder

* Internal names and the structure of the field table.
* How `wire.hpp` represents the reader and writer cursors.
* The CMake target layout, within the files above.
