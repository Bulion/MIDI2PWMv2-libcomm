#include "libcomm/pwm_endpoint.h"

#include "flatbuffers/verifier.h"

#include <vector>

namespace libcomm {

PwmEndpoint::PwmEndpoint(WriteCallback write, bool synchronous_ack)
    : transport_(write, synchronous_ack) {}

bool PwmEndpoint::Send(flatbuffers::DetachedBuffer&& buffer) {
    return transport_.Send(buffer.data(), buffer.size());
}

bool PwmEndpoint::HandleIncoming(const std::uint8_t* data, std::size_t size) {
    return transport_.HandleIncoming(
        data,
        size,
        FrameTransport::DataHandler::create<const PwmEndpoint, &PwmEndpoint::HandleFrame>(*this));
}

void PwmEndpoint::OnChannelTelemetry(ChannelTelemetryHandler handler) {
    telemetry_handler_ = handler;
}

void PwmEndpoint::OnChannelConfig(ChannelConfigHandler handler) {
    config_handler_ = handler;
}

void PwmEndpoint::OnFaultLog(FaultLogHandler handler) {
    fault_log_handler_ = handler;
}

void PwmEndpoint::OnFaultControl(FaultControlHandler handler) {
    fault_control_handler_ = handler;
}

bool PwmEndpoint::HandleFrame(const std::uint8_t* data, std::size_t size) const {
    if (!data || size == 0U) {
        return false;
    }

    if (!midi2pwm::pwm::EnvelopeBufferHasIdentifier(data)) {
        return false;
    }

    flatbuffers::Verifier verifier(data, size);
    if (!midi2pwm::pwm::VerifyEnvelopeBuffer(verifier)) {
        return false;
    }

    const auto* envelope = midi2pwm::pwm::GetEnvelope(data);
    if (!envelope) {
        return false;
    }

    switch (envelope->message_type()) {
        case midi2pwm::pwm::Message::ChannelTelemetry: {
            if (telemetry_handler_) {
                const auto* telemetry = envelope->message_as_ChannelTelemetry();
                if (telemetry) {
                    telemetry_handler_(*telemetry);
                }
            }
            return true;
        }
        case midi2pwm::pwm::Message::ChannelConfig: {
            if (config_handler_) {
                const auto* config = envelope->message_as_ChannelConfig();
                if (config) {
                    config_handler_(*config);
                }
            }
            return true;
        }
        case midi2pwm::pwm::Message::FaultLog: {
            if (fault_log_handler_) {
                const auto* log = envelope->message_as_FaultLog();
                if (log) {
                    fault_log_handler_(*log);
                }
            }
            return true;
        }
        case midi2pwm::pwm::Message::FaultControlCommand: {
            if (fault_control_handler_) {
                const auto* cmd = envelope->message_as_FaultControlCommand();
                if (cmd) {
                    fault_control_handler_(*cmd);
                }
            }
            return true;
        }
        default:
            return false;
    }
}

namespace {

flatbuffers::DetachedBuffer BuildEnvelope(flatbuffers::FlatBufferBuilder& builder,
                                          midi2pwm::pwm::Message type,
                                          flatbuffers::Offset<void> payload) {
    const auto envelope = midi2pwm::pwm::CreateEnvelope(builder, type, payload);
    builder.Finish(envelope, midi2pwm::pwm::EnvelopeIdentifier());
    return builder.Release();
}

}  // namespace

flatbuffers::DetachedBuffer BuildChannelTelemetryMessage(std::uint16_t channel_number,
                                                         midi2pwm::pwm::ChannelConfiguration configuration,
                                                         midi2pwm::pwm::ChannelStatus status,
                                                         std::uint16_t note,
                                                         float voltage,
                                                         float current,
                                                         float midpoint,
                                                         float min_point,
                                                         float max_point,
                                                         bool had_fault) {
    flatbuffers::FlatBufferBuilder builder;
    const auto telemetry = midi2pwm::pwm::CreateChannelTelemetry(
        builder,
        channel_number,
        configuration,
        status,
        note,
        voltage,
        current,
        midpoint,
        min_point,
        max_point,
        had_fault);
    return BuildEnvelope(builder, midi2pwm::pwm::Message::ChannelTelemetry, telemetry.Union());
}

flatbuffers::DetachedBuffer BuildChannelConfigMessage(std::uint16_t channel_number,
                                                      midi2pwm::pwm::ChannelConfiguration configuration,
                                                      std::uint16_t note,
                                                      float midpoint,
                                                      float min_point,
                                                      float max_point) {
    flatbuffers::FlatBufferBuilder builder;
    const auto config = midi2pwm::pwm::CreateChannelConfig(
        builder,
        channel_number,
        configuration,
        note,
        midpoint,
        min_point,
        max_point);
    return BuildEnvelope(builder, midi2pwm::pwm::Message::ChannelConfig, config.Union());
}

flatbuffers::DetachedBuffer BuildFaultLogMessage(std::uint32_t log_size,
                                                 const FaultLogEntryData* entries,
                                                 std::size_t entry_count) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<midi2pwm::pwm::FaultLogEntry>> entry_offsets;
    if (entries && entry_count > 0U) {
        entry_offsets.reserve(entry_count);
        for (std::size_t i = 0; i < entry_count; ++i) {
            entry_offsets.push_back(
                midi2pwm::pwm::CreateFaultLogEntry(builder, entries[i].timestamp_ms, entries[i].fault));
        }
    }

    const auto entries_vector = builder.CreateVector(
        entry_offsets.empty() ? nullptr : entry_offsets.data(),
        static_cast<flatbuffers::uoffset_t>(entry_offsets.size()));

    const auto log = midi2pwm::pwm::CreateFaultLog(builder, log_size, entries_vector);
    return BuildEnvelope(builder, midi2pwm::pwm::Message::FaultLog, log.Union());
}

flatbuffers::DetachedBuffer BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation operation) {
    flatbuffers::FlatBufferBuilder builder;
    const auto command = midi2pwm::pwm::CreateFaultControlCommand(builder, operation);
    return BuildEnvelope(builder, midi2pwm::pwm::Message::FaultControlCommand, command.Union());
}

}  // namespace libcomm
