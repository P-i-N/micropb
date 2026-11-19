# Optional reflection and narrow integer types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One `micropb` CMake target that works on GCC 16 and on the Arm GNU Toolchain, Arm compile tests, standard integer types by size and sign, and narrow `sint32`/`sint64` members and enums from the options file.

**Architecture:** CMake detects reflection once and configures the `micropb` target; tests that need reflection are added only when it is on. The type rules in `meta_schema.hpp` widen (standard integers, narrow ZigZag); `scalar_code_of` in `codec.hpp` recognizes ZigZag by encoding instead of by two exact types. The plugin gets `int_size` on `sint32`/`sint64` and a new `enum_intsize` option.

**Tech Stack:** C++23/C++26 (GCC 16.2 MSYS2 UCRT64, Arm GNU Toolchain 13.3 and 14.2), CMake 3.25 + Ninja + CTest, Python 3 `unittest` with `protoc` 36.2.

**Spec:** `docs/superpowers/specs/2026-11-19-optional-reflection-design.md`

## Global Constraints

* Headers stay in `src/micropb/`, namespace `pb`, house snake_case, errors as `std::expected<std::size_t, pb::error>`. No heap, no exceptions, no RTTI.
* Library code without reflection must compile as C++23. Reflection code stays inside `#if defined(__cpp_impl_reflection)`.
* The wire format does not change. All reference vectors pass without a change to the vectors.
* Each compile-time rule is a `static_assert` with the text `pb: <rule>: ...`, and each new rule has a compile-fail test whose file name or registered rule matches that text.
* All existing tests keep passing with unchanged expectations (except `test_wire.cpp` line 37, Task 2).
* Tests build with `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
* Arm compile flags, verbatim from the spec: `-std=c++23 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Os -mcpu=cortex-m4 -mthumb`.
* Status lines, verbatim: `micropb: reflection on` and `micropb: reflection off (C++23, field lists only)`.
* Plain `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t` stay compile errors with today's `pb: unmapped_type:` text.
* Generated headers are checked in: after a plugin change, regenerate with `MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest` and commit the result.
* Build and test commands below assume `PATH` starts with `C:\msys64\ucrt64\bin` for the compiler, and that `python` is the user's Python 3.13 with `protobuf` (`C:\Users\janp\AppData\Local\Programs\Python\Python313\python.exe`), not MSYS2's.
* Commit messages: `type(micropb): text`, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A firmware project that does `add_subdirectory(micropb)` with the Arm toolchain must not build micropb's host tests and must get `reflection off`. Pinned in Task 2 by the `arm_<version>.consumer` test.
2. `long` is 4 bytes on Windows and Arm and 8 bytes on Linux x86-64: a test that hard-codes `long` as `int32` breaks on Linux. Pinned in Task 3 by asserting against `sizeof(long)`.
3. A sequence of `signed char` or of a 1-byte ZigZag type must be a packed repeated field, not `bytes`; only `std::uint8_t` (`unsigned char`) elements are `bytes`. Pinned in Tasks 3 and 4.
4. A PC sends a `sint32` value wider than the narrow member (for example 100000 into a 16-bit ZigZag member): decode succeeds and truncates, as `int64` -> `int32` does today. Pinned in Task 4.
5. `enum_intsize` range boundaries: `IS_8` accepts -128 and 127 and rejects -129 and 128; the error names the smallest size that fits. Pinned in Task 6.

---

### Task 1: One `micropb` target with `MICROPB_REFLECTION`

**Files:**
- Modify: `CMakeLists.txt` (target block lines 6-17; test registration lines 38-160)
- Modify: `README.md` section "Add it to a project" (lines 166-187)

**Interfaces:**
- Produces: cache option `MICROPB_REFLECTION` (`AUTO`|`ON`|`OFF`, default `AUTO`); cache option `MICROPB_TESTS` (BOOL, default `${PROJECT_IS_TOP_LEVEL}`); normal variable `MICROPB_HAS_REFLECTION` (TRUE/FALSE) that later tasks use to guard reflection-only tests; target `micropb_cxx23` unchanged.

Ruling recorded in this plan (not in the spec): `MICROPB_TESTS` exists because `add_subdirectory(micropb)` in a firmware project would otherwise compile micropb's host test executables with the Arm compiler. The spec's goal "a project links `micropb` and it works on the Arm GNU Toolchain" needs it. The README limit "`add_subdirectory` also adds the tests" goes away.

- [ ] **Step 1: Implement the detection in `CMakeLists.txt`**

`include(CheckCXXSourceCompiles)`; with `CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection"` compile a source that is `#if !defined(__cpp_impl_reflection)` `#error` `#endif` plus `int main() {}`, result variable `MICROPB_COMPILER_HAS_REFLECTION`. Then:
* `AUTO`: `MICROPB_HAS_REFLECTION` = the check result.
* `ON` and the check failed: `message(FATAL_ERROR "micropb: MICROPB_REFLECTION is ON, but the compiler does not support -std=c++26 -freflection")`.
* `OFF`: `MICROPB_HAS_REFLECTION` FALSE without running the check.
* A value other than the three: `FATAL_ERROR` naming the three values.
* With reflection: `micropb` gets `cxx_std_26` and `-freflection` (as today). Without: `cxx_std_23` only.
* `message(STATUS ...)` with one of the two verbatim status lines.

