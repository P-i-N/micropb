# micropb
C++26 Google Protobuf serialization library

## Build and test

Compiler: GCC 16.2 or newer with `-freflection` (MSYS2 UCRT64: `C:/msys64/ucrt64/bin/g++.exe`). The library is header-only. The flags are `-std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.

The CMake target `micropb` is an `INTERFACE` library. A target that links it gets the include directory, C++26 (`cxx_std_26`) and `-freflection`. The tests add `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`.

On Windows, put `C:\msys64\ucrt64\bin` in `PATH`. The test executables need the libstdc++ DLL from that folder.

```sh
cmake -S . -B .build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build .build
ctest --test-dir .build --output-on-failure
```

The tests of the wire primitives, the types and the schema are `constexpr` functions that `static_assert` checks. `main` runs the same tests at run time. The tests of `encode` and `decode` run only at run time: the codec reads and writes struct members through byte offsets, which constant evaluation does not allow.

A sanitizer build takes the role of constant evaluation for the codec. `-DMICROPB_SANITIZE=ON` builds the tests with AddressSanitizer and UndefinedBehaviorSanitizer. On MinGW it uses `-fsanitize=undefined -fsanitize-undefined-trap-on-error`, because MinGW has no sanitizer run-time library; this configuration is not yet verified.

```sh
cmake -S . -B .build-sanitize -G Ninja -DMICROPB_SANITIZE=ON
cmake --build .build-sanitize
ctest --test-dir .build-sanitize --output-on-failure
```

`cmake --build .build --target size_report` prints the size of one translation unit that encodes and decodes 16 and 32 message types at `-Os` (`tests/size/`). It needs GNU `size`.

The tests in `tests/compile_fail/` must not compile. Each one is a CTest test that builds an object target, which is not part of the default build. The test passes only when the compiler output contains the `static_assert` message that names its rule (`pb: <rule>:`).

## Reference vectors

`tests/vectors/` holds `vectors.proto` and one `<message>.<case>.txtpb` text-format file for each test case. `vectors.hpp` contains the bytes that `protoc --encode` writes for them. It is generated and checked in. The build and the tests do not run `protoc`. `messages.hpp` has the C++ mirrors of `vectors.proto`.

To make `vectors.hpp` again (MSYS2 `mingw-w64-ucrt-x86_64-protobuf`):

```sh
cmake -DPROTOC=C:/msys64/ucrt64/bin/protoc.exe -P tests/vectors/generate.cmake
```

The output is the same on each run. It has no timestamp and no absolute path. The script writes temporary files only to `.build/vectors/`.

## Usage

micropb encodes and decodes proto3 messages from plain C++ structs. It uses reflection, so the structs need no macros or generated code. Reflection makes one constant descriptor table for each message type at compile time. One interpreter encodes and decodes all message types from these tables, so each more message type costs a table and not a copy of the codec.

```cpp
#include <array>
#include <cstdint>
#include <span>
#include <variant>

#include <micropb/micropb.hpp>

struct pose {
    pb::repeated<float, 3> pos; // std::array<float, 3> also works, for encode only
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

Type mapping:

* Integers: use only the fixed-width aliases of `<cstdint>` (`std::int32_t`, `std::uint64_t`, ...). Only these are portable: `std::int32_t` is `int` on this host, but `long` on `arm-none-eabi`. A type such as `long` that is not the alias on the target, and `char`, do not compile. Use `pb::sint32`, `pb::sint64`, `pb::fixed32`, `pb::fixed64`, `pb::sfixed32` and `pb::sfixed64` for the other wire encodings.
* `float`, `double` and `bool`: as in proto3.
* `enum` and `enum class`: proto3 `enum`, which is `int32` on the wire. A negative value is a 10-byte varint. An enum wider than 32 bits does not compile.
* `std::string_view`: `string`. `std::span<const std::uint8_t>`, `pb::repeated<std::uint8_t, N>` and `pb::span_buffer<std::uint8_t>`: `bytes`. `std::array<std::uint8_t, N>`: `bytes` with a fixed length. Decoding does not copy a `std::string_view` or a `std::span`.
* `std::optional<T>`: field with explicit presence. `std::variant<std::monostate, ...>`: `oneof`.
* `pb::repeated<T, N>` (storage in the struct) and `pb::span_buffer<T>` (storage that the caller supplies): repeated fields. `std::array<T, N>` and `std::span<const T>` are repeated fields for encode only.
* Nested structs: messages. A message must be an aggregate with only public data members and no base class.

These do not compile: integer types that are not `<cstdint>` aliases on the target, `char`, `const` members (a `const` nested struct compiles for encode only), bit-fields, base classes, private or protected members, enums wider than 32 bits, and a member at an offset above 65535 bytes.

Framing is the job of the caller. `decode` reads exactly one message from the span that it gets, and `encode` writes one message. A stream of messages needs a length or another boundary that the application provides.
