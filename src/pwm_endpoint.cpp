#include "libcomm/pwm_endpoint.h"
#include "libcomm/logging.h"

#include "etl/algorithm.h"
#include "etl/vector.h"
#include "flatbuffers/verifier.h"

#include <vector>

namespace libcomm
{

static constexpr const char* TAG = "PwmEndpoint";

PwmEndpoint::PwmEndpoint(WriteCallback writeCallback)
    : transport_(writeCallback)
{
}

bool PwmEndpoint::Send(flatbuffers::DetachedBuffer &&serializedMessageBuffer)
{
    const std::uint8_t *bufferData = serializedMessageBuffer.data();
    std::size_t bufferSizeBytes = serializedMessageBuffer.size();

    return transport_.Send(bufferData, bufferSizeBytes);
}

bool PwmEndpoint::HandleIncoming(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    auto handler = FrameTransport::DataHandler::create<PwmEndpoint, &PwmEndpoint::HandleFrame>(*this);
    return transport_.HandleIncoming(receivedData, receivedSizeBytes, handler);
}

void PwmEndpoint::OnChannelTelemetry(ChannelTelemetryHandler callbackHandler)
{
    telemetry_handler_ = callbackHandler;
}

void PwmEndpoint::OnChannelConfig(ChannelConfigHandler callbackHandler)
{
    config_handler_ = callbackHandler;
}

void PwmEndpoint::OnFaultLog(FaultLogHandler callbackHandler)
{
    fault_log_handler_ = callbackHandler;
}

void PwmEndpoint::OnFaultControl(FaultControlHandler callbackHandler)
{
    fault_control_handler_ = callbackHandler;
}

void PwmEndpoint::OnHeartBeat(HeartBeatHandler callbackHandler)
{
    heartbeat_handler_ = callbackHandler;
}

void PwmEndpoint::OnResponse(ResponseHandler callbackHandler)
{
    response_handler_ = callbackHandler;
}

bool PwmEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame: null data or zero size");
        return false;
    }

    bool hasValidFlatBuffersIdentifier = midi2pwm::pwm::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidFlatBuffersIdentifier) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid FlatBuffers identifier in PWM message");
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::pwm::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        LIBCOMM_LOG_ERROR(TAG, "FlatBuffers verification failed for PWM envelope");
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::pwm::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        LIBCOMM_LOG_ERROR(TAG, "Failed to deserialize PWM envelope");
        return false;
    }

    switch (deserializedEnvelope->message_type()) {
    case midi2pwm::pwm::Message::ChannelTelemetry: {
        const auto *channelTelemetryMessage = deserializedEnvelope->message_as_ChannelTelemetry();
        if (channelTelemetryMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received ChannelTelemetry: channel=%u status=%u", channelTelemetryMessage->channel_number(), static_cast<unsigned int>(channelTelemetryMessage->status()));
            if (telemetry_handler_) {
                LIBCOMM_LOG_DEBUG(TAG, "Telemetry: voltage=%d mV current=%d mA fault=%d", static_cast<int>(channelTelemetryMessage->voltage() * 1000), static_cast<int>(channelTelemetryMessage->current() * 1000), channelTelemetryMessage->had_fault());
                telemetry_handler_(*channelTelemetryMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for ChannelTelemetry");
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::ChannelConfig: {
        const auto *channelConfigMessage = deserializedEnvelope->message_as_ChannelConfig();
        if (channelConfigMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received ChannelConfig: channel=%u config=%u", channelConfigMessage->channel_number(), static_cast<unsigned int>(channelConfigMessage->configuration()));
            if (config_handler_) {
                LIBCOMM_LOG_DEBUG(TAG, "Config: note=%u", channelConfigMessage->note());
                config_handler_(*channelConfigMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for ChannelConfig");
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::FaultLog: {
        const auto *faultLogMessage = deserializedEnvelope->message_as_FaultLog();
        if (faultLogMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received FaultLog: log_size=%u", static_cast<unsigned int>(faultLogMessage->log_size()));
            if (fault_log_handler_) {
                fault_log_handler_(*faultLogMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for FaultLog");
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::FaultControlCommand: {
        const auto *faultControlCommand = deserializedEnvelope->message_as_FaultControlCommand();
        if (faultControlCommand) {
            LIBCOMM_LOG_INFO(TAG, "Received FaultControlCommand: operation=%u", static_cast<unsigned int>(faultControlCommand->operation()));
            if (fault_control_handler_) {
                fault_control_handler_(*faultControlCommand);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for FaultControlCommand");
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::HeartBeat: {
        const auto *heartBeatMessage = deserializedEnvelope->message_as_HeartBeat();
        if (heartBeatMessage) {
            LIBCOMM_LOG_DEBUG(TAG, "Received HeartBeat: sequence=%u", static_cast<unsigned int>(heartBeatMessage->sequence()));
            if (heartbeat_handler_) {
                heartbeat_handler_(*heartBeatMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for HeartBeat");
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::Response: {
        const auto *responseMessage = deserializedEnvelope->message_as_Response();
        if (responseMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received Response: status=%u", static_cast<unsigned int>(responseMessage->status()));
            if (response_handler_) {
                response_handler_(*responseMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for Response");
            }
        }
        return true;
    }
    default:
        LIBCOMM_LOG_ERROR(TAG, "Unknown PWM message type: %u", static_cast<unsigned int>(deserializedEnvelope->message_type()));
        return false;
    }
}

namespace
{

flatbuffers::DetachedBuffer buildSerializedPwmMessageEnvelope(
    flatbuffers::FlatBufferBuilder &flatBuffersBuilder,
    midi2pwm::pwm::Message messageType,
    flatbuffers::Offset<void> serializedPayloadOffset)
{
    auto serializedEnvelope = midi2pwm::pwm::CreateEnvelope(flatBuffersBuilder, messageType, serializedPayloadOffset);
    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::pwm::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildChannelTelemetryMessage(
    std::uint16_t pwmChannelNumber,
    midi2pwm::pwm::ChannelConfiguration channelConfigurationMode,
    midi2pwm::pwm::ChannelStatus channelOperationalStatus,
    std::uint16_t assignedMidiNoteNumber,
    float measuredVoltageVolts,
    float measuredCurrentAmps,
    float midpointPositionValue,
    float minimumPositionValue,
    float maximumPositionValue,
    bool channelExperiencedFaultCondition)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedTelemetryMessage = midi2pwm::pwm::CreateChannelTelemetry(
        flatBuffersBuilder,
        pwmChannelNumber,
        channelConfigurationMode,
        channelOperationalStatus,
        assignedMidiNoteNumber,
        measuredVoltageVolts,
        measuredCurrentAmps,
        midpointPositionValue,
        minimumPositionValue,
        maximumPositionValue,
        channelExperiencedFaultCondition);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::ChannelTelemetry, serializedTelemetryMessage.Union());
}

flatbuffers::DetachedBuffer BuildChannelConfigMessage(
    std::uint16_t pwmChannelNumber,
    midi2pwm::pwm::ChannelConfiguration channelConfigurationMode,
    std::uint16_t assignedMidiNoteNumber,
    float midpointPositionValue,
    float minimumPositionValue,
    float maximumPositionValue)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedConfigMessage = midi2pwm::pwm::CreateChannelConfig(
        flatBuffersBuilder,
        pwmChannelNumber,
        channelConfigurationMode,
        assignedMidiNoteNumber,
        midpointPositionValue,
        minimumPositionValue,
        maximumPositionValue);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::ChannelConfig, serializedConfigMessage.Union());
}

flatbuffers::DetachedBuffer BuildFaultLogMessage(
    std::uint32_t totalLogSizeEntries, const FaultLogEntryData *faultLogEntries, std::size_t providedEntryCount)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    constexpr std::size_t MAXIMUM_FAULT_LOG_ENTRIES_PER_MESSAGE = 64U;
    etl::vector<flatbuffers::Offset<midi2pwm::pwm::FaultLogEntry>, MAXIMUM_FAULT_LOG_ENTRIES_PER_MESSAGE>
        serializedEntryOffsets;

    if (faultLogEntries && providedEntryCount > 0U) {
        std::size_t entriesToSerializeCount = etl::min(providedEntryCount, serializedEntryOffsets.max_size());

        for (std::size_t entryIndex = 0; entryIndex < entriesToSerializeCount; ++entryIndex) {
            std::uint64_t faultTimestampMilliseconds = faultLogEntries[entryIndex].timestamp_ms;
            midi2pwm::pwm::FaultType faultType = faultLogEntries[entryIndex].fault;

            serializedEntryOffsets.push_back(
                midi2pwm::pwm::CreateFaultLogEntry(flatBuffersBuilder, faultTimestampMilliseconds, faultType));
        }
    }

    const flatbuffers::Offset<midi2pwm::pwm::FaultLogEntry> *vectorDataPointer =
        serializedEntryOffsets.empty() ? nullptr : serializedEntryOffsets.data();
    auto serializedEntriesVector = flatBuffersBuilder.CreateVector(
        vectorDataPointer, static_cast<flatbuffers::uoffset_t>(serializedEntryOffsets.size()));

    auto serializedFaultLogMessage =
        midi2pwm::pwm::CreateFaultLog(flatBuffersBuilder, totalLogSizeEntries, serializedEntriesVector);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::FaultLog, serializedFaultLogMessage.Union());
}

flatbuffers::DetachedBuffer BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation controlOperation)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedFaultControlCommand = midi2pwm::pwm::CreateFaultControlCommand(flatBuffersBuilder, controlOperation);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::FaultControlCommand, serializedFaultControlCommand.Union());
}

flatbuffers::DetachedBuffer BuildHeartBeatMessage(bool shouldRequestTelemetry)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedHeartBeatMessage = midi2pwm::pwm::CreateHeartBeat(flatBuffersBuilder, shouldRequestTelemetry);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::HeartBeat, serializedHeartBeatMessage.Union());
}

flatbuffers::DetachedBuffer BuildResponseMessage(midi2pwm::pwm::ResponseStatus responseStatus, std::uint32_t errorCode)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedResponseMessage = midi2pwm::pwm::CreateResponse(flatBuffersBuilder, responseStatus, errorCode);

    return buildSerializedPwmMessageEnvelope(
        flatBuffersBuilder, midi2pwm::pwm::Message::Response, serializedResponseMessage.Union());
}

} // namespace libcomm
