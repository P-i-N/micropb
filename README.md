# micropb

A small proto3 encoder and decoder for embedded C++. It sends messages between a device and PC tools, for example on a USB serial port.

A message is a plain struct. With C++26 reflection (GCC 16), the struct is the schema: no `.proto` file, no generator, no macro. Without reflection (C++23, for example the Arm GNU Toolchain 13 or 14), each message type needs a field list. A `protoc` plugin writes the structs and their field lists from a `.proto` file.

```cpp
struct ping {
    std::uint32_t seq;
};

std::array<std::uint8_t, 16> buffer{};
const auto size = pb::encode(ping{7}, buffer); // 08 07
```

## Contents

* [Features](#features)
* [Example](#example)
* [How it works](#how-it-works)
* [Add it to a project](#add-it-to-a-project)
* [Define messages](#define-messages)
* [Generate structs from a .proto file](#generate-structs-from-a-proto-file)
* [Compilers without reflection](#compilers-without-reflection)
* [Encode](#encode)
* [Decode](#decode)
* [Errors](#errors)
* [Compile-time checks](#compile-time-checks)
* [Talk to a PC tool](#talk-to-a-pc-tool)
* [Code size](#code-size)
* [Build and test](#build-and-test)
* [Repository layout](#repository-layout)
* [Status and limits](#status-and-limits)
* [License](#license)

## Features

* **Three functions:** `pb::encode`, `pb::decode` and `pb::encoded_size`.
* **Small code:** one interpreter encodes and decodes all message types. Each more message type adds a constant table, not a copy of the codec.
* **No heap, no exceptions, no RTTI.** Errors are `std::expected` values. All storage is in your structs or in buffers that you supply.
* **Zero-copy decode** of strings and bytes: a `std::string_view` or a `std::span` points into the input buffer.
* **Same bytes as `protoc`.** The tests compare the output with reference vectors from the Google C++ runtime.
* **Mistakes are compile errors.** A duplicate field number or a type without a proto3 mapping does not compile.
* **Header-only:** five headers, standard library only.

## Example

```cpp
#include <array>
#include <cstdint>
#include <span>
#include <variant>

#include <micropb/micropb.hpp>

struct pose {
    pb::repeated<float, 3> pos;
    pb::repeated<float, 4> rot;
};

struct ping {
    std::uint32_t seq;
};

struct envelope {
    std::uint32_t id;
    std::variant<std::monostate, pose, ping> payload; // oneof
};

bool send_and_receive() {
    std::array<std::uint8_t, 64> buffer{};
    const auto size = pb::encode(envelope{1, ping{7}}, buffer); // std::expected<std::size_t, pb::error>
    if (!size)
        return false;

    envelope in{};
    const auto used = pb::decode(std::span<const std::uint8_t>(buffer).first(*size), in);
    if (!used)
        return false;

    const auto* received = std::get_if<ping>(&in.payload);
    return in.id == 1 && received && received->seq == 7;
}
```

The encoded message has 6 bytes:

| Bytes | Meaning |
| --- | --- |
| `08 01` | field 1 (`id`), varint, value 1 |
| `1a 02` | field 3 (`ping`, the second alternative of `payload`), 2 bytes follow |
| `08 07` | in `ping`: field 1 (`seq`), varint, value 7 |

## How it works

Each message type has a field list: the type, the field number and the offset of each member. Reflection makes the list from the struct, or the `protoc` plugin writes it into the generated header. Templates make constant descriptor tables from the list. At run time, one interpreter reads the tables for all message types.

* The interpreter reads and writes scalars with `std::memcpy`, through the byte offset of the member.
* `std::optional` and `std::variant` have no layout that the standard specifies. The interpreter uses them only through one small accessor function for each type.
* The tables are `constexpr` objects. The linker puts them in read-only memory, and there is no initialization at run time.

## Add it to a project

**Requirements:** a C++23 compiler (GCC 12 or newer, for `std::expected`). Structs without a field list need GCC 16.2 or newer with `-std=c++26 -freflection`.

With CMake, add the directory and link the `micropb` target:

```cmake
add_subdirectory(micropb)
target_link_libraries(firmware PRIVATE micropb)
```

* `micropb` is an `INTERFACE` library. It gives the include directory, and C++26 with `-freflection` when the compiler supports them, otherwise C++23.
* `MICROPB_REFLECTION` is `AUTO` (the default), `ON` or `OFF`. `AUTO` checks whether the compiler accepts `-std=c++26 -freflection`. `ON` stops with an error when it does not. `OFF` never uses reflection. CMake prints `micropb: reflection on` or `micropb: reflection off (C++23, field lists only)`.
* `micropb_cxx23` gives the include directory and C++23 only. It never uses reflection.
* `MICROPB_TESTS` builds the tests of micropb. It is on only when micropb is the top-level project.

Without CMake, add `src/` to the include path and compile with `-std=c++23`, or with `-std=c++26 -freflection`.

Include one header:

```cpp
#include <micropb/micropb.hpp>
```

The library does not use exceptions or RTTI, so `-fno-exceptions -fno-rtti` are safe.

## Define messages

A message is a class aggregate: only public data members, no base class, no constructor.

### Type mapping

| C++ type | proto3 type | Notes |
| --- | --- | --- |
| `bool` | `bool` | |
| `std::int32_t`, `std::int64_t` | `int32`, `int64` | A negative value is a 10-byte varint. |
| `std::uint32_t`, `std::uint64_t` | `uint32`, `uint64` | |
| `std::int8_t`, `std::int16_t` | `int32` | Decode truncates to the width of the member. |
| `std::uint8_t`, `std::uint16_t` | `uint32` | Decode truncates to the width of the member. |
| `signed char`, `short`, `int`, `long`, `long long` and the unsigned forms | as the fixed-width type of the same size and sign | `long` is `int32` on Windows and `arm-none-eabi`, and `int64` on Linux x86-64. |
| `pb::wire_int<T, pb::int_encoding::ZIGZAG>`, `T` a signed integer | `sint32` (1, 2 or 4 bytes), `sint64` (8 bytes) | ZigZag: small for negative values. Decode truncates to the width of the member. `pb::sint32` and `pb::sint64` are the 4- and 8-byte cases. |
| `pb::fixed32`, `pb::fixed64` | `fixed32`, `fixed64` | |
| `pb::sfixed32`, `pb::sfixed64` | `sfixed32`, `sfixed64` | |
| `float`, `double` | `float`, `double` | |
| `enum`, `enum class` | `enum` | `int32` on the wire. An enum wider than 32 bits does not compile. |
| `std::string_view` | `string` | Decode gives a view into the input. |
| `std::span<const std::uint8_t>` | `bytes` | Decode gives a view into the input. |
| `std::array<std::uint8_t, N>` | `bytes` | Fixed length, see [Bytes](#bytes). |
| struct | `message` | A nested message. |
| `std::optional<T>` | `optional T` | Explicit presence. |
| `std::variant<std::monostate, A, B>` | `oneof` | `std::monostate` means that no alternative is set. |
| `pb::repeated<T, N>` | `repeated T` | See [Repeated fields](#repeated-fields). |

Plain `char`, the character types (`wchar_t`, `char8_t`, `char16_t`, `char32_t`), `__int128` and `volatile` members do not compile. Use the fixed-width types of `<cstdint>` when the `.proto` file must be the same for each target: `std::int32_t` is `int` on a PC, but `long` on `arm-none-eabi`.

`pb::sint32` and the other `pb::` integer types are small wrappers. They convert to and from the integer type that they hold:

```cpp
struct sample {
    pb::sint32 delta;
};

sample s{-5};
std::int32_t value = s.delta; // -5
```

### Field numbers

The first member has the field number 1. Each next member has the previous number + 1. A `std::variant` uses one number for each alternative after `std::monostate`, in order.

With reflection, two annotations change the numbers:

* `[[= pb::tag{N}]]` sets the number of a member to `N`. The members after it continue from `N + 1`.
* `[[= pb::oneof_tags<A, B, ...>{}]]` sets the numbers of the alternatives of a `std::variant`. The members after it continue from the last number + 1.

```cpp
struct status {
    std::uint32_t uptime;                            // 1
    std::uint32_t errors;                            // 2
    [[= pb::tag{10}]] float temperature;             // 10
    float voltage;                                   // 11
    std::variant<std::monostate, pose, ping> detail; // pose = 12, ping = 13
    [[= pb::oneof_tags<20, 21>{}]]                   //
    std::variant<std::monostate, pose, ping> extra;  // pose = 20, ping = 21
    bool ready;                                      // 22
};
```

The encoder writes the fields in ascending field number.

> [!IMPORTANT]
> Field numbers are the contract with the other side of the link. A new member in the middle of a struct changes the numbers of all members after it. Add new members at the end, or give explicit numbers.

### Presence

| Member | When the encoder writes it |
| --- | --- |
| scalar, string, bytes | when the value is not zero or not empty (`-0.0` is not zero) |
| `std::optional<T>` | when it has a value, also when the value is zero |
| nested message | always |
| `std::optional<message>` | when it has a value |
| `std::variant` | the active alternative only |
| repeated field | when it has one element or more |

### Repeated fields

Scalar elements are packed. Strings, bytes and messages are one field for each element.

| Container | Storage | Decode |
| --- | --- | --- |
| `pb::repeated<T, N>` | in the struct, up to `N` elements | yes |
| `pb::span_buffer<T>` | a span that you supply | yes |
| `std::array<T, N>` | in the struct, always `N` elements | no |
| `std::span<const T>` | a view of your data | no |

`pb::repeated<T, N>` has `push_back` (returns `false` when it is full), `size`, `capacity`, `empty`, `clear`, `operator[]`, `begin` and `end`.

Give a `pb::span_buffer<T>` its storage before you decode:

```cpp
struct samples {
    pb::span_buffer<std::int32_t> values;
};

std::array<std::int32_t, 256> storage{};
samples received{pb::span_buffer<std::int32_t>(storage)};
const auto result = pb::decode(input, received);
```

A `pb::span_buffer` cannot be in a `std::optional`, in a `std::variant`, or in the element of a repeated field: use `pb::repeated` there.

### Bytes

| Type | Decode |
| --- | --- |
| `std::span<const std::uint8_t>` | a view into the input, no copy |
| `pb::repeated<std::uint8_t, N>` | a copy, up to `N` bytes |
| `pb::span_buffer<std::uint8_t>` | a copy into your storage |
| `std::array<std::uint8_t, N>` | a copy; fixed length, see below |
| `std::span<const std::uint8_t, N>` | no (encode only) |

A `std::array<std::uint8_t, N>` is a bytes field with a fixed length, for example a MAC address. The encoder always writes `N` bytes. On decode, shorter input leaves the remaining bytes 0, and longer input is the error `CAPACITY_EXCEEDED`.

## Generate structs from a .proto file

`tools/protoc-gen-micropb.py` is a plugin for `protoc`. It reads a proto3 file and writes a header with the structs and their field lists. It needs only Python 3.8 or newer (tested with Python 3.13 and 3.14), no Python package.

```sh
protoc --plugin=protoc-gen-micropb=tools/protoc-gen-micropb.py --micropb_out=gen --micropb_opt=options_dir=proto -I proto proto/app.proto
```

This command writes `gen/app.pb.hpp`. The folder `gen` must exist. On Windows, use `tools\protoc-gen-micropb.bat` as the plugin, because `protoc` cannot start a `.py` file there.

`proto/app.proto`:

```proto
syntax = "proto3";
package app;

enum Mode {
  MODE_IDLE = 0;
  MODE_RUN = 1;
}

message Sample {
  uint32 time = 1;
  repeated float values = 2;
  bytes mac = 3;
  optional sint32 offset = 10;
  Mode mode = 11;
}
```

`proto/app.options`:

```text
# <name pattern>  <option>:<value> ...
Sample.values     max_count:8
Sample.mac        max_size:6 fixed_length:true
```

`gen/app.pb.hpp`:

```cpp
// Generated by protoc-gen-micropb from app.proto. Do not edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <micropb/types.hpp>

namespace app {

enum class Mode : std::int32_t {
    IDLE = 0,
    RUN = 1,
};

struct Sample {
    std::uint32_t time;
    pb::repeated<float, 8> values;
    std::array<std::uint8_t, 6> mac;
    std::optional<pb::sint32> offset;
    Mode mode;
};

} // namespace app

// Field lists: with these, the messages also work on a compiler without reflection.

template <>
struct pb::fields_of<::app::Sample> : pb::field_list<::app::Sample,
    pb::field_at<decltype(::app::Sample::time), 1, offsetof(::app::Sample, time)>,
    pb::field_at<decltype(::app::Sample::values), 2, offsetof(::app::Sample, values)>,
    pb::field_at<decltype(::app::Sample::mac), 3, offsetof(::app::Sample, mac)>,
    pb::field_at<decltype(::app::Sample::offset), 10, offsetof(::app::Sample, offset)>,
    pb::field_at<decltype(::app::Sample::mode), 11, offsetof(::app::Sample, mode)>> {};
```

### What the plugin writes

The types are those of [Type mapping](#type-mapping). In addition:

* **Names** stay as they are in the `.proto` file. A C++ keyword gets the suffix `_`: `class` becomes `class_`. The same applies to `pb`, `std` and the macro names `NULL`, `EOF`, `errno`, `stdin`, `stdout`, `stderr`, `assert` and `offsetof`. A name that is a different macro in your project, for example `HIGH`, does not compile.
* **Package:** `package a.b` becomes `namespace a::b`.
* **Enum values** lose the prefix `<ENUM NAME>_` when each value has it and the remaining text starts with a letter: `MODE_IDLE` becomes `Mode::IDLE`. The comparison ignores case. For a nested enum, the prefix is the name of the enum without the outer message: `STATE_` for `Device.State`.
* **Nested types** become top-level types: `Outer.Inner` is `Outer_Inner`.
* **Order:** the enums come first. Each struct comes after the structs that it contains.
* **`oneof`** becomes a `std::variant` with the name of the `oneof`.
* **A message field without `optional`** is a plain member, which the encoder always writes.
* **`string`** is a `std::string_view`. **`bytes`** without an option is a `std::span<const std::uint8_t>`.
* **`import`** becomes `#include "<name>.pb.hpp"` when the file uses a type of the imported file. A type of a different package gets its full name, `::a::b::name`.

### The options file

The plugin reads sizes from `<name>.options`, in the format of NanoPB options files. Each line has a name pattern and one or more options. `#` and `//` start a comment.

| Option | For | Result |
| --- | --- | --- |
| `max_count:N` | a repeated field | `pb::repeated<T, N>` |
| `max_count:N fixed_count:true` | a repeated field | `std::array<T, N>` (encode only) |
| `max_size:N` | a `bytes` field | `pb::repeated<std::uint8_t, N>` |
| `max_size:N fixed_length:true` | a `bytes` field | `std::array<std::uint8_t, N>` |
| `int_size:IS_8`, `IS_16`, `IS_32`, `IS_64` | `int32`, `int64`, `uint32`, `uint64` | `std::int8_t` ... `std::uint64_t`, with the sign of the field |
| `int_size:IS_8`, `IS_16`, `IS_32`, `IS_64` | `sint32`, `sint64` | `pb::wire_int<std::intN_t, pb::int_encoding::ZIGZAG>`; `pb::sint32` for `IS_32`, `pb::sint64` for `IS_64` |
| `enum_intsize:IS_8`, `IS_16`, `IS_32` | an enum | `enum class E : std::int8_t`, `std::int16_t` or `std::int32_t` (the default) |

* The pattern is the name of the field: `package.Message.field`, or `Message.field` without the package. For `enum_intsize` it is the name of the enum. `*` and `?` are wildcards: `* max_count:8` sets a default for all fields.
* A narrow member saves RAM, not bytes on the wire. Decode truncates a value to the width of the member, also an enum value from a newer peer. An enum value that does not fit the type is an error when you generate the header.
* `int_size` on a wildcard line also narrows each `sint32` and `sint64` field that it matches. Check an existing options file before you generate headers again.
* When more than one line sets the same option for a field, the last line wins.
* Each repeated field must have a `max_count`. Else the plugin stops with an error.
* The plugin looks for `<name>.options` with the path that `protoc` has for the `.proto` file, from the current folder. Give more folders with `--micropb_opt=options_dir=<folder>`. When it finds no options file, a `bytes` field is a `std::span` and an integer has its full size, without a message.
* An unknown option and a pattern that matches no field give a warning. An option that does not apply to its field gives a warning when its line has no wildcard. `max_size` on a `string` field is such a case.
* The file must be UTF-8 text.

The plugin does not read options in the `.proto` file, such as `[(nanopb).max_size = 6]`.

### What the plugin does not accept

The plugin stops with an error for: a file that is not proto3, a `map` field, a message that contains itself, a `oneof` with two members of the same type, `int_size:IS_8` on a repeated unsigned field (a sequence of `std::uint8_t` is `bytes`), and `enum_intsize:IS_64`. It ignores services and `extend` blocks. It ignores `[packed = false]`: the library always packs scalar elements, and each protobuf parser reads the two forms.

## Compilers without reflection

Reflection only finds the members of a struct and makes the field list. All other code is C++23. Thus a message type with a field list works with `-std=c++23`.

* **Generated structs:** the plugin writes a field list for each message. Nothing more is necessary.
* **Your own structs:** write the field list, or use a compiler with reflection.

A field list is a specialization of `pb::fields_of`, with one `pb::field_at` for each field: the type of the member, the field number and the offset of the member.

```cpp
struct ping {
    std::uint32_t seq;
    std::string_view note;
};

template <>
struct pb::fields_of<ping> : pb::field_list<ping,
    pb::field_at<decltype(ping::seq), 1, offsetof(ping, seq)>,
    pb::field_at<decltype(ping::note), 2, offsetof(ping, note)>> {};
```

* The entries can be in any order.
* The list is in the scope of `pb`. When your type has a name that `pb` also has, for example `tag` or `error`, write `::tag`.
* A `std::variant` member has one entry for each alternative after `std::monostate`, with the index of the alternative as a fourth argument: `pb::field_at<decltype(msg::choice), 10, offsetof(msg, choice), 1>`.
* `offsetof` is a macro. For a type with a comma in its name, such as `box<T, N>`, make an alias first.
* A `const` member is encode-only. `decltype` keeps the `const`.
* The specialization must come before the first use of the type with `pb::encode`, `pb::decode` or `pb::encoded_size`.
* A field list has priority over reflection. Thus a generated message uses the same table on each compiler.
* The [compile-time checks](#compile-time-checks) also apply to a field list, except the rules that need reflection: `not_aggregate`, `base_class`, `inaccessible_member`, `bit_field` and the rules for annotations.

Without reflection and without a field list, a message type does not compile: `pb: no_field_list`.

> [!WARNING]
> The compiler cannot compare a list that a person wrote with the struct. Always write `decltype(T::member)` and `offsetof(T, member)` with the same member. An entry with a different type or offset compiles, and the codec then reads or writes the wrong memory. The library finds a member that is in the list two times, a `std::variant` without all its alternatives, an offset that is not in the message, and the list of a different type. It does not find a member that is not in the list.

## Encode

```cpp
template <typename T>
std::size_t pb::encoded_size(const T& msg);

template <typename T>
std::expected<std::size_t, pb::error> pb::encode(const T& msg, std::span<std::uint8_t> out);
```

`encode` writes one message into `out` and returns the number of bytes. It never writes past `out`. When `out` is too small, it returns `pb::error::BUFFER_TOO_SMALL`, and the contents of `out` are not valid.

`encoded_size` returns the number of bytes that `encode` needs for this message.

## Decode

```cpp
template <typename T>
std::expected<std::size_t, pb::error> pb::decode(std::span<const std::uint8_t> in, T& msg);
```

`decode` reads exactly one message from `in`. On success it returns `in.size()`.

* It resets `msg` first. You can use the same object again for each message. A `pb::repeated` and a `pb::span_buffer` keep their storage and get the size 0.
* It skips a field number that the message does not have, and a known field with a different wire type. Thus the other side can add fields, and old firmware continues to work.
* A scalar that occurs more than one time: the last value wins. A nested message: the occurrences merge. A `oneof`: the last alternative wins.
* A repeated scalar field can be packed or not packed, also mixed in one message.
* A varint that is too wide for the member loses its upper bits. A `bool` is `true` for each value that is not 0.
* An enum value that the enum does not name is kept as its integer value.
* On an error, `msg` holds the fields that were read before the error.
* It does not examine whether a `string` field is correct UTF-8.

> [!WARNING]
> A `std::string_view` or a `std::span<const std::uint8_t>` in a decoded message points into the input buffer. Do not use it after the buffer changes or goes away.

## Errors

`pb::error` is an `enum class`. `encode` and `decode` return it in a `std::expected`.

| Error | From | Meaning |
| --- | --- | --- |
| `BUFFER_TOO_SMALL` | encode | The output span is too short. |
| `TRUNCATED` | decode | The input ends in a field, or a length is larger than the remaining input. |
| `INVALID_VARINT` | decode | A varint is longer than 10 bytes or has more than 64 bits. |
| `INVALID_WIRE_TYPE` | decode | A key has the wire type 3, 4, 6 or 7. Groups are not supported. |
| `INVALID_TAG` | decode | A key has the field number 0, or a field number above 2^29 - 1. |
| `CAPACITY_EXCEEDED` | decode | A field has more elements or bytes than its container holds. The decoder examines the capacity before it reads the next element. |

## Compile-time checks

A message that the codec cannot handle correctly does not compile. Each error is a `static_assert` with a message that starts with `pb: <rule>:`.

**Field numbers**

| Rule | Cause |
| --- | --- |
| `duplicate_tag` | Two fields have the same field number. |
| `tag_zero` | A field number is 0. |
| `tag_too_large` | A field number is above 2^29 - 1. |
| `tag_reserved` | A field number is from 19000 to 19999, which protobuf reserves. |
| `tag_on_variant` | `pb::tag` is on a `std::variant`. Use `pb::oneof_tags`. |
| `oneof_tags_on_non_variant` | `pb::oneof_tags` is on a member that is not a `std::variant`. |
| `oneof_tags_count` | `pb::oneof_tags` does not list one number for each alternative. |

**Message types**

| Rule | Cause |
| --- | --- |
| `not_aggregate` | A message is not a class aggregate. |
| `base_class` | A message has a base class. |
| `inaccessible_member` | A message has a private or protected data member. |
| `bit_field` | A member is a bit-field. |
| `member_offset_too_large` | A member is at an offset above 65535 bytes. |
| `unmapped_type` | A member type has no proto3 mapping, for example `char`, `volatile int` or a pointer. |
| `zigzag_unsigned` | A `pb::wire_int<T, pb::int_encoding::ZIGZAG>` has an unsigned `T`. |
| `enum_too_wide` | An enum is wider than 32 bits. |

**Combinations that proto3 does not have**

| Rule | Cause |
| --- | --- |
| `variant_no_monostate` | The first alternative of a `std::variant` is not `std::monostate`. |
| `monostate_member` | `std::monostate` is used in a different place. |
| `variant_duplicate_alternative` | Two alternatives of a `std::variant` have the same type. |
| `variant_repeated_alternative`, `variant_optional_alternative`, `variant_variant_alternative` | An alternative is a repeated field, a `std::optional` or a `std::variant`. |
| `optional_repeated`, `optional_optional`, `optional_variant` | The value of a `std::optional` is a repeated field, a `std::optional` or a `std::variant`. |
| `repeated_repeated`, `repeated_optional`, `repeated_variant` | The element of a repeated field is a repeated field, a `std::optional` or a `std::variant`. |
| `span_buffer_in_optional`, `span_buffer_in_variant`, `span_buffer_in_repeated` | A `pb::span_buffer` is in a place where it cannot get its storage. |

**Field lists that a person wrote**

| Rule | Cause |
| --- | --- |
| `no_field_list` | The compiler has no reflection and the message type has no `pb::fields_of`. |
| `field_list_type` | `pb::fields_of<T>` derives from the `pb::field_list` of a different type. |
| `field_list_offset` | The offset of a `pb::field_at` is not in the message. Use `offsetof`. |
| `field_list_alternative` | A `std::variant` member does not have one entry for each alternative with its index, or a different member has an index. |
| `field_list_overlap` | Two entries name the same member. |

**Decode only**

| Rule | Cause |
| --- | --- |
| `decode_encode_only` | A repeated field is in a `std::array` or a `std::span<const T>`. |
| `decode_fixed_span` | A bytes field is in a `std::span` with a static extent. |
| `decode_const_member` | A message has a `const` member. A `const` member compiles for `encode`. |

## Talk to a PC tool

`micropb` writes and reads one message. It does not find the start and the end of a message in a stream of bytes. Your code must frame the messages, for example with a length before each message, or with COBS and a checksum.

The PC tool uses Google protobuf (Python, C#, C++ or a different language) with the same `.proto` file. With generated structs, use the `.proto` file that the plugin read. With structs that you wrote, write the `.proto` file by hand: one field for each member, with the numbers of [Field numbers](#field-numbers).

## Code size

The interpreter has a fixed size. Each message type adds only tables and the small accessor functions of its `std::optional` and `std::variant` members.

`cmake --build .build --target size_report` builds one object file that encodes and decodes one envelope message with 16 and with 32 message types as `oneof` alternatives. Each message type has 8 fields of mixed types. The numbers include the code, the constants and the tables (GCC 16.2, `-Os`, x86-64):

| Message types | Bytes |
| ---: | ---: |
| 16 | 9400 |
| 32 | 13736 |
| each more type | 271 |

The first design of this library made one template copy of the codec for each message type: 35490 bytes for 32 message types, about 940 bytes for each more type. The price of the interpreter is speed, which is not important for a serial link.

The numbers are from a PC. No measurement on Arm is available.

## Build and test

```sh
cmake -S . -B .build -G Ninja
cmake --build .build
ctest --test-dir .build --output-on-failure
```

On Windows with MSYS2 UCRT64, add `-DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`, and put `C:\msys64\ucrt64\bin` in `PATH`: the test programs need the libstdc++ DLL from that folder. Run the tests also with `-DMICROPB_REFLECTION=OFF` in a second build folder.

The tests use `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.

| Tests | What they examine |
| --- | --- |
| `test_wire`, `test_types`, `test_schema` | wire primitives, containers, field numbers and type rules (`static_assert`) |
| `test_codec` | the descriptor tables, and one message with each kind of member |
| `test_encode`, `test_decode` | `encode` and `decode` against the reference vectors |
| `test_wire_compat` | round trips, each prefix and each single changed byte of each vector |
| `test_generated` | the headers that the plugin wrote, against bytes from `protoc`; the field lists against reflection |
| `test_field_list` | a field list that a person wrote |
| `test_scalars` | the standard integer types and narrow ZigZag members |
| `*_cxx23` | the tests that need no reflection, built as C++23 |
| `no_startup_code` | the tables need no code before `main()` (`nm`) |
| `compile_fail.*` | each rule of [Compile-time checks](#compile-time-checks): the file must not compile, and the output must name the rule |
| `generator` | the plugin: each rule, each option, each error (Python `unittest` through `protoc`) |

### Arm tests

When CMake finds `arm-none-eabi-g++` (in `PATH` and, on Windows, in `C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin`), it adds tests for each such compiler, named `arm_<compiler version>.<test>`:

* the tests that need no reflection are compiled with `-std=c++23 -Os -mcpu=cortex-m4 -mthumb` and the flags above; nothing is linked or run;
* `no_startup_code` with `arm-none-eabi-nm`;
* `consumer` configures and builds `tests/consumer`, a small project that links the `micropb` target;
* `consumer_reflection_on`, for a compiler without reflection: `-DMICROPB_REFLECTION=ON` must stop with the error of micropb.

`-DMICROPB_ARM_COMPILERS="<g++>;<g++>"` sets the compilers. `-DMICROPB_ARM_CPU=<cpu>` sets `-mcpu`.

### Sanitizers

The codec reads and writes members through byte offsets, so its tests cannot run in constant evaluation. `-DMICROPB_SANITIZE=ON` builds the tests with AddressSanitizer and UndefinedBehaviorSanitizer. On MinGW it uses `-fsanitize=undefined -fsanitize-undefined-trap-on-error`, because MinGW has no sanitizer run-time library.

### Generated files

The test `generator` runs only when CMake finds Python and `protoc`. It starts the plugin with `python -S`, without the installed packages. The tests find `protoc` in the environment variable `PROTOC`, in `PATH`, or in the Python package `grpcio-tools`. To use a specific Python, configure with `-DPython3_EXECUTABLE=<path>`.

`tests/vectors/vectors.pb.hpp` and `tests/generator/*.pb.hpp` are generated and checked in, so `test_generated` does not need `protoc`. The test `generator` fails when one of them is not current. To write them again:

```sh
MICROPB_UPDATE_GENERATED=1 python tests/generator/test_plugin.py CheckedInTest
```

`tests/vectors/` holds `vectors.proto` and one `<message>.<case>.txtpb` file for each test case. `vectors.hpp` has the bytes that `protoc --encode` writes for them. `messages.hpp` has the C++ structs for `vectors.proto`. To write `vectors.hpp` again:

```sh
cmake -DPROTOC=C:/msys64/ucrt64/bin/protoc.exe -P tests/vectors/generate.cmake
```

The output has no timestamp and no absolute path. The script writes temporary files only to `.build/vectors/`.

## Repository layout

```text
src/micropb/
    micropb.hpp      include this header
    types.hpp        pb::error, pb::repeated, pb::span_buffer, pb::sint32 ..., pb::tag, pb::oneof_tags
    wire.hpp         reader and writer for varints, fixed-width values and keys
    meta_schema.hpp  type rules, compile-time checks, field lists from reflection
    codec.hpp        descriptor tables, interpreter, encoded_size, encode, decode
tools/
    protoc-gen-micropb.py   protoc plugin: .proto file -> header with structs
    protoc-gen-micropb.bat  starts the plugin on Windows
tests/
    test_*.cpp       the tests
    generator/       tests and test schemas of the protoc plugin
    compile_fail/    one file for each compile-time rule
    consumer/        a project that uses the micropb target, for the Arm tests
    vectors/         reference vectors from protoc
    size/            the source of the size report
docs/superpowers/    design documents and implementation plans
```

## Status and limits

* **Compilers:** GCC 16.2 on Linux, and on Windows from MSYS2 UCRT64, with and without reflection. The Arm GNU Toolchain 13.3 and 14.2, and `arm-none-eabi` GCC 16.2 from MSYS2: compile only (`-mcpu=cortex-m4`), not run on a device and not measured. Clang is not tested: it does not know the attribute `gnu::noclone` of the codec and gives a warning. The MinGW variant of `MICROPB_SANITIZE` is not verified.
* **`protoc` plugin:** tested on Windows with `protoc` 36.2, Python 3.13 and the MSYS2 Python 3.14. On Windows, `tools/protoc-gen-micropb.bat` starts the first `python` in `PATH`. The version without the `protobuf` package is not tested on Linux.
* **Planned:** a generator that writes the `.proto` file from the C++ structs.
* **Not supported:** `map`, proto2, groups, `Any` and the other well-known types, JSON, streaming encode or decode. Unknown fields are skipped and not kept.
* **Not `constexpr`:** `encode`, `decode` and `encoded_size` run only at run time.
* **Message size:** each member must be at an offset of 65535 bytes or less in its struct.
* **Recursion:** a message type cannot contain itself.

## License

BSD 3-Clause. See [LICENSE](LICENSE).
