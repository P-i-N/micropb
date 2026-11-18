# micropb table-driven codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the template encoder and decoder of `micropb` with one interpreter that reads descriptor tables, which C++26 reflection builds for each message type at compile time.

**Architecture:** `codec.hpp` has two parts. Part 1 builds constant descriptors with reflection: one value descriptor for each distinct type, one field table for each message. Part 2 is a set of `inline` functions that are not templates. They encode, decode and reset a message from its descriptors through `void` pointers and byte offsets. `encode.hpp` and `decode.hpp` go away. `wire.hpp` and the tag table in `meta_schema.hpp` stay.

**Tech Stack:** C++26, GCC 16.2 with `-freflection`, CMake + Ninja, CTest, GNU `size`.

**Spec:** `docs/superpowers/specs/2026-11-18-micropb-table-codec-design.md` (amends `docs/superpowers/specs/2026-11-18-micropb-design.md`).

## Global Constraints

- Flags: `-std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror`. Standard library only. No heap, no exceptions, no RTTI. The library stays header-only.
- Build dir `.build/` (ignored by git). Commands, with GCC 16.2 as the default compiler (on MSYS2 add `-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`):
  - `cmake -S . -B .build -G Ninja`
  - `cmake --build .build`
  - `ctest --test-dir .build --output-on-failure`
- The descriptor tables are built with reflection: `std::meta` queries (`nonstatic_data_members_of`, `offset_of`, `type_of`, `template_arguments_of`), `template for`, splices, `std::define_static_array`. No `offsetof`, no `std::index_sequence` folds, no macros in the library. Tests can use what they like.
- No assumption about the layout of `std::optional`, `std::variant`, or of `std::span<const T>` for different `T`. `std::string_view` and `std::span<const std::uint8_t>` are copied as complete objects with `std::memcpy`.
- No second implementation of encode or decode. No `if consteval` path.
- The bytes on the wire do not change. `tests/vectors/vectors.hpp` is not edited.
- All descriptors are `constexpr` objects with constant initialization. No descriptor contains `std::meta::info`.
- `wire.hpp` and the public API of `types.hpp` do not change.
- Each `pb: <rule>:` message of the existing compile-fail tests stays word for word.
- House style: snake_case, SCREAMING_CASE enumerators, `_` member prefix, `[[nodiscard]]` on value returns, `#pragma once`, comments in ASD-STE100, full-width `////` section rules, includes in the order of the existing headers.
- Two GCC 16.2 facts from this session's probes:
  - In a function that also runs at run time, do not use a member of a `constexpr field_info` directly in run-time code. Copy it first: `constexpr std::uint32_t number = field.number;`. Without this, GCC makes the complete function `consteval`.
  - A counter that only a `template for` body changes gives `-Wunused-but-set-variable` when the loop has no elements (a message without fields). Mark it `[[maybe_unused]]`.
- Commit messages: `type(micropb): summary`, ending with `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`. Branch `jp/micropb-table-codec`. Do not push.

## Review Focus

1. A message object that is decoded two times: the second decode must not show elements of a repeated message field from the first. The reset only sets the count to 0, so the decoder must reset each new element. Test in Task 4 (`test_reuse_repeated_message`).
2. A `oneof` in an object that is decoded two times: the second decode must not keep the alternative or its values from the first. Test in Task 4 (`test_reuse_oneof`).
3. An output buffer of each length from 0 to one byte too short, for a message with nested messages: `encode` returns `BUFFER_TOO_SMALL` and writes nothing past the buffer. The writer changes to count-only mode in the middle of the message. Test in Task 3 (`test_encode_each_short_buffer`).
4. A message type without fields, at the top level and nested: its field table has length 0. Tests in Task 2 (descriptor), Task 3 and Task 4.
5. A `pb::span_buffer` with no storage: decode of one element returns `CAPACITY_EXCEEDED` and does not write through a null pointer; decode of empty input gives success. Test in Task 4 (`test_span_buffer_no_storage`).

---

### Task 1: Branch and base commits

**Files:**
- Commit as they are: `src/micropb/wire.hpp`, `src/micropb/encode.hpp`, `src/micropb/decode.hpp`, `tests/test_wire.cpp`
- Commit as they are: `docs/superpowers/specs/2026-11-18-micropb-table-codec-design.md`, `docs/superpowers/plans/2026-11-18-micropb-table-codec.md`

**Interfaces:**
- Produces: the sticky-state cursors that all later tasks use. `pb::writer`: `write_varint`, `write_fixed32`, `write_fixed64`, `write_bytes(const B&)`, `write_key`, `advance(std::size_t)`, `counts_only()`, `size()`, `fits()`; a default-constructed writer only counts. `pb::reader`: `read_varint`, `read_fixed32`, `read_fixed64`, `read_length_delimited`, `read_key`, `skip`, `fail(error)`, `absorb(const reader&)`, `failed()`, `status()`, `empty()`.

