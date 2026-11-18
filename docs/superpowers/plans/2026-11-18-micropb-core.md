# micropb core codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build stage 1 of `micropb`: a header-only, `constexpr` proto3 encoder and decoder for plain C++ structs, byte-identical to the Google C++ runtime.

**Architecture:** `wire.hpp` reads and writes wire primitives. `meta_schema.hpp` uses C++26 reflection to make one sorted tag table per message type and to classify member types. `encode.hpp` computes sizes first (approach B), with a compile-time shortcut for types of known size, then writes. `decode.hpp` reads into the structs and skips unknown fields. The tests are `constexpr` functions, checked with `static_assert` and again at runtime. The reference vectors come from `protoc --encode` and are checked in.

**Tech Stack:** C++26, GCC 16.2 (MSYS2 ucrt64), CMake + Ninja, CTest, `protoc` 36.2 (vector generation only).

**Spec:** `docs/superpowers/specs/2026-11-18-micropb-design.md`

## Global Constraints

- Compiler: `C:/msys64/ucrt64/bin/g++.exe`, flags `-std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- Runtime: test executables need `C:\msys64\ucrt64\bin` in `PATH` (libstdc++ DLL).
- Build dir: `.build/` (ignored by git). Commands:
  - `cmake -S . -B .build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`
  - `cmake --build .build`
  - `ctest --test-dir .build --output-on-failure`
- Headers in `src/micropb/`, namespace `pb`, `#pragma once`. Include as `<micropb/xxx.hpp>`.
- Core headers include only: `<cstdint> <cstddef> <span> <string_view> <array> <optional> <variant> <expected> <bit> <type_traits> <utility> <algorithm> <meta>`. No heap, no exceptions, no RTTI.
- House style (`C:\GitHub\CLAUDE.md`): snake_case, SCREAMING_CASE enumerators, `_` member prefix, `[[nodiscard]]` on value returns, public API first and state last, comments in ASD-STE100, full-width `////` section rules.
- No new dependency. `protoc` is approved for generating vectors only; the build never runs it.
- Errors: `std::expected<std::size_t, pb::error>`.
- Commit messages: `type(micropb): summary`, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `jp/micropb`. Do not push.
- Test file names differ from the spec's file list (one test file per header, not only `test_constexpr.cpp`). Task 1 updates the spec's file list.

## Review Focus

1. Payloads of 128 bytes or more: the length prefix is 2 bytes or more, for nested messages and packed fields. Test in Task 4 (`text.long`) and Task 5 (envelope with a long `text`).
2. Output buffer one byte too short: `encode` returns `BUFFER_TOO_SMALL` and does not write past the span. Test in Task 4.
3. Input cut at any byte: `decode` never reads out of bounds. It returns `TRUNCATED`, or success when the cut is at a field boundary. Test in Task 8 (prefix sweep, evaluated in `constexpr`, which rejects any out-of-bounds read).
4. A length prefix larger than the remaining input, also near `2^32` and `2^64`: `TRUNCATED`, no integer overflow. Test in Task 1.
5. Unknown fields of all four wire types between known fields (the PC tool adds a field): the decoder skips them and keeps the known values. Test in Task 6.

---

### Task 1: Build scaffold and wire primitives

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `src/micropb/types.hpp` (only `error`, `wire_type` in this task), `src/micropb/wire.hpp`
- Test: `tests/test_wire.cpp`
- Modify: `README.md` (build commands, compiler, PATH note), `docs/superpowers/specs/2026-11-18-micropb-design.md` (test file list: `test_wire.cpp`, `test_types.cpp`, `test_schema.cpp`, `test_encode.cpp`, `test_decode.cpp`, `test_wire_compat.cpp`, `tests/compile_fail/`, `tests/vectors/`)

**Interfaces:**
- Produces (`types.hpp`):
  - `enum class error { BUFFER_TOO_SMALL, TRUNCATED, INVALID_VARINT, INVALID_WIRE_TYPE, INVALID_TAG, CAPACITY_EXCEEDED };`
  - `enum class wire_type : std::uint8_t { VARINT = 0, FIXED64 = 1, LENGTH_DELIMITED = 2, FIXED32 = 5 };`
