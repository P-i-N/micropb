# micropb table-driven codec design

This document changes stage 1 of `2026-11-18-micropb-design.md` (the core codec). Where the two documents disagree, this one is correct. All parts of the first document that this one does not name stay as they are.

## Intent

The device firmware will have tens of message types. Today each message type gets its own compiled encode and decode functions. A measurement in this session (GCC 16.2, `-Os`, x86-64, message types of 8 mixed fields in one `oneof` envelope) gave about 940 bytes of code for each message type, and 35.5 KB for 32 types. That is too much flash for a device.

The change: reflection builds one constant descriptor table for each message type at compile time. One interpreter, which is not a template, encodes and decodes all message types from these tables. A throwaway prototype in this session gave about 260 bytes for each message type and 12.8 KB for 32 types, with the same bytes on the wire.

The user's priorities, in the user's words: the usage of the library must be easier and simpler than NanoPB in a C++ project; the final binary size is important; few, short and simple header files are a plus; readability is important.

## Goals

* The public API and the usage do not change: plain structs, `pb::encode`, `pb::decode`, `pb::encoded_size`. No `.proto` file, no generator, no callbacks, no descriptor that the user writes or sees.
* The bytes on the wire and all decode results stay the same, with one exception: for an input that has more than one error, the error code can be a different one (see "Order of errors"). All reference vectors pass without a change to the vectors.
* Code size grows slowly with the number of message types. Targets for the benchmark in "Size report" (GCC 16.2, `-Os`, x86-64, code and tables together):
    * 32 message types: 15 KB or less (today: 35.5 KB; prototype: 12.8 KB).
    * Each more message type of 8 fields: 300 bytes or less (today: about 940; prototype: about 260).
* One implementation of encode and decode. A reader can understand the complete codec from one header.
* All schema errors stay compile errors, with the same `pb: <rule>:` messages.
* No heap, no exceptions, no RTTI. The library stays header-only.

## Non-goals

* `constexpr` encode, decode and `encoded_size`. The interpreter reads and writes struct members through byte offsets, which constant evaluation does not allow. The user accepted this loss. The one feature that users lose is the encoding of a constant message at compile time.
* Speed. The prototype was 1.6 to 2.7 times slower than the template code on the host (about 1 µs for a 262-byte message). This is acceptable for a serial link.
* Code size for one or two message types. There the template code was smaller (2.7 KB against 5.0 KB for one type). The library does not keep a second code path for this case.
* New features or API changes: a maximum encoded size, decode into `std::array`, a 32-bit varint path. These come later, each as a separate change.
* Measurement on ARM. No ARM compiler with reflection is available. The size targets are host numbers.

## Anti-goals

* A second implementation of encode or decode, for example the template code behind `if consteval`. It doubles the code that a reader must understand and keep correct.
* An assumption about the memory layout of `std::optional` or `std::variant`. Their layouts are not specified.
* A descriptor that needs initialization at run time. All tables are constants in read-only memory.
* Any change of the bytes on the wire.

## Constraints