- [ ] **Step 1: Make the branch.** `git switch -c jp/micropb-table-codec`. The working tree changes come with it.
- [ ] **Step 2: Verify the base.** Configure, build, run `ctest`. Expected: `100% tests passed, 0 tests failed out of 34`.
- [ ] **Step 3: Commit the cursors**, the four source files only: `refac(micropb): keep the error state in reader and writer`.
- [ ] **Step 4: Commit the two documents:** `docs(micropb): add table codec design and plan`.

---

### Task 2: Descriptors

**Files:**
- Create: `src/micropb/codec.hpp` (part 1 only), `tests/test_codec.cpp`, `tests/compile_fail/bit_field.cpp`, `tests/compile_fail/member_offset_too_large.cpp`
- Modify: `src/micropb/meta_schema.hpp` (`field_kind`, `check_message_type`), `CMakeLists.txt`

**Interfaces:**
- Consumes: `pb::kind_of<T>()`, `pb::build_tag_table<T>()`, `pb::field_info`, `pb::no_alternative`, `pb::detail::alternatives<V>()`, `pb::detail::element_t<S>`, `pb::detail::is_instance_of<T>(templ)` from `meta_schema.hpp`.
- Produces, in `meta_schema.hpp`: `enum class field_kind : std::uint8_t` (same enumerators, new underlying type).
- Produces, in `codec.hpp`, namespace `pb::detail`:

```cpp
// Scalar code: how the interpreter loads and stores a scalar.
inline constexpr std::uint8_t scalar_size_mask = 0x03; // log2 of the size in bytes
inline constexpr std::uint8_t scalar_signed = 0x04;    // sign-extend on load
inline constexpr std::uint8_t scalar_zigzag = 0x08;    // sint32, sint64
inline constexpr std::uint8_t scalar_fixed = 0x10;     // fixed32 or fixed64 wire type, by size
inline constexpr std::uint8_t scalar_bool = 0x20;
inline constexpr std::uint8_t scalar_enum = 0x40;      // int32 on the wire
template <typename T> consteval std::uint8_t scalar_code_of();

struct message_desc; struct sequence_desc; struct optional_desc; struct variant_desc;

struct value_desc {
    field_kind kind;
    std::uint8_t scalar; // SCALAR: scalar code
    union {
        std::uint32_t size;             // FIXED_BYTES: N. Also the active member for SCALAR and STRING (0).
        const message_desc* message;    // MESSAGE
        const sequence_desc* sequence;  // REPEATED and BYTES. Null for the view std::span<const std::uint8_t>.
        const optional_desc* optional;  // OPTIONAL
        const variant_desc* variant;    // ONEOF
    };
};

struct field_desc {
    std::uint32_t number;
    std::uint16_t offset;      // of the member in the message, in bytes
    std::uint16_t alternative; // 0 for a plain member, else the index in the std::variant
    const value_desc* type;    // of the member; for an alternative, of the std::variant
};

struct message_desc { const field_desc* fields; std::uint16_t count; }; // sorted by number

struct sequence_view { void* data; std::size_t count; std::size_t capacity; std::size_t* stored_count; };
using sequence_accessor = sequence_view (*)(void* sequence);
enum class sequence_storage : std::uint8_t { REPEATED, ARRAY, ACCESSOR };
struct sequence_desc {
    const value_desc* element;
    sequence_accessor access;   // ACCESSOR: std::span<const T> and pb::span_buffer<T>. Else null.
    std::uint32_t element_size;
    std::uint32_t capacity;     // REPEATED and ARRAY: N
    std::uint16_t count_offset; // REPEATED: offset of the count member
    sequence_storage storage;
};

enum class optional_op : std::uint8_t { GET, ENGAGE, RESET };
using optional_accessor = void* (*)(void* optional, optional_op op);
struct optional_desc { const value_desc* value; optional_accessor access; };

using variant_accessor = void* (*)(void* variant, std::size_t index, bool select);
struct variant_desc { const value_desc* const* alternatives; variant_accessor access; }; // one entry for each alternative

template <typename T> struct value_descriptor { static const value_desc value; };
template <typename T> struct message_descriptor { static const message_desc value; };
```

Accessor contracts:
- `sequence_accessor`: `stored_count` is the address of the count that decode changes, or null for the encode-only `std::span<const T>` (there `capacity == count`).
- `optional_accessor`: `GET` returns the address of the value, or null when empty. `ENGAGE` gives an empty optional a value-initialized value and returns the address of the value. `RESET` empties it and returns null.
- `variant_accessor`: returns the address of alternative `index` when it is active. Else, with `select`, it makes alternative `index` active with a value-initialized value and returns its address. Else null. `access(v, 0, true)` resets the variant.

Scalar codes: `bool` = size + `scalar_bool`. Enum = size + `scalar_enum`, + `scalar_signed` when the underlying type is signed. Signed integer = size + `scalar_signed`. Unsigned integer = size. `sint32`, `sint64` = size + `scalar_signed` + `scalar_zigzag`. `float`, `double`, `fixed32`, `fixed64`, `sfixed32`, `sfixed64` = size + `scalar_fixed`.

