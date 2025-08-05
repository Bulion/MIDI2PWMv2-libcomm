# libcomm

Platform-agnostic communication library for microcontrollers.

This repository provides a minimal C++20 library and uses CMake to
fetch and build dependencies such as FlatBuffers and TinyFrame.

## Building

```
cmake -S . -B build
cmake --build build
```

To cross compile for ARM using `arm-none-eabi-gcc`:

```
cmake -S . -B build -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_CXX_COMPILER=arm-none-eabi-g++
cmake --build build
```

