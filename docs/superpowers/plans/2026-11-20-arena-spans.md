# Arena spans Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `decode` fills `std::span<const T>` repeated fields from a caller-supplied `pb::arena`; `pb::repeated` stays for fields with a size; `pb::span_buffer` goes away; the plugin writes a span for a repeated field without `max_count`.

**Architecture:** `pb::arena` is a bump allocator over a caller `std::span<std::uint8_t>` in `types.hpp`. The interpreter in `codec.hpp` gets an `arena*` parameter. At a record of a span field whose block is full, it counts the elements of that field up to the end of the current message, takes a new block (capacity header + items), copies the old items, and continues. `meta_schema.hpp` gets the rules `recursive_message` and `decode_needs_arena`.

**Tech Stack:** C++23 (C++26 reflection optional), GCC 16 from MSYS2 UCRT64, Arm GNU Toolchain 13.3/14.2 (compile tests), CMake + CTest, Python 3 plugin.

**Spec:** `docs/superpowers/specs/2026-11-20-arena-spans-design.md`. Read it fully before each task. `CLAUDE.md` holds the coding rules.

## Global Constraints

* No heap, no exceptions, no RTTI in `src/micropb/`. Errors are values: `std::expected<std::size_t, pb::error>`.
* `pb::arena` uses `std::uint8_t` storage, not `std::byte`. Not PMR.
* New error value `pb::error::ARENA_FULL`, the last enumerator.
* Rule texts exactly: `pb: recursive_message:`, `pb: decode_needs_arena:`. Each has a test in `tests/compile_fail/<rule>.cpp`, listed in the `foreach(rule ...)` of `CMakeLists.txt`.
* `decode` never writes outside the arena or outside a block, for any input. Each item add examines the block capacity.
* No table or static object that needs run-time initialization (`no_startup_code`).
* One interpreter: the count uses the decoder's `reader`; no second decode path.
* Do not assume the layout of `std::span`, `std::optional`, `std::variant`: use accessor functions.
* Element types of a span field are trivially copyable and trivially destructible (`static_assert` in the descriptor).
* Wire bytes stay identical to `protoc`. Never change a reference vector in `tests/vectors/`.
* Do not edit generated files by hand: `tests/vectors/vectors.hpp`, `tests/vectors/vectors.pb.hpp`, `tests/generator/*.pb.hpp`. Make them again with `MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest` (MSYS2 Python, `C:\msys64\ucrt64\bin` first in `PATH`).
* Hand-written valid wire bytes in a test are checked with `protoc --decode_raw` before use; give the command output in the report.
* Every task: build and run all tests in `.build` (reflection) and `.build-off` (`-DMICROPB_REFLECTION=OFF`); both include the Arm compile tests. Commands:
  ```sh
  cmake --build .build && ctest --test-dir .build --output-on-failure
  cmake --build .build-off && ctest --test-dir .build-off --output-on-failure
  ```
* A task that changes `codec.hpp` gives `cmake --build .build --target size_report` before and after, in its report and commit message. Today: 16 types 9400 bytes, 32 types 13736 bytes, one more type 271 bytes.
* Style: `.clang-format`, snake_case, `_` member prefix, `[[nodiscard]]`, `///` comments in ASD-STE100, no narration comments. Commit messages `type(micropb): summary`, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Do not push.
* README.md and the applicable design change in the same commit as the behavior they describe.
* Do not edit `CLAUDE.md`.

## Review Focus

1. Arena storage at an odd address: blocks must still be aligned for the element (`allocate` aligns the address, not the offset).
2. A packed record whose length prefix is larger than the remaining input: `TRUNCATED`, not `ARENA_FULL`, and no allocation of the claimed size.
3. `allocate` with a size near `SIZE_MAX`, or a count × element size that overflows: null, never a wrapped small block.
4. An error in the middle of a span field: the span in `msg` has only the items that were added, and points into the arena.
5. Two messages decoded one after the other into one arena: the first message stays valid and unchanged.

Each item has a test in the task that owns the code (Task 1: items 1, 3; Task 3: items 2, 4, 5).

---

### Task 1: `pb::arena`

**Files:**
- Modify: `src/micropb/types.hpp` (new struct after `span_buffer`, new enumerator)
- Test: `tests/test_types.cpp`