- [ ] **Step 1: Write `tests/test_codec.cpp`** and add it with `micropb_add_test(test_codec)`. It includes `<micropb/codec.hpp>` and has these `static_assert` checks (`d::` is `pb::detail::`, `K::` is `pb::field_kind::`):

```cpp
struct plain { std::uint8_t a; std::uint32_t b; double c; };
struct nothing {};
struct holder {
    std::uint32_t id;
    [[= pb::tag{5}]] plain child;
    std::optional<float> maybe;
    pb::repeated<float, 4> floats;
    pb::repeated<std::uint8_t, 16> blob;
    std::array<std::uint8_t, 6> mac;
    std::span<const std::int32_t> view;
    std::string_view name;
    nothing none;
    std::variant<std::monostate, plain, float> choice;
};
constexpr const d::message_desc& p = d::message_descriptor<plain>::value;
constexpr const d::message_desc& h = d::message_descriptor<holder>::value;

// plain: three scalars at offsets 0, 4, 8, numbers 1, 2, 3
static_assert(p.count == 3 && p.fields[0].offset == 0 && p.fields[1].offset == 4 && p.fields[2].offset == 8);
static_assert(p.fields[2].number == 3 && p.fields[2].type->kind == K::SCALAR && p.fields[0].alternative == 0);
static_assert(p.fields[1].type == &d::value_descriptor<std::uint32_t>::value);

// holder: 11 entries, numbers 1 5 6 7 8 9 10 11 12 13 14
static_assert(h.count == 11 && h.fields[1].number == 5 && h.fields[8].number == 12 && h.fields[10].number == 14);
static_assert(h.fields[1].type->kind == K::MESSAGE && h.fields[1].type->message == &p);
static_assert(h.fields[2].type->kind == K::OPTIONAL &&
              h.fields[2].type->optional->value == &d::value_descriptor<float>::value &&
              h.fields[2].type->optional->access != nullptr);
static_assert(h.fields[3].type->kind == K::REPEATED &&
              h.fields[3].type->sequence->storage == d::sequence_storage::REPEATED &&
              h.fields[3].type->sequence->capacity == 4 && h.fields[3].type->sequence->element_size == 4 &&
              h.fields[3].type->sequence->element == &d::value_descriptor<float>::value);
static_assert(h.fields[4].type->kind == K::BYTES && h.fields[4].type->sequence->capacity == 16);
static_assert(h.fields[5].type->kind == K::FIXED_BYTES && h.fields[5].type->size == 6);
static_assert(h.fields[6].type->kind == K::REPEATED &&
              h.fields[6].type->sequence->storage == d::sequence_storage::ACCESSOR &&
              h.fields[6].type->sequence->access != nullptr);
static_assert(h.fields[7].type->kind == K::STRING);
static_assert(h.fields[8].type->kind == K::MESSAGE && h.fields[8].type->message->count == 0);
static_assert(h.fields[9].alternative == 1 && h.fields[10].alternative == 2 &&
              h.fields[9].offset == h.fields[10].offset && h.fields[9].type == h.fields[10].type &&
              h.fields[9].type->kind == K::ONEOF &&
              h.fields[9].type->variant->alternatives[1] == &d::value_descriptor<plain>::value &&
              h.fields[9].type->variant->alternatives[2] == &d::value_descriptor<float>::value);

static_assert(d::value_descriptor<std::span<const std::uint8_t>>::value.kind == K::BYTES &&
              d::value_descriptor<std::span<const std::uint8_t>>::value.sequence == nullptr);
static_assert(d::value_descriptor<std::array<float, 3>>::value.sequence->storage == d::sequence_storage::ARRAY &&
              d::value_descriptor<std::array<float, 3>>::value.sequence->capacity == 3);

static_assert(d::scalar_code_of<bool>() == (0 | d::scalar_bool));
static_assert(d::scalar_code_of<std::int8_t>() == (0 | d::scalar_signed));
static_assert(d::scalar_code_of<std::uint16_t>() == 1 && d::scalar_code_of<std::uint64_t>() == 3);
static_assert(d::scalar_code_of<pb::sint32>() == (2 | d::scalar_signed | d::scalar_zigzag));
static_assert(d::scalar_code_of<float>() == (2 | d::scalar_fixed) && d::scalar_code_of<pb::sfixed64>() == (3 | d::scalar_fixed));
enum class small : std::uint8_t { ZERO };
enum class level : std::int32_t { ZERO };
static_assert(d::scalar_code_of<small>() == (0 | d::scalar_enum));
static_assert(d::scalar_code_of<level>() == (2 | d::scalar_enum | d::scalar_signed));

static_assert(sizeof(d::field_desc) <= (sizeof(void*) == 4 ? 12 : 16));
static_assert(sizeof(d::value_desc) <= 2 * sizeof(void*));
```

  `main` runs three run-time tests of the accessors, each through the function pointer in the descriptor:
  - `test_optional_accessor`: on `std::optional<plain>`: `GET` is null; `ENGAGE` returns non-null and the optional has a value with `b == 0`; a second `ENGAGE` after `b = 7` keeps 7; `GET` returns the same address; `RESET` returns null and the optional is empty.
  - `test_variant_accessor`: on `std::variant<std::monostate, plain, float>`: `access(v, 1, false)` is null; `access(v, 1, true)` makes index 1 active; after `b = 7`, `access(v, 1, true)` keeps 7; `access(v, 2, false)` is null; `access(v, 0, true)` makes index 0 active.
  - `test_sequence_accessor`: on `std::span<const std::int32_t>` of 3 items: `count == 3`, `capacity == 3`, `data` is the address of the first item, `stored_count == nullptr`. On `pb::span_buffer<std::int32_t>` over 4 items with 2 pushed: `count == 2`, `capacity == 4`, `*stored_count == 2`, and `*stored_count = 3` makes `size() == 3`.

