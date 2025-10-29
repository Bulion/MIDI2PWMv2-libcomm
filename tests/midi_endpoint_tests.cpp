#include <catch2/catch_test_macros.hpp>

#include "libcomm/midi_endpoint.h"
#include "libcomm/frame_transport.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

struct NullMidiWriter {
    bool Write(const std::uint8_t*, std::size_t)
    {
        return true;
    }
};

struct MidiLoopback {
    libcomm::MidiEndpoint* receiver{nullptr};
    std::vector<std::uint8_t> last_frame;
    std::size_t call_count{0};

    bool Write(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        last_frame.assign(data, data + size);
        REQUIRE(receiver != nullptr);
        return receiver->HandleIncoming(data, size);
    }
};

struct PingProbe {
    std::uint32_t sequence{0};
    std::uint64_t timestamp{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::midi::Ping& message)
    {
        ++call_count;
        sequence = message.sequence();
        timestamp = message.timestamp();
    }
};

struct ChannelProbe {
    midi2pwm::midi::ChannelMessageType type{midi2pwm::midi::ChannelMessageType::NoteOff};
    std::uint8_t channel{0};
    std::uint16_t data1{0};
    std::uint16_t data2{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::midi::ChannelMessage& message)
    {
        ++call_count;
        type = message.message_type();
        channel = message.channel();
        data1 = message.data1();
        data2 = message.data2();
    }
};

struct SystemCommonProbe {
    midi2pwm::midi::SystemCommonType type{midi2pwm::midi::SystemCommonType::TimeCodeQuarterFrame};
    std::uint16_t value{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::midi::SystemCommonMessage& message)
    {
        ++call_count;
        type = message.message_type();
        value = message.value();
    }
};

struct SystemRealTimeProbe {
    midi2pwm::midi::SystemRealTimeType type{midi2pwm::midi::SystemRealTimeType::TimingClock};
    std::size_t call_count{0};

    void Handle(const midi2pwm::midi::SystemRealTimeMessage& message)
    {
        ++call_count;
        type = message.message_type();
    }
};

struct SystemExclusiveProbe {
    std::vector<std::uint8_t> manufacturer;
    std::vector<std::uint8_t> payload;
    std::size_t call_count{0};

    void Handle(const midi2pwm::midi::SystemExclusiveMessage& message)
    {
        ++call_count;
        if (const auto* mfg = message.manufacturer_id()) {
            manufacturer.assign(mfg->begin(), mfg->end());
        } else {
            manufacturer.clear();
        }

        if (const auto* data = message.payload()) {
            payload.assign(data->begin(), data->end());
        } else {
            payload.clear();
        }
    }
};

struct MidiEndpointHarness {
    NullMidiWriter null_writer{};
    MidiLoopback loopback{};
    libcomm::MidiEndpoint receiver;
    libcomm::MidiEndpoint sender;

    MidiEndpointHarness()
        : receiver(libcomm::MidiEndpoint::WriteCallback::create<NullMidiWriter, &NullMidiWriter::Write>(null_writer), true)
        , sender(libcomm::MidiEndpoint::WriteCallback::create<MidiLoopback, &MidiLoopback::Write>(loopback), true)
    {
        loopback.receiver = &receiver;
    }
};

struct FrameCaptureWriter {
    std::vector<std::uint8_t> buffer;
    std::size_t call_count{0};

    bool Write(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        buffer.assign(data, data + size);
        return true;
    }
};

} // namespace

TEST_CASE("MidiEndpoint routes Ping envelopes to handler", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    PingProbe probe;
    harness.receiver.OnPing(libcomm::MidiEndpoint::PingHandler::create<PingProbe, &PingProbe::Handle>(probe));

    auto buffer = libcomm::BuildPingMessage(42U, 99U);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.sequence == 42U);
    CHECK(probe.timestamp == 99U);
    CHECK(harness.loopback.call_count == 1);
}