**Interfaces:**
- Produces, in namespace `pb`:
  ```cpp
  enum class error { ..., CAPACITY_EXCEEDED, ARENA_FULL };
  struct arena
  {
  	constexpr explicit arena( std::span<std::uint8_t> storage );
  	[[nodiscard]] constexpr std::size_t used() const;
  	[[nodiscard]] constexpr std::size_t capacity() const;
  	constexpr void clear();
  	/// Null when the storage is too small. `alignment` is a power of 2.
  	[[nodiscard]] void* allocate( std::size_t size, std::size_t alignment );
  private:
  	std::span<std::uint8_t> _storage;
  	std::size_t _used = 0;
  };
  ```
- Doc comments: the text of the spec, section "`pb::arena`". The `ARENA_FULL` comment: "decode: the arena has no space for the items of a repeated field." Change the `CAPACITY_EXCEEDED` comment to "decode: a field has more elements or bytes than its `pb::repeated` or `std::array` holds."

- [ ] **Step 1: Write the failing tests in `tests/test_types.cpp`**, in the style of `test_repeated`:
  * `constexpr bool test_arena_empty()` (`static_assert`): `arena a{ std::span<std::uint8_t>( storage ) }` with 16 bytes: `used() == 0`, `capacity() == 16`; after `clear()` still 0.
  * `bool test_arena_allocate()` (run time): 64-byte storage. `allocate(3, 1)` not null, `used() == 3`. `allocate(8, 8)` gives an address that is a multiple of 8; then `3 + 8 <= used()` and `used() <= 3 + 7 + 8`.
  * `bool test_arena_odd_start()` (Review Focus 1): storage is `alignas(8) std::array<std::uint8_t, 65>`, arena over `subspan(1)`. `allocate(8, 8)` gives an 8-aligned address inside the storage.
  * `bool test_arena_full()`: 16 bytes; `allocate(16, 1)` not null, then `allocate(1, 1) == nullptr` and `used()` stays 16. `clear()`, then `allocate(16, 1)` not null.
  * `bool test_arena_overflow()` (Review Focus 3): `allocate(SIZE_MAX, 1)`, `allocate(SIZE_MAX - 2, 8)` after one byte is used: both null, `used()` unchanged.
  * Add each to `main`.
- [ ] **Step 2: Run** `cmake --build .build` — expected: compile error, `pb::arena` is not declared.
- [ ] **Step 3: Implement `pb::arena` and `error::ARENA_FULL` in `types.hpp`.** Align with `std::uintptr_t` arithmetic on the address of `_storage.data() + _used`; examine `padding + size` against the remaining bytes without overflow (compare, do not add first).
- [ ] **Step 4: Run all tests in both build folders** (Global Constraints). Expected: all pass.
- [ ] **Step 5: Commit** `feat(micropb): pb::arena, a bump allocator over caller storage`.

---

### Task 2: Rule `recursive_message`

**Files:**
- Modify: `src/micropb/meta_schema.hpp`
- Create: `tests/compile_fail/recursive_message.cpp`
- Modify: `CMakeLists.txt` (rule list)

**Interfaces:**
- Produces: `template <typename T> [[nodiscard]] consteval bool detail::is_recursive()` — `true` when a member, element, optional value or alternative of `T`, directly or through other message types, is `T`. Message types are found through `field_list_of`. The `static_assert` text: `"pb: recursive_message: a message cannot contain itself, also through a std::span or other messages"`.
- The assert must fire from every entry point that builds a descriptor or checks a type (`build_tag_table`, `encode`, `encoded_size`, `decode`). Put it where `list_traits<...>::check()` runs for the message.

- [ ] **Step 1: Write `tests/compile_fail/recursive_message.cpp`** in the style of `span_buffer_in_variant.cpp`, with reflection structs:
  ```cpp
  struct node { std::int32_t id; std::span<const node> children; };
  struct a; struct b { std::span<const a> as; }; struct a { b inner; };
  ```
  and a use that builds the table of `node` and of `a` (for example `pb::encoded_size`). Add `recursive_message` to the `foreach(rule ...)` list.
