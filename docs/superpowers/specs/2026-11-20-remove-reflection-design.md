# Remove C++26 reflection

Date: 2026-11-20. Status: implemented. This document changes `2026-11-18-micropb-design.md`, `2026-11-18-micropb-table-codec-design.md`, `2026-11-19-protoc-plugin-design.md`, `2026-11-19-field-lists-design.md`, and `2026-11-19-optional-reflection-design.md`.

## Intent

Generated headers always write a `pb::fields_of<T>` specialization for each struct, and device firmware builds with C++23 toolchains that have no reflection. The C++26 reflection front end is no longer necessary and splits the CMake build and the test suite.

## Changes

### Library

* `src/micropb/meta_schema.hpp` is renamed to `src/micropb/schema.hpp`. `<meta>` is no longer used.
* Removed from `pb`: `tag`, `oneof_tags`, `field_info`, `no_alternative`, and `build_tag_table`.
* Every message type needs a `pb::fields_of<T>` specialization (`pb: no_field_list`).
* Removed compile-time rules that applied only to reflection or annotations: `tag_on_variant`, `oneof_tags_on_non_variant`, `oneof_tags_count`, `base_class`, `inaccessible_member`, and `bit_field`.
* `not_aggregate` moves into `list_traits::check()`, so it applies in C++23.
* Stage 2 of `2026-11-18-micropb-design.md` (`schema_gen.hpp`, C++ structs to `.proto`) is dropped.

### Build and tests

* `micropb` sets `cxx_std_23` unconditionally. `MICROPB_REFLECTION`, the `-freflection` probe, `micropb_cxx23`, the `*_cxx23` duplicate test targets, and `consumer_reflection_on` are removed.
* All test messages and `tests/size/size_report.cpp` have `pb::fields_of` specializations, so the complete test suite, all `compile_fail` tests, and `size_report` build as C++23 (and in the `arm_*` compile tests).

### Plugin

* The comment above the field-list block in generated headers becomes `// Field lists.`.
* Synthetic `map_entry` messages are excluded from dependency sorting and type collection before `field_type` reports `map fields are not supported`.

### Fixes from the code review

* `check_decodable` rejects `const` elements in `std::array` and `pb::repeated`, and `const` alternatives in `std::variant` (`pb: decode_const_member`).
* `entries_overlap` catches an empty member that appears two times at one offset (`pb: field_list_overlap`), while still allowing `[[no_unique_address]]` members of different types to share an offset.
* `alternatives_complete` counts only variant entries (`alternative != 0`) at a shared offset (`pb: field_list_alternative`).
* `reserve` absorbs a reader error from `count_record` into the message reader, so an invalid varint on the first record of an empty `std::span<const T>` field keeps `INVALID_VARINT` instead of reporting `TRUNCATED`.
