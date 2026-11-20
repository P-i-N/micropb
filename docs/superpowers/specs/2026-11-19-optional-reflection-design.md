# Optional reflection and narrow integer types

Date: 2026-11-19. Status: draft for approval. This document changes `2026-11-19-field-lists-design.md` (CMake part), `2026-11-19-protoc-plugin-design.md` (options file) and the type rules of `2026-11-18-micropb-design.md`.

It has three parts: one CMake target for GCC 16 and the Arm GNU Toolchain; the standard integer types (`int`, `long`, ...) by size and sign; narrow `sint32`/`sint64` members and narrow enums from the options file.

## Intent

The firmware is built with the Arm GNU Toolchain, which has no C++26 reflection. The code of `micropb` already works without reflection (field lists, `2026-11-19-field-lists-design.md`). The build does not: the target `micropb` always adds `-std=c++26 -freflection`, so a firmware project that links it fails with an unclear compiler error, and the README says that GCC 16.2 is required. The user wants reflection to be optional, so that older compilers can use the library. For now, "older compilers" means only the Arm GNU Toolchain.

## Goals

* A project links `micropb` and it works on GCC 16 (with reflection) and on the Arm GNU Toolchain 13.3 and 14.2 (without reflection). No second target to know about.
* The build of `micropb` shows that the Arm toolchains accept the library, so that a change that breaks them is found at once.
* The README states the real requirements.
* A struct with `int` or `long` members compiles the same way on the host and on the firmware.
* The options file can make a `sint32`/`sint64` field and an enum as narrow in RAM as an `int32` field is today with `int_size`. Small values stay short on the wire.

## Non-goals

* clang, MSVC, or other compilers. (A probe with clang 20 and the MSVC library gave warnings for `gnu::noclone` and for `offsetof` on `std::optional`; these stay open.)
* Running the tests on an Arm target or in an emulator. The Arm tests only compile.
* A code size measurement for Arm. The size report uses hand-written structs that need reflection.
* A change to the wire format or to the layout of the descriptors.
* Narrow `fixed32`, `sfixed32`, `fixed64` and `sfixed64` members. These are fixed width on the wire, so a narrow member saves only RAM. Not asked for.
* A member type with a different sign than its field (for example `sint32` as `std::uint16_t`). Negative values do not survive a round trip.
* `(nanopb)` options inside the `.proto` file. The new options are read only from the options file, as all options today.

## Anti-goals

* A `micropb` target that silently uses a different language standard than the one the user asked for with `MICROPB_REFLECTION=ON`.
* A build of `micropb` or of its tests that needs an Arm toolchain. Without one, the Arm tests are off and all other tests run.

## Approaches

* **A. One target that detects reflection.** Selected by the user.
* B. Two targets (`micropb`, `micropb_cxx23`) and a README text about the choice. Rejected: the default target still fails on the Arm compiler, and each firmware project must know about the second target.
* C. Two build configurations (host, and Arm with a toolchain file). Rejected: two build folders for each change, more than "Arm only for now" needs.

## Design

### The target `micropb`

* A cache option `MICROPB_REFLECTION` with the values `AUTO` (default), `ON` and `OFF`.
    * `AUTO`: CMake compiles a small test source once with `-std=c++26 -freflection` (`check_cxx_source_compiles`; the source fails when `__cpp_impl_reflection` is not defined). When it compiles, the target gives C++26 and `-freflection`; else C++23 only.
    * `ON`: C++26 and `-freflection`. A compiler that fails the check is a `FATAL_ERROR` that names the option.
    * `OFF`: C++23 only, also on GCC 16. Thus the host can use exactly the tables of the firmware.
* CMake prints one status line: `micropb: reflection on` or `micropb: reflection off (C++23, field lists only)`.
* `micropb_cxx23` stays: the target that never uses reflection. The tests use it for the C++23 configuration on the host. The README names it only as this.

### The Arm tests

