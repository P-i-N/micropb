# Field lists: `micropb` without reflection

Date: 2026-11-19. Status: implemented. The user selected the approach and the C++23 floor, then left with the words "do whatever", so the builder made the remaining decisions. They are in "Changes during the implementation".

## Intent

Today each use of `micropb` needs a compiler with C++26 static reflection, which is only GCC 16. ARM toolchains are older. The messages that `protoc-gen-micropb` writes must also work on a C++23 compiler without reflection. Reflection stays as the way for structs that a person writes and that have no `.proto` file.

## Goals

* A generated header `app.pb.hpp` works with `pb::encode`, `pb::decode` and `pb::encoded_size` on a C++23 compiler without reflection. No more include, no more build step.
* On a compiler with reflection, nothing changes for hand-written structs: no field list is necessary.
* The tables in ROM and the interpreter are the same for the two ways. The code size does not change.
* The same compile-time rules apply to the two ways, with the same error texts.

## Non-goals

* C++20 or older. The API continues to return `std::expected`.
* A macro for field lists.
* A change to the wire format, to the interpreter or to the descriptor structs.

## Anti-goals

* Two implementations of the type rules that can disagree.
* A table that the program builds at start-up, or a table in RAM.
* A generated message that silently uses a different table on GCC 16 than on an older compiler.

## Approaches

1. **A field list in the generated header, one line for each field. Selected by the user.** The C++ type of each member still sets its encoding, so the plugin does not repeat the type rules.
2. The field list in a second header. Rejected: a missing include is an error on an old compiler but silently uses reflection on GCC 16.
3. The plugin writes the complete tables. Rejected: the Python code would repeat each type rule of the library.

## Design

### The field list

A field list is a type. It names the message type and has one entry for each field: the member type, the field number and the member offset. An alternative of a `std::variant` member also has its index.

```cpp
namespace pb {
template <typename M, std::uint32_t Number, std::size_t Offset, std::size_t Alternative = 0>
struct field_at {};

template <typename T, typename... Fields>
struct field_list {};

template <typename T>
struct fields_of; // the field list of the message type T
}
```

The plugin writes one specialization of `pb::fields_of` for each message, after the structs of the header:

```cpp
// Field lists: with these, the messages also work on a compiler without reflection.
template <>
struct pb::fields_of<app::Sample> : pb::field_list<app::Sample,
    pb::field_at<decltype(app::Sample::time), 1, offsetof(app::Sample, time)>,
    pb::field_at<decltype(app::Sample::values), 2, offsetof(app::Sample, values)>,
    pb::field_at<decltype(app::Sample::payload), 12, offsetof(app::Sample, payload), 1>,
    pb::field_at<decltype(app::Sample::payload), 13, offsetof(app::Sample, payload), 2>> {};
```

A person can write the same list for a struct of their own. This is the way to use a hand-written struct on an older compiler.

### One back end, two front ends

```text
struct + reflection  --(front end 1: meta_schema.hpp)--+
                                                       +--> pb::field_list --> rules, table --> interpreter
.proto + plugin      --(front end 2: app.pb.hpp)-------+
```

* **Front end 1, reflection.** The primary template of `pb::fields_of<T>` exists only when the compiler has reflection (`__cpp_impl_reflection`). It reads the members and the `pb::tag` annotations, as today, and makes the `pb::field_list` type with `std::meta::substitute`. A test in this session showed that this type is identical to a list that is written by hand.
* **Front end 2, generated.** The specialization in the generated header. It has priority over the primary template on each compiler, so a generated message uses one table everywhere.
* **Back end.** Ordinary templates read the entries of the list. They make the sorted `field_desc` table, apply the number rules (`tag_zero`, `duplicate_tag`, `tag_reserved`, `tag_too_large`, `member_offset_too_large`), the member type rules (`optional_repeated`, `variant_no_monostate`, ...) and the decode rules (`decode_encode_only`, `decode_const_member`, ...). These rules are in one place and the two front ends use them.
* **No list and no reflection:** the error `pb: no_field_list: this message type has no pb::fields_of. Generate it with protoc-gen-micropb, write it by hand, or use a compiler with reflection`.

### Changes in the library