- Produces (`wire.hpp`):
  - `constexpr std::size_t varint_size(std::uint64_t value);`
  - `constexpr std::uint32_t zigzag_encode(std::int32_t)`, `constexpr std::uint64_t zigzag_encode(std::int64_t)`, `constexpr std::int32_t zigzag_decode(std::uint32_t)`, `constexpr std::int64_t zigzag_decode(std::uint64_t)`
  - `struct field_key { std::uint32_t number; wire_type type; };`
  - `struct writer` with `constexpr explicit writer(std::span<std::uint8_t> out)`, and `[[nodiscard]] constexpr bool` members `write_varint(std::uint64_t)`, `write_fixed32(std::uint32_t)`, `write_fixed64(std::uint64_t)`, `write_bytes(std::span<const std::uint8_t>)`, `write_key(std::uint32_t number, wire_type)`; `[[nodiscard]] constexpr std::size_t size() const`. A `false` return means the span is full; the writer writes nothing past the span.
  - `struct reader` with `constexpr explicit reader(std::span<const std::uint8_t> in)`, and `[[nodiscard]] constexpr` members `std::expected<std::uint64_t, error> read_varint()`, `std::expected<std::uint32_t, error> read_fixed32()`, `std::expected<std::uint64_t, error> read_fixed64()`, `std::expected<std::span<const std::uint8_t>, error> read_length_delimited()`, `std::expected<field_key, error> read_key()`, `std::expected<void, error> skip(wire_type)`, `bool empty() const`, `std::size_t position() const`.
- The test harness pattern for all test files: each test is `constexpr bool test_name()`; the file ends with `static_assert(test_name());` for each test and `int main()` that returns 1 if any test returns `false` at runtime.

- [ ] **Step 1: Write `CMakeLists.txt`**

`micropb` is an `INTERFACE` library with include dir `src/`. Function `micropb_add_test(name)` builds `tests/<name>.cpp` and registers it with CTest. Add `.build/` to `.gitignore`.

- [ ] **Step 2: Write the failing tests in `tests/test_wire.cpp`**

```cpp
test_varint_size:   varint_size(0) == 1, (127) == 1, (128) == 2, (16383) == 2, (16384) == 3, (UINT64_MAX) == 10
test_zigzag:        zigzag_encode(0) == 0, (-1) == 1, (1) == 2, (-2) == 3,
                    (INT32_MAX) == 0xFFFFFFFE, (INT32_MIN) == 0xFFFFFFFF,
                    (INT64_MIN) == UINT64_MAX; zigzag_decode inverts each
test_write_varint:  150 -> {0x96,0x01}; 300 -> {0xAC,0x02}; UINT64_MAX -> 9 x 0xFF then 0x01
test_write_fixed:   write_fixed32(0x12345678) -> {0x78,0x56,0x34,0x12}; write_fixed64(1) -> {1,0,0,0,0,0,0,0}
test_write_key:     (1, VARINT) -> {0x08}; (2, LENGTH_DELIMITED) -> {0x12}; (16, VARINT) -> {0x80,0x01}
test_writer_full:   writer on 1-byte span: write_varint(300) == false; a guard byte after the span keeps its value
test_read_varint:   {0x96,0x01} -> 150; {0x80} -> TRUNCATED; 11 x 0x80 then 0x01 -> INVALID_VARINT
test_read_key:      {0x08} -> {1, VARINT}; {0x00} -> INVALID_TAG; {0x0B} (type 3) -> INVALID_WIRE_TYPE;
                    {0x0F} (type 7) -> INVALID_WIRE_TYPE; key with number 2^29 -> INVALID_TAG
test_read_length:   {0x02,0xAA,0xBB} -> span of 2 bytes pointing into the input (same address);
                    {0x05,0xAA} -> TRUNCATED;
                    length 0xFFFFFFFF ({0xFF,0xFF,0xFF,0xFF,0x0F}) -> TRUNCATED;
                    length UINT64_MAX (9 x 0xFF, 0x01) -> TRUNCATED
test_skip:          skip(VARINT) on {0x96,0x01}, skip(FIXED32) on 4 bytes, skip(FIXED64) on 8 bytes,
                    skip(LENGTH_DELIMITED) on {0x01,0xAA}: each leaves empty() == true;
                    skip(FIXED64) on 3 bytes -> TRUNCATED
```

