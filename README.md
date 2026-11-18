# micropb
C++26 Google Protobuf serialization library

## Build and test

Compiler: GCC 16.2 or newer with `-freflection` (MSYS2 UCRT64: `C:/msys64/ucrt64/bin/g++.exe`). The library is header-only. The flags are `-std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.

On Windows, put `C:\msys64\ucrt64\bin` in `PATH`. The test executables need the libstdc++ DLL from that folder.

```sh
cmake -S . -B .build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build .build
ctest --test-dir .build --output-on-failure
```

Each test is a `constexpr` function that `static_assert` checks. `main` runs the same tests at run time.

The tests in `tests/compile_fail/` must not compile. Each one is a CTest test that builds an object target, which is not part of the default build. The test passes only when the compiler output contains the `static_assert` message that names its rule (`pb: <rule>:`).

## Reference vectors

`tests/vectors/` holds `vectors.proto` and one `<message>.<case>.txtpb` text-format file for each test case. `vectors.hpp` contains the bytes that `protoc --encode` writes for them. It is generated and checked in. The build and the tests do not run `protoc`. `messages.hpp` has the C++ mirrors of `vectors.proto`.

To make `vectors.hpp` again (MSYS2 `mingw-w64-ucrt-x86_64-protobuf`):

```sh
cmake -DPROTOC=C:/msys64/ucrt64/bin/protoc.exe -P tests/vectors/generate.cmake
```

The output is the same on each run. It has no timestamp and no absolute path. The script writes temporary files only to `.build/vectors/`.