- [ ] **Step 2: Guard the tests**

Wrap everything after the two library targets in `if(MICROPB_TESTS)` (`enable_testing()` included). Inside, the tests that need reflection are added only `if(MICROPB_HAS_REFLECTION)`: `test_schema`, `test_codec`, `test_encode`, `test_decode`, `test_wire_compat`, the `micropb_add_compile_fail_test` loop, the `size_report_*` objects, the `size_report` target and `micropb_strict_warnings`. `test_wire`, `test_types`, `test_field_list`, `test_generated` (linked to `micropb`), the `*_cxx23` tests, `no_startup_code`, the `generator` test and the `micropb_add_list_compile_fail_test` tests stay unconditional inside `MICROPB_TESTS`.

- [ ] **Step 3: Verify the three modes**

```sh
cmake -S . -B .build/auto -G Ninja            # expect "micropb: reflection on"
cmake --build .build/auto && ctest --test-dir .build/auto        # expect 100% of 58 tests
cmake -S . -B .build/off -G Ninja -DMICROPB_REFLECTION=OFF     # expect "micropb: reflection off (C++23, field lists only)"
cmake --build .build/off && ctest --test-dir .build/off          # expect 100%, fewer tests, no test_encode
cmake -S . -B .build/bad -G Ninja -DMICROPB_REFLECTION=MAYBE   # expect FATAL_ERROR naming AUTO, ON, OFF
cmake -S . -B .build/notests -G Ninja -DMICROPB_TESTS=OFF && cmake --build .build/notests   # expect no test targets built
```

- [ ] **Step 4: README "Add it to a project"**

Requirements become: a C++23 compiler; GCC 16 with `-std=c++26 -freflection` for structs without a field list. Describe `MICROPB_REFLECTION` (three values, the status line), `MICROPB_TESTS` (default on only when micropb is the top-level project), and `micropb_cxx23` as the target that never uses reflection. Without CMake: C++23, or C++26 with `-freflection`.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt README.md
git commit -m "build(micropb): detect reflection and add the options MICROPB_REFLECTION and MICROPB_TESTS"
```

---

### Task 2: Arm compile tests and a consumer project

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/test_wire.cpp:37`
- Create: `tests/consumer/CMakeLists.txt`, `tests/consumer/consumer.cpp`
- Modify: `README.md` sections "Build and test", "Compilers without reflection" (the NOTE at line ~469), "Status and limits" (Compiler, ARM, CMake lines; the uncommitted working-tree change to this section is kept and included)

**Interfaces:**
- Consumes: `MICROPB_TESTS`, `MICROPB_HAS_REFLECTION` (Task 1).
- Produces: CMake function `micropb_add_arm_test(<test name>)` that registers the compile test of `tests/<test name>.cpp` for every Arm compiler; Tasks 3 and 4 call it for `test_scalars`. Cache variables `MICROPB_ARM_COMPILERS` (list of paths) and `MICROPB_ARM_CPU` (default `cortex-m4`).