- [ ] **Step 3: Run the tests and see them fail**

Run the three build commands. Expected: compile error, `micropb/wire.hpp` not found.

- [ ] **Step 4: Implement `types.hpp` (`error`, `wire_type`) and `wire.hpp`**

`read_key` rejects field numbers 0 and above `2^29 - 1` with `INVALID_TAG`, and wire types 3, 4, 6, 7 with `INVALID_WIRE_TYPE`. `read_length_delimited` compares the length with the remaining bytes before any addition, so a large length cannot overflow.

- [ ] **Step 5: Run the tests and see them pass**

Expected: `100% tests passed`.

- [ ] **Step 6: Commit** — `feat(micropb): add wire primitives and build`

---

### Task 2: Value types

**Files:**
- Modify: `src/micropb/types.hpp`
- Test: `tests/test_types.cpp`

**Interfaces:**
- Produces:
  - `enum class int_encoding { ZIGZAG, FIXED };`
  - `template <typename T, int_encoding E> struct wire_int { T value{}; constexpr wire_int() = default; constexpr wire_int(T v); constexpr operator T() const; };` (implicit conversions both ways)
  - `using sint32 = wire_int<std::int32_t, int_encoding::ZIGZAG>; using sint64 = wire_int<std::int64_t, int_encoding::ZIGZAG>; using fixed32 = wire_int<std::uint32_t, int_encoding::FIXED>; using fixed64 = wire_int<std::uint64_t, int_encoding::FIXED>; using sfixed32 = wire_int<std::int32_t, int_encoding::FIXED>; using sfixed64 = wire_int<std::int64_t, int_encoding::FIXED>;`
  - `struct tag { std::uint32_t value; };`
  - `template <std::uint32_t... Tags> struct oneof_tags {};`
  - `template <typename T, std::size_t N> struct repeated` with `size()`, `capacity()`, `empty()`, `begin()/end()` (const and non-const), `operator[]`, `[[nodiscard]] constexpr bool push_back(const T&)` (`false` when full), `clear()`. State: `std::array<T, N> _items; std::size_t _count = 0;`
  - `template <typename T> struct span_buffer` with `constexpr explicit span_buffer(std::span<T> storage)` and the same members as `repeated`. State: `std::span<T> _storage; std::size_t _count = 0;`

- [ ] **Step 1: Write the failing tests**

```cpp
test_wire_int:     pb::sint32 s = -5; std::int32_t i = s; i == -5; sizeof(pb::sint64) == 8
test_repeated:     pb::repeated<int, 2> r; r.push_back(1) && r.push_back(2) && !r.push_back(3);
                   r.size() == 2; r[1] == 2; r.clear(); r.empty()
test_span_buffer:  std::array<int, 2> storage{}; pb::span_buffer<int> b{storage};
                   b.push_back(7); storage[0] == 7; b.capacity() == 2; b.clear(); b.size() == 0
```

- [ ] **Step 2: Run and see them fail.** Expected: `sint32` is not a member of `pb`.
- [ ] **Step 3: Implement the types in `types.hpp`.**
- [ ] **Step 4: Run and see them pass.**
- [ ] **Step 5: Commit** — `feat(micropb): add wire wrappers and repeated containers`

---

### Task 3: Tag table and type classification

**Files:**
- Create: `src/micropb/meta_schema.hpp`
- Test: `tests/test_schema.cpp`, `tests/compile_fail/*.cpp`
- Modify: `CMakeLists.txt` (compile-fail tests)

