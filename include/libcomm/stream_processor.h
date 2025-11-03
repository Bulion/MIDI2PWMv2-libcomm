#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/array.h>
#include <etl/delegate.h>

#include "frame_transport.h"

namespace libcomm {

class StreamProcessor {
public:
    using FrameCallback = etl::delegate<void(const std::uint8_t*, std::size_t)>;
    using ErrorCallback = etl::delegate<void(int, const char*)>;
    using WriteCallback = etl::delegate<bool(const std::uint8_t*, std::size_t)>;

    enum class ErrorType : int {
        BufferOverflow = 0,
        InvalidFrameSize = 1,
        CrcFailure = 2,
        InvalidHeader = 3,
        SyncRecoveryAttempt = 4
    };

    explicit StreamProcessor(WriteCallback write = WriteCallback());

    StreamProcessor(const StreamProcessor&) = delete;
    StreamProcessor& operator=(const StreamProcessor&) = delete;

    void feed(const std::uint8_t* data, std::size_t size);
    void reset();

    void setFrameCallback(const FrameCallback& callback);
    void setErrorCallback(const ErrorCallback& callback);

    std::size_t getBufferUsage() const;

private:
    static constexpr std::size_t kBufferCapacity =
        FrameTransport::kHeaderSize + FrameTransport::kMaxFrameSize + FrameTransport::kCrcSize;
    static constexpr std::size_t kMinimumFrameSize =
        FrameTransport::kHeaderSize + FrameTransport::kCrcSize;

    bool extractFrames();
    bool trySync();
    void reportError(ErrorType type, const char* message);

    etl::array<std::uint8_t, kBufferCapacity> buffer_{};
    std::size_t size_{0};

    FrameCallback frameCallback_;
    ErrorCallback errorCallback_;
    WriteCallback writeCallback_;
};

}  // namespace libcomm