TEST_CASE("MidiEndpoint routes channel messages to handler", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    ChannelProbe probe;
    harness.receiver.OnChannelMessage(
        libcomm::MidiEndpoint::ChannelMessageHandler::create<ChannelProbe, &ChannelProbe::Handle>(probe));

    auto buffer = libcomm::BuildNoteOnMessage(7U, 64U, 120U);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.type == midi2pwm::midi::ChannelMessageType::NoteOn);
    CHECK(probe.channel == 7U);
    CHECK(probe.data1 == 64U);
    CHECK(probe.data2 == 120U);
}

TEST_CASE("MidiEndpoint routes system common envelopes", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    SystemCommonProbe probe;
    harness.receiver.OnSystemCommon(
        libcomm::MidiEndpoint::SystemCommonHandler::create<SystemCommonProbe, &SystemCommonProbe::Handle>(probe));

    auto buffer = libcomm::BuildSongPositionPointerMessage(0x3456U);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.type == midi2pwm::midi::SystemCommonType::SongPositionPointer);
    CHECK(probe.value == 0x3456U);
}

TEST_CASE("MidiEndpoint routes system real-time envelopes", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    SystemRealTimeProbe probe;
    harness.receiver.OnSystemRealTime(libcomm::MidiEndpoint::SystemRealTimeHandler::create<SystemRealTimeProbe,
                                                                                             &SystemRealTimeProbe::Handle>(
        probe));

    auto buffer = libcomm::BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType::SystemReset);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.type == midi2pwm::midi::SystemRealTimeType::SystemReset);
}

TEST_CASE("MidiEndpoint routes system exclusive envelopes", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    SystemExclusiveProbe probe;
    harness.receiver.OnSystemExclusive(
        libcomm::MidiEndpoint::SystemExclusiveHandler::create<SystemExclusiveProbe, &SystemExclusiveProbe::Handle>(
            probe));

    const std::array<std::uint8_t, 3> manufacturer{{0x01U, 0x60U, 0x7DU}};
    const std::array<std::uint8_t, 4> payload{{0x11U, 0x22U, 0x33U, 0x44U}};

    auto buffer = libcomm::BuildSystemExclusiveMessage(
        manufacturer.data(), manufacturer.size(), payload.data(), payload.size());
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.manufacturer == std::vector<std::uint8_t>(manufacturer.begin(), manufacturer.end()));
    CHECK(probe.payload == std::vector<std::uint8_t>(payload.begin(), payload.end()));
}

TEST_CASE("MidiEndpoint HandleIncoming tolerates missing handlers", "[midi_endpoint]")
{
    MidiEndpointHarness harness;

    auto buffer = libcomm::BuildProgramChangeMessage(3U, 12U);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    CHECK(harness.loopback.call_count == 1);
}

TEST_CASE("MidiEndpoint HandleIncoming rejects envelopes with invalid identifier", "[midi_endpoint]")
{
    NullMidiWriter null_writer;
    auto write_callback =
        libcomm::MidiEndpoint::WriteCallback::create<NullMidiWriter, &NullMidiWriter::Write>(null_writer);
    libcomm::MidiEndpoint endpoint(write_callback, true);

    auto payload = libcomm::BuildNoteOffMessage(1U, 45U, 100U);
    FrameCaptureWriter writer;
    auto frame_writer =
        libcomm::FrameTransport::WriteCallback::create<FrameCaptureWriter, &FrameCaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(frame_writer, true);
    REQUIRE(transport.Send(payload.data(), payload.size()));

    std::vector<std::uint8_t> corrupted = writer.buffer;
    std::size_t payload_start = libcomm::FrameTransport::kPrefixSize + libcomm::FrameTransport::kHeaderSize;
    corrupted[payload_start + 0] ^= 0xFFU;

    REQUIRE_FALSE(endpoint.HandleIncoming(corrupted.data(), corrupted.size()));
}

TEST_CASE("MidiEndpoint HandleIncoming rejects envelopes with invalid packet type", "[midi_endpoint]")
{
    NullMidiWriter null_writer;
    auto write_callback =
        libcomm::MidiEndpoint::WriteCallback::create<NullMidiWriter, &NullMidiWriter::Write>(null_writer);
    libcomm::MidiEndpoint endpoint(write_callback, true);

    flatbuffers::FlatBufferBuilder builder;
    auto envelope = midi2pwm::midi::CreateEnvelope(builder, midi2pwm::midi::Packet::NONE);
    builder.Finish(envelope, midi2pwm::midi::EnvelopeIdentifier());
    auto payload = builder.Release();

    FrameCaptureWriter writer;
    auto frame_writer =
        libcomm::FrameTransport::WriteCallback::create<FrameCaptureWriter, &FrameCaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(frame_writer, true);
    REQUIRE(transport.Send(payload.data(), payload.size()));

    REQUIRE_FALSE(endpoint.HandleIncoming(writer.buffer.data(), writer.buffer.size()));
}

