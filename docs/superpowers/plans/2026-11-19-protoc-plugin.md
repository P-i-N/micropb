# `protoc-gen-micropb` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `protoc` plugin in Python that writes a header of plain C++26 structs for `micropb` from a proto3 file.

**Architecture:** One Python file, `tools/protoc-gen-micropb.py`. It reads a `CodeGeneratorRequest` with `google.protobuf.compiler.plugin_pb2`, flattens the types of each file, sorts the structs by dependency and writes text. Sizes come from a NanoPB-format `.options` file. Python tests run the plugin through `protoc`. Generated headers are checked in and compiled by a C++ test.

**Tech Stack:** Python 3 (`protobuf` package, `unittest`), `protoc`, C++26 (GCC 16, `-freflection`), CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-11-19-protoc-plugin-design.md`

## Global Constraints

* The plugin is one file. It imports only the standard library and `google.protobuf`.
* The build of the library and the C++ tests must not need `protoc` or Python.
* Generated text has no timestamp and no absolute path. Line ends are `\n`.
* Errors go to `CodeGeneratorResponse.error`. Warnings go to `stderr`. An error writes no header.
* Comments and documents use Simplified Technical English. Commit messages: `type(micropb): summary`.
* Test tools on this machine: the venv `<scratchpad>/pbenv` (`python -m grpc_tools.protoc`, protobuf 7.36.2). The tests find `protoc` in this order: environment variable `PROTOC`, `protoc` on `PATH`, the module `grpc_tools.protoc`.

## Review Focus

* A field whose name is equal to the name of its type (`inner inner = 1;`): the header must compile. The plugin writes the full type name there.
* `int_size:IS_8` on a repeated unsigned field would give a sequence of `std::uint8_t`, which the library reads as `bytes`. The plugin must stop with an error.
* A wildcard rule such as `* max_count:8` must not give a warning for each field that is not repeated.
* An import that the file does not use for a field type (for example `nanopb.proto`) must not give an `#include`.
* A message that is used before its definition in the `.proto` file, and a nested type that another top-level message uses.

---

### Task 1: Plugin core

**Files:** Create `tools/protoc-gen-micropb.py`, `tests/generator/test_plugin.py`.

**Interfaces:** Produces the test helper `run(files: dict[str, str], *protoc_args, cwd_sub="") -> Result(rc, headers: dict[str, str], stderr)` and, in the plugin, `main()`, `generate(request) -> response`, `class Error(Exception)`.

- [ ] Tests: a message with all 15 scalar types, `string`, `bytes`, an enum with prefix and without, a nested message field, the package to namespace rule, no package, the first line, the includes, an empty message, output path `sub/name.pb.hpp`. Run them, see them fail (no plugin).
- [ ] Implement, run, pass, commit.

### Task 2: Options file and repeated fields

**Interfaces:** Produces in the plugin `class Options` with `load(path)`, `for_field(full_name) -> dict` and `warnings()`.

- [ ] Tests: `max_count`, `fixed_count`, `max_size`, `fixed_length`, `int_size` (signed and unsigned), wildcard, last line wins, name with and without package, comments, `options_dir`, repeated string and message, `int_size` on a repeated field. Fail first.
- [ ] Implement, pass, commit.

### Task 3: Optional, oneof, numbers, nesting, order, imports

- [ ] Tests: `optional` scalar and message, a `oneof` with consecutive numbers and with gaps, `pb::tag` on a gap only, nested message and enum flattened to `Outer_Inner`, use before definition, a nested type used by another message, import with another package (`#include`, `::a::b::name`), import that is not used, keyword field name, field name equal to type name. Fail first.
- [ ] Implement, pass, commit.

### Task 4: Errors and warnings

- [ ] Tests, one for each line of the design section "Errors", and for the warnings: unknown option, option that does not apply (exact pattern only), pattern that matches no field. Also `int_size:IS_8` on a repeated unsigned field. An error writes no header and `protoc` returns a non-zero code. Fail first.
- [ ] Implement, pass, commit.

### Task 5: Generated headers, C++ test, CMake

**Files:** Create `tests/vectors/vectors.options`, `tests/vectors/vectors.pb.hpp`, `tests/generator/features.proto`, `shared.proto`, `features.options`, `features.pb.hpp`, `shared.pb.hpp`, `tests/test_generated.cpp`. Modify `CMakeLists.txt`.

- [ ] Python test `test_checked_in_headers`: makes each header again and compares it with the checked-in file. `MICROPB_UPDATE_GENERATED=1` writes the files.
- [ ] `tests/test_generated.cpp`: each reference vector of `vectors.hpp` round-trips through the generated types of `micropb::test`. The types of `features.pb.hpp` encode and decode.
- [ ] CMake: `micropb_add_test(test_generated)`. The CTest test `generator` runs `test_plugin.py` only when `test_plugin.py --check-tools` returns 0.
- [ ] All CTest tests pass in the default and Release builds. Commit.

### Task 6: Documents

- [ ] `tools/protoc-gen-micropb.bat`, README section "Generate structs from a .proto file" plus the layout and status sections, the stage text in `2026-11-18-micropb-design.md`. Commit.
