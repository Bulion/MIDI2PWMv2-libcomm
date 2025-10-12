# MIDI2PWMv2-libcomm

Initial scaffold for the MCU-to-MCU communication layer. The project is built
around [FlatBuffers](https://google.github.io/flatbuffers/) to define and
serialize messages that can be shared between the STM32 firmware, ESP32 GUI and
desktop test utilities. All reusable code relies on the
[Embedded Template Library (ETL)](https://www.etlcpp.com/) rather than the C++
STL so it can drop straight into embedded builds.

## Highlights
- Fetches FlatBuffers v2.0.8 and ETL 20.37.1 via CMake `FetchContent`.
- Generates C++ sources from the FlatBuffers schemas in `schemas/` and exposes
  lightweight helpers for MIDI (`libcomm::MidiEndpoint`) and PWM (`libcomm::PwmEndpoint`).
  Both endpoints share a common frame transport that adds CRC32 integrity checks
  and ACK/NACK handshakes with automatic retry (up to three attempts by default).
- Provides optional desktop examples that move messages through a simple D-Bus
  transport (`libcomm_dbus_sender` / `libcomm_dbus_receiver`).

## MIDI 1.0 coverage

The FlatBuffers schema and helper builders cover the full MIDI 1.0 message
surface:

- **Channel Voice**: Note On/Off, Polyphonic Key Pressure, Control Change,
  Program Change, Channel Pressure, Pitch Bend.
- **System Common**: Time Code Quarter Frame, Song Position Pointer, Song
  Select, Tune Request.
- **System Real-Time**: Timing Clock, Start, Continue, Stop, Active Sensing,
  System Reset.
- **System Exclusive**: Manufacturer ID plus arbitrary payload blocks.

Midi endpoint callbacks can be registered for Ping (utility), channel voice, system
common, system real-time and SysEx messages. Builders in
`include/libcomm/midi_endpoint.h` produce ready-to-send FlatBuffers envelopes for
each MIDI message type.

## PWM communication

The new PWM schemas (`pwm_*.fbs`) cover configuration, telemetry and fault
handling between the ESP32 UI and STM32 power stage:

- `ChannelConfig` captures the static setup of each PWM output.
- `ChannelTelemetry` conveys live electrical measurements and status.
- `FaultLog` and `FaultControlCommand` provide fault introspection and control.

Use `libcomm::PwmEndpoint` from `include/libcomm/pwm_endpoint.h` to send and
receive these envelopes. The D-Bus examples demonstrate exercising both MIDI
and PWM traffic on a desktop host.

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

### Frame transport

`libcomm::FrameTransport` bundles FlatBuffers payloads into framed packets with a
CRC32, sequence numbers, and an ACK/NACK handshake. By default the transport
expects an explicit ACK frame from the peer; pass `true` for the `synchronous_ack`
constructor parameter when linking against a request/response medium (such as the
desktop D-Bus harness) so the transport can rely on the immediate boolean result
instead. The trampoline automatically retries a failed transmission up to three
times before surfacing an error to the caller.

## Running the D-Bus example

1. In one terminal start the receiver:
   ```bash
   ./build/libcomm_dbus_receiver
   ```
2. In a second terminal run the sender application:
   ```bash
   ./build/libcomm_dbus_sender
   ```

The receiver prints the decoded Ping, channel voice, system common, real-time
and SysEx messages once they arrive over the D-Bus session bus.
