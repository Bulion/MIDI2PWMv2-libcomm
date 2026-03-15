#include "libcomm/midi_endpoint.h"
#include "libcomm/logging.h"

#include "flatbuffers/verifier.h"

namespace libcomm
{

static constexpr const char* TAG = "MidiEndpoint";

MidiEndpoint::MidiEndpoint(WriteCallback writeCallback)
    : transport_(writeCallback)
{
}

bool MidiEndpoint::Send(flatbuffers::DetachedBuffer &&serializedMessageBuffer)
{
    const std::uint8_t *bufferData = serializedMessageBuffer.data();
    std::size_t bufferSizeBytes = serializedMessageBuffer.size();

    return transport_.Send(bufferData, bufferSizeBytes);
}

bool MidiEndpoint::HandleIncoming(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    auto handler = FrameTransport::DataHandler::create<MidiEndpoint, &MidiEndpoint::HandleFrame>(*this);
    return transport_.HandleIncoming(receivedData, receivedSizeBytes, handler);
}

bool MidiEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame: null data or zero size");
        return false;
    }

    bool hasValidFlatBuffersIdentifier = midi2pwm::midi::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidFlatBuffersIdentifier) {
        LIBCOMM_LOG_DEBUG(TAG, "Invalid FlatBuffers identifier in MIDI message");
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::midi::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        LIBCOMM_LOG_ERROR(TAG, "FlatBuffers verification failed for MIDI envelope");
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::midi::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        LIBCOMM_LOG_ERROR(TAG, "Failed to deserialize MIDI envelope");
        return false;
    }

    switch (deserializedEnvelope->packet_type()) {
    case midi2pwm::midi::Packet::Ping: {
        LIBCOMM_LOG_INFO(TAG, "Received Ping message");
        if (ping_handler_) {
            const auto *pingMessage = deserializedEnvelope->packet_as_Ping();
            if (pingMessage) {
                LIBCOMM_LOG_DEBUG(TAG, "Ping: seq=%u timestamp=%u", static_cast<unsigned int>(pingMessage->sequence()), static_cast<unsigned int>(pingMessage->timestamp()));
                ping_handler_(*pingMessage);
            }
        } else {
            LIBCOMM_LOG_WARN(TAG, "No handler registered for Ping message");
        }
        break;
    }
    case midi2pwm::midi::Packet::ChannelMessage: {
        const auto *channelMessage = deserializedEnvelope->packet_as_ChannelMessage();
        if (channelMessage) {
            LIBCOMM_LOG_DEBUG(TAG, "Received ChannelMessage: type=%u channel=%u", static_cast<unsigned int>(channelMessage->message_type()), channelMessage->channel());
            if (channel_handler_) {
                LIBCOMM_LOG_DEBUG(TAG, "ChannelMessage: data1=%u data2=%u", channelMessage->data1(), channelMessage->data2());
                channel_handler_(*channelMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for ChannelMessage");
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemCommonMessage: {
        LIBCOMM_LOG_INFO(TAG, "Received SystemCommonMessage");
        if (system_common_handler_) {
            const auto *systemCommonMessage = deserializedEnvelope->packet_as_SystemCommonMessage();
            if (systemCommonMessage) {
                LIBCOMM_LOG_DEBUG(TAG, "SystemCommon: type=%u value=%u", static_cast<unsigned int>(systemCommonMessage->message_type()), systemCommonMessage->value());
                system_common_handler_(*systemCommonMessage);
            }
        } else {
            LIBCOMM_LOG_WARN(TAG, "No handler registered for SystemCommonMessage");
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemRealTimeMessage: {
        LIBCOMM_LOG_DEBUG(TAG, "Received SystemRealTimeMessage");
        if (system_real_time_handler_) {
            const auto *systemRealTimeMessage = deserializedEnvelope->packet_as_SystemRealTimeMessage();
            if (systemRealTimeMessage) {
                LIBCOMM_LOG_DEBUG(TAG, "SystemRealTime: type=%u", static_cast<unsigned int>(systemRealTimeMessage->message_type()));
                system_real_time_handler_(*systemRealTimeMessage);
            }
        } else {
            LIBCOMM_LOG_WARN(TAG, "No handler registered for SystemRealTimeMessage");
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemExclusiveMessage: {
        const auto *systemExclusiveMessage = deserializedEnvelope->packet_as_SystemExclusiveMessage();
        if (systemExclusiveMessage) {
#if LIBCOMM_LOG_LEVEL >= LOG_LEVEL_INFO
            std::size_t payloadSize = systemExclusiveMessage->payload() ? systemExclusiveMessage->payload()->size() : 0;
            LIBCOMM_LOG_INFO(TAG, "Received SystemExclusiveMessage: payload_size=%u", static_cast<unsigned int>(payloadSize));
#endif
            if (system_exclusive_handler_) {
                system_exclusive_handler_(*systemExclusiveMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for SystemExclusiveMessage");
            }
        }
        break;
    }
    default:
        LIBCOMM_LOG_ERROR(TAG, "Unknown MIDI packet type: %u", static_cast<unsigned int>(deserializedEnvelope->packet_type()));
        return false;
    }

    return true;
}

void MidiEndpoint::OnPing(PingHandler callbackHandler)
{
    ping_handler_ = callbackHandler;
}

void MidiEndpoint::OnChannelMessage(ChannelMessageHandler callbackHandler)
{
    channel_handler_ = callbackHandler;
}

void MidiEndpoint::OnSystemCommon(SystemCommonHandler callbackHandler)
{
    system_common_handler_ = callbackHandler;
}

void MidiEndpoint::OnSystemRealTime(SystemRealTimeHandler callbackHandler)
{
    system_real_time_handler_ = callbackHandler;
}

void MidiEndpoint::OnSystemExclusive(SystemExclusiveHandler callbackHandler)
{
    system_exclusive_handler_ = callbackHandler;
}

flatbuffers::DetachedBuffer BuildPingMessage(std::uint32_t sequenceNumber, std::uint64_t timestampValue)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedPingMessage = midi2pwm::midi::CreatePing(flatBuffersBuilder, sequenceNumber, timestampValue);
    auto serializedEnvelope =
        midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::Ping, serializedPingMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

namespace
{

flatbuffers::DetachedBuffer buildSerializedChannelMessageEnvelope(
    std::uint8_t midiChannelNumber,
    midi2pwm::midi::ChannelMessageType messageType,
    std::uint8_t firstDataByte,
    std::uint8_t secondDataByte)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedChannelMessage = midi2pwm::midi::CreateChannelMessage(
        flatBuffersBuilder, midiChannelNumber, messageType, firstDataByte, secondDataByte);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(
        flatBuffersBuilder, midi2pwm::midi::Packet::ChannelMessage, serializedChannelMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer
BuildNoteOffMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t velocityValue)
{
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::NoteOff, noteNumber, velocityValue);
}

flatbuffers::DetachedBuffer
BuildNoteOnMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t velocityValue)
{
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::NoteOn, noteNumber, velocityValue);
}

flatbuffers::DetachedBuffer
BuildPolyphonicKeyPressureMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t pressureValue)
{
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure, noteNumber, pressureValue);
}

flatbuffers::DetachedBuffer
BuildControlChangeMessage(std::uint8_t midiChannelNumber, std::uint8_t controllerNumber, std::uint8_t controllerValue)
{
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::ControlChange, controllerNumber, controllerValue);
}

flatbuffers::DetachedBuffer BuildProgramChangeMessage(std::uint8_t midiChannelNumber, std::uint8_t programNumber)
{
    constexpr std::uint16_t UNUSED_SECOND_DATA_BYTE = 0U;
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::ProgramChange, programNumber, UNUSED_SECOND_DATA_BYTE);
}

flatbuffers::DetachedBuffer BuildChannelPressureMessage(std::uint8_t midiChannelNumber, std::uint8_t pressureValue)
{
    constexpr std::uint16_t UNUSED_SECOND_DATA_BYTE = 0U;
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::ChannelPressure, pressureValue, UNUSED_SECOND_DATA_BYTE);
}

flatbuffers::DetachedBuffer BuildPitchBendMessage(std::uint8_t midiChannelNumber, std::uint16_t pitchBendValue)
{
    constexpr std::uint8_t MIDI_DATA_BYTE_MASK = 0x7F;
    std::uint8_t leastSignificantByte = static_cast<std::uint8_t>(pitchBendValue & MIDI_DATA_BYTE_MASK);
    std::uint8_t mostSignificantByte = static_cast<std::uint8_t>((pitchBendValue >> 7) & MIDI_DATA_BYTE_MASK);
    return buildSerializedChannelMessageEnvelope(
        midiChannelNumber, midi2pwm::midi::ChannelMessageType::PitchBend, leastSignificantByte, mostSignificantByte);
}

namespace
{

flatbuffers::DetachedBuffer
buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType messageType, std::uint16_t messageDataValue)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedSystemCommonMessage =
        midi2pwm::midi::CreateSystemCommonMessage(flatBuffersBuilder, messageType, messageDataValue);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(
        flatBuffersBuilder, midi2pwm::midi::Packet::SystemCommonMessage, serializedSystemCommonMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildTimeCodeQuarterFrameMessage(std::uint8_t quarterFrameValue)
{
    return buildSerializedSystemCommonMessageEnvelope(
        midi2pwm::midi::SystemCommonType::TimeCodeQuarterFrame, quarterFrameValue);
}

flatbuffers::DetachedBuffer BuildSongPositionPointerMessage(std::uint16_t songPositionBeats)
{
    return buildSerializedSystemCommonMessageEnvelope(
        midi2pwm::midi::SystemCommonType::SongPositionPointer, songPositionBeats);
}

flatbuffers::DetachedBuffer BuildSongSelectMessage(std::uint8_t songNumber)
{
    return buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType::SongSelect, songNumber);
}

flatbuffers::DetachedBuffer BuildTuneRequestMessage()
{
    constexpr std::uint16_t TUNE_REQUEST_HAS_NO_DATA_VALUE = 0U;
    return buildSerializedSystemCommonMessageEnvelope(
        midi2pwm::midi::SystemCommonType::TuneRequest, TUNE_REQUEST_HAS_NO_DATA_VALUE);
}

flatbuffers::DetachedBuffer BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType realTimeMessageType)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedSystemRealTimeMessage =
        midi2pwm::midi::CreateSystemRealTimeMessage(flatBuffersBuilder, realTimeMessageType);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(
        flatBuffersBuilder, midi2pwm::midi::Packet::SystemRealTimeMessage, serializedSystemRealTimeMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

flatbuffers::DetachedBuffer BuildSystemExclusiveMessage(
    const std::uint8_t *manufacturerIdBytes,
    std::size_t manufacturerIdLengthBytes,
    const std::uint8_t *sysexPayloadBytes,
    std::size_t sysexPayloadLengthBytes)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> serializedManufacturerIdOffset = 0;
    if (manufacturerIdBytes && manufacturerIdLengthBytes > 0U) {
        serializedManufacturerIdOffset = flatBuffersBuilder.CreateVector(
            manufacturerIdBytes, static_cast<flatbuffers::uoffset_t>(manufacturerIdLengthBytes));
    }

    flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> serializedPayloadOffset = 0;
    if (sysexPayloadBytes && sysexPayloadLengthBytes > 0U) {
        serializedPayloadOffset = flatBuffersBuilder.CreateVector(
            sysexPayloadBytes, static_cast<flatbuffers::uoffset_t>(sysexPayloadLengthBytes));
    }

    auto serializedSystemExclusiveMessage = midi2pwm::midi::CreateSystemExclusiveMessage(
        flatBuffersBuilder, serializedManufacturerIdOffset, serializedPayloadOffset);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(
        flatBuffersBuilder, midi2pwm::midi::Packet::SystemExclusiveMessage, serializedSystemExclusiveMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace libcomm
