#pragma once

#include <cstddef>
#include <cstdint>
#include <etl/delegate.h>

#include "flatbuffers/flatbuffers.h"
#include "libcomm/frame_transport.h"
#include "log_messages_generated.h"

namespace libcomm {

class LogEndpoint {
public:
    using WriteCallback = FrameTransport::WriteCallback;
    using LogForwardHandler = etl::delegate<void(const midi2pwm::log::LogForward&)>;

    explicit LogEndpoint(WriteCallback write);

    LogEndpoint(const LogEndpoint&) = delete;
    LogEndpoint& operator=(const LogEndpoint&) = delete;
    ~LogEndpoint() = default;

    bool Send(flatbuffers::DetachedBuffer&& buffer);
    bool HandleIncoming(const std::uint8_t* data, std::size_t size);

    void OnLogForward(LogForwardHandler handler);

private:
    bool HandleFrame(const std::uint8_t* data, std::size_t size) const;

    FrameTransport transport_;
    LogForwardHandler log_forward_handler_;
};

flatbuffers::DetachedBuffer BuildLogForwardMessage(
    midi2pwm::log::LogLevel level,
    const char* tag,
    const char* message);

}  // namespace libcomm
