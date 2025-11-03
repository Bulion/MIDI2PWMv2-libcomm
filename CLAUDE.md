# libcomm - Project-Specific Guidelines

## Project Overview

libcomm is a shared communication library that provides a reliable message transport layer between the STM32 firmware, ESP32 GUI, and desktop utilities. The library is designed for cross-platform embedded use, relying on FlatBuffers for serialization and the Embedded Template Library (ETL) for STL-like containers that work in resource-constrained environments.

## Design Philosophy

This is a **library project** - code here is consumed by multiple targets with different constraints:
- **STM32 firmware**: Real-time embedded, FreeRTOS, no dynamic allocation
- **ESP32 GUI**: Embedded with more resources, FreeRTOS, limited heap
- **Desktop utilities**: Full standard library, testing and development

**Core principle**: Write once, run everywhere - from bare-metal MCU to desktop.

## Technology Stack

- **Serialization**: FlatBuffers 2.0.8 (zero-copy, schema-driven)
- **Containers**: Embedded Template Library (ETL) 20.37.1 (STL-like, no heap)
- **Language**: C++20
- **Build System**: CMake 3.20+
- **Testing**: Desktop examples with D-Bus transport

## Architecture

### Message Flow

```
Application → Endpoint (MIDI/PWM) → FrameTransport → Physical Layer
                ↑                         ↑
           FlatBuffers                 CRC32 + ACK
```

### Core Components

1. **FlatBuffers Schemas** (`schemas/`):
   - `midi_messages.fbs`: Full MIDI 1.0 specification
   - `pwm_messages.fbs`: Root PWM message envelope
   - `pwm_types.fbs`: Common PWM type definitions
   - `pwm_channel_config.fbs`: Channel configuration messages
   - `pwm_channel_data.fbs`: Channel telemetry messages
   - `pwm_fault_log.fbs`: Fault logging messages
   - `pwm_fault_control.fbs`: Fault control commands
   - `pwm_heartbeat.fbs`: Heartbeat messages
   - `pwm_response.fbs`: Response/acknowledgment messages
   - Schema changes propagate to all consumers automatically

2. **FrameTransport** (`include/libcomm/frame_transport.h`):
   - Adds framing, CRC32, sequence numbers, ACK/NACK
   - Automatic retry (up to 3 attempts by default)
   - Transport-agnostic (works over UART, USB, D-Bus, etc.)

3. **Endpoints** (`include/libcomm/midi_endpoint.h`, `pwm_endpoint.h`):
   - High-level API for specific message types
   - Builder pattern for constructing messages
   - Callback registration for received messages

4. **Logging System** (`include/libcomm/logging.h`):
   - Global logger registration for library-wide diagnostics
   - Compile-time configurable log levels
   - TAG-based component identification
   - Zero overhead when disabled

## Logging System

libcomm includes a comprehensive logging mechanism that allows applications to monitor internal library operations, diagnose communication issues, and track message flow. The logging system is designed to be lightweight, flexible, and completely optional.

### Key Features

- **Global Logger Registration**: Single logger for entire library, set once by application
- **TAG-Based Component Identification**: Each internal component (FrameTransport, MidiEndpoint, PwmEndpoint) uses a unique TAG
- **Compile-Time Level Control**: Choose maximum log level at build time
- **Zero Overhead When Disabled**: Set `LOG_LEVEL_OFF` to completely remove all logging code
- **No Application ifdefs Required**: Logging API is always available, regardless of compile-time settings
- **Embedded-Friendly**: No dynamic allocation, application controls output destination

### Log Levels

The library supports four log levels, in increasing verbosity:

1. **ERROR**: Recoverable failures that prevent normal operation
   - CRC mismatches, protocol version mismatches
   - FlatBuffers verification failures
   - Invalid message identifiers
   - Payload size violations
   - Write callback failures

2. **WARN**: Unusual conditions that don't prevent operation
   - ACK timeouts (will retry)
   - No handler registered for received message
   - Received NACK (will retry)
   - Transmission failures (will retry)

