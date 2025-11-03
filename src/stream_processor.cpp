#include "libcomm/stream_processor.h"

#include <cstring>

#include <etl/algorithm.h>
#include <flatbuffers/base.h>

#include "libcomm/logging.h"

namespace libcomm {

static constexpr const char* TAG = "StreamProcessor";

StreamProcessor::StreamProcessor(WriteCallback write)
    : writeCallback_(write)
{
}

void StreamProcessor::feed(const std::uint8_t* data, std::size_t size)
{
    if (!data || size == 0U) {
        return;
    }

    if (!frameCallback_.is_valid()) {
        LIBCOMM_LOG_ERROR(TAG, "Frame callback not registered");
        reportError(ErrorType::InvalidHeader, "Frame callback not registered");
        return;
    }

    std::size_t spaceAvailable = buffer_.size() - size_;
    if (size > spaceAvailable) {
        LIBCOMM_LOG_WARN(TAG, "Buffer overflow: %u bytes available, %u bytes received",
                         static_cast<unsigned int>(spaceAvailable),
                         static_cast<unsigned int>(size));
        reportError(ErrorType::BufferOverflow, "Insufficient buffer space");

        if (!trySync()) {
            LIBCOMM_LOG_ERROR(TAG, "Sync recovery failed - resetting buffer");
            size_ = 0;
        }

        spaceAvailable = buffer_.size() - size_;
        if (size > spaceAvailable) {
            LIBCOMM_LOG_ERROR(TAG, "Still insufficient space after recovery - discarding data");
            return;
        }
    }

    etl::copy(data, data + size, buffer_.begin() + size_);
    size_ += size;

    LIBCOMM_LOG_DEBUG(TAG, "Received %u bytes, buffer now contains %u bytes",
                      static_cast<unsigned int>(size),
                      static_cast<unsigned int>(size_));

    extractFrames();
}

void StreamProcessor::reset()
{
    size_ = 0;
    LIBCOMM_LOG_DEBUG(TAG, "Buffer reset");
}

void StreamProcessor::setFrameCallback(const FrameCallback& callback)
{
    frameCallback_ = callback;
}

void StreamProcessor::setErrorCallback(const ErrorCallback& callback)
{
    errorCallback_ = callback;
}

std::size_t StreamProcessor::getBufferUsage() const
{
    return size_;
}

bool StreamProcessor::extractFrames()
{
    bool extractedAny = false;

    auto dummyWriteCallback = [](const std::uint8_t*, std::size_t) -> bool {
        return false;
    };

    auto dataHandler = [this](const std::uint8_t* payload, std::size_t payloadSize) -> bool {
        LIBCOMM_LOG_INFO(TAG, "Extracted frame payload: %u bytes",
                         static_cast<unsigned int>(payloadSize));
        frameCallback_(payload, payloadSize);
        return true;
    };

    FrameTransport::WriteCallback writeCallback =
        FrameTransport::WriteCallback::create(dummyWriteCallback);
    FrameTransport frameTransport(writeCallback);

    while (size_ >= FrameTransport::kHeaderSize) {
        std::uint32_t payloadSizeBytesLittleEndian = 0;
        std::memcpy(&payloadSizeBytesLittleEndian, &buffer_[4], sizeof(payloadSizeBytesLittleEndian));
        std::uint32_t payloadSizeBytes = flatbuffers::EndianScalar(payloadSizeBytesLittleEndian);

        std::size_t expectedTotalSizeBytes = FrameTransport::kHeaderSize +
                                              static_cast<std::size_t>(payloadSizeBytes) +
                                              FrameTransport::kCrcSize;

        if (expectedTotalSizeBytes > buffer_.size()) {
            LIBCOMM_LOG_ERROR(TAG, "Invalid frame size in header: %u exceeds buffer capacity %u",
                              static_cast<unsigned int>(expectedTotalSizeBytes),
                              static_cast<unsigned int>(buffer_.size()));
            reportError(ErrorType::InvalidFrameSize, "Frame size exceeds buffer capacity");

            if (!trySync()) {
                size_ = 0;
                break;
            }
            continue;
        }

        if (size_ < kMinimumFrameSize) {
            break;
        }

        if (size_ < expectedTotalSizeBytes) {
            LIBCOMM_LOG_DEBUG(TAG, "Incomplete frame: have %u bytes, need %u bytes - waiting",
                              static_cast<unsigned int>(size_),
                              static_cast<unsigned int>(expectedTotalSizeBytes));
            break;
        }

        bool frameProcessedSuccessfully = frameTransport.HandleIncoming(
            buffer_.data(),
            expectedTotalSizeBytes,
            dataHandler);

        if (frameProcessedSuccessfully) {
            LIBCOMM_LOG_DEBUG(TAG, "Frame processed successfully, consumed %u bytes",
                              static_cast<unsigned int>(expectedTotalSizeBytes));
            extractedAny = true;

            std::size_t remainingBytes = size_ - expectedTotalSizeBytes;
            if (remainingBytes > 0) {
                etl::copy(buffer_.begin() + expectedTotalSizeBytes,
                          buffer_.begin() + size_,
                          buffer_.begin());
                LIBCOMM_LOG_DEBUG(TAG, "Moved %u remaining bytes to start of buffer",
                                  static_cast<unsigned int>(remainingBytes));
            }
            size_ = remainingBytes;
        } else {
            LIBCOMM_LOG_WARN(TAG, "Failed to process frame (invalid header/CRC) - attempting sync");
            reportError(ErrorType::CrcFailure, "Frame validation failed");

            if (!trySync()) {
                std::size_t remainingBytes = size_ - expectedTotalSizeBytes;
                if (remainingBytes > 0) {
                    etl::copy(buffer_.begin() + expectedTotalSizeBytes,
                              buffer_.begin() + size_,
                              buffer_.begin());
                }
                size_ = remainingBytes;
            }
            continue;
        }
    }

    return extractedAny;
}

bool StreamProcessor::trySync()
{
    static constexpr std::uint8_t kProtocolVersion = 1U;

    LIBCOMM_LOG_DEBUG(TAG, "Attempting sync recovery, searching for protocol version byte");
    reportError(ErrorType::SyncRecoveryAttempt, "Searching for frame header");

    for (std::size_t searchPos = 1; searchPos < size_; ++searchPos) {
        if (buffer_[searchPos] == kProtocolVersion) {
            LIBCOMM_LOG_INFO(TAG, "Found potential frame header at offset %u, discarding %u bytes",
                             static_cast<unsigned int>(searchPos),
                             static_cast<unsigned int>(searchPos));

            std::size_t remainingBytes = size_ - searchPos;
            etl::copy(buffer_.begin() + searchPos,
                      buffer_.begin() + size_,
                      buffer_.begin());
            size_ = remainingBytes;

            return true;
        }
    }

    LIBCOMM_LOG_WARN(TAG, "Sync recovery failed - no protocol version byte found");
    return false;
}

void StreamProcessor::reportError(ErrorType type, const char* message)
{
    if (errorCallback_.is_valid()) {
        errorCallback_(static_cast<int>(type), message);
    }
}

}  // namespace libcomm
