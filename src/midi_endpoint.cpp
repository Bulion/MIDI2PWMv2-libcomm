#include "libcomm/midi_endpoint.h"

#include "flatbuffers/verifier.h"

namespace libcomm
{

MidiEndpoint::MidiEndpoint(WriteCallback writeCallback, bool useSynchronousAcknowledgment)
    : transport_(writeCallback, useSynchronousAcknowledgment)
{
}

bool MidiEndpoint::Send(flatbuffers::DetachedBuffer &&serializedMessageBuffer)
{
    const std::uint8_t* bufferData = serializedMessageBuffer.data();
    std::size_t bufferSizeBytes = serializedMessageBuffer.size();

    return transport_.Send(bufferData, bufferSizeBytes);
}

bool MidiEndpoint::HandleIncoming(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    auto frameHandlerDelegate = FrameTransport::DataHandler::create<const MidiEndpoint, &MidiEndpoint::HandleFrame>(*this);

    return transport_.HandleIncoming(receivedData, receivedSizeBytes, frameHandlerDelegate);
}

bool MidiEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        return false;
    }

    bool hasValidFlatBuffersIdentifier = midi2pwm::midi::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidFlatBuffersIdentifier) {
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::midi::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::midi::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        return false;
    }

    switch (deserializedEnvelope->packet_type()) {
    case midi2pwm::midi::Packet::Ping: {
        if (ping_handler_) {
            const auto *pingMessage = deserializedEnvelope->packet_as_Ping();
            if (pingMessage) {
                ping_handler_(*pingMessage);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::ChannelMessage: {
        if (channel_handler_) {
            const auto *channelMessage = deserializedEnvelope->packet_as_ChannelMessage();
            if (channelMessage) {
                channel_handler_(*channelMessage);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemCommonMessage: {
        if (system_common_handler_) {
            const auto *systemCommonMessage = deserializedEnvelope->packet_as_SystemCommonMessage();
            if (systemCommonMessage) {
                system_common_handler_(*systemCommonMessage);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemRealTimeMessage: {
        if (system_real_time_handler_) {
            const auto *systemRealTimeMessage = deserializedEnvelope->packet_as_SystemRealTimeMessage();
            if (systemRealTimeMessage) {
                system_real_time_handler_(*systemRealTimeMessage);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemExclusiveMessage: {
        if (system_exclusive_handler_) {
            const auto *systemExclusiveMessage = deserializedEnvelope->packet_as_SystemExclusiveMessage();
            if (systemExclusiveMessage) {
                system_exclusive_handler_(*systemExclusiveMessage);
            }
        }
        break;
    }
    default:
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
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::Ping, serializedPingMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

namespace
{

flatbuffers::DetachedBuffer buildSerializedChannelMessageEnvelope(
    std::uint8_t midiChannelNumber,
    midi2pwm::midi::ChannelMessageType messageType,
    std::uint16_t firstDataByte,
    std::uint16_t secondDataByte)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedChannelMessage = midi2pwm::midi::CreateChannelMessage(flatBuffersBuilder, midiChannelNumber, messageType, firstDataByte, secondDataByte);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::ChannelMessage, serializedChannelMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildNoteOffMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t velocityValue)
{
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::NoteOff, noteNumber, velocityValue);
}

flatbuffers::DetachedBuffer BuildNoteOnMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t velocityValue)
{
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::NoteOn, noteNumber, velocityValue);
}

flatbuffers::DetachedBuffer
BuildPolyphonicKeyPressureMessage(std::uint8_t midiChannelNumber, std::uint8_t noteNumber, std::uint8_t pressureValue)
{
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure, noteNumber, pressureValue);
}

flatbuffers::DetachedBuffer BuildControlChangeMessage(std::uint8_t midiChannelNumber, std::uint8_t controllerNumber, std::uint8_t controllerValue)
{
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::ControlChange, controllerNumber, controllerValue);
}

flatbuffers::DetachedBuffer BuildProgramChangeMessage(std::uint8_t midiChannelNumber, std::uint8_t programNumber)
{
    constexpr std::uint16_t UNUSED_SECOND_DATA_BYTE = 0U;
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::ProgramChange, programNumber, UNUSED_SECOND_DATA_BYTE);
}

flatbuffers::DetachedBuffer BuildChannelPressureMessage(std::uint8_t midiChannelNumber, std::uint8_t pressureValue)
{
    constexpr std::uint16_t UNUSED_SECOND_DATA_BYTE = 0U;
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::ChannelPressure, pressureValue, UNUSED_SECOND_DATA_BYTE);
}

flatbuffers::DetachedBuffer BuildPitchBendMessage(std::uint8_t midiChannelNumber, std::uint16_t pitchBendValue)
{
    constexpr std::uint16_t UNUSED_SECOND_DATA_BYTE = 0U;
    return buildSerializedChannelMessageEnvelope(midiChannelNumber, midi2pwm::midi::ChannelMessageType::PitchBend, pitchBendValue, UNUSED_SECOND_DATA_BYTE);
}

namespace
{

flatbuffers::DetachedBuffer buildSerializedSystemCommonMessageEnvelope(
    midi2pwm::midi::SystemCommonType messageType,
    std::uint16_t messageDataValue)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedSystemCommonMessage = midi2pwm::midi::CreateSystemCommonMessage(flatBuffersBuilder, messageType, messageDataValue);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::SystemCommonMessage, serializedSystemCommonMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildTimeCodeQuarterFrameMessage(std::uint8_t quarterFrameValue)
{
    return buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType::TimeCodeQuarterFrame, quarterFrameValue);
}

flatbuffers::DetachedBuffer BuildSongPositionPointerMessage(std::uint16_t songPositionBeats)
{
    return buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType::SongPositionPointer, songPositionBeats);
}

flatbuffers::DetachedBuffer BuildSongSelectMessage(std::uint8_t songNumber)
{
    return buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType::SongSelect, songNumber);
}

flatbuffers::DetachedBuffer BuildTuneRequestMessage()
{
    constexpr std::uint16_t TUNE_REQUEST_HAS_NO_DATA_VALUE = 0U;
    return buildSerializedSystemCommonMessageEnvelope(midi2pwm::midi::SystemCommonType::TuneRequest, TUNE_REQUEST_HAS_NO_DATA_VALUE);
}

flatbuffers::DetachedBuffer BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType realTimeMessageType)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedSystemRealTimeMessage = midi2pwm::midi::CreateSystemRealTimeMessage(flatBuffersBuilder, realTimeMessageType);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::SystemRealTimeMessage, serializedSystemRealTimeMessage.Union());

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
        serializedManufacturerIdOffset = flatBuffersBuilder.CreateVector(manufacturerIdBytes, static_cast<flatbuffers::uoffset_t>(manufacturerIdLengthBytes));
    }

    flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> serializedPayloadOffset = 0;
    if (sysexPayloadBytes && sysexPayloadLengthBytes > 0U) {
        serializedPayloadOffset = flatBuffersBuilder.CreateVector(sysexPayloadBytes, static_cast<flatbuffers::uoffset_t>(sysexPayloadLengthBytes));
    }

    auto serializedSystemExclusiveMessage = midi2pwm::midi::CreateSystemExclusiveMessage(flatBuffersBuilder, serializedManufacturerIdOffset, serializedPayloadOffset);
    auto serializedEnvelope = midi2pwm::midi::CreateEnvelope(flatBuffersBuilder, midi2pwm::midi::Packet::SystemExclusiveMessage, serializedSystemExclusiveMessage.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::midi::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace libcomm