**Interfaces:**
- Consumes: Task 2 types.
- Produces:
  - `enum class field_kind { SCALAR, STRING, BYTES, FIXED_BYTES, MESSAGE, OPTIONAL, ONEOF, REPEATED };`
  - `template <typename T> consteval field_kind kind_of();` for a member type. `std::array<std::uint8_t, N>` is `FIXED_BYTES`; `std::span<const std::uint8_t>`, `pb::repeated<std::uint8_t, N>`, `pb::span_buffer<std::uint8_t>` are `BYTES`; other `std::array`, `std::span<const T>`, `pb::repeated`, `pb::span_buffer` are `REPEATED`; any other aggregate class is `MESSAGE`; an unmapped type is a compile error.
  - `template <typename T> consteval wire_type wire_type_of();` for scalar types, by the spec's type table.
  - `template <typename T> consteval bool is_packed();` for the element type of a `REPEATED` field: `true` for scalar elements.
  - `inline constexpr std::size_t no_alternative = std::size_t(-1);`
  - `struct field_info { std::meta::info member; std::uint32_t number; std::size_t alternative; };` — one entry per plain member, one per `std::variant` alternative (index into the variant, `std::monostate` is index 0 and has no entry).
  - `template <typename T> consteval std::span<const field_info> build_tag_table();` — sorted by `number`, made static with `std::define_static_array`.
  - `template <typename T> consteval std::optional<std::size_t> known_size();` — the encoded size of a message of type `T` when its minimum and maximum are equal, else `std::nullopt`.

- [ ] **Step 1: Write the failing tests**

```cpp
struct t_auto  { std::int32_t a; std::int32_t b; [[=pb::tag{10}]] std::int32_t c; std::int32_t d; };
struct t_oneof { std::int32_t a; std::variant<std::monostate, std::int32_t, float> v; std::int32_t b; };
struct t_tags  { [[=pb::oneof_tags<7, 5>{}]] std::variant<std::monostate, std::int32_t, float> v; std::int32_t b; };
struct pose    { std::array<float, 3> pos; std::array<float, 4> rot; };
struct mac     { std::array<std::uint8_t, 6> addr; };
struct holder  { pose p; };
struct plain   { std::int32_t a; };

test_auto_tags:   numbers of t_auto == {1, 2, 10, 11}
test_oneof_tags:  t_oneof == {1, 2, 3, 4}; entry 2 has alternative 1, entry 3 has alternative 2
test_explicit:    t_tags sorted == {5 (v, alt 2), 6 (b), 7 (v, alt 1)}   // b = last listed tag + 1, by the spec
test_kinds:       kind_of<pb::sint32>() == SCALAR; <std::string_view> == STRING;
                  <std::span<const std::uint8_t>> == BYTES; <std::array<std::uint8_t, 6>> == FIXED_BYTES;
                  <pb::repeated<float, 4>> == REPEATED; <pose> == MESSAGE; <std::optional<int>> == OPTIONAL
test_wire_types:  wire_type_of<float>() == FIXED32; <double> == FIXED64; <pb::sfixed64> == FIXED64;
                  <std::int8_t> == VARINT; <bool> == VARINT
test_known_size:  known_size<pose>() == 32; known_size<mac>() == 8; known_size<holder>() == 34;
                  known_size<plain>() == std::nullopt
```

- [ ] **Step 2: Write the compile-fail tests**

One file per rule in `tests/compile_fail/`, each registered as a CTest test that builds an object target with `WILL_FAIL TRUE` (target excluded from `all`, test runs `cmake --build .build --target <name>`). Rules: `duplicate_tag`, `tag_zero`, `tag_too_large` (`2^29`), `tag_reserved` (`19000`), `oneof_tags_count`, `variant_no_monostate`, `variant_repeated_alternative`, `unmapped_type` (`char*`), `span_buffer_in_variant`, `span_buffer_in_optional`, `span_buffer_in_repeated`, `optional_repeated`.

- [ ] **Step 3: Run and see them fail.** Expected: `test_schema` fails to compile; compile-fail tests pass for the wrong reason (header missing). Check that the compile-fail tests fail later for the intended reason: each `static_assert` message names the rule.
- [ ] **Step 4: Implement `meta_schema.hpp`.** Compare annotation types after `std::meta::remove_cvref(std::meta::type_of(a))`.
- [ ] **Step 5: Run and see all tests pass.** Read the build log of one compile-fail test to confirm the `static_assert` message.
- [ ] **Step 6: Commit** — `feat(micropb): add tag table and type classification`

---

### Task 4: Reference vectors and encoding of single fields