3. **INFO**: Notable events in normal operation
   - Messages sent/received successfully
   - Message types and basic parameters
   - Configuration changes
   - Telemetry received

4. **DEBUG**: Detailed operational information
   - Frame transmission details (sequence numbers, sizes)
   - Message content (MIDI note numbers, PWM values, etc.)
   - Retry attempts
   - ACK/NACK reception

### Compile-Time Configuration

The maximum log level is set at build time using the CMake option `LIBCOMM_LOG_LEVEL`:

```cmake
# Options (default: LOG_LEVEL_INFO)
set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_OFF")    # No logging (zero overhead)
set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_ERROR")  # Only errors
set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_WARN")   # Errors and warnings
set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_INFO")   # Errors, warnings, and info
set(LIBCOMM_LOG_LEVEL "LOG_LEVEL_DEBUG")  # All logging
```

**Important**: Log levels above the compile-time maximum are completely removed from the binary - there is zero runtime overhead for disabled levels.

### Registering a Logger

The logging API is always available, so applications never need `#ifdef` guards:

```cpp
#include "libcomm/logging.h"

void MyLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    // Your implementation: format and output the log message
    // tag identifies the component (e.g., "FrameTransport", "MidiEndpoint")
    // format is a printf-style format string
    // args are the variadic arguments for the format string
}

int main() {
    libcomm::SetGlobalLogger(MyLogger);  // Always works, no ifdefs needed!

    // Rest of application...
}
```

**Note**: The logger receives `va_list` arguments. Use `std::vfprintf()`, `vsnprintf()`, or similar functions to format the message.

### Example Logger Implementation

libcomm provides a reference logger implementation in `examples/common/`:

```cpp
#include "examples/common/example_logger.h"

int main() {
    InstallExampleLogger();  // Installs a stderr-based logger with timestamps

    // Library operations will now be logged
    libcomm::MidiEndpoint endpoint(writeCallback);
    endpoint.SendPing();
}
```

Example output:
```
[14:23:45] [DEBUG] [FrameTransport] Transmitting frame type=0 seq=1 payload_size=16
[14:23:45] [INFO ] [FrameTransport] Frame sent successfully seq=1 (received ACK)
[14:23:45] [INFO ] [MidiEndpoint] Received Ping message
[14:23:45] [DEBUG] [MidiEndpoint] Ping: seq=1 timestamp=1234567890
```

### Format String Limitations

To maintain embedded compatibility, the logging system has these restrictions:

- **No `%zu` format specifier**: Not supported on all embedded platforms
- **Use `%u` for `size_t`**: Cast to `unsigned int` first
- **Supported specifiers**: `%d`, `%u`, `%x`, `%s`, `%p`

Example:
```cpp
size_t payloadSize = 256;
LIBCOMM_LOG_INFO(TAG, "Payload size: %u", static_cast<unsigned int>(payloadSize));
```

### Thread Safety

The library does not add locking around logger calls. If your application is multi-threaded, **your logger implementation must be thread-safe**.

Example thread-safe logger:
```cpp
#include <mutex>

static std::mutex logMutex;

void ThreadSafeLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    std::lock_guard<std::mutex> lock(logMutex);

    // Format and output...
}
```

### When Logging is Disabled (`LOG_LEVEL_OFF`)

When `LIBCOMM_LOG_LEVEL` is set to `LOG_LEVEL_OFF`:

- ✅ `SetGlobalLogger()` and `GetGlobalLogger()` exist and can be called (safe no-op)
- ✅ `LogLevel` enum and `LogSink` typedef exist
- ❌ Internal logging calls are completely compiled out (zero overhead)
- ✅ Application code unchanged - no conditional compilation needed

This allows applications to always include logger setup code, even when the library is built without logging support.

### Custom Logger Examples

**Embedded UART Logger**:
```cpp
void UartLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    uart_send_string(buffer);
}
```

**RTT (Real-Time Transfer) Logger**:
```cpp
#include "SEGGER_RTT.h"

void RttLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    char buffer[256];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    SEGGER_RTT_Write(0, buffer, len);
}
```