- [ ] **Step 2: Run** `cmake --build .build`. Expected: FAIL, `micropb/codec.hpp: No such file or directory`.
- [ ] **Step 3: Give `field_kind` the underlying type `std::uint8_t`** in `meta_schema.hpp`.
- [ ] **Step 4: Write part 1 of `codec.hpp`:** the types above, `scalar_code_of`, the three accessor function templates, and the `consteval` builders of `value_descriptor<T>::value` and `message_descriptor<T>::value`.
  - Build the field table from `build_tag_table<T>()` with `template for`, and keep it with `std::define_static_array`. Do the same for the alternatives of a `std::variant`.
  - Get the offsets of the private members of `pb::repeated` with `nonstatic_data_members_of(^^R, access_context::unchecked())` and `offset_of`. A `static_assert` checks that the items are at offset 0.
  - The variant accessor walks `alternatives<V>()` with `template for` and uses `emplace<alternative>` and `std::get_if<alternative>` by type. The rule `variant_duplicate_alternative` makes each type unique.
  - A member offset above 65535 is a `static_assert` failure with the message `pb: member_offset_too_large: a member must be at an offset of 65535 bytes or less`.
- [ ] **Step 5: Add the rule `bit_field`** to `detail::check_message_type` in `meta_schema.hpp`, with `std::meta::is_bit_field`: `pb: bit_field: a message member cannot be a bit-field`.
- [ ] **Step 6: Add the two compile-fail tests** in the form of `tests/compile_fail/tag_zero.cpp`, and their names to the `foreach` list in `CMakeLists.txt`. `bit_field.cpp`: `struct msg { std::uint32_t a : 3; };` and `static_assert(pb::build_tag_table<msg>().size() == 1);`. `member_offset_too_large.cpp`: includes `<micropb/codec.hpp>`, `struct msg { std::array<std::uint8_t, 70000> big; std::uint32_t after; };` and `static_assert(pb::detail::message_descriptor<msg>::value.count == 2);`.
- [ ] **Step 7: Build and run `ctest`.** Expected: `100% tests passed, 0 tests failed out of 37`.
- [ ] **Step 8: Commit:** `feat(micropb): build descriptor tables with reflection`.

---

### Task 3: Encode from the descriptors

**Files:**
- Modify: `src/micropb/codec.hpp` (start of part 2), `src/micropb/micropb.hpp`, `src/micropb/meta_schema.hpp`, `tests/test_encode.cpp`, `tests/test_wire_compat.cpp`, `tests/test_schema.cpp`
- Delete: `src/micropb/encode.hpp`

**Interfaces:**
- Consumes: Task 2's descriptors and accessors; `pb::writer` and `pb::varint_size` from `wire.hpp`.
- Produces, in `codec.hpp`. None of the `detail` functions is a template:

```cpp
namespace pb::detail {
[[nodiscard]] inline std::uint64_t load_scalar(const void* value, std::uint8_t code); // the value as the wire carries it
[[nodiscard]] inline wire_type wire_of(const value_desc& type); // OPTIONAL: of its value. SCALAR: by code. Else LENGTH_DELIMITED.
[[nodiscard]] inline sequence_view view_of(const sequence_desc& sequence, void* object);
inline void emit_value(writer& sink, std::uint32_t number, const value_desc& type, const void* value, bool always);
inline void emit_message(writer& sink, const message_desc& message, const void* object);
[[nodiscard]] inline std::size_t encoded_size(const message_desc& message, const void* object);
[[nodiscard]] inline std::expected<std::size_t, error> encode(const message_desc& message, const void* object,
                                                              std::span<std::uint8_t> out);
} // namespace pb::detail

template <typename T> [[nodiscard]] std::size_t pb::encoded_size(const T& msg);
template <typename T> [[nodiscard]] std::expected<std::size_t, error> pb::encode(const T& msg, std::span<std::uint8_t> out);
```