* A cache variable `MICROPB_ARM_COMPILERS`: a list of paths to `arm-none-eabi-g++`. By default CMake fills it with `arm-none-eabi-g++` from `PATH` and with each `bin/arm-none-eabi-g++.exe` under the standard Windows install folder of the Arm GNU Toolchain (`C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*`). The same compiler is listed once. An empty list turns the Arm tests off, with one status line.
* For each compiler and each of `test_wire`, `test_types`, `test_field_list` and `test_generated`, CTest has a test `arm_<version>.<test>` that compiles the source to an object file with `-std=c++23 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Os -mcpu=cortex-m4 -mthumb`. The test passes when the compiler returns 0.
    * `test_wire`, `test_types` and `test_field_list` are mostly `static_assert` tests, so the compile runs them on a 32-bit target. `test_generated` only compiles.
    * The field descriptor has a size limit for 32-bit targets (12 bytes); its `static_assert` is now checked on a real 32-bit target.
* For each compiler, one more test examines the object of `test_generated` with that toolchain's `nm` for start-up code (`_GLOBAL__sub_I`), as `no_startup_code` does on the host.

### A finding of the probe

`tests/test_wire.cpp` does not compile on the Arm toolchains: `for (std::int32_t v : {0, -1, 1, -2, 2, i32_max, i32_min})` mixes `int` and `long`, because `std::int32_t` is `long` on `arm-none-eabi`. The library compiles; the test is wrong. The fix gives the list an explicit element type.

The same fact affects users: the library accepts the integer types `std::int8_t` ... `std::uint64_t` by exact type. On `arm-none-eabi`, `std::int32_t` is `long`, so a hand-written struct with an `int` member is a compile error on the firmware and compiles on the host. Generated structs use `std::int32_t` and are not affected.

**Decision of the user: the library accepts each standard integer type by its size and sign.**

* Accepted: `signed char`, `short`, `int`, `long`, `long long` and their unsigned forms. Each one is encoded like the fixed-width type of the same size and sign: `int32` for a signed type of 1, 2 or 4 bytes, `int64` for a signed type of 8 bytes, `uint32` and `uint64` for the unsigned types. The decode rules (truncation to the width of the member) are the same as for the fixed-width types.
* Thus `long` is `int32` on `arm-none-eabi` and on Windows, and `int64` on Linux x86-64. This is the size of the type on that compiler, as for `std::int32_t` today.
* An `unsigned char` element in a sequence is `bytes`, as `std::uint8_t` is today (`std::uint8_t` is `unsigned char` on these compilers, so this is no change).
* Not accepted: plain `char` (its sign differs between compilers), `wchar_t`, `char8_t`, `char16_t`, `char32_t`. They stay compile errors with the error text of today.
* `pb::sint32`, `pb::fixed32` and the other wrappers do not change.
* The rule is in the type rules of `meta_schema.hpp`, which both front ends use, so it applies to reflected structs and to field lists.
* Tests: `test_schema.cpp` or `test_types.cpp` gets `static_assert`s that each accepted type has the expected kind and wire type, and that `char` and `wchar_t` are rejected (a compile-fail test). `test_encode.cpp` gets one message with `int`, `long` and `unsigned long long` members, compared with the bytes of the same message with fixed-width types. The Arm compile runs the `static_assert`s on a target where `std::int32_t` is `long`.
* README, "Type mapping": one line for the standard integer types.

### Narrow `sint32` and `sint64` members

Added on the user's request after the first draft. Today `int_size` changes only `int32`, `int64`, `uint32` and `uint64`, and `pb::sint32` always stores a `std::int32_t`. For a signed value that can be negative, the user has to choose between 10 bytes on the wire for each negative value (`int32` with `int_size`) and 4 bytes of RAM (`sint32`).

**Library.**

* `pb::wire_int<T, pb::int_encoding::ZIGZAG>` is accepted for each signed integer type `T` that the integer rule above accepts, of 1, 2, 4 or 8 bytes. A `T` of 1, 2 or 4 bytes is `sint32` on the wire; a `T` of 8 bytes is `sint64`. `pb::sint32` and `pb::sint64` stay as they are; they are two of these types.
* Decode: a member of 1, 2 or 8 bytes decodes the varint as 64-bit ZigZag and then truncates the value to the width of `T`. A member of 4 bytes truncates the varint to 32 bits first and then decodes it as 32-bit ZigZag (the rule of Google for `sint32`). The two rules give the same value for each value in range and differ only for an out-of-range `sint64` input: the `sint64` value 2^31 gives 0 in a 4-byte member.
* Encode: the value is sign-extended and written as ZigZag. For each value that `T` can hold, the bytes are the bytes of `pb::sint32` with the same value. (ZigZag of a value in the `int32` range gives the same varint in 32 and in 64 bits.)
* An unsigned `T` with `ZIGZAG` is a compile error with a new rule, for example `pb: zigzag_unsigned: ...` (name left to the builder), with a compile-fail test.
* `pb::wire_int<T, pb::int_encoding::FIXED>` with a `T` other than today's four stays a compile error (non-goal above).
* The codec already stores scalars by size and sign. The change is in the type rules of `meta_schema.hpp` and in `scalar_code_of` in `codec.hpp`, which today recognizes ZigZag only by the two exact types.
* A narrow ZigZag element in a repeated field is packed, as `pb::sint32` is. An element of 1 byte is not `bytes`: only `std::uint8_t` elements are `bytes`.