- [ ] **Step 2: Run** `ctest --test-dir .build -R compile_fail.recursive_message --output-on-failure`. Expected: FAIL — today the compiler stops in endless template recursion, without the rule text.
- [ ] **Step 3: Implement `is_recursive` and the `static_assert`.** Carry the visited types in a template pack to stop at a cycle. Make the other recursive checks (`check_decodable`, `contains_span_buffer`) stop at a recursive type, so that the compiler reaches the `static_assert` before its instantiation depth limit.
- [ ] **Step 4: Run all tests in both build folders.** Expected: all pass, `compile_fail.recursive_message` included.
- [ ] **Step 5: Commit** `feat(micropb): rule recursive_message`. Add one line to the spec section "Rules" only if the implementation differs from it.

---

### Task 3: Decode `std::span<const T>` into the arena

**Files:**
- Modify: `src/micropb/codec.hpp` (descriptors, interpreter, public `decode`)
- Modify: `src/micropb/meta_schema.hpp` (`check_decodable`, `decode_encode_only` text, `decode_needs_arena`)
- Create: `tests/compile_fail/decode_needs_arena.cpp`; delete nothing yet
- Modify: `CMakeLists.txt` (rule list)
- Test: `tests/test_decode.cpp`, `tests/test_field_list.cpp`, `tests/test_wire_compat.cpp`, `tests/compile_fail/decode_encode_only.cpp` (only if its expected text no longer matches)
- Docs: `README.md` (API, type mapping, errors, lifetime, arena margin), `docs/superpowers/specs/2026-11-18-micropb-design.md` and `2026-11-18-micropb-table-codec-design.md` (one section "Changes: arena spans" each, naming the spec)

**Interfaces:**
- Consumes: `pb::arena`, `pb::error::ARENA_FULL` (Task 1); `is_recursive` (Task 2).
- Produces:
  ```cpp
  template <typename T>
  [[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg, arena& memory );
  template <typename T>
  [[nodiscard]] std::expected<std::size_t, error> decode( std::span<const std::uint8_t> in, T& msg ); // static_assert !needs_arena<T>()
  template <typename T> [[nodiscard]] consteval bool detail::needs_arena(); // T has a std::span<const E> repeated field (E not std::uint8_t), at any depth
  ```
  Assert text: `"pb: decode_needs_arena: a std::span<const T> repeated field needs a pb::arena, use decode( in, msg, arena )"`. New `decode_encode_only` text: `"pb: decode_encode_only: std::array is encode-only, use pb::repeated or std::span<const T> to decode"`.
- Internal changes the plan fixes (names are free otherwise):
  * `sequence_desc` gets `std::uint16_t element_align` and a binder `void ( * )( void* sequence, void* data, std::size_t count )` that sets a `std::span<const T>` member through its constructor. Null for other storages.
  * The interpreter functions `decode_message`, `decode_value`, `decode_repeated`, `decode_element` and `detail::decode` get a parameter `arena* memory` (null from the overload without an arena; a span record with a null arena gives `ARENA_FULL`). `decode_message` passes the field number to `decode_repeated`, because the count needs it.
  * Block layout: the capacity `std::size_t` is in the bytes just before the first item; the items are aligned to `element_align`.
  * The count: a copy of the reader at the current record, the rules of the spec section "Count and fill". It counts only records with the field number; it skips other values with `skip`. A reader error during the count ends the count with the elements counted so far.
  * `reset_value` sets a span field to empty through the binder.