Encode rules, the same as the template encoder that this task removes (`encode.hpp` at the base commit is the reference):
- `load_scalar`: load 1, 2, 4 or 8 bytes with `std::memcpy` into a typed temporary; sign-extend when `scalar_signed`; for `scalar_enum` convert to `std::int32_t` and sign-extend to 64 bits; for `scalar_zigzag` apply ZigZag.
- Scalar: omitted when `!always` and the loaded value is 0 (so `-0.0` is written). `scalar_fixed` writes `write_fixed32` or `write_fixed64` by size, else `write_varint`.
- String, bytes: omitted when `!always` and empty. Fixed-length bytes: omitted only when `N == 0`.
- Nested message: always written. When the writer can write (`!counts_only()`), measure the fields with a second writer that only counts, write the length, then the fields. When it only counts, walk the fields one time and add `varint_size` of their size.
- `std::optional`: written when it has a value, with `always == true`.
- Repeated: nothing when the count is 0. Scalar elements: one packed field; the length is `count * size` for `scalar_fixed`, else the sum of the varint sizes. Other elements: one field for each element with `always == true`.
- `oneof`: for a field with `alternative != 0`, ask the variant accessor for that alternative without `select`; write it with `always == true` when the result is not null.
- `encode` returns `BUFFER_TOO_SMALL` when `!fits()`, else `size()`.

- [ ] **Step 1: Change the tests to run-time tests.** In `tests/test_encode.cpp` and `tests/test_wire_compat.cpp`: include `<micropb/codec.hpp>` or `<micropb/micropb.hpp>`, remove `constexpr` from the test functions and their helpers, remove all `static_assert(test_...())` lines, and remove the comment block about `std::string_view` and `constexpr` in `test_wire_compat.cpp`. `main` stays. Remove `test_known_size_used` from `test_encode.cpp`, and `test_known_size` with the types that only it uses from `tests/test_schema.cpp`.
- [ ] **Step 2: Add these tests to `tests/test_encode.cpp`** and to its `main`:

```cpp
struct nothing {};
struct holds_nothing { nothing n; std::uint32_t x; };
bool test_encode_empty_message() {  // Review Focus 4
    constexpr std::array<std::uint8_t, 4> expected{0x0a, 0x00, 0x10, 0x01};
    return matches(holds_nothing{{}, 1}, expected) && encoded_size(nothing{}) == 0;
}

struct level3 { std::string_view s; };
struct level2 { level3 c; };
struct level1 { level2 c; };
bool test_encode_deep() {  // three length prefixes of two bytes each
    const std::string text(200, 'x');  // #include <string>, test only
    std::array<std::uint8_t, 256> buffer{};
    const auto size = encode(level1{{{text}}}, buffer);
    constexpr std::array<std::uint8_t, 10> head{0x0a, 0xce, 0x01, 0x0a, 0xcb, 0x01, 0x0a, 0xc8, 0x01, 'x'};
    return size && *size == 209 && encoded_size(level1{{{text}}}) == 209 &&
           std::equal(head.begin(), head.end(), buffer.begin()) && buffer[208] == 'x';
}

template <typename T>
bool each_short_buffer_fails(const T& value) {  // Review Focus 3
    const std::size_t size = encoded_size(value);
    for (std::size_t length = 0; length < size; ++length) {
        std::array<std::uint8_t, 512> buffer{};
        buffer.fill(guard);
        const auto result = encode(value, std::span(buffer).first(length));
        if (result || result.error() != error::BUFFER_TOO_SMALL)
            return false;
        if (!std::all_of(buffer.begin() + length, buffer.end(), [](std::uint8_t b) { return b == guard; }))
            return false;
    }
    return true;
}
bool test_encode_each_short_buffer() {
    return each_short_buffer_fails(text_long()) && each_short_buffer_fails(envelope{4, text_long()}) &&
           each_short_buffer_fails(lists_full());
}

struct buffers { pb::span_buffer<std::int32_t> ints; pb::span_buffer<std::uint8_t> bytes; };
bool test_encode_span_buffer() {
    std::array<std::int32_t, 4> int_storage{};
    std::array<std::uint8_t, 4> byte_storage{};
    buffers value{pb::span_buffer<std::int32_t>(int_storage), pb::span_buffer<std::uint8_t>(byte_storage)};
    for (std::int32_t i : {1, -2, 3}) static_cast<void>(value.ints.push_back(i));
    for (std::uint8_t b : {7, 8, 9}) static_cast<void>(value.bytes.push_back(b));
    constexpr std::array<std::uint8_t, 19> expected{0x0a, 0x0c, 0x01, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                    0xff, 0xff, 0x01, 0x03, 0x12, 0x03, 7,    8,    9};
    return matches(value, expected);
}
```

- [ ] **Step 3: Run** `cmake --build .build`. Expected: FAIL in `test_encode.cpp`, `encode` and `encoded_size` are not declared.
- [ ] **Step 4: Write the encode part of `codec.hpp`** (signatures and rules above), after a `////` rule under part 1. Add `#include <cstring>`.
- [ ] **Step 5: Delete `src/micropb/encode.hpp`.** In `micropb.hpp` replace its include with `<micropb/codec.hpp>`. In `meta_schema.hpp` remove `known_size`, `detail::known_field_size`, `detail::delimited_size` and `detail::fixed_width`.
- [ ] **Step 6: Build and run `ctest`.** Expected: `100% tests passed, 0 tests failed out of 37`. `test_decode` still uses the template decoder.
- [ ] **Step 7: Commit:** `feat(micropb): encode from descriptor tables`.

---

### Task 4: Decode and reset from the descriptors

