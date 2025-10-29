#include "libcomm/pwm_endpoint.h"

#include "etl/algorithm.h"
#include "etl/vector.h"
#include "flatbuffers/verifier.h"

#include <vector>

namespace libcomm
{

PwmEndpoint::PwmEndpoint(WriteCallback writeCallback, bool useSynchronousAcknowledgment)
    : transport_(writeCallback, useSynchronousAcknowledgment)
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
    auto frameHandlerDelegate =
        FrameTransport::DataHandler::create<const PwmEndpoint, &PwmEndpoint::HandleFrame>(*this);

    return transport_.HandleIncoming(receivedData, receivedSizeBytes, frameHandlerDelegate);
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

bool PwmEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        return false;
    }

    bool hasValidFlatBuffersIdentifier = midi2pwm::pwm::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidFlatBuffersIdentifier) {
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::pwm::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::pwm::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        return false;
    }

    switch (deserializedEnvelope->message_type()) {
    case midi2pwm::pwm::Message::ChannelTelemetry: {
        if (telemetry_handler_) {
            const auto *channelTelemetryMessage = deserializedEnvelope->message_as_ChannelTelemetry();
            if (channelTelemetryMessage) {
                telemetry_handler_(*channelTelemetryMessage);
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::ChannelConfig: {
        if (config_handler_) {
            const auto *channelConfigMessage = deserializedEnvelope->message_as_ChannelConfig();
            if (channelConfigMessage) {
                config_handler_(*channelConfigMessage);
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::FaultLog: {
        if (fault_log_handler_) {
            const auto *faultLogMessage = deserializedEnvelope->message_as_FaultLog();
            if (faultLogMessage) {
                fault_log_handler_(*faultLogMessage);
            }
        }
        return true;
    }
    case midi2pwm::pwm::Message::FaultControlCommand: {
        if (fault_control_handler_) {
            const auto *faultControlCommand = deserializedEnvelope->message_as_FaultControlCommand();
            if (faultControlCommand) {
                fault_control_handler_(*faultControlCommand);
            }
        }
        return true;
    }
    default:
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

} // namespace libcomm