- [ ] **Step 1: Write the failing Arm test**

In `CMakeLists.txt`, inside `if(MICROPB_TESTS)`: fill `MICROPB_ARM_COMPILERS` (if not set by the user) with `find_program` of `arm-none-eabi-g++` on `PATH` plus each `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin/arm-none-eabi-g++.exe` (`file(GLOB)`), deduplicated by `get_filename_component(... REALPATH)`. Get each compiler's version with `execute_process(<g++> -dumpversion)`; the test prefix is `arm_<version>` (for example `arm_14.2.1`). Empty list: `message(STATUS "micropb: no arm-none-eabi-g++ found, the Arm tests are off")`.

`micropb_add_arm_test(name)`: for each compiler, `add_test(NAME arm_<version>.<name> COMMAND <g++> <Arm flags> -mcpu=${MICROPB_ARM_CPU} -I${CMAKE_CURRENT_SOURCE_DIR}/src -I${CMAKE_CURRENT_SOURCE_DIR}/tests -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/<name>.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/arm_<version>/<name>.o)` (create the folder at configure time). Call it for `test_wire`, `test_types`, `test_field_list`, `test_generated`.

Start-up code: for each compiler, `arm_<version>.no_startup_code` runs `arm-none-eabi-nm` from the same `bin` folder on `arm_<version>/test_generated.o`, `FAIL_REGULAR_EXPRESSION "_GLOBAL__sub_I"`, and `DEPENDS`/`FIXTURES_REQUIRED` on the `test_generated` compile test so it runs after it.

- [ ] **Step 2: Run and see `test_wire` fail**

Run: `cmake -S . -B .build/auto -G Ninja && ctest --test-dir .build/auto -R "^arm_" --output-on-failure`
Expected: `arm_13.3.1.test_wire` and `arm_14.2.1.test_wire` FAIL with `unable to deduce 'std::initializer_list<auto>&&'`; the other Arm tests pass.

- [ ] **Step 3: Fix `tests/test_wire.cpp:37`**

Give the list an explicit element type, for example `for (std::int32_t v : std::initializer_list<std::int32_t>{0, -1, 1, -2, 2, i32_max, i32_min})` or a `constexpr std::array<std::int32_t, 7>`. Check the rest of `test_wire.cpp` and `test_types.cpp` for the same mix (`int` literals with `std::int32_t` constants) only if the Arm compile reports more.

- [ ] **Step 4: Consumer project test**

`tests/consumer/CMakeLists.txt`: `cmake_minimum_required(VERSION 3.25)`, `project(consumer LANGUAGES CXX)`, `add_subdirectory(${MICROPB_SOURCE_DIR} micropb)`, `add_library(consumer STATIC consumer.cpp)`, `target_link_libraries(consumer PRIVATE micropb)`, `target_compile_options(consumer PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)`. `consumer.cpp` includes `tests/vectors/vectors.pb.hpp` (via `${MICROPB_SOURCE_DIR}/tests` on the include path) and `<micropb/micropb.hpp>`, and defines two non-inline functions that call `pb::encode` and `pb::decode` on one generated message type.

For each Arm compiler, a test `arm_<version>.consumer` runs, in a fresh folder under the build directory, `cmake -S tests/consumer -B <folder> -G Ninja -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_CXX_COMPILER=<g++> -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_CXX_FLAGS="-mcpu=${MICROPB_ARM_CPU} -mthumb" -DMICROPB_SOURCE_DIR=<repo>` and then `cmake --build <folder>`, in one `cmake -P` script `tests/consumer/run.cmake` (inputs passed with `-D`: the compiler, the CPU, the source and binary folders, and `MODE` = `build` or `reflection_on`). The script runs each step with `execute_process(... RESULT_VARIABLE ... OUTPUT_VARIABLE ... ERROR_VARIABLE ...)`, prints the output, and calls `message(FATAL_ERROR)` when a step returns non-zero or when the configure output does not contain `micropb: reflection off (C++23, field lists only)`. The test passes when the script returns 0. It removes its folder first, so each run configures from scratch. A second test `arm_<version>.consumer_reflection_on` runs the same script with `MODE=reflection_on`: only the configure step, with `-DMICROPB_REFLECTION=ON`; the script returns 0 only when configure fails and its output contains `MICROPB_REFLECTION is ON, but the compiler does not support`.

