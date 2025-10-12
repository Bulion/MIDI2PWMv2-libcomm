#include "libcomm/endpoint.h"

#include "flatbuffers/verifier.h"

namespace libcomm
{

Endpoint::Endpoint(WriteCallback write)
    : write_(write)
{
}

bool Endpoint::Send(flatbuffers::DetachedBuffer &&buffer) const
{
    if (!write_) {
        return false;
    }

    const bool result = write_(buffer.data(), buffer.size());
    return result;
}

bool Endpoint::HandleIncoming(const std::uint8_t *data, std::size_t size) const
{
    if (!data || size == 0) {
        return false;
    }

    if (!midi2pwm::midi::EnvelopeBufferHasIdentifier(data)) {
        return false;
    }

    flatbuffers::Verifier verifier(data, size);
    if (!midi2pwm::midi::VerifyEnvelopeBuffer(verifier)) {
        return false;
    }

    const auto *envelope = midi2pwm::midi::GetEnvelope(data);
    if (!envelope) {
        return false;
    }

    switch (envelope->packet_type()) {
    case midi2pwm::midi::Packet::Ping: {
        if (ping_handler_) {
            const auto *ping = envelope->packet_as_Ping();
            if (ping) {
                ping_handler_(*ping);
            }
        }
    }
    case midi2pwm::midi::Packet::ChannelMessage: {
        if (channel_handler_) {
            const auto *channel = envelope->packet_as_ChannelMessage();
            if (channel) {
                channel_handler_(*channel);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemCommonMessage: {
        if (system_common_handler_) {
            const auto *sys_common = envelope->packet_as_SystemCommonMessage();
            if (sys_common) {
                system_common_handler_(*sys_common);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemRealTimeMessage: {
        if (system_real_time_handler_) {
            const auto *sys_rt = envelope->packet_as_SystemRealTimeMessage();
            if (sys_rt) {
                system_real_time_handler_(*sys_rt);
            }
        }
        break;
    }
    case midi2pwm::midi::Packet::SystemExclusiveMessage: {
        if (system_exclusive_handler_) {
            const auto *sysex = envelope->packet_as_SystemExclusiveMessage();
            if (sysex) {
                system_exclusive_handler_(*sysex);
            }
        }
    } break;
    default:
        return false;
    }

    return true;
}

void Endpoint::OnPing(PingHandler handler)
{
    ping_handler_ = handler;
}

void Endpoint::OnChannelMessage(ChannelMessageHandler handler)
{
    channel_handler_ = handler;
}

void Endpoint::OnSystemCommon(SystemCommonHandler handler)
{
    system_common_handler_ = handler;
}

void Endpoint::OnSystemRealTime(SystemRealTimeHandler handler)
{
    system_real_time_handler_ = handler;
}

void Endpoint::OnSystemExclusive(SystemExclusiveHandler handler)
{
    system_exclusive_handler_ = handler;
}

flatbuffers::DetachedBuffer BuildPingMessage(std::uint32_t sequence, std::uint64_t timestamp)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto ping = midi2pwm::midi::CreatePing(builder, sequence, timestamp);
    const auto envelope = midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::Ping, ping.Union());
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    return builder.Release();
}

namespace
{

flatbuffers::DetachedBuffer BuildChannelEnvelope(
    std::uint8_t channel, midi2pwm::midi::ChannelMessageType type, std::uint16_t data1, std::uint16_t data2)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto msg = midi2pwm::midi::CreateChannelMessage(builder, channel, type, data1, data2);
    const auto envelope = midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::ChannelMessage, msg.Union());
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    return builder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildNoteOffMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::NoteOff, note, velocity);
}

flatbuffers::DetachedBuffer BuildNoteOnMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::NoteOn, note, velocity);
}

flatbuffers::DetachedBuffer
BuildPolyphonicKeyPressureMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t pressure)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure, note, pressure);
}

flatbuffers::DetachedBuffer BuildControlChangeMessage(std::uint8_t channel, std::uint8_t controller, std::uint8_t value)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::ControlChange, controller, value);
}

flatbuffers::DetachedBuffer BuildProgramChangeMessage(std::uint8_t channel, std::uint8_t program)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::ProgramChange, program, 0U);
}

flatbuffers::DetachedBuffer BuildChannelPressureMessage(std::uint8_t channel, std::uint8_t pressure)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::ChannelPressure, pressure, 0U);
}

flatbuffers::DetachedBuffer BuildPitchBendMessage(std::uint8_t channel, std::uint16_t value)
{
    return BuildChannelEnvelope(channel, midi2pwm::midi::ChannelMessageType::PitchBend, value, 0U);
}

namespace
{

flatbuffers::DetachedBuffer BuildSystemCommonEnvelope(midi2pwm::midi::SystemCommonType type, std::uint16_t value)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto message = midi2pwm::midi::CreateSystemCommonMessage(builder, type, value);
    const auto envelope =
        midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::SystemCommonMessage, message.Union());
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    return builder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildTimeCodeQuarterFrameMessage(std::uint8_t value)
{
    return BuildSystemCommonEnvelope(midi2pwm::midi::SystemCommonType::TimeCodeQuarterFrame, value);
}

flatbuffers::DetachedBuffer BuildSongPositionPointerMessage(std::uint16_t position)
{
    return BuildSystemCommonEnvelope(midi2pwm::midi::SystemCommonType::SongPositionPointer, position);
}

flatbuffers::DetachedBuffer BuildSongSelectMessage(std::uint8_t song_number)
{
    return BuildSystemCommonEnvelope(midi2pwm::midi::SystemCommonType::SongSelect, song_number);
}

flatbuffers::DetachedBuffer BuildTuneRequestMessage()
{
    return BuildSystemCommonEnvelope(midi2pwm::midi::SystemCommonType::TuneRequest, 0U);
}

flatbuffers::DetachedBuffer BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType type)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto message = midi2pwm::midi::CreateSystemRealTimeMessage(builder, type);
    const auto envelope =
        midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::SystemRealTimeMessage, message.Union());
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    return builder.Release();
}

flatbuffers::DetachedBuffer BuildSystemExclusiveMessage(
    const std::uint8_t *manufacturer_id,
    std::size_t manufacturer_id_length,
    const std::uint8_t *payload,
    std::size_t payload_length)
{
    flatbuffers::FlatBufferBuilder builder;

    flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> manufacturer_offset = 0;
    if (manufacturer_id && manufacturer_id_length > 0U) {
        manufacturer_offset =
            builder.CreateVector(manufacturer_id, static_cast<flatbuffers::uoffset_t>(manufacturer_id_length));
    }

    flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> payload_offset = 0;
    if (payload && payload_length > 0U) {
        payload_offset = builder.CreateVector(payload, static_cast<flatbuffers::uoffset_t>(payload_length));
    }

    const auto message = midi2pwm::midi::CreateSystemExclusiveMessage(builder, manufacturer_offset, payload_offset);
    const auto envelope =
        midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::SystemExclusiveMessage, message.Union());
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    return builder.Release();
}

} // namespace libcomm