**Files:**
- Create: `tests/vectors/vectors.proto`, `tests/vectors/*.txtpb`, `tests/vectors/generate.cmake`, `tests/vectors/vectors.hpp` (generated, checked in), `tests/vectors/messages.hpp`, `src/micropb/encode.hpp`
- Test: `tests/test_encode.cpp`
- Modify: `README.md` (how to make the vectors again)

**Interfaces:**
- Consumes: Task 1 `writer`, Task 3 `build_tag_table`, `kind_of`, `wire_type_of`, `known_size`.
- Produces:
  - `template <typename T> [[nodiscard]] constexpr std::size_t encoded_size(const T& msg);`
  - `template <typename T> [[nodiscard]] constexpr std::expected<std::size_t, error> encode(const T& msg, std::span<std::uint8_t> out);`
  - Generator: `cmake -DPROTOC=C:/msys64/ucrt64/bin/protoc.exe -P tests/vectors/generate.cmake`. For each `tests/vectors/<message>.<case>.txtpb` in sorted order it runs `protoc --encode=micropb.test.<message> vectors.proto` and writes `inline constexpr std::array<std::uint8_t, N> <message>_<case>{...};` in `namespace vectors` into `vectors.hpp`. No timestamps or absolute paths in the output. Temporary files go only under `.build/`.
  - `messages.hpp`: the C++ mirror structs of `vectors.proto`, shared by the encode, decode and compatibility tests.

- [ ] **Step 1: Write `vectors.proto` and the C++ mirrors**

```proto
syntax = "proto3";
package micropb.test;
enum color { COLOR_NONE = 0; COLOR_RED = 1; COLOR_BLUE = 2; }
message scalars { int32 i32 = 1; int64 i64 = 2; uint32 u32 = 3; uint64 u64 = 4; sint32 s32 = 5; sint64 s64 = 6;
                  bool flag = 7; float f = 8; double d = 9; fixed32 fx32 = 10; fixed64 fx64 = 11;
                  sfixed32 sfx32 = 12; sfixed64 sfx64 = 13; color c = 14; int32 i8 = 15; uint32 u16 = 16; }
message text  { string name = 1; bytes data = 2; bytes mac = 3; }
message inner { int32 x = 1; string label = 2; }
message outer { inner child = 1; optional int32 maybe = 2; optional inner maybe_child = 3; int32 gap = 10; }
```

C++: `enum class color : std::int32_t { NONE, RED, BLUE };` `scalars` uses `std::int8_t i8` and `std::uint16_t u16`; `text { std::string_view name; std::span<const std::uint8_t> data; std::array<std::uint8_t, 6> mac; }`; `outer { inner child; std::optional<std::int32_t> maybe; std::optional<inner> maybe_child; [[=pb::tag{10}]] std::int32_t gap; }`.

Every `text` case sets `mac` to 6 bytes, and every `outer` case sets `child`, because the C++ side always writes them.

- [ ] **Step 2: Write the `.txtpb` cases and generate `vectors.hpp`**

Cases: `scalars.zero` (empty), `scalars.max` (each field at its maximum), `scalars.negative` (each signed field `-1`, `f: -0.0`, `c: COLOR_BLUE`), `text.short` (`name: "hi"`, 3 bytes `data`), `text.long` (`name` of 200 `a` characters), `outer.set` (`child {x: 1}`, `maybe: 0`, `maybe_child {}`, `gap: 3`), `outer.unset` (`child {}`).

- [ ] **Step 3: Write the failing tests**

```cpp
test_encode_<case>:      for each case: encode(value, buf) == vectors::<message>_<case>.size()
                         and the bytes are equal; encoded_size(value) == the same size
test_encode_too_small:   encode(text_long_value, span of encoded_size - 1) == unexpected(BUFFER_TOO_SMALL);
                         a guard byte after the span keeps its value
test_encode_order:       struct back { [[=pb::tag{2}]] std::int32_t a; [[=pb::tag{1}]] std::int32_t b; }
                         {a = 1, b = 1} encodes as {0x08, 0x01, 0x10, 0x01}
```

`text.long` checks Review Focus 1 (prefix `0xC8 0x01`). `test_encode_too_small` checks Review Focus 2.

- [ ] **Step 4: Run and see them fail.** Expected: `micropb/encode.hpp` not found.
- [ ] **Step 5: Implement `encoded_size` and `encode` in `encode.hpp`**