- [ ] **Step 1: Record the size report before the change:** `cmake --build .build --target size_report`. Keep the three numbers.
- [ ] **Step 2: Write the failing tests.**
  * `tests/compile_fail/decode_needs_arena.cpp`: a struct with `std::span<const std::int32_t> ints;` in a nested message, decoded with `pb::decode( in, msg )`. Add `decode_needs_arena` to the rule list.
  * `tests/test_field_list.cpp` (runs with and without reflection): a struct with `std::span<const inner>` and `std::span<const float>` plus its `pb::fields_of` list; round trip `encode` → `decode( in, msg, arena )`; compare items; `encode` of the decoded message gives the same bytes.
  * `tests/test_decode.cpp`, with an arena over storage followed by 16 canary bytes (`0xA5`) that each test examines after `decode`:
    * `test_arena_exact()`: measure `used()` after one decode; a second arena of exactly that size decodes; one byte less gives `ARENA_FULL`.
    * `test_arena_mixed_records()`: records of two span fields mixed (`ints` and `names` alternate), and packed and unpacked records of one scalar field mixed. Hand-written bytes, checked with `protoc --decode_raw`.
    * `test_arena_nested_repeated()`: a span of messages whose elements have span fields.
    * `test_arena_merge()`: a nested message that occurs two times, each with items of one span field: the result has the items of both, in order.
    * `test_arena_oneof_and_optional()`: a oneof alternative and a `std::optional` whose message has a span field.
    * `test_arena_alignment()`: a span of `std::int8_t` (`int32` on the wire), then a span of `double`, in one arena; each `data()` is aligned for its type.
    * `test_arena_bad_packed()`: a packed varint record whose last varint has no end (`TRUNCATED`), a packed `fixed32` record of length 6 (`TRUNCATED`); canaries unchanged.
    * `test_arena_huge_length()` (Review Focus 2): a packed record with length prefix `0xFF 0xFF 0xFF 0x0F` and 3 bytes after it: `TRUNCATED`, `used()` stays 0.
    * `test_arena_error_mid_field()` (Review Focus 4): an unpacked span field of 3 elements whose input ends in the third: error `TRUNCATED`; the span has 2 items with the correct values, and its `data()` is inside the arena storage.
    * `test_arena_two_messages()` (Review Focus 5): decode message A, then message B into the same arena; A's items still have their values.
    * `test_arena_order()`: two encodings of one message with a different field order: the `used()` values differ by no more than `alignof( std::max_align_t ) - 1` for each block.
    * `test_arena_wrong_wire_type()`: a `fixed32` record for a varint span field is skipped and not counted: `used()` equals the use without that record.
    * `test_no_arena_needed()`: a message with only `pb::repeated` fields decodes with `decode( in, msg )`.
  * `tests/test_wire_compat.cpp`: `span_lists`, a twin of `messages::lists` with `std::span<const T>` members. Decode `lists.full` and `lists.empty` from `vectors.hpp` with an arena; the items equal the `pb::repeated` decode; `encode` gives the reference bytes. Run the existing prefix sweep and corruption sweep also on `span_lists`, with canaries.
- [ ] **Step 3: Run both build folders.** Expected: compile errors (`decode` has no arena overload) and `compile_fail.decode_needs_arena` FAIL.
- [ ] **Step 4: Implement** the descriptor, interpreter and rule changes above. Keep `pb::repeated` decode as it is.
- [ ] **Step 5: Run all tests in both build folders, and once with `-DMICROPB_SANITIZE=ON`.** Expected: all pass.
- [ ] **Step 6: Size.** `cmake --build .build --target size_report`: give the numbers before and after. Then measure the cost of spans once: copy `tests/size/size_report.cpp` to the scratchpad, change each `pb::repeated<T, N>` (not `std::uint8_t`) to `std::span<const T>` and pass an arena to `decode`, compile with the flags of `size_report_16`/`_32`, and give the numbers. Do not change `tests/size/size_report.cpp`.
- [ ] **Step 7: Docs.** README: `decode` with an arena, `pb::arena`, `ARENA_FULL`, the type mapping row, the lifetime of a decoded message (input and arena), "measure `used()` and add a margin". The two base designs: section "Changes: arena spans". The spec: section "Changes during the implementation" with the size numbers and any difference from the spec.
- [ ] **Step 8: Commit** `feat(micropb): decode std::span repeated fields into a pb::arena`, with the size numbers in the message.

---

### Task 4: Remove `pb::span_buffer`

**Files:**
- Modify: `src/micropb/types.hpp`, `src/micropb/meta_schema.hpp`, `src/micropb/codec.hpp`
- Delete: `tests/compile_fail/span_buffer_in_optional.cpp`, `span_buffer_in_variant.cpp`, `span_buffer_in_repeated.cpp`
- Modify: `CMakeLists.txt` (rule list), `tests/test_codec.cpp`, `tests/test_decode.cpp`, `tests/test_encode.cpp`, `tests/test_schema.cpp`, `tests/test_types.cpp`
- Docs: `README.md`, `docs/superpowers/specs/2026-11-18-micropb-design.md` (add one line to its "Changes: arena spans" section)

**Interfaces:**
- Consumes: the arena decode of Task 3.
- Removes: `pb::span_buffer`, `detail::is_span_buffer`, `sequence_traits<span_buffer<T>>`, `contains_span_buffer`, `sequence_access::count_of`, the three `span_buffer_in_*` asserts. `sequence_storage::ACCESSOR` stays for `std::span<const T>`.

