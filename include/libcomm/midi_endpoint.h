#pragma once

#include <cstddef>
#include <cstdint>
#include <etl/delegate.h>

#include "flatbuffers/flatbuffers.h"
#include "libcomm/frame_transport.h"
#include "midi_messages_generated.h"

namespace libcomm
{

class MidiEndpoint
{
public:
    using WriteCallback = FrameTransport::WriteCallback;
    using PingHandler = etl::delegate<void(const midi2pwm::midi::Ping &)>;
    using ChannelMessageHandler = etl::delegate<void(const midi2pwm::midi::ChannelMessage &)>;
    using SystemCommonHandler = etl::delegate<void(const midi2pwm::midi::SystemCommonMessage &)>;
    using SystemRealTimeHandler = etl::delegate<void(const midi2pwm::midi::SystemRealTimeMessage &)>;
    using SystemExclusiveHandler = etl::delegate<void(const midi2pwm::midi::SystemExclusiveMessage &)>;

    explicit MidiEndpoint(WriteCallback write, bool synchronous_ack = false);

    MidiEndpoint(const MidiEndpoint &) = delete;
    MidiEndpoint &operator=(const MidiEndpoint &) = delete;
    ~MidiEndpoint() = default;

    bool Send(flatbuffers::DetachedBuffer &&buffer);
    bool HandleIncoming(const std::uint8_t *data, std::size_t size);

    void OnPing(PingHandler handler);
    void OnChannelMessage(ChannelMessageHandler handler);
    void OnSystemCommon(SystemCommonHandler handler);
    void OnSystemRealTime(SystemRealTimeHandler handler);
    void OnSystemExclusive(SystemExclusiveHandler handler);

private:
    bool HandleFrame(const std::uint8_t *data, std::size_t size) const;

    FrameTransport transport_;
    PingHandler ping_handler_;
    ChannelMessageHandler channel_handler_;
    SystemCommonHandler system_common_handler_;
    SystemRealTimeHandler system_real_time_handler_;
    SystemExclusiveHandler system_exclusive_handler_;
};

flatbuffers::DetachedBuffer BuildPingMessage(std::uint32_t sequence, std::uint64_t timestamp);
flatbuffers::DetachedBuffer BuildNoteOffMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity);
flatbuffers::DetachedBuffer BuildNoteOnMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity);
flatbuffers::DetachedBuffer
BuildPolyphonicKeyPressureMessage(std::uint8_t channel, std::uint8_t note, std::uint8_t pressure);
flatbuffers::DetachedBuffer
BuildControlChangeMessage(std::uint8_t channel, std::uint8_t controller, std::uint8_t value);
flatbuffers::DetachedBuffer BuildProgramChangeMessage(std::uint8_t channel, std::uint8_t program);
flatbuffers::DetachedBuffer BuildChannelPressureMessage(std::uint8_t channel, std::uint8_t pressure);
flatbuffers::DetachedBuffer BuildPitchBendMessage(std::uint8_t channel, std::uint16_t value);

flatbuffers::DetachedBuffer BuildTimeCodeQuarterFrameMessage(std::uint8_t value);
flatbuffers::DetachedBuffer BuildSongPositionPointerMessage(std::uint16_t position);
flatbuffers::DetachedBuffer BuildSongSelectMessage(std::uint8_t song_number);
flatbuffers::DetachedBuffer BuildTuneRequestMessage();

flatbuffers::DetachedBuffer BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType type);

flatbuffers::DetachedBuffer BuildSystemExclusiveMessage(
    const std::uint8_t *manufacturer_id,
    std::size_t manufacturer_id_length,
    const std::uint8_t *payload,
    std::size_t payload_length);

} // namespace libcomm