The size of a nested message is `known_size<T>()` when it has a value, else the walk. `encode` writes the length prefix of each submessage from `encoded_size` of that submessage. A plain nested message member is always written. `std::optional` is written when engaged, also when its value is zero. `std::array<std::uint8_t, N>` with `N > 0` is always written.

- [ ] **Step 6: Run and see them pass.**
- [ ] **Step 7: Commit** — `feat(micropb): encode single fields and add protoc vectors`

---

### Task 5: Encoding of repeated fields and oneof

**Files:**
- Modify: `src/micropb/encode.hpp`, `tests/vectors/vectors.proto`, `tests/vectors/messages.hpp`, `tests/vectors/vectors.hpp` (generated)
- Create: new `.txtpb` cases
- Test: `tests/test_encode.cpp`

**Interfaces:**
- Consumes: Task 4 `encode`, `encoded_size`.
- Produces: no new public API.

- [ ] **Step 1: Add messages**

```proto
message lists    { repeated int32 ints = 1; repeated float floats = 2; repeated string names = 3;
                   repeated inner items = 4; repeated sint64 zz = 5; }
message pose     { repeated float pos = 1; repeated float rot = 2; }
message ping     { uint32 seq = 1; }
message envelope { uint32 id = 1; oneof payload { pose pose = 2; ping ping = 3; text text = 4; } }
```

C++: `lists` uses `pb::repeated<..., 8>` for every field. `pose { std::array<float, 3> pos; std::array<float, 4> rot; }`. `envelope { std::uint32_t id; std::variant<std::monostate, pose, ping, text> payload; }`.

Cases: `lists.empty`, `lists.full` (8 elements in each field, negative `ints`, `names` with an empty string), `pose.zero` (all 7 floats `0`), `envelope.none` (`id: 1`), `envelope.pose`, `envelope.ping` (`seq: 0` — the alternative is set, so `ping {}` is written), `envelope.long` (`text` with 200-character `name`).

- [ ] **Step 2: Write the failing tests**

```cpp
test_encode_<case>:   the same pattern as Task 4, for each new case
test_encode_spans:    lists with ints given as std::span<const std::int32_t> and std::array<std::int32_t, 3>
                      encode the same bytes as pb::repeated with the same values
test_known_size_used: encoded_size(pose{}) == 32 is a constant expression and equals the walked size
```

- [ ] **Step 3: Run and see them fail.**
- [ ] **Step 4: Implement repeated and oneof in `encode.hpp`**

Packed field: one key, a length prefix, then the values; an empty field is omitted. Unpacked field: one key and value for each element. `std::variant`: write only the active alternative with its tag; `std::monostate` writes nothing; a scalar alternative with a zero value is written.

- [ ] **Step 5: Run and see them pass.**
- [ ] **Step 6: Commit** — `feat(micropb): encode repeated fields and oneof`

---

### Task 6: Decoding of single fields

**Files:**
- Create: `src/micropb/decode.hpp`
- Test: `tests/test_decode.cpp`

**Interfaces:**
- Consumes: Task 1 `reader`, Task 3 table and classification, Task 4 vectors and mirrors.
- Produces: `template <typename T> [[nodiscard]] constexpr std::expected<std::size_t, error> decode(std::span<const std::uint8_t> in, T& msg);` — returns `in.size()` on success.

- [ ] **Step 1: Write the failing tests**

