#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(ETL_TARGET_OS_CMSIS_OS2) || defined(ETL_TARGET_OS_FREERTOS)
#    define LIBCOMM_HAS_CONDITION_VARIABLE 0
#else
#    define LIBCOMM_HAS_CONDITION_VARIABLE 1
#endif

#if LIBCOMM_HAS_CONDITION_VARIABLE
#    include <condition_variable>
#    include <mutex>
#endif
#include <vector>

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

    explicit FrameTransport(WriteCallback write, bool synchronous_ack = false);

    FrameTransport(const FrameTransport&) = delete;
    FrameTransport& operator=(const FrameTransport&) = delete;

    bool Send(const std::uint8_t* payload, std::size_t size, std::size_t max_retries = 3U);
    bool HandleIncoming(const std::uint8_t* data, std::size_t size, const DataHandler& handler);

private:
    enum class FrameType : std::uint8_t {
        Data = 0,
        Ack = 1,
        Nack = 2
    };

    enum class AckState {
        None,
        Ack,
        Nack
    };

    static constexpr std::uint8_t kProtocolVersion = 1U;
    static std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t length);

    bool TransmitFrame(FrameType type,
                       std::uint16_t sequence,
                       const std::uint8_t* payload,
                       std::size_t size) const;
    bool ProcessAck(std::uint16_t sequence, FrameType type);
    std::uint16_t NextSequence();

    WriteCallback write_;

    mutable etl::mutex mutex_;
#if LIBCOMM_HAS_CONDITION_VARIABLE
    std::condition_variable_any ack_cv_;
    bool awaiting_ack_{false};
    std::uint16_t pending_sequence_{0};
    AckState ack_state_{AckState::None};
#endif

    std::uint16_t next_sequence_{1};
    bool synchronous_ack_{false};
};

}  // namespace libcomm
