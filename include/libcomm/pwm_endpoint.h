#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/delegate.h>

#include "flatbuffers/flatbuffers.h"
#include "libcomm/frame_transport.h"
#include "pwm_messages_generated.h"

namespace libcomm {

class PwmEndpoint {
public:
    using WriteCallback = FrameTransport::WriteCallback;
    using ChannelTelemetryHandler = etl::delegate<void(const midi2pwm::pwm::ChannelTelemetry&)>;
    using ChannelConfigHandler = etl::delegate<void(const midi2pwm::pwm::ChannelConfig&)>;
    using FaultLogHandler = etl::delegate<void(const midi2pwm::pwm::FaultLog&)>;
    using FaultControlHandler = etl::delegate<void(const midi2pwm::pwm::FaultControlCommand&)>;
    using HeartBeatHandler = etl::delegate<void(const midi2pwm::pwm::HeartBeat&)>;
    using ResponseHandler = etl::delegate<void(const midi2pwm::pwm::Response&)>;

    explicit PwmEndpoint(WriteCallback write, bool synchronous_ack = false);

    PwmEndpoint(const PwmEndpoint&) = delete;
    PwmEndpoint& operator=(const PwmEndpoint&) = delete;
    ~PwmEndpoint() = default;

    bool Send(flatbuffers::DetachedBuffer&& buffer);
    bool HandleIncoming(const std::uint8_t* data, std::size_t size);

    void OnChannelTelemetry(ChannelTelemetryHandler handler);
    void OnChannelConfig(ChannelConfigHandler handler);
    void OnFaultLog(FaultLogHandler handler);
    void OnFaultControl(FaultControlHandler handler);
    void OnHeartBeat(HeartBeatHandler handler);
    void OnResponse(ResponseHandler handler);

private:
    bool HandleFrame(const std::uint8_t* data, std::size_t size) const;

    FrameTransport transport_;
    ChannelTelemetryHandler telemetry_handler_;
    ChannelConfigHandler config_handler_;
    FaultLogHandler fault_log_handler_;
    FaultControlHandler fault_control_handler_;
    HeartBeatHandler heartbeat_handler_;
    ResponseHandler response_handler_;
};

flatbuffers::DetachedBuffer BuildChannelTelemetryMessage(std::uint16_t channel_number,
                                                         midi2pwm::pwm::ChannelConfiguration configuration,
                                                         midi2pwm::pwm::ChannelStatus status,
                                                         std::uint16_t note,
                                                         float voltage,
                                                         float current,
                                                         float midpoint,
                                                         float min_point,
                                                         float max_point,
                                                         bool had_fault);

flatbuffers::DetachedBuffer BuildChannelConfigMessage(std::uint16_t channel_number,
                                                      midi2pwm::pwm::ChannelConfiguration configuration,
                                                      std::uint16_t note,
                                                      float midpoint,
                                                      float min_point,
                                                      float max_point);

struct FaultLogEntryData {
    std::uint32_t timestamp_ms;
    midi2pwm::pwm::FaultType fault;
};

flatbuffers::DetachedBuffer BuildFaultLogMessage(std::uint32_t log_size,
                                                 const FaultLogEntryData* entries,
                                                 std::size_t entry_count);

flatbuffers::DetachedBuffer BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation operation);

flatbuffers::DetachedBuffer BuildHeartBeatMessage(bool request_telemetry = false);

flatbuffers::DetachedBuffer BuildResponseMessage(midi2pwm::pwm::ResponseStatus status,
                                                 std::uint32_t error_code = 0);

}  // namespace libcomm