- [ ] **Step 5: Run all tests**

Run: `cmake --build .build/auto && ctest --test-dir .build/auto`
Expected: 100% pass; with two Arm toolchains, 2 × (4 compile + 1 nm + 2 consumer) = 14 more tests than after Task 1.

- [ ] **Step 6: README**

"Build and test": the Arm tests, `MICROPB_ARM_COMPILERS`, `MICROPB_ARM_CPU`, that they only compile, and that they are off without an Arm toolchain. "Compilers without reflection": replace the NOTE with "Tested with the Arm GNU Toolchain 13.3 and 14.2 (compile only, `-mcpu=cortex-m4`)". "Status and limits": ARM line (compiled with 13.3 and 14.2, not run, not measured); CMake line (tests only when micropb is the top-level project, `MICROPB_TESTS`).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt tests/test_wire.cpp tests/consumer README.md
git commit -m "test(micropb): compile the tests and a consumer project with the Arm GNU Toolchain"
```

---

### Task 3: Standard integer types by size and sign

**Files:**
- Modify: `src/micropb/meta_schema.hpp` (`detail::is_scalar`, line ~117)
- Create: `tests/test_scalars.cpp`
- Create: `tests/compile_fail/char_member.cpp`, `tests/compile_fail/wchar_member.cpp`
- Modify: `CMakeLists.txt` (register `test_scalars` for `micropb`, as `_cxx23`, and with `micropb_add_arm_test`; register the two compile-fail tests with `micropb_add_list_compile_fail_test(<name> unmapped_type)`)
- Modify: `README.md` "Type mapping" (table and the paragraph "Use only the fixed-width integer types ...")

**Interfaces:**
- Consumes: `micropb_add_arm_test` (Task 2).
- Produces: `tests/test_scalars.cpp`, a test without reflection (structs with hand-written `pb::fields_of` lists, as in `tests/test_field_list.cpp`), that Task 4 extends.

- [ ] **Step 1: Write the failing tests in `tests/test_scalars.cpp`**

`static_assert`s, using `pb::kind_of`, `pb::wire_type_of` and `pb::is_packed` (check their exact namespace in `meta_schema.hpp`):

```cpp
static_assert(pb::kind_of<signed char>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<short>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<int>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<long long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned short>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned int>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned long>() == pb::field_kind::SCALAR);
static_assert(pb::kind_of<unsigned long long>() == pb::field_kind::SCALAR);
static_assert(pb::wire_type_of<long>() == pb::wire_type::VARINT);
static_assert(pb::kind_of<pb::repeated<signed char, 4>>() == pb::field_kind::REPEATED);   // not bytes
static_assert(pb::kind_of<pb::repeated<unsigned char, 4>>() == pb::field_kind::BYTES);
static_assert(pb::is_packed<pb::repeated<signed char, 4>>());
```

Run-time test `test_standard_integers()`: struct `standard { int a; long b; unsigned long long c; short d; signed char e; unsigned int f; }` and struct `fixed_width` with the same field numbers and the fixed-width type of the same size and sign for each member (`long` -> `std::int32_t` when `sizeof(long) == 4`, else `std::int64_t`; write this with `std::conditional_t`, never a hard-coded width). Both get `pb::fields_of` lists. Values: `a = -1`, `b = -123456`, `c = 0xFFFF'FFFF'FFFF'FFFF`, `d = -300`, `e = -5`, `f = 4000000000u`. Encode both; the bytes and `encoded_size` must be equal. Decode the bytes into a `standard`; every member equals the original. `main` runs it and returns non-zero on failure (the style of the existing tests).

`tests/compile_fail/char_member.cpp`: a struct with a `char` member and a hand-written field list, used with `pb::encoded_size`. `wchar_member.cpp`: the same with `wchar_t`.

