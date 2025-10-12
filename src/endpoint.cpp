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

    if (!midi2pwm::comm::EnvelopeBufferHasIdentifier(data)) {
        return false;
    }

    flatbuffers::Verifier verifier(data, size);
    if (!midi2pwm::comm::VerifyEnvelopeBuffer(verifier)) {
        return false;
    }

    const auto *envelope = midi2pwm::comm::GetEnvelope(data);
    if (!envelope) {
        return false;
    }

    switch (envelope->packet_type()) {
    case midi2pwm::comm::Packet::Ping: {
        if (ping_handler_) {
            const auto *ping = envelope->packet_as_Ping();
            if (ping) {
                ping_handler_(*ping);
            }
        }
        break;
    }
    case midi2pwm::comm::Packet::ControlChange: {
        if (control_change_handler_) {
            const auto *cc = envelope->packet_as_ControlChange();
            if (cc) {
                control_change_handler_(*cc);
            }
        }
        break;
    }
    default:
        return false;
    }

    return true;
}

void Endpoint::OnPing(PingHandler handler)
{
    ping_handler_ = handler;
}

void Endpoint::OnControlChange(ControlChangeHandler handler)
{
    control_change_handler_ = handler;
}

flatbuffers::DetachedBuffer BuildPingMessage(std::uint32_t sequence, std::uint64_t timestamp)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto ping = midi2pwm::comm::CreatePing(builder, sequence, timestamp);
    const auto envelope = midi2pwm::comm::CreateEnvelope(builder, midi2pwm::comm::Packet::Ping, ping.Union());
    builder.Finish(envelope, midi2pwm::comm::EnvelopeIdentifier());
    return builder.Release();
}

flatbuffers::DetachedBuffer BuildControlChangeMessage(std::uint8_t channel, std::uint8_t controller, std::uint8_t value)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto control_change = midi2pwm::comm::CreateControlChange(builder, channel, controller, value);
    const auto envelope =
        midi2pwm::comm::CreateEnvelope(builder, midi2pwm::comm::Packet::ControlChange, control_change.Union());
    builder.Finish(envelope, midi2pwm::comm::EnvelopeIdentifier());
    return builder.Release();
}

} // namespace libcomm
