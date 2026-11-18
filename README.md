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