- [ ] **Step 2: Run them and see them fail**

Run: `cmake --build .build/auto` — expected: `test_scalars` fails to compile with `pb: unmapped_type:` for `short` (and for `int` on Arm). The two compile-fail tests already pass (they must keep passing after Step 3).

- [ ] **Step 3: Widen `detail::is_scalar` in `meta_schema.hpp`**

Accept `std::is_integral_v<T>` types that are not `bool`, plain `char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`, in addition to today's list. The codec needs no change: `scalar_code_of` already works from `sizeof` and `std::is_signed_v`. Check that `kind_of`'s bytes test (`std::is_same_v<element_t<T>, std::uint8_t>`) is unchanged.

- [ ] **Step 4: Run the tests**

Run: `cmake -S . -B .build/auto -G Ninja && cmake --build .build/auto && ctest --test-dir .build/auto`
Expected: 100% pass, including `test_scalars`, `test_scalars_cxx23`, `arm_*.test_scalars`, `compile_fail.char_member`, `compile_fail.wchar_member`.

- [ ] **Step 5: README "Type mapping"**

Add a row: `signed char`, `short`, `int`, `long`, `long long` and the unsigned forms -> the proto type of the fixed-width type of the same size and sign; note that `long` is `int32` on Windows and `arm-none-eabi` and `int64` on Linux x86-64. Replace the paragraph "Use only the fixed-width integer types ..." with: plain `char` and the character types do not compile; the fixed-width types are the portable choice when the `.proto` file must be the same for each target.

- [ ] **Step 6: Commit**

```bash
git add src/micropb/meta_schema.hpp tests/test_scalars.cpp tests/compile_fail/char_member.cpp tests/compile_fail/wchar_member.cpp CMakeLists.txt README.md
git commit -m "feat(micropb): accept the standard integer types by size and sign"
```

---

### Task 4: Narrow ZigZag members in the library

**Files:**
- Modify: `src/micropb/meta_schema.hpp` (`detail::is_scalar`, `wire_type_of`, a new rule)
- Modify: `src/micropb/codec.hpp:47-60` (`scalar_code_of`)
- Modify: `tests/test_scalars.cpp`
- Create: `tests/compile_fail/zigzag_unsigned.cpp`
- Modify: `CMakeLists.txt` (`micropb_add_list_compile_fail_test(zigzag_unsigned zigzag_unsigned)`)
- Modify: `README.md` "Type mapping"

**Interfaces:**
- Consumes: `tests/test_scalars.cpp` (Task 3); `pb::wire_int<T, pb::int_encoding E>` and `pb::int_encoding::ZIGZAG` from `types.hpp` (unchanged).
- Produces: `pb::wire_int<T, pb::int_encoding::ZIGZAG>` accepted for every signed integer `T` that Task 3 accepts; Task 5's generated text names it exactly as `pb::wire_int<std::int16_t, pb::int_encoding::ZIGZAG>`.

- [ ] **Step 1: Write the failing tests in `tests/test_scalars.cpp`**

```cpp
using zz8 = pb::wire_int<std::int8_t, pb::int_encoding::ZIGZAG>;
using zz16 = pb::wire_int<std::int16_t, pb::int_encoding::ZIGZAG>;
using zz_int = pb::wire_int<int, pb::int_encoding::ZIGZAG>;
using zz64 = pb::wire_int<long long, pb::int_encoding::ZIGZAG>;
static_assert(pb::kind_of<zz8>() == pb::field_kind::SCALAR);
static_assert(pb::wire_type_of<zz16>() == pb::wire_type::VARINT);
static_assert(pb::kind_of<pb::repeated<zz8, 4>>() == pb::field_kind::REPEATED);   // not bytes
static_assert(pb::is_packed<pb::repeated<zz8, 4>>());
```