* All constraints of the first document stay: GCC 16.2 with `-std=c++26 -freflection -fno-exceptions -fno-rtti`, standard library only, the house style, namespace `pb`, headers in `src/micropb/`, errors as `std::expected<std::size_t, pb::error>`.
* The descriptor tables are built with C++26 static reflection (the user's requirement): `std::meta` queries such as `nonstatic_data_members_of`, `offset_of`, `type_of` and `template_arguments_of`, `template for`, splices and `std::define_static_array`. The user does not write a descriptor, a macro or a registration call. Where reflection can do the work, the builder does not use older template techniques such as `std::index_sequence` folds or `offsetof`.
* `<cstring>` is a new header in the core, for `std::memcpy` and `std::memset`. It is part of a freestanding C++26 library.
* This design builds on the sticky-state `pb::writer` and `pb::reader` in `wire.hpp`. At the time of writing, that change is in the working tree (`wire.hpp`, `encode.hpp`, `decode.hpp`, `tests/test_wire.cpp`) and is not committed. Its tests pass in Debug, Release and MinSizeRel builds and under the sanitizers. The first task of the plan commits it as it is, as a separate commit, after the user says so. The builder does not change it before that.
* `wire.hpp` does not change and stays `constexpr`. Its tests stay `static_assert` tests.

## Approaches

The open choice was where the codec gets its knowledge of a message type.

* **A. Tables only.** The interpreter replaces the template encode and decode. One implementation. The tests of encode and decode run only at run time.
* **B. Tables at run time, templates at compile time.** The template code stays behind `if consteval`. This keeps `constexpr` encode and decode and the `static_assert` tests. It adds about 450 lines and a second code path.
* **C. Templates only (the code of today).** Smallest and fastest for one or two message types. About 940 bytes for each more message type.

**Decision: A** (the user's choice). C is too large for tens of message types. B is against the priorities "few, short and simple header files" and "readability".

## How the codec works

### Descriptors

Reflection makes these constants. The names are examples.

* **Value descriptor.** One for each distinct type that is a member, an element, a `std::optional` value or a `std::variant` alternative. It holds the kind (`field_kind`), and one more datum that depends on the kind:
    * scalar: a scalar code (below);
    * fixed-length bytes (`std::array<std::uint8_t, N>`): `N`;
    * nested message: a pointer to its message descriptor;
    * repeated field, or bytes in `pb::repeated` or `pb::span_buffer`: a pointer to a sequence descriptor;
    * `std::optional`: a pointer to an optional descriptor;
    * `std::variant`: a pointer to a variant descriptor.
* **Field descriptor.** One for each entry of the tag table (`build_tag_table<T>()`): the field number, the byte offset of the member in the message (`std::meta::offset_of`), the alternative index for a `std::variant` alternative (0 for a plain member), and a pointer to the value descriptor of the member type. Its size is 12 bytes or less on a 32-bit target and 16 bytes or less on a 64-bit target. A `static_assert` checks this.
* **Message descriptor.** A pointer to the field descriptors, sorted by field number, and their count.
* **Sequence descriptor.** The storage type (`pb::repeated`, `pb::span_buffer`, `std::array`, `std::span<const T>`), the size of one element, a pointer to the value descriptor of the element, and what the storage type needs: the capacity and the place of the count for `pb::repeated`, the count `N` for `std::array`, a pointer to the accessor function for the two span types (see "Access to the standard types").
* **Optional descriptor.** A pointer to the value descriptor of the value, and a pointer to one small accessor function for this `std::optional` type. The accessor can: give the address of the value or null, make the optional contain a value-initialized value, and reset the optional.
* **Variant descriptor.** A pointer to the value descriptors of the alternatives, and a pointer to one small accessor function for this `std::variant` type. The accessor gives the address of alternative `I` when it is active, else null. On request it first makes alternative `I` active with a value-initialized value. Alternative 0 is `std::monostate`, so the same function resets the variant.

A scalar code is one byte: the size of the type (1, 2, 4 or 8 bytes) and flags for signed, ZigZag, fixed width, `bool` and enum. From it the interpreter loads the wire value from memory and stores a wire value to memory. It uses `std::memcpy` with a typed temporary, so it does not depend on the byte order of the host.

The descriptors do not contain `std::meta::info`. They are `constexpr` objects with static storage and constant initialization, so the linker puts them in read-only memory and keeps one copy of each.

### Interpreter

The interpreter is a set of `inline` functions that are not templates. They take a writer or a reader, a descriptor, and a `void` pointer to the value.

* **Encode** walks the field descriptors of a message in order and writes each field by its kind. The rules are the rules of today: implicit presence, explicit presence for `std::optional` and `oneof` alternatives, packed scalars, the size of a nested message measured before its length prefix, one walk when the writer only counts.
* **Decode** reads one key at a time, finds the field descriptor with that field number, and reads the value by its kind. A field number that the message does not have is skipped. All rules in "Decode semantics" of the first document stay.
* **Reset** sets a message to its initial state before `decode`, also from the descriptors: a scalar and a fixed-length bytes field become zero bytes, a string or a bytes view becomes empty, a sequence gets the count 0 and keeps its storage, a `std::optional` and a `std::variant` are reset through their accessor, a nested message is reset member by member. The decoder also resets the new element of a repeated field before it reads into it.

The only templates that stay are the functions that build the descriptors (`consteval`), the accessors for `std::optional` and `std::variant`, and the three public functions. Each public function only passes the descriptor of `T` and the address of the message to the interpreter.

### Access to the standard types

* `std::string_view` and `std::span<const std::uint8_t>` (a bytes view) are trivially copyable. The interpreter copies the complete object out of or into the message with `std::memcpy`, into an object of exactly that type. It does not read their members by offset.
* A repeated field in a `std::span<const T>` or a `pb::span_buffer<T>` is used through one small accessor function for each distinct type. The accessor gives the address of the first element, the count and the capacity. The interpreter does not assume that spans of different element types have the same layout. (Builder's call in this design. The cost is one small function for each distinct span type, and these types are few.)
* `std::optional` and `std::variant` are used only through their accessor functions.
* `std::array<T, N>` has its elements at offset 0 and the count `N`.
* `pb::repeated` is a type of this library. The interpreter can know its layout. How it gets the offsets of its private members is left to the builder.

### Order of errors

The template decoder reads the element of a repeated field into a temporary and then adds it to the sequence. The interpreter has no temporary of a type that it does not know, so it reads into the next free element of the sequence. It must examine the capacity first. An input that has an element too many, and in which that element is also malformed, gave the error of the element (for example `TRUNCATED`). It now gives `CAPACITY_EXCEEDED`. Each input that was an error stays an error, and each input that was correct gives the same message. (Builder's call in this design.)

## Public API

No change to the signatures, except that the three functions are no longer `constexpr`:

```cpp
template <typename T>
[[nodiscard]] std::size_t encoded_size(const T& msg);

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> encode(const T& msg, std::span<uint8_t> out);

template <typename T>
[[nodiscard]] std::expected<std::size_t, error> decode(std::span<const uint8_t> in, T& msg);
```

`pb::known_size<T>()` is removed. It served only the template encoder, and only for messages made of `std::array` fields. The interpreter measures a nested message in one walk.

`kind_of`, `wire_type_of`, `is_packed` and `build_tag_table` stay. The `.proto` generator of stage 2 uses them.

## Compile-time checks

All 28 rules in `tests/compile_fail/` stay, with the same messages. These need attention because the template code gave them without a special check:

* `decode_encode_only`: `std::array` and `std::span<const T>` as a repeated field are encode-only. One descriptor now serves encode and decode, so `pb::decode<T>` needs a `consteval` check that walks `T`.
* A `const` nested struct compiles for encode only (README). `pb::decode<T>` must reject a message that has a `const` member at any depth. The interpreter writes through a `void` pointer, so without this check the write would compile.

New rules, each with a test in `tests/compile_fail/`:

* A bit-field member is an error. The interpreter cannot address it with a byte offset. Today it compiles for encode only, by accident.
* If the field descriptor keeps the offset in 16 bits, a member at an offset above 65535 is an error.

## Files

```text
src/micropb/
    types.hpp        no change to the API
    wire.hpp         no change
    meta_schema.hpp  tag table, type classification, compile-time checks; known_size removed
    codec.hpp        new: descriptors, interpreter, encoded_size, encode, decode
    micropb.hpp      umbrella header
```

`encode.hpp` and `decode.hpp` are removed. The library goes from six headers to five. `codec.hpp` has two parts with a section rule between them: the descriptors and their construction at compile time, then the interpreter. The interpreter part has no templates.

## Tests

* `test_wire.cpp`, `test_types.cpp`, `test_schema.cpp` and the compile-fail tests stay `static_assert` tests. `test_schema.cpp` loses the `known_size` test.
* `test_encode.cpp`, `test_decode.cpp` and `test_wire_compat.cpp` become run-time tests. Each test function stays, without `constexpr` and without its `static_assert`. `main` runs them, as today. The checks against the `protoc` reference vectors do not change.
* The comments in the tests and the README that say "decoding a `std::string_view` is not `constexpr`" are removed, because no decode is `constexpr` now.
* Constant evaluation found each out-of-bounds read and each undefined operation in the template code. A sanitizer build takes this role:
    * a CMake option, off by default, builds the tests with AddressSanitizer and UndefinedBehaviorSanitizer;
    * a new test decodes each prefix of each reference vector, and each copy of the vector in which one byte is changed. Each byte is changed three times: XOR with `0x01`, with `0x80` and with `0xFF`. Each vector is decoded as its own message type. The test passes when `decode` returns for each input and, where `decode` gives success, `encode` of the result into a buffer of exactly `encoded_size(result)` bytes gives success with that size. Under the sanitizers it shows out-of-bounds access in the interpreter.
    * The user's main compiler is GCC on MSYS2. The builder must find which sanitizers are available there. If AddressSanitizer is not available, the option uses `-fsanitize=undefined -fsanitize-undefined-trap-on-error` on that platform, and the full sanitizer run happens on Linux.
* New tests for the descriptors: the size limits of the field descriptor, and one message that has each kind and each sequence storage type, encoded and decoded.
* The tests must also build and pass with `-DCMAKE_BUILD_TYPE=Release` and `MinSizeRel`. The first review found a warning that showed only at `-O2`.

### Size report

A build target that is not a test compiles one translation unit at `-Os` and prints its size. The translation unit has the 32 message types of "Reference: the benchmark" in one `oneof` envelope, one function that calls `pb::encode` and one function that calls `pb::decode`. The source is checked in. The build does not run a generator and needs no new tool.

* The size is the sum of the `text` and `data` columns that GNU `size` prints for the object file. On Linux this includes `.eh_frame` and the read-only tables with relocations (`.data.rel.ro`). The numbers of this document were measured this way.
* The targets in "Goals" apply to the Linux x86-64 object file, compiled with the flags of the tests. On other platforms the target only prints the number.
* The target also builds the same source with the first 16 message types. The cost of one more message type is the difference of the two sizes, divided by 16.
* A result above a target does not fail the build. The builder stops and reports the numbers to the user before the work is merged. The user decides if more work on the size is necessary.
* The 32 message types use member types from one small pool, so they share their value descriptors. A real protocol with one enum or one nested struct for each message has more descriptors. The benchmark does not show this cost.

## Documents

The implementation also updates the text that this change makes wrong:

* `2026-11-18-micropb-design.md`: the sentence "no descriptor tables in ROM" in Intent, the `constexpr` goal, the `constexpr` in the API block, the "known size" extension of approach B, the file list, and the `std::string_view` note.
* `README.md`: the paragraph that says encode, decode and `encoded_size` are `constexpr`, and the description of the tests.

## Left to the builder

* All names of the descriptor types and the interpreter functions.
* The exact layout of each descriptor, within the size limits of the field descriptor.
* How the decoder finds a field descriptor. A linear search that starts after the last match is sufficient: fields usually arrive in ascending order.
* The form of the accessor functions for `std::optional` and `std::variant`, and how to keep the variant accessor small. In the prototype it was the largest per-type cost (about 60 bytes for each alternative).
* How the interpreter gets the layout of `pb::repeated`, and the form of the accessor function for the span types. The private layout of `pb::repeated` and `pb::span_buffer` can change if that makes the interpreter simpler. Their public API cannot.
* The names of the new compile-time rules.
* The name of the CMake option for the sanitizers.

## Reference: the benchmark

These are the message types of the size report, exactly as they were measured in this session. `top` is the message that the two functions encode and decode.

```cpp
struct m0 { bool f0; pb::fixed32 f1; std::optional<float> f2; double f3; std::int32_t f4; std::uint8_t f5; pb::repeated<std::uint8_t, 16> f6; std::optional<std::uint32_t> f7; };
struct m1 { pb::repeated<std::int32_t, 8> f0; std::optional<std::uint32_t> f1; std::int16_t f2; pb::repeated<float, 4> f3; float f4; std::int32_t f5; std::uint8_t f6; std::uint32_t f7; };
struct m2 { double f0; pb::repeated<float, 4> f1; pb::fixed32 f2; std::uint8_t f3; std::uint32_t f4; std::optional<std::uint32_t> f5; bool f6; float f7; };
struct m3 { float f0; pb::repeated<std::int32_t, 8> f1; std::string_view f2; std::uint32_t f3; double f4; pb::repeated<std::uint8_t, 16> f5; std::uint64_t f6; std::int16_t f7; };
struct m4 { double f0; std::int16_t f1; float f2; pb::repeated<float, 4> f3; pb::repeated<std::uint8_t, 16> f4; std::uint32_t f5; std::uint64_t f6; std::optional<float> f7; };
struct m5 { pb::repeated<std::int32_t, 8> f0; std::optional<std::uint32_t> f1; std::uint64_t f2; float f3; std::string_view f4; double f5; std::int16_t f6; std::uint8_t f7; };
struct m6 { pb::fixed32 f0; pb::repeated<std::int32_t, 8> f1; std::uint32_t f2; pb::repeated<float, 4> f3; std::uint64_t f4; std::int16_t f5; std::int32_t f6; pb::sint32 f7; };
struct m7 { pb::fixed32 f0; std::int32_t f1; pb::repeated<std::uint8_t, 16> f2; std::string_view f3; std::optional<float> f4; std::uint64_t f5; pb::repeated<float, 4> f6; std::int16_t f7; };
struct m8 { pb::repeated<float, 4> f0; bool f1; pb::repeated<std::int32_t, 8> f2; pb::fixed32 f3; std::optional<std::uint32_t> f4; std::uint64_t f5; std::uint8_t f6; std::uint32_t f7; };
struct m9 { std::uint8_t f0; float f1; pb::repeated<std::uint8_t, 16> f2; double f3; pb::repeated<float, 4> f4; std::optional<float> f5; pb::sint32 f6; std::string_view f7; };
struct m10 { pb::repeated<std::uint8_t, 16> f0; std::int32_t f1; std::optional<std::uint32_t> f2; std::int16_t f3; std::uint64_t f4; pb::repeated<std::int32_t, 8> f5; pb::sint32 f6; std::uint8_t f7; };
struct m11 { double f0; std::string_view f1; std::optional<std::uint32_t> f2; pb::repeated<std::uint8_t, 16> f3; std::uint32_t f4; std::optional<float> f5; std::uint8_t f6; bool f7; };
struct m12 { double f0; std::int16_t f1; pb::sint32 f2; std::optional<float> f3; std::uint64_t f4; float f5; std::uint32_t f6; pb::repeated<std::int32_t, 8> f7; };
struct m13 { std::optional<std::uint32_t> f0; pb::repeated<float, 4> f1; std::uint64_t f2; std::string_view f3; pb::fixed32 f4; double f5; std::uint8_t f6; bool f7; };
struct m14 { std::uint32_t f0; pb::repeated<float, 4> f1; double f2; pb::repeated<std::uint8_t, 16> f3; std::uint64_t f4; pb::sint32 f5; std::optional<float> f6; pb::fixed32 f7; };
struct m15 { pb::repeated<float, 4> f0; std::uint8_t f1; std::uint32_t f2; std::optional<std::uint32_t> f3; std::string_view f4; pb::fixed32 f5; std::uint64_t f6; float f7; };
struct m16 { std::optional<float> f0; std::optional<std::uint32_t> f1; std::uint8_t f2; std::string_view f3; pb::repeated<float, 4> f4; double f5; std::uint32_t f6; std::uint64_t f7; };
struct m17 { std::int16_t f0; std::optional<std::uint32_t> f1; pb::fixed32 f2; std::uint32_t f3; float f4; std::uint8_t f5; pb::sint32 f6; std::uint64_t f7; };
struct m18 { std::string_view f0; std::optional<float> f1; std::int32_t f2; double f3; std::uint64_t f4; bool f5; std::uint32_t f6; pb::repeated<std::int32_t, 8> f7; };
struct m19 { pb::sint32 f0; std::optional<float> f1; std::uint32_t f2; std::optional<std::uint32_t> f3; pb::repeated<std::int32_t, 8> f4; bool f5; float f6; std::int16_t f7; };
struct m20 { float f0; double f1; pb::fixed32 f2; pb::sint32 f3; std::string_view f4; bool f5; std::int32_t f6; pb::repeated<std::int32_t, 8> f7; };
struct m21 { std::string_view f0; bool f1; std::uint64_t f2; pb::sint32 f3; std::int16_t f4; pb::repeated<std::int32_t, 8> f5; pb::repeated<std::uint8_t, 16> f6; std::optional<std::uint32_t> f7; };
struct m22 { std::int16_t f0; std::optional<std::uint32_t> f1; pb::repeated<std::int32_t, 8> f2; std::int32_t f3; std::uint32_t f4; bool f5; pb::repeated<float, 4> f6; std::string_view f7; };
struct m23 { std::optional<float> f0; double f1; float f2; bool f3; std::int32_t f4; pb::repeated<std::int32_t, 8> f5; std::uint64_t f6; std::uint8_t f7; };
struct m24 { std::optional<float> f0; std::uint8_t f1; std::uint32_t f2; float f3; pb::repeated<std::int32_t, 8> f4; pb::repeated<float, 4> f5; pb::sint32 f6; pb::repeated<std::uint8_t, 16> f7; };
struct m25 { std::string_view f0; std::optional<std::uint32_t> f1; pb::repeated<std::uint8_t, 16> f2; std::uint64_t f3; std::int16_t f4; pb::repeated<float, 4> f5; double f6; float f7; };
struct m26 { pb::repeated<std::int32_t, 8> f0; float f1; std::uint64_t f2; std::int16_t f3; std::uint32_t f4; pb::repeated<float, 4> f5; pb::fixed32 f6; std::string_view f7; };
struct m27 { std::optional<float> f0; std::uint32_t f1; pb::repeated<std::uint8_t, 16> f2; bool f3; pb::sint32 f4; float f5; pb::repeated<std::int32_t, 8> f6; double f7; };
struct m28 { pb::sint32 f0; std::optional<float> f1; std::int32_t f2; bool f3; double f4; std::uint8_t f5; pb::repeated<float, 4> f6; pb::repeated<std::uint8_t, 16> f7; };
struct m29 { bool f0; std::uint32_t f1; std::uint64_t f2; pb::repeated<std::int32_t, 8> f3; pb::fixed32 f4; float f5; pb::repeated<std::uint8_t, 16> f6; std::optional<std::uint32_t> f7; };
struct m30 { std::string_view f0; std::optional<float> f1; pb::repeated<std::int32_t, 8> f2; double f3; pb::repeated<std::uint8_t, 16> f4; pb::fixed32 f5; std::uint64_t f6; std::uint32_t f7; };
struct m31 { double f0; float f1; std::string_view f2; std::int32_t f3; pb::repeated<std::int32_t, 8> f4; pb::fixed32 f5; pb::repeated<float, 4> f6; pb::repeated<std::uint8_t, 16> f7; };
struct top { std::uint32_t id; std::variant<std::monostate, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19, m20, m21, m22, m23, m24, m25, m26, m27, m28, m29, m30, m31> payload; };
```

## Reference: the prototype

The prototype is not in the repository. It handled scalars, strings, bytes, nested messages, `std::optional`, `std::variant` and `pb::repeated`. It did not handle `pb::span_buffer`, fixed-length bytes, `std::array` and `std::span` sequences, and the reset from descriptors. It kept the template reset, and it did not reset the element of a repeated message field. The final code must handle all of these, so the fixed part will be somewhat larger than in the prototype. The size targets in "Goals" allow for this.

Its results, which the targets come from (GCC 16.2, `-Os`, x86-64, encode and decode, code and tables):

| Message types | Template code | Prototype |
| --- | --- | --- |
| 1 | 2729 | 4959 |
| 2 | 4682 | 5256 |
| 4 | 7941 | 5731 |
| 8 | 12794 | 6675 |
| 16 | 20411 | 8655 |
| 32 | 35490 | 12793 |

It gave the same bytes as the template encoder for four test messages. It gave the same result as the template decoder for each prefix and each single-byte corruption of those messages, under AddressSanitizer and UndefinedBehaviorSanitizer.