* `kind_of`, `element_t`, the sequence tests, `static_capacity`, `access_variant` and the other helpers change from reflection to class template specializations. Their results do not change.
* `pb::repeated` and `pb::span_buffer` give the codec access to their count member with a `friend` declaration. Reflection read this private member until now.
* `meta_schema.hpp` has two parts: the rules (no reflection) and the reflection front end, which is inside `#if defined(__cpp_impl_reflection)`. `codec.hpp` does not include `<meta>`. The number of headers stays five.
* The rules that only reflection can examine stay in front end 1: `not_aggregate`, `base_class`, `inaccessible_member`, `bit_field`, and the annotation rules. A generated struct cannot break them.
* `build_tag_table<T>()` stays, for the tests and for the planned `.proto` generator. It is available only with reflection.

### Changes in the plugin

* After the namespace of the header, the plugin writes the block of field lists: one entry for each field in the order of the `.proto` file, and one entry for each member of a `oneof`.
* The header includes `<cstddef>` for `offsetof`, and always `<micropb/types.hpp>`.
* The block is always written. There is no parameter for it.

## Tests

* All existing tests pass without a change to their expectations, the 32 compile-fail rules included (same error texts).
* `test_generated` gets, for each generated message, `static_assert(std::is_same_v<...>)`: the list of the plugin is identical to the list that reflection makes. Thus the two front ends cannot go apart.
* New CTest configuration without reflection: `test_generated`, `test_wire` and `test_types` build with `-std=c++23` and without `-freflection`, and pass. It also checks that the objects have no start-up code for the tables.
* New compile-fail test `no_field_list`, built without reflection.
* The plugin tests check the text of the field-list block: plain fields, a `oneof`, nested types, a keyword name, an empty message.
* `size_report` before and after: the numbers must not grow.

## Limits of the verification

GCC 16 is the only compiler on this machine. "No reflection" is tested as GCC 16 with `-std=c++23` and without `-freflection`. This does not show that GCC 13 or 14, Clang, or an `arm-none-eabi` toolchain accept the code. A test with a real older compiler stays open.

## Calls of the builder

* The field list is a type, not an array of values. Thus the rules can examine the member types, and the generated list can be compared with the reflected list by `std::is_same_v`.
* `offsetof` gives the offset. A generated struct is a standard-layout type with the standard library of GCC, where `offsetof` is fully defined. For a type that is not standard-layout, `offsetof` is "conditionally supported": GCC and Clang accept it with the warning `-Winvalid-offsetof`. The generated header does not turn this warning off.
* The entries of a list do not have to be in the order of the field numbers. The back end sorts them.
* The file name `meta_schema.hpp` stays, although one half of it does not use reflection.

## Changes during the implementation

* **The generated structs have no `pb::tag` and no `pb::oneof_tags` annotation.** An annotation is C++26 syntax, so a C++23 compiler does not accept it. The field numbers are only in the field list. Thus, on GCC 16, the list that reflection makes for a generated struct has the numbers 1, 2, 3 ..., and the test compares members, offsets and alternatives but not numbers. The numbers are tested against the hand-written structs of `tests/vectors/messages.hpp` and against bytes from `protoc`.
* **Four more rules** for lists that a person writes: `field_list_type`, `field_list_offset`, `field_list_alternative`, `field_list_overlap`. They find the mistakes that a compiler can find without reflection. They cannot find an entry whose type or offset is that of a different member: such a list makes the codec read or write the wrong memory. The README has a warning.
* **`const` members:** a `const` member of each kind is accepted for `encode`. Before this change a `const` scalar and a `const std::string_view` did not compile. The first version of this change by mistake rejected `const` containers; the final review found it.
* **`build_tag_table`** applies its rules through the same function as the codec (`list_traits::check`), so the compile-fail tests examine the code that the codec uses.
* **Full names in generated lists:** the plugin writes `::app::Sample`, because the list is in the scope of `pb` and a package such as `detail` would find `pb::detail`.
* `static_assert(false)` in a template became a condition that depends on the type, so that a compiler before GCC 13 also accepts the header (not tested).
* **A CMake target `micropb_cxx23`**: the include directory and C++23, without `-freflection`.
* The C++23 configuration is not a second build folder. It is a set of more test targets (`*_cxx23`) in the same build.
* `std::monostate` has no field list. The rule functions skip it.
* The code size became smaller: 14,517 bytes for 32 message types (before: 14,700).