Run-time `test_narrow_zigzag()`: struct `narrow { zz8 a; zz16 b; zz_int c; zz64 d; pb::repeated<zz16, 3> e; }` and struct `wide { pb::sint32 a; pb::sint32 b; pb::sint32 c; pb::sint64 d; pb::repeated<pb::sint32, 3> e; }`, same numbers 1-5, field lists for both. Values `a = -128`, `b = -32768`, `c = -1`, `d = -5'000'000'000`, `e = {-1, 0, 32767}`. Encode both: equal bytes and equal `encoded_size`. Decode into `narrow`: equal values. Truncation: encode `wide` with `b = 100000` (and the other fields zero), decode into `narrow`: success, and `narrow.b == static_cast<std::int16_t>(100000)` (= -31072).

`tests/compile_fail/zigzag_unsigned.cpp`: a struct with a `pb::wire_int<std::uint16_t, pb::int_encoding::ZIGZAG>` member and a field list, used with `pb::encoded_size`.

- [ ] **Step 2: Run them and see them fail**

Run: `cmake --build .build/auto`
Expected: `test_scalars` fails with `pb: unmapped_type:` for `zz8`; `compile_fail.zigzag_unsigned` fails because the error text is `unmapped_type`, not `zigzag_unsigned`.

- [ ] **Step 3: Implement**

* `meta_schema.hpp`: a trait `detail::is_zigzag<T>` (true for `wire_int<T2, ZIGZAG>`); `is_scalar` accepts it when `T2` is a signed integer type that Task 3's rule accepts. An unsigned `T2` gives `static_assert` `"pb: zigzag_unsigned: a ZigZag (sint32, sint64) member needs a signed integer type"` from `kind_of`. `wire_int<T2, FIXED>` stays limited to today's four aliases.
* `codec.hpp` `scalar_code_of`: replace `is_one_of<T, sint32, sint64>` with `detail::is_zigzag<T>`. The size bits come from `sizeof(T)`, which equals `sizeof(T2)`. Check the encoder and decoder paths for ZigZag of sizes 1 and 2: encode must sign-extend to 64 bits before ZigZag; decode must ZigZag-decode as 64 bits and then truncate to the stored size (the same path as an `int64` varint into a 2-byte member). If the interpreter assumes that a ZigZag scalar has size 4 or 8, fix that branch.

- [ ] **Step 4: Run the tests**

Run: `cmake --build .build/auto && ctest --test-dir .build/auto`
Expected: 100% pass, including the Arm compile of `test_scalars`.

- [ ] **Step 5: README "Type mapping"**

Row: `pb::wire_int<T, pb::int_encoding::ZIGZAG>` with a signed integer `T` -> `sint32` (1, 2 or 4 bytes) or `sint64` (8 bytes); decode truncates; `pb::sint32`/`pb::sint64` are the 4- and 8-byte cases. One sentence: this is what `int_size` on a `sint32` field gives (see the options file).

- [ ] **Step 6: Commit**

```bash
git add src/micropb/meta_schema.hpp src/micropb/codec.hpp tests/test_scalars.cpp tests/compile_fail/zigzag_unsigned.cpp CMakeLists.txt README.md
git commit -m "feat(micropb): accept narrow ZigZag members"
```

---

### Task 5: `int_size` on `sint32` and `sint64` in the plugin

**Files:**
- Modify: `tools/protoc-gen-micropb.py` (`SIZED_INTEGERS` line 56; the type text near line 296; the docstring options list)
- Modify: `tests/generator/test_plugin.py` (`OptionsTest`, `WarningTest`)
- Modify: `tests/generator/features.proto`, `tests/generator/features.options`, regenerated `tests/generator/features.pb.hpp`
- Modify: `tests/test_generated.cpp` (round trip of the new fields)
- Modify: `README.md` "The options file" table

**Interfaces:**
- Consumes: the generated text `pb::wire_int<std::intN_t, pb::int_encoding::ZIGZAG>` (Task 4).

- [ ] **Step 1: Write the failing plugin tests**

In `OptionsTest`:

```python
def test_int_size_on_sint(self):
    text = header("message m { sint32 a = 1; sint32 b = 2; sint64 c = 3; sint32 d = 4; sint64 e = 5; }",
                  "m.a int_size:IS_8\nm.b int_size:IS_16\nm.c int_size:IS_32\nm.d int_size:IS_32\nm.e int_size:IS_64\n")
    self.assertEqual(members(text, "m"), [
        "pb::wire_int<std::int8_t, pb::int_encoding::ZIGZAG> a;",
        "pb::wire_int<std::int16_t, pb::int_encoding::ZIGZAG> b;",
        "pb::wire_int<std::int32_t, pb::int_encoding::ZIGZAG> c;",
        "pb::sint32 d;",
        "pb::sint64 e;"])

def test_int_size_on_repeated_sint(self):
    text = header("message m { repeated sint32 a = 1; }", "m.a max_count:4 int_size:IS_8\n")
    self.assertEqual(members(text, "m"), ["pb::repeated<pb::wire_int<std::int8_t, pb::int_encoding::ZIGZAG>, 4> a;"])
```

In `WarningTest`, extend or add a test: `int_size` on `fixed32`, `sfixed32` and an enum field gives `warning: int_size does not apply to <name>` (look at `test_option_that_does_not_apply` for the form).

- [ ] **Step 2: Run them and see them fail**

Run: `python tests/generator/test_plugin.py OptionsTest WarningTest`
Expected: `test_int_size_on_sint` and `test_int_size_on_repeated_sint` FAIL (members stay `pb::sint32`, and a warning is printed).

- [ ] **Step 3: Implement in the plugin**

Add `TYPE_SINT32` and `TYPE_SINT64` to the types `int_size` applies to. For them: `IS_32` on `sint32` and `IS_64` on `sint64` keep `pb::sint32`/`pb::sint64`; any other size writes `pb::wire_int<std::int{bits}_t, pb::int_encoding::ZIGZAG>`. The `IS_8`-on-repeated-unsigned error stays only for unsigned types. Update the docstring list of options. Check that the include detection (`INCLUDES` patterns) adds `<cstdint>` and `<micropb/types.hpp>` for the new text.

- [ ] **Step 4: Run the plugin tests**

Run: `python tests/generator/test_plugin.py`
Expected: all pass except `CheckedInTest` if Step 5 is not done yet.

- [ ] **Step 5: Use it in `features.proto` and regenerate**

Add to `message Device` in `tests/generator/features.proto`: `sint32 trim_delta = <next free number>;` and `repeated sint64 history_delta = <next>;`. In `features.options`: `Device.trim_delta int_size:IS_16` and `Device.history_delta max_count:3 int_size:IS_16`. Regenerate: `MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest`. In `tests/test_generated.cpp`, extend the existing `Device` round trip (encode, decode, compare) with `trim_delta = -300` and `history_delta = {-1, 2, -32768}`. The reflection comparison of the field list in `test_generated.cpp` must still pass on GCC 16.

- [ ] **Step 6: Run all tests**

Run: `cmake --build .build/auto && ctest --test-dir .build/auto`
Expected: 100% pass (`generator`, `test_generated`, `test_generated_cxx23`, `arm_*.test_generated`).

- [ ] **Step 7: README**

In "The options file" table: `int_size` applies to `int32`, `int64`, `uint32`, `uint64`, `sint32`, `sint64`; a narrow `sint32`/`sint64` gives `pb::wire_int<std::intN_t, pb::int_encoding::ZIGZAG>`. One sentence: the sign of the C++ type is always the sign of the field.

- [ ] **Step 8: Commit**

```bash
git add tools/protoc-gen-micropb.py tests/generator tests/test_generated.cpp README.md
git commit -m "feat(micropb): apply int_size to sint32 and sint64 in the protoc plugin"
```

---

### Task 6: `enum_intsize` in the plugin

**Files:**
- Modify: `tools/protoc-gen-micropb.py` (`Options.PARSERS`, `enum_text` line ~243, `check_rules`, the docstring)
- Modify: `tests/generator/test_plugin.py` (`OptionsTest`, `ErrorTest`, `WarningTest`)
- Modify: `tests/generator/features.proto`, `tests/generator/features.options`, regenerated `tests/generator/features.pb.hpp`
- Modify: `tests/test_generated.cpp`
- Modify: `README.md` "The options file"