**Telemetry/Metrics Logger**:
```cpp
void TelemetryLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    if (level == libcomm::LogLevel::Error) {
        increment_error_counter(tag);
        send_alert_to_monitoring_system(tag, format, args);
    }
}
```

### Internal Logging Components

Each compilation unit in libcomm uses a static TAG constant:

| Component | TAG | Logs |
|-----------|-----|------|
| `frame_transport.cpp` | `"FrameTransport"` | Frame-level operations, CRC, ACK/NACK, retries |
| `midi_endpoint.cpp` | `"MidiEndpoint"` | MIDI message types, handler invocation, validation |
| `pwm_endpoint.cpp` | `"PwmEndpoint"` | PWM message types, telemetry, faults, configuration |

Applications can filter logs by TAG if desired:
```cpp
void SelectiveLogger(libcomm::LogLevel level, const char* tag, const char* format, va_list args) {
    if (strcmp(tag, "FrameTransport") == 0) {
        return;  // Ignore FrameTransport logs
    }

    // Log everything else...
}
```

### Best Practices

1. **Development**: Use `LOG_LEVEL_DEBUG` or `LOG_LEVEL_INFO` for visibility during development
2. **Production**: Use `LOG_LEVEL_ERROR` or `LOG_LEVEL_OFF` to minimize overhead
3. **Debugging**: Temporarily enable logging on specific devices experiencing issues
4. **Testing**: Use logging to verify correct message flow in integration tests
5. **Performance**: Profile with logging disabled if performance is critical

## Library Design Best Practices

### API Stability

- **Public API**: Only headers in `include/libcomm/` are public
- **Semantic versioning**: Major version bump for breaking changes
- **Deprecation policy**: Mark deprecated functions, remove in next major version
- **Documentation**: All public APIs must be documented (consider Doxygen)

### Cross-Platform Compatibility

- **No STL in headers**: Use ETL containers in public interfaces
- **No platform-specific types**: Use fixed-width integers (`uint8_t`, `uint32_t`)
- **Abstract synchronization**: Provide platform adapter interface
- **Feature detection**: Use CMake options for optional features

### Header-Only vs. Compiled

- **Currently**: Compiled library (`.cpp` files in `src/`)
- **Consider**: Header-only for simplicity (move implementation to `.h`)
- **Trade-off**: Header-only increases compile time but simplifies integration

### Error Handling

- **No exceptions in library code**: Embedded targets may not support exceptions
- **Return codes**: Use explicit return types (bool, enum, std::optional)
- **Error propagation**: Caller decides how to handle errors
- **Logging**: Provide optional logging callback, don't force a logger

Example:
```cpp
// Bad: throws exception
void send_message(const Message& msg);

// Good: returns success/failure
bool send_message(const Message& msg);

// Better: returns detailed error
enum class SendResult { Success, BufferFull, TransportError, Timeout };
SendResult send_message(const Message& msg);
```

### Resource Management

- **Preallocated buffers**: Avoid dynamic allocation in hot paths
- **Static configuration**: Buffer sizes as template parameters or compile-time constants
- **Bounded containers**: ETL containers have fixed capacity
- **RAII**: Use destructors for cleanup (even without exceptions)

Example:
```cpp
// Buffer size as template parameter
template<size_t MaxFrameSize = 256>
class FrameTransport {
    etl::array<uint8_t, MaxFrameSize> buffer;
    // ...
};
```

## FlatBuffers Integration

### Schema Design

- **Versioning**: Use FlatBuffers evolution features (optional fields, deprecated)
- **Enums**: Prefer enums over magic numbers for readability
- **Namespaces**: Group related messages in FlatBuffers namespaces
- **Documentation**: Document schemas with comments

Example schema best practices:
```flatbuffers
namespace libcomm.midi;

// MIDI channel voice messages (MIDI 1.0 specification)
enum MessageType : uint8_t {
    NoteOff = 0x80,
    NoteOn = 0x90,
    // ...
}

table NoteOnMessage {
    channel: uint8_t;    // MIDI channel (0-15)
    note: uint8_t;       // Note number (0-127)
    velocity: uint8_t;   // Velocity (0-127)
}
```

### Code Generation