struct FailingMidiWriter {
    std::size_t call_count{0};

    bool Write(const std::uint8_t*, std::size_t)
    {
        ++call_count;
        return false;
    }
};

TEST_CASE("MidiEndpoint Send propagates transport failures", "[midi_endpoint]")
{
    FailingMidiWriter failing_writer;
    auto write_callback =
        libcomm::MidiEndpoint::WriteCallback::create<FailingMidiWriter, &FailingMidiWriter::Write>(failing_writer);
    libcomm::MidiEndpoint endpoint(write_callback, true);

    auto buffer = libcomm::BuildChannelPressureMessage(9U, 77U);
    REQUIRE_FALSE(endpoint.Send(std::move(buffer)));
    CHECK(failing_writer.call_count == 3);
}

namespace {

void VerifyEnvelopeHasType(const flatbuffers::DetachedBuffer& buffer, midi2pwm::midi::Packet expected)
{
    REQUIRE(midi2pwm::midi::EnvelopeBufferHasIdentifier(buffer.data()));

    const auto* envelope = midi2pwm::midi::GetEnvelope(buffer.data());
    REQUIRE(envelope != nullptr);
    REQUIRE(envelope->packet_type() == expected);
}

} // namespace

TEST_CASE("Midi message builders populate envelopes correctly", "[midi_endpoint]")
{
    using midi2pwm::midi::Packet;

    SECTION("Ping builder")
    {
        auto buffer = libcomm::BuildPingMessage(5U, 123456U);
        VerifyEnvelopeHasType(buffer, Packet::Ping);
        const auto* envelope = midi2pwm::midi::GetEnvelope(buffer.data());
        const auto* ping = envelope->packet_as_Ping();
        REQUIRE(ping != nullptr);
        CHECK(ping->sequence() == 5U);
        CHECK(ping->timestamp() == 123456U);
    }

    SECTION("Channel message builders")
    {
        const std::uint8_t channel = 4U;

        auto verify_channel = [&](flatbuffers::DetachedBuffer&& buf,
                                  midi2pwm::midi::ChannelMessageType type,
                                  std::uint16_t data1,
                                  std::uint16_t data2) {
            VerifyEnvelopeHasType(buf, Packet::ChannelMessage);
            const auto* envelope = midi2pwm::midi::GetEnvelope(buf.data());
            const auto* message = envelope->packet_as_ChannelMessage();
            REQUIRE(message != nullptr);
            CHECK(message->channel() == channel);
            CHECK(message->message_type() == type);
            CHECK(message->data1() == data1);
            CHECK(message->data2() == data2);
        };

        verify_channel(libcomm::BuildNoteOffMessage(channel, 21U, 1U),
                       midi2pwm::midi::ChannelMessageType::NoteOff,
                       21U,
                       1U);
        verify_channel(libcomm::BuildNoteOnMessage(channel, 60U, 100U),
                       midi2pwm::midi::ChannelMessageType::NoteOn,
                       60U,
                       100U);
        verify_channel(libcomm::BuildPolyphonicKeyPressureMessage(channel, 12U, 55U),
                       midi2pwm::midi::ChannelMessageType::PolyphonicKeyPressure,
                       12U,
                       55U);
        verify_channel(libcomm::BuildControlChangeMessage(channel, 74U, 32U),
                       midi2pwm::midi::ChannelMessageType::ControlChange,
                       74U,
                       32U);
        verify_channel(libcomm::BuildProgramChangeMessage(channel, 9U),
                       midi2pwm::midi::ChannelMessageType::ProgramChange,
                       9U,
                       0U);
        verify_channel(libcomm::BuildChannelPressureMessage(channel, 80U),
                       midi2pwm::midi::ChannelMessageType::ChannelPressure,
                       80U,
                       0U);
        verify_channel(libcomm::BuildPitchBendMessage(channel, 0x1FF0U),
                       midi2pwm::midi::ChannelMessageType::PitchBend,
                       0x1FF0U,
                       0U);
    }

    SECTION("System common builders")
    {
        auto tc = libcomm::BuildTimeCodeQuarterFrameMessage(0x5U);
        VerifyEnvelopeHasType(tc, Packet::SystemCommonMessage);
        auto envelope = midi2pwm::midi::GetEnvelope(tc.data());
        auto message = envelope->packet_as_SystemCommonMessage();
        REQUIRE(message != nullptr);
        CHECK(message->message_type() == midi2pwm::midi::SystemCommonType::TimeCodeQuarterFrame);
        CHECK(message->value() == 0x5U);

        auto song_select = libcomm::BuildSongSelectMessage(0x22U);
        VerifyEnvelopeHasType(song_select, Packet::SystemCommonMessage);
        envelope = midi2pwm::midi::GetEnvelope(song_select.data());
        message = envelope->packet_as_SystemCommonMessage();
        REQUIRE(message != nullptr);
        CHECK(message->message_type() == midi2pwm::midi::SystemCommonType::SongSelect);
        CHECK(message->value() == 0x22U);

        auto tune_request = libcomm::BuildTuneRequestMessage();
        VerifyEnvelopeHasType(tune_request, Packet::SystemCommonMessage);
        envelope = midi2pwm::midi::GetEnvelope(tune_request.data());
        message = envelope->packet_as_SystemCommonMessage();
        REQUIRE(message != nullptr);
        CHECK(message->message_type() == midi2pwm::midi::SystemCommonType::TuneRequest);
        CHECK(message->value() == 0U);
    }

    SECTION("System real-time builder")
    {
        auto buffer = libcomm::BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType::Start);
        VerifyEnvelopeHasType(buffer, Packet::SystemRealTimeMessage);
        const auto* envelope = midi2pwm::midi::GetEnvelope(buffer.data());
        const auto* message = envelope->packet_as_SystemRealTimeMessage();
        REQUIRE(message != nullptr);
        CHECK(message->message_type() == midi2pwm::midi::SystemRealTimeType::Start);
    }

    SECTION("System exclusive builder with manufacturer and payload")
    {
        const std::array<std::uint8_t, 2> manufacturer{{0x00U, 0x20U}};
        const std::array<std::uint8_t, 3> payload{{0xAAU, 0xBBU, 0xCCU}};

        auto buffer =
            libcomm::BuildSystemExclusiveMessage(manufacturer.data(), manufacturer.size(), payload.data(), payload.size());
        VerifyEnvelopeHasType(buffer, Packet::SystemExclusiveMessage);
        const auto* envelope = midi2pwm::midi::GetEnvelope(buffer.data());
        const auto* message = envelope->packet_as_SystemExclusiveMessage();
        REQUIRE(message != nullptr);
        REQUIRE(message->manufacturer_id() != nullptr);
        REQUIRE(message->payload() != nullptr);
        CHECK(std::vector<std::uint8_t>(message->manufacturer_id()->begin(), message->manufacturer_id()->end()) ==
              std::vector<std::uint8_t>(manufacturer.begin(), manufacturer.end()));
        CHECK(std::vector<std::uint8_t>(message->payload()->begin(), message->payload()->end()) ==
              std::vector<std::uint8_t>(payload.begin(), payload.end()));
    }

    SECTION("System exclusive builder handles empty manufacturer and payload pointers")
    {
        auto buffer = libcomm::BuildSystemExclusiveMessage(nullptr, 0U, nullptr, 0U);
        VerifyEnvelopeHasType(buffer, Packet::SystemExclusiveMessage);
        const auto* envelope = midi2pwm::midi::GetEnvelope(buffer.data());
        const auto* message = envelope->packet_as_SystemExclusiveMessage();
        REQUIRE(message != nullptr);
        CHECK(message->manufacturer_id() == nullptr);
        CHECK(message->payload() == nullptr);
    }
}