**Interfaces:**
- Consumes: `Options.of(name, package)` and `Options.check_use(name, found, used)` (existing; `of` sets `rule.matched`, so calling it for an enum's full name makes "matches no field" silent for enum rules).

- [ ] **Step 1: Write the failing plugin tests**

`OptionsTest`:
* `test_enum_intsize`: `enum E { E_A = 0; E_B = 1; }` with `E enum_intsize:IS_8` -> the header contains `enum class E : std::int8_t {`; `IS_16` -> `std::int16_t`; `IS_32` -> `std::int32_t`.
* `test_enum_intsize_nested_and_wildcard`: `message M { enum Mode { MODE_A = 0; } Mode m = 1; }` with `M.Mode enum_intsize:IS_8` -> `enum class M_Mode : std::int8_t {`; a second case with `pkg.*.Mode` / `*Mode` in a file with `package pkg;` gives the same.
* `test_enum_intsize_bounds`: `IS_8` with values -128 and 127 -> `std::int8_t`, no warning.

`ErrorTest`:
* `test_enum_intsize_value_does_not_fit`: values 0 and 128 with `IS_8` -> the error names the enum `E`, the value `128` and `IS_16`. Value -129 with `IS_8` -> names `-129` and `IS_16`. Value 40000 with `IS_16` -> names `IS_32`.
* `test_enum_intsize_64`: `E enum_intsize:IS_64` -> error that an enum is `int32` and `IS_64` is not possible.

`WarningTest`:
* `test_enum_intsize_on_a_field`: `m.a enum_intsize:IS_8` on an `int32` field -> `warning: enum_intsize does not apply to m.a`.
* `test_enum_rule_is_not_a_rule_without_a_match`: a file with only an enum rule gives no `matches no field` warning.

- [ ] **Step 2: Run them and see them fail**

Run: `python tests/generator/test_plugin.py OptionsTest ErrorTest WarningTest`
Expected: the new tests FAIL (`enum_intsize` is an unknown option today).

- [ ] **Step 3: Implement in the plugin**

* `PARSERS["enum_intsize"]`: accepts `IS_8`, `IS_16`, `IS_32`, `IS_64` (parse `IS_64` so that the error in the next point names the enum instead of the generic parser error).
* In `enum_text` (or where it is called): `found = options.of(enum.proto_name, package)`; `enum_intsize` gives the bits; `IS_64` -> `Error(f"{file}: {name}: enum_intsize:IS_64 is not possible, a proto3 enum is int32")` (follow the existing error text form). Find the smallest of 8, 16, 32 that holds all values; when it is larger than the requested size, `Error` with the enum name, the first value that does not fit and the size that fits, in the form of the other errors. Write `enum class {name} : std::int{bits}_t {`. `check_use(enum name, found, {"enum_intsize"})` warns for the other options on an enum rule.
* Fields: `enum_intsize` is not in the set of options a field uses, so `check_use` already warns. Check it.
* Update the docstring list of options.

- [ ] **Step 4: Run the plugin tests**

Run: `python tests/generator/test_plugin.py`
Expected: all pass except `CheckedInTest` until Step 5.

- [ ] **Step 5: Use it in `features.proto` and regenerate**

Add `Device.State enum_intsize:IS_8` to `features.options` (the enum `Device_State` exists in `features.proto`; if its values do not fit `IS_8`, use a new small enum). Regenerate with `MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest`. In `tests/test_generated.cpp`: `static_assert(sizeof(features::Device_State) == 1);` and keep the existing round trip of `state` passing.

- [ ] **Step 6: Run all tests**

Run: `cmake --build .build/auto && ctest --test-dir .build/auto`
Expected: 100% pass.

- [ ] **Step 7: README**

"The options file" table: `enum_intsize:IS_8|IS_16|IS_32`, applies to an enum (name pattern of the enum, `package.Enum` or `package.Message.Enum`), gives `std::int8_t` / `std::int16_t` / `std::int32_t`; a value that does not fit is an error; it saves RAM, not bytes on the wire.

- [ ] **Step 8: Commit**

```bash
git add tools/protoc-gen-micropb.py tests/generator tests/test_generated.cpp README.md
git commit -m "feat(micropb): add the option enum_intsize to the protoc plugin"
```