- **Generated code**: FlatBuffers compiler generates C++ from schemas
- **Don't edit generated files**: Regenerate when schemas change
- **CMake integration**: Automate generation in build process
- **Version control**: Consider checking in generated files for embedded projects without flatc

### Buffer Management

- **Zero-copy**: FlatBuffers allows direct buffer access without deserialization
- **Buffer ownership**: Caller owns buffers, library doesn't allocate
- **Alignment**: FlatBuffers requires proper alignment - use provided builders

## Embedded Template Library (ETL) Usage

### When to Use ETL

- **Public interfaces**: Use ETL containers in library API
- **Fixed capacity**: When maximum size is known at compile time
- **No heap**: When dynamic allocation is forbidden
- **Deterministic**: When predictable memory usage is required

### Common ETL Containers

- `etl::array<T, N>`: Fixed-size array (like `std::array`)
- `etl::vector<T, N>`: Dynamic-size vector with compile-time max capacity
- `etl::queue<T, N>`: FIFO queue with fixed capacity
- `etl::unordered_map<K, V, N>`: Hash map with fixed capacity
- `etl::function<Signature, N>`: Type-erased callable (like `std::function`)

### ETL vs. STL

| Feature | ETL | STL |
|---------|-----|-----|
| Dynamic allocation | No | Yes |
| Max size | Compile-time | Runtime |
| Exception safety | No exceptions | Exceptions |
| Availability | Embedded + Desktop | Desktop only |
| Standard | Non-standard | C++ standard |

### Migration Strategy

For desktop builds, you can optionally allow STL:
```cpp
#ifdef LIBCOMM_USE_STL
    #include <vector>
    template<typename T> using Vector = std::vector<T>;
#else
    #include <etl/vector.h>
    template<typename T, size_t N> using Vector = etl::vector<T, N>;
#endif
```

## Platform Adaptation Layer

### Synchronization Primitives

Different platforms provide different synchronization:
- **Desktop**: `std::mutex`, `std::condition_variable`
- **FreeRTOS**: Mutexes, semaphores, queues via CMSIS-RTOS v2
- **Bare-metal**: Critical sections, atomic operations

Provide platform-specific adapters or abstract interface:
```cpp
class ISyncPrimitive {
public:
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual ~ISyncPrimitive() = default;
};

// Platform provides concrete implementation
```

### Current Approach

CMake options control platform-specific features:
- `LIBCOMM_ETL_TARGET_OS`: Target operating system (CMSIS_OS2, FREERTOS, NONE)
- `LIBCOMM_ETL_NO_STL`: Disable ETL's STL compatibility layer

## Testing Strategy

### Unit Testing

- **Desktop tests**: Full unit tests with standard library support
- **Mock transports**: Test endpoints without physical hardware
- **Schema validation**: Verify FlatBuffers schema compatibility
- **Fuzzing**: Consider fuzzing frame parser for robustness

### Integration Testing

- **D-Bus examples**: Current approach - test MIDI/PWM over D-Bus
- **Loopback tests**: Send messages back to self, verify roundtrip
- **Hardware-in-loop**: Test with actual STM32 ↔ ESP32 communication

### Continuous Integration

Consider adding:
- Automated builds for all target platforms
- Schema compatibility checks
- Code coverage reports
- Static analysis (clang-tidy, cppcheck)

## Build System Integration

### As a Subdirectory

libcomm is designed to be included via `add_subdirectory()`:
```cmake
set(LIBCOMM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/libcomm ${CMAKE_BINARY_DIR}/libcomm)
target_link_libraries(my_target libcomm)
```

### CMake Options

Control build features:
- `LIBCOMM_BUILD_EXAMPLES`: Build D-Bus examples (default: ON)
- `LIBCOMM_BUILD_TESTS`: Build unit tests (default: OFF)
- `LIBCOMM_LOG_LEVEL`: Maximum log level to compile in (default: LOG_LEVEL_INFO)
  - Options: `LOG_LEVEL_OFF`, `LOG_LEVEL_ERROR`, `LOG_LEVEL_WARN`, `LOG_LEVEL_INFO`, `LOG_LEVEL_DEBUG`