**Files:**
- Modify: `src/micropb/codec.hpp`, `src/micropb/meta_schema.hpp`, `src/micropb/micropb.hpp`, `tests/test_decode.cpp`, `tests/test_codec.cpp`, `tests/compile_fail/decode_encode_only.cpp`, `CMakeLists.txt`
- Create: `tests/compile_fail/decode_const_member.cpp`
- Delete: `src/micropb/decode.hpp`

**Interfaces:**
- Consumes: Task 2's descriptors and accessors, Task 3's `wire_of` and `view_of`, `pb::reader`.
- Produces, in `meta_schema.hpp`: `template <typename T> consteval void pb::detail::check_decodable();`
- Produces, in `codec.hpp`. None of the `detail` functions is a template:

```cpp
namespace pb::detail {
inline void store_scalar(void* value, std::uint8_t code, std::uint64_t bits);
inline void reset_value(const value_desc& type, void* value);
inline void reset_message(const message_desc& message, void* object);
inline void decode_value(reader& in, wire_type wire, const value_desc& type, void* value);
inline void decode_message(reader& in, const message_desc& message, void* object);
[[nodiscard]] inline std::expected<std::size_t, error> decode(const message_desc& message,
                                                              std::span<const std::uint8_t> in, void* object);
} // namespace pb::detail

template <typename T> [[nodiscard]] std::expected<std::size_t, error> pb::decode(std::span<const std::uint8_t> in, T& msg);
```

Decode rules, the same as the template decoder that this task removes (`decode.hpp` at the base commit is the reference), with the two changes of the spec:
- `pb::decode<T>` calls `check_decodable<T>()`, then `detail::decode`. `detail::decode` resets the message, reads it, and returns `in.size()` or the error of the reader.
- `store_scalar`: `scalar_zigzag` decodes ZigZag (from the low 32 bits when the size is 4); `scalar_bool` stores `bits != 0`; then store the low 1, 2, 4 or 8 bytes with `std::memcpy` from a typed temporary.
- `decode_message`: read a key; stop when the reader failed; find the field with that number by a linear search that starts after the entry of the last match; no match: `skip`.
- A field with a wire type other than `wire_of` its type: `skip`. Exception: a repeated scalar field also accepts `LENGTH_DELIMITED` (packed), and each other repeated field accepts only `LENGTH_DELIMITED`.
- A failed read does not change a scalar, a string or a bytes field.
- Bytes in `pb::repeated` or `pb::span_buffer`: longer than the capacity is `CAPACITY_EXCEEDED`; else copy and set the count. Fixed-length bytes: longer than `N` is `CAPACITY_EXCEEDED`; shorter fills the remainder with 0.
- Nested message: read from a second reader over the length-delimited bytes, then `absorb`. It is not reset first, so a second occurrence merges.
- `std::optional`: `ENGAGE`, then read into the value.
- `oneof` alternative: wrong wire type: `skip`, and the variant does not change. Else call the accessor with `select`, then read into the result. The same alternative again merges; a different one replaces.
- Repeated, change 1 (in-place): examine the capacity first, `CAPACITY_EXCEEDED` when full. Then `reset_value` the next free element, read into it, and add 1 to the count only when the read did not fail.
- Reset, change 2 (from descriptors): scalar and fixed-length bytes: zero bytes. String and bytes view: `std::memcpy` of an empty object of exactly that type. Sequence: `*stored_count = 0`, the storage stays. `std::optional`: `RESET`. `std::variant`: `access(v, 0, true)`. Nested message: `reset_message`.

`check_decodable<T>()` walks `T` in the way of `detail::contains_span_buffer` and gives these errors:
- `std::array` or `std::span<const T>` as a repeated field, at any depth: `pb: decode_encode_only: std::array and std::span<const T> are encode-only, use pb::repeated or pb::span_buffer to decode` (the text of today).
- a `const` member at any depth: `pb: decode_const_member: decode cannot write to a const member`.

- [ ] **Step 1: Change `tests/test_decode.cpp` to a run-time test**, as in Task 3 Step 1. Include `<micropb/codec.hpp>`.
- [ ] **Step 2: Add these tests to `tests/test_decode.cpp`** and to its `main`:

```cpp
bool test_reuse_repeated_message() {  // Review Focus 1
    constexpr std::array<std::uint8_t, 7> first{0x22, 0x05, 0x08, 0x01, 0x12, 0x01, 'a'};
    constexpr std::array<std::uint8_t, 2> second{0x22, 0x00};
    lists message{};
    const auto a = decode(bytes_of(first), message);
    const bool filled = a && message.items.size() == 1 && message.items[0].x == 1 && message.items[0].label == "a";
    const auto b = decode(bytes_of(second), message);
    return filled && b && message.items.size() == 1 && message.items[0].x == 0 && message.items[0].label.empty();
}

bool test_reuse_oneof() {  // Review Focus 2
    constexpr std::array<std::uint8_t, 4> first{0x1a, 0x02, 0x08, 0x07};
    constexpr std::array<std::uint8_t, 2> second{0x1a, 0x00};
    envelope_owned message{};
    const auto a = decode(bytes_of(first), message);
    const auto* seven = std::get_if<ping>(&message.payload);
    const bool filled = a && seven && seven->seq == 7;
    const auto b = decode(bytes_of(second), message);
    const auto* zero = std::get_if<ping>(&message.payload);
    const bool replaced = b && zero && zero->seq == 0;
    const auto c = decode(std::span<const std::uint8_t>(), message);
    return filled && replaced && c && std::holds_alternative<std::monostate>(message.payload);
}

bool test_span_buffer_no_storage() {  // Review Focus 5
    constexpr std::array<std::uint8_t, 3> one{0x0a, 0x01, 0x05};
    int_buffer message{pb::span_buffer<std::int32_t>(std::span<std::int32_t>())};
    const auto full = decode(bytes_of(one), message);
    const auto none = decode(std::span<const std::uint8_t>(), message);
    return !full && full.error() == error::CAPACITY_EXCEEDED && none && message.ints.empty();
}

struct nothing {};
struct holds_nothing { nothing n; std::uint32_t x; };
bool test_decode_empty_message() {  // Review Focus 4
    constexpr std::array<std::uint8_t, 6> in{0x0a, 0x02, 0x08, 0x05, 0x10, 0x01};  // an unknown field in `n`
    holds_nothing message{};
    nothing alone{};
    return decode(bytes_of(in), message) && message.x == 1 && decode(std::span<const std::uint8_t>(), alone);
}

bool test_error_order() {  // spec, "Order of errors"
    constexpr std::array<std::uint8_t, 3> bad{0x22, 0x01, 0x08};  // one `items` element that is cut
    std::array<std::uint8_t, 8 * 2 + 3> full{};                   // eight empty elements, then the cut one
    for (std::size_t i = 0; i < 8; ++i)
        full[2 * i] = 0x22;
    std::copy(bad.begin(), bad.end(), full.begin() + 16);
    return fails<lists>(bytes_of(bad), error::TRUNCATED) && fails<lists>(bytes_of(full), error::CAPACITY_EXCEEDED);
}
```

- [ ] **Step 3: Add `test_all_kinds` to `tests/test_codec.cpp`.** One message type `all` has a member of each scalar type of the README type mapping (also an enum with an unsigned 8-bit underlying type and one with negative values), `std::string_view`, `std::span<const std::uint8_t>`, `std::array<std::uint8_t, 6>`, `pb::repeated<std::uint8_t, 16>`, a nested message, `std::optional` of a scalar and of a message, `pb::repeated` of `std::int32_t`, `pb::sint32`, `double`, a message and `std::string_view`, a `pb::span_buffer<std::int32_t>`, a `std::variant<std::monostate, message, message, std::uint32_t>` with `pb::oneof_tags`, and a last member with `[[= pb::tag{100}]]`. A second type `all_views` has the same members in the same order, with `std::array<T, N>` and `std::span<const T>` in place of the `pb::repeated` scalar members. The test fills both with the same non-default values (negative numbers, `-0.0f`, a value of 2^40 in the `sint64`, the largest `std::uint64_t`) and asserts: `encode(all)` and `encode(all_views)` give the same bytes; `decode` of those bytes into a second `all` gives success; `encode` of the decoded message gives the same bytes again; `encoded_size` agrees each time. It runs one time for each `variant` alternative and one time for an `all` with all members default.
- [ ] **Step 4: Change `tests/compile_fail/decode_encode_only.cpp`** to include `<micropb/codec.hpp>`. Add `tests/compile_fail/decode_const_member.cpp` (`struct inner { std::int32_t x; }; struct msg { const inner c; };`, then the two lines at the end of `decode_encode_only.cpp`) and its name to the `foreach` list.
- [ ] **Step 5: Run** `cmake --build .build`. Expected: FAIL in `test_decode.cpp` and `test_codec.cpp`, `decode` is not declared.
- [ ] **Step 6: Write `check_decodable` in `meta_schema.hpp` and the decode and reset part of `codec.hpp`** (signatures and rules above).
- [ ] **Step 7: Delete `src/micropb/decode.hpp`** and remove its include from `micropb.hpp`. Remove from `meta_schema.hpp` each function that nothing uses now. `kind_of`, `wire_type_of`, `is_packed` and `build_tag_table` stay.
- [ ] **Step 8: Build and run `ctest`.** Expected: `100% tests passed, 0 tests failed out of 38`.
- [ ] **Step 9: Commit:** `feat(micropb): decode from descriptor tables`.

---

### Task 5: Sanitizer build and corruption sweep

**Files:**
- Modify: `CMakeLists.txt`, `tests/test_wire_compat.cpp`

**Interfaces:**
- Consumes: `pb::encode`, `pb::decode`, `pb::encoded_size`; `prefixes_are_safe`, `pose_owned` and `envelope_owned` in `tests/test_wire_compat.cpp`.
- Produces: the CMake option `MICROPB_SANITIZE` (default `OFF`).