```cpp
test_decode_<case>:     for each Task 4 case: decode(vectors::<case>, msg) == size; msg equals the Task 4 value
test_zero_copy:         after decoding text.short, msg.name.data() points into the vector
test_reset:             decode into a scalars with every field set, from scalars.zero: every field is 0
test_last_wins:         inner from {0x08, 0x01, 0x08, 0x02}: x == 2
test_merge:             outer from child {x: 1} then child {label: "a"} (two field-1 records): child.x == 1, child.label == "a"
test_unknown_skip:      inner {x: 5} with fields 5 (varint), 6 (fixed64), 7 (length-delimited, 3 bytes),
                        8 (fixed32) before and after it: x == 5, result == input size       // Review Focus 5
test_wrong_wire_type:   inner with field 1 as fixed32: x == 0, success
test_narrowing:         i32 field with varint 2^40 + 7 -> i32 == 7; i8 with 300 -> i8 == 44;
                        u16 with 70000 -> u16 == 4464; flag with varint 2 -> true
test_fixed_bytes:       mac with 4 bytes -> {b0, b1, b2, b3, 0, 0}; mac with 7 bytes -> CAPACITY_EXCEEDED;
                        pb::repeated<std::uint8_t, 4> bytes field with 5 bytes -> CAPACITY_EXCEEDED
test_errors:            {0x08} -> TRUNCATED; {0x0B} -> INVALID_WIRE_TYPE; {0x00} -> INVALID_TAG
```

- [ ] **Step 2: Run and see them fail.**
- [ ] **Step 3: Implement `decode` for scalars, strings, bytes, fixed bytes, nested messages and `std::optional`**

`decode` first resets `msg` (value-initialized fields; `span_buffer` keeps its span, count 0). A nested message that occurs again merges into the existing value without a reset. A field number that is not in the table, or a known number with the wrong wire type, is skipped.

- [ ] **Step 4: Run and see them pass.**
- [ ] **Step 5: Commit** — `feat(micropb): decode single fields`

---

### Task 7: Decoding of repeated fields and oneof

**Files:**
- Modify: `src/micropb/decode.hpp`
- Test: `tests/test_decode.cpp`

**Interfaces:**
- Consumes: Task 5 vectors and mirrors, Task 6 `decode`.
- Produces: no new public API.

- [ ] **Step 1: Write the failing tests**

```cpp
test_decode_<case>:      for each Task 5 case: decode gives the Task 5 value
test_unpacked_scalars:   ints given as 3 separate varint records decode to {a, b, c}
test_mixed_packing:      ints as one packed record {1, 2} then one unpacked record 3 -> {1, 2, 3}
test_capacity:           9 ints into pb::repeated<std::int32_t, 8> -> CAPACITY_EXCEEDED (packed and unpacked)
test_packed_bad_length:  floats packed with length 5 -> TRUNCATED
test_span_buffer:        struct sb { pb::span_buffer<std::int32_t> ints; }; storage of 4;
                         decode {1, 2} -> storage[0..1] == {1, 2}, size 2; decode again -> same storage, size reset
test_oneof_last_wins:    envelope with ping then pose -> holds pose
test_oneof_merge:        envelope with ping {seq: 1} then an empty ping -> ping.seq == 1
```

- [ ] **Step 2: Run and see them fail.**
- [ ] **Step 3: Implement repeated and oneof decoding**

A scalar repeated field accepts packed and unpacked records. A message alternative that is already active merges; a different alternative replaces the active one with a value-initialized alternative before it decodes.

- [ ] **Step 4: Run and see them pass.**
- [ ] **Step 5: Commit** — `feat(micropb): decode repeated fields and oneof`

---

### Task 8: Compatibility suite and umbrella header

**Files:**
- Create: `src/micropb/micropb.hpp` (includes `types.hpp`, `wire.hpp`, `meta_schema.hpp`, `encode.hpp`, `decode.hpp`)
- Test: `tests/test_wire_compat.cpp`
- Modify: `README.md` (usage example with the envelope)

**Interfaces:**
- Consumes: everything above, through `<micropb/micropb.hpp>` only.

- [ ] **Step 1: Write the failing tests**

```cpp
test_round_trip:      for each vector: decode, then encode the result -> the same bytes
test_prefix_sweep:    for each vector and each length L < size: decode(first L bytes) returns
                      TRUNCATED, or success with value L                                // Review Focus 3
test_envelope_stream: encode 3 envelopes (pose, ping, text) into one buffer, each with
                      its size; decode each slice and get the same alternatives back
```

- [ ] **Step 2: Run and see them fail.** Expected: `micropb/micropb.hpp` not found.
- [ ] **Step 3: Write `micropb.hpp` and the README example.**
- [ ] **Step 4: Run all tests and see them pass.** Show the full `ctest` output.
- [ ] **Step 5: Commit** — `feat(micropb): add umbrella header and compatibility tests`