- `LIBCOMM_ETL_NO_STL`: Disable STL in ETL (default: OFF)
- `LIBCOMM_ETL_TARGET_OS`: Target OS for ETL (default: NONE)
  - Options: `NONE`, `CMSIS_OS2`, `FREERTOS`
- `LIBCOMM_FLATC_COMMAND`: Path to flatc compiler (automatically detected, can be overridden for cross-compilation)

### Dependency Management

- **FlatBuffers**: Fetched via CMake `FetchContent` at configure time
- **ETL**: Fetched via CMake `FetchContent` at configure time
- **Version pinning**: Use specific versions (2.0.8, 20.37.1) for reproducibility

## Common Pitfalls

### FlatBuffers

- **Verification**: Always verify buffers before accessing (prevents crashes on corrupt data)
- **Buffer lifetime**: FlatBuffers references original buffer - don't free prematurely
- **Null checks**: FlatBuffers accessors can return null - always check
- **String length**: String fields have length, use `.c_str()` carefully

### ETL Containers

- **Capacity errors**: ETL throws errors (not exceptions) when capacity exceeded
- **Define ETL_NO_CHECKS for production**: Removes bounds checking for performance
- **Size vs. capacity**: `.size()` is current, `.capacity()` is maximum
- **Iterator invalidation**: Same rules as STL containers apply

### Frame Transport

- **Sequence wrap-around**: Sequence numbers wrap at 255 - handle gracefully
- **Timeout handling**: Caller responsible for timeout detection
- **Concurrent access**: Not thread-safe by default - add locking if needed
- **MTU limits**: Respect maximum frame size for physical layer

## Performance Optimization

### FlatBuffers

- **No parsing**: FlatBuffers are already in wire format - no parsing overhead
- **Direct access**: Access fields directly from buffer - zero-copy
- **Cache locality**: Well-designed schemas improve cache performance

### Frame Transport

- **Reduce copies**: Pass buffers by reference, avoid unnecessary copies
- **Preallocate buffers**: Don't allocate per-message
- **Batch messages**: Send multiple small messages as one large frame if possible

### ETL

- **Reserve capacity**: Use `.reserve()` when final size is known
- **Avoid reallocations**: ETL doesn't reallocate, but choose appropriate max size
- **Inline small functions**: Compiler can optimize simple ETL operations

## Versioning and Compatibility

### Schema Evolution

FlatBuffers supports forward/backward compatibility:
- **Add fields**: New optional fields at end of table
- **Deprecate fields**: Mark as deprecated, don't remove
- **Default values**: Provide sensible defaults for new fields
- **Version field**: Consider adding version field to root table

### API Versioning

- **Major.Minor.Patch**: Follow semantic versioning
- **API/ABI stability**: Breaking changes only in major versions
- **Deprecation warnings**: Use `[[deprecated]]` attribute
- **Changelog**: Maintain detailed changelog for consumers

## Documentation

### Code Comments

- **Public API**: Every public function, class, and constant must be documented
- **Purpose over implementation**: Explain "why" not "how"
- **Parameters**: Document all parameters, return values, and exceptions/errors
- **Examples**: Provide usage examples for complex APIs

### External Documentation

- **README.md**: High-level overview, quick start, building
- **Architecture docs**: Diagram of message flow and component interaction
- **Integration guide**: How to integrate libcomm into a new project
- **Migration guide**: How to upgrade between major versions

## Security Considerations

- **Input validation**: Verify all incoming messages before processing
- **CRC checking**: FrameTransport provides CRC32 - always enable
- **Buffer bounds**: Never trust size fields from network without validation
- **Timing attacks**: Consider if timing information leaks sensitive data
- **DoS prevention**: Rate-limit incoming messages to prevent resource exhaustion

## Future Enhancements

Potential areas for improvement:
- Header-only library option
- More transport adapters (CAN, I2C, SPI)
- Compression support for large messages
- Encryption layer for sensitive data
- Message priority and QoS
- Streaming support for large payloads
- Alternative serialization backends (MessagePack, Protobuf)
