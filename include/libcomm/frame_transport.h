#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/delegate.h>
#include <etl/mutex.h>

namespace libcomm {

class FrameTransport {
public:
    using WriteCallback = etl::delegate<bool(const std::uint8_t*, std::size_t)>;
    using DataHandler = etl::delegate<bool(const std::uint8_t*, std::size_t)>;

    static constexpr std::size_t kHeaderSize = 8U;
    static constexpr std::size_t kCrcSize = 4U;
    static constexpr std::size_t kMaxFrameSize = 4096U;

    explicit FrameTransport(WriteCallback write);

    FrameTransport(const FrameTransport&) = delete;
    FrameTransport& operator=(const FrameTransport&) = delete;

    bool Send(const std::uint8_t* payload, std::size_t size);
    bool HandleIncoming(const std::uint8_t* data, std::size_t size, const DataHandler& handler);

private:
    enum class FrameType : std::uint8_t {
        Data = 0
    };

    static constexpr std::uint8_t kProtocolVersion = 1U;
    static std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t length);

    bool TransmitFrame(FrameType type,
                       std::uint16_t sequence,
                       const std::uint8_t* payload,
                       std::size_t size) const;
    std::uint16_t NextSequence();

    WriteCallback write_;

    mutable etl::mutex mutex_;
    std::uint16_t next_sequence_{1};
};

}  // namespace libcomm