- [ ] **Step 1: Add `corruptions_are_safe` to `tests/test_wire_compat.cpp`** and call it from a new `test_corruption_sweep` for the same vector and type pairs that the two prefix sweep tests use. Change `prefixes_are_safe` to use the same check of a successful decode (`reencodes`):

```cpp
/// A decoded message encodes into a buffer of exactly its encoded size.
template <typename T>
bool reencodes(const T& message) {
    std::array<std::uint8_t, 1024> out{};
    const std::size_t size = encoded_size(message);
    if (size > out.size())
        return false;
    const auto encoded = encode(message, std::span(out).first(size));
    return encoded && *encoded == size;
}

/// `decode` returns for each copy of `wire` with one changed byte. Sanitizers find a bad access.
template <typename T, std::size_t N>
bool corruptions_are_safe(const std::array<std::uint8_t, N>& wire) {
    for (std::size_t i = 0; i < N; ++i) {
        for (const std::uint8_t mask : {0x01, 0x80, 0xFF}) {
            std::array<std::uint8_t, N> in = wire;
            in[i] ^= mask;
            T message{};
            if (decode(std::span<const std::uint8_t>(in), message) && !reencodes(message))
                return false;
        }
    }
    return true;
}
```

- [ ] **Step 2: Add the option to `CMakeLists.txt`.** With `MICROPB_SANITIZE=ON` the test targets (not the compile-fail targets) get, for compile and link: `-fsanitize=address,undefined -fno-sanitize-recover=all`; under `if(MINGW)` they get `-fsanitize=undefined -fsanitize-undefined-trap-on-error` and no link option.
- [ ] **Step 3: Run the tests in four configurations**, each in its own build dir: default; `-DCMAKE_BUILD_TYPE=Release`; `-DCMAKE_BUILD_TYPE=MinSizeRel`; `-DMICROPB_SANITIZE=ON`. Expected for each: `100% tests passed, 0 tests failed out of 38`. Fix the codec, not the test, when the sanitizer build fails.
- [ ] **Step 4: Commit:** `test(micropb): add sanitizer option and corruption sweep`.

The `MINGW` branch cannot be run on Linux. Say in the final report that it was not run.

---

### Task 6: Size report, documents, final check

**Files:**
- Create: `tests/size/size_report.cpp`, `tests/size/report.cmake`
- Modify: `CMakeLists.txt`, `README.md`, `docs/superpowers/specs/2026-11-18-micropb-design.md`

**Interfaces:**
- Consumes: `pb::encode`, `pb::decode`.
- Produces: the build target `size_report`.

- [ ] **Step 1: Write `tests/size/size_report.cpp`.** It has the 32 structs `m0` to `m31` from the spec section "Reference: the benchmark", copied exactly. `top` has the first `MICROPB_SIZE_TYPES` (16 or 32) of them as alternatives. It defines two functions with external linkage:

```cpp
std::expected<std::size_t, pb::error> size_report_encode(const top& m, std::span<std::uint8_t> out) { return pb::encode(m, out); }
std::expected<std::size_t, pb::error> size_report_decode(std::span<const std::uint8_t> in, top& m) { return pb::decode(in, m); }
```

- [ ] **Step 2: Add the target.** Two `OBJECT` libraries, `size_report_16` and `size_report_32`, both `EXCLUDE_FROM_ALL`, with the test flags, `-Os` and `MICROPB_SIZE_TYPES`. A custom target `size_report` builds both and runs `tests/size/report.cmake` with the two object paths. The script runs GNU `size` (found with `find_program`; without it the script prints that it is not available), adds the `text` and `data` columns of each object, and prints:

```text
micropb size report (text + data, -Os)
16 message types: <a> bytes
32 message types: <b> bytes (target: 15000 or less)
one more message type: <(b - a) / 16> bytes (target: 300 or less)
```

- [ ] **Step 3: Run** `cmake --build .build --target size_report` and record the three numbers. Run `nm -C` on the 32-type object and confirm that it has no `_GLOBAL__sub_I` symbol (no initialization at run time) and no `emit_`, `decode_` or `reset_` function with a template argument list.
- [ ] **Step 4: If a number is above its target, stop.** Report the numbers and the 10 largest symbols (`nm -S --size-sort -C`) to the user before any more work. Do not tune the code without that decision.
- [ ] **Step 5: Update the documents.**
  - `README.md`: the header list and the test description in "Build and test" (run-time tests for the codec, `MICROPB_SANITIZE`, the `size_report` target); in "Usage" remove the paragraph that says encode, decode and `encoded_size` are `constexpr`; add the two new rules (bit-field, `const` member for decode) to the list of what does not compile.
  - `docs/superpowers/specs/2026-11-18-micropb-design.md`: the sentence about descriptor tables in "Intent", the `constexpr` goal, the `constexpr` in the API block, the known-size extension under "Approaches", the note about `std::string_view` in the goals, and the file list. At each place state the new fact and name the table codec design as the source.
- [ ] **Step 6: Final check.** Run the four configurations of Task 5 Step 3 again. Expected: 38 of 38 in each.
- [ ] **Step 7: Commit:** `build(micropb): add size report and update documents`.