**Plugin.**

* `int_size` also applies to `sint32` and `sint64`. With `IS_8`, `IS_16`, `IS_32` or `IS_64` the member is `pb::wire_int<std::intN_t, pb::int_encoding::ZIGZAG>`. (Builder's call: no new alias names such as `pb::sint16`. The generated text names the existing template.)
* `IS_32` on `sint32` and `IS_64` on `sint64` give `pb::sint32` and `pb::sint64`, as without the option.
* `int_size` stays without effect, with the warning of today, on `fixed32`, `sfixed32`, `fixed64`, `sfixed64`, `bool`, `float`, `double`, strings, bytes, messages and enum fields.

**Tests.** `static_assert`s for the kind and the wire type of each narrow ZigZag type; an encode and decode test of a message with `int8_t`, `int16_t` and `int64_t` ZigZag members whose bytes are compared with the same message with `pb::sint32` and `pb::sint64`, including a value that is too wide and is truncated on decode; the compile-fail test for an unsigned `T`; plugin tests for `int_size` on `sint32` and `sint64`, single and repeated, and the generated header compiled in `test_generated` (both front ends, so the `std::is_same_v` check of the field list covers it).

### Narrow enums

Added on the user's request after the first draft. Today the plugin writes each enum as `enum class E : std::int32_t`. The library already accepts each underlying type of 4 bytes or less.

* New option `enum_intsize` in the options file, with the values `IS_8`, `IS_16` and `IS_32`, as in NanoPB. It applies to an enum type, not to a field: the pattern is compared with the full name of the enum (`package.Enum`, or `package.Message.Enum` for a nested enum) and with the name without the package, by the rules of today.
* The underlying type is `std::int8_t`, `std::int16_t` or `std::int32_t`. An enum value outside that range is a plugin error that names the enum, the value and the smallest size that fits.
* `IS_64` is a plugin error: a proto3 enum is `int32` (library rule `enum_too_wide`).
* `enum_intsize` on a field, and `int_size` on an enum field, are ignored with the warning of today ("does not apply").
* The warning "pattern matches no field" also considers enum names, so that a line for an enum does not give it.
* Wire: no change. An enum is an `int32` varint whatever the underlying type, so values 0 to 127 are 1 byte. The option saves RAM on the device.
* Tests: plugin tests for each value, a nested enum, a wildcard, a value that does not fit, `IS_64`, and the option on a field; one narrow enum in `tests/generator/features.proto` with `features.options`, so that the checked-in header is compiled and checked against reflection in `test_generated`.

### README

* "Generate structs from a .proto file", the options table: `int_size` on `sint32` and `sint64`, and the new `enum_intsize`.
* "Add it to a project": the requirements become "a C++23 compiler; GCC 16 with reflection for structs without a field list". The option `MICROPB_REFLECTION` and the status line are described.
* "Compilers without reflection": tested with the Arm GNU Toolchain 13.3 and 14.2 (compile only).
* "Build and test": the Arm tests and `MICROPB_ARM_COMPILERS`.
* "Status and limits": the Arm line and the compiler line change. The uncommitted change of today to this section is part of this work.

## Left to the builder

* The test source for the reflection check and the name of the CMake helper function.
* How the Arm test names get the version (from the folder name or from `--version`).
* The `-mcpu` value can be a cache variable if that is simple. Default `cortex-m4`.

## Changes: remove reflection

`2026-11-20-remove-reflection-design.md` removes `MICROPB_REFLECTION` and `micropb_cxx23` (`micropb` is unconditionally C++23). The standard integer rules, narrow ZigZag types, narrow enums, and the Arm compile tests stay.