- [ ] **Step 1: Convert the tests.** Each `span_buffer` test in `test_decode.cpp` (`test_span_buffer`, `test_span_buffer_nested`, `test_span_buffer_repeated`, `test_span_buffer_no_storage`) becomes the same case with `std::span<const T>` and an arena, where Task 3 does not already cover it; `test_span_buffer_no_storage` becomes "an arena of capacity 0 gives `ARENA_FULL`". In `test_codec.cpp`, `test_encode.cpp`, `test_schema.cpp`, `test_types.cpp`, replace `span_buffer` with `std::span<const T>` where the test is about a sequence with an accessor, else delete the line. State in the report each deleted assertion and the test that now covers its behavior.
- [ ] **Step 2: Remove the code and the three compile-fail tests** (rules removed by the spec). Update the comments that name `span_buffer` (`codec.hpp` "`std::span<const T>` and `pb::span_buffer<T>`", the `decode` doc comment).
- [ ] **Step 3: Run all tests in both build folders.** Expected: all pass; `grep -rn span_buffer src tests README.md` gives no result.
- [ ] **Step 4: Size report** before and after (expected: same or smaller).
- [ ] **Step 5: Commit** `feat(micropb): remove pb::span_buffer, the arena replaces it`.

---

### Task 5: Plugin writes `std::span<const T>` without `max_count`

**Files:**
- Modify: `tools/protoc-gen-micropb.py` (`field_type`, around the `max_count` branch; the docstring table)
- Test: `tests/generator/test_plugin.py`, `tests/test_generated.cpp`
- Regenerate: `tests/generator/*.pb.hpp`, `tests/vectors/vectors.pb.hpp` (expected: no change for the vectors)
- Docs: `README.md` (options table, the sentence "Each repeated field must have a `max_count` or `type:FT_POINTER`"), `docs/superpowers/specs/2026-11-19-protoc-plugin-design.md` (section "Changes: arena spans"; it updates "Errors" and the `FT_POINTER` section's "encode only")

**Interfaces:**
- Consumes: decode with an arena (Task 3).
- Produces: for a repeated field without `max_count` (after wildcards), the member type `std::span<const T>`. The error "a repeated field needs a capacity" and its "No file … was found" hint go away. All other options keep their result.

- [ ] **Step 1: Change the tests in `test_plugin.py`.**
  * `test_repeated_without_max_count`: `header(PROTO3 + "package p; message m { repeated int32 a = 1; }")` contains `std::span<const std::int32_t> a;`.
  * `test_repeated_without_max_count_says_when_no_options_file_was_found`: delete (the error is gone); state it in the report.
  * New `test_repeated_message_without_max_count`: a repeated message field and a repeated `bytes` field without options give `std::span<const p::n>` and `std::span<const std::span<const std::uint8_t>>`.
  * New `test_max_count_still_gives_repeated`: `m.a max_count:4` gives `pb::repeated<std::int32_t, 4>` (keep if `test_max_count` already pins it; then skip).
- [ ] **Step 2: Run** `python tests/generator/test_plugin.py`. Expected: the changed test fails with the old error text.
- [ ] **Step 3: Implement** in `field_type`: without `max_count` and without `pointer`, `text = f"std::span<const {text}>"`. Update the docstring rows.
- [ ] **Step 4: Regenerate** the checked-in headers with `MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest`; run `git diff --stat` and explain each changed file.
- [ ] **Step 5: `tests/test_generated.cpp`:** new `test_views_decode()`: decode the libprotoc bytes of `test_views` (`12 14 0a 06 08 01 12 02 61 62 0a 00 12 03 01 d8 04 1a 03 78 79 7a`) into `Report` with an arena; the batch has 2 slots (first `index` 1, `key` "ab"), deltas `-1, 300`, blob "xyz". Add to `main`. Runs with and without reflection.
- [ ] **Step 6: Run all tests in both build folders** and `python tests/generator/test_plugin.py`. Expected: all pass.
- [ ] **Step 7: Docs** as listed in **Files**.
- [ ] **Step 8: Commit** `feat(micropb): plugin writes std::span for a repeated field without max_count`.
