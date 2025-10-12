# MIDI2PWMv2-libcomm

Initial scaffold for the MCU-to-MCU communication layer. The project is built
around [FlatBuffers](https://google.github.io/flatbuffers/) to define and
serialize messages that can be shared between the STM32 firmware, ESP32 GUI and
desktop test utilities. All reusable code relies on the
[Embedded Template Library (ETL)](https://www.etlcpp.com/) rather than the C++
STL so it can drop straight into embedded builds.

## Highlights
- Fetches FlatBuffers v2.0.8 and ETL 20.37.1 via CMake `FetchContent`.
- Generates C++ sources from `schemas/comm.fbs` and exposes a lightweight
  `libcomm::Endpoint` helper for sending and parsing payloads.
- Provides optional desktop examples that move messages through a simple D-Bus
  transport (`libcomm_dbus_sender` / `libcomm_dbus_receiver`).

## Building
```bash
cmake -S . -B build
cmake --build build
```

By default the build also compiles the D-Bus examples. If the `dbus-1`
development package or `pkg-config` is missing, the examples are skipped
automatically. You can disable them manually with:

```bash
cmake -S . -B build -DLIBCOMM_BUILD_EXAMPLES=OFF
```

## Running the D-Bus example

1. In one terminal start the receiver:
   ```bash
   ./build/libcomm_dbus_receiver
   ```
2. In a second terminal run the sender application:
   ```bash
   ./build/libcomm_dbus_sender
   ```

The receiver prints the decoded `Ping` and `ControlChange` messages once they
arrive over the D-Bus session bus.
