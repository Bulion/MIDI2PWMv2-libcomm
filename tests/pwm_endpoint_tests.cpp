#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libcomm/pwm_endpoint.h"
#include "libcomm/frame_transport.h"

#include <array>
#include <cstdint>
#include <vector>

using Catch::Approx;

namespace {

struct NullPwmWriter {
    bool Write(const std::uint8_t*, std::size_t)
    {
        return true;
    }
};

struct PwmLoopback {
    libcomm::PwmEndpoint* receiver{nullptr};
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

struct TelemetryProbe {
    midi2pwm::pwm::ChannelConfiguration configuration{midi2pwm::pwm::ChannelConfiguration::FullBridge};
    midi2pwm::pwm::ChannelStatus status{midi2pwm::pwm::ChannelStatus::Inactive};
    std::uint16_t channel{0};
    std::uint16_t note{0};
    float voltage{0.0F};
    float current{0.0F};
    bool had_fault{false};
    midi2pwm::pwm::OutputModeType output_mode{midi2pwm::pwm::OutputModeType::Instant};
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::ChannelTelemetry& message)
    {
        ++call_count;
        configuration = message.configuration();
        status = message.status();
        channel = message.channel_number();
        note = message.note();
        voltage = message.voltage();
        current = message.current();
        had_fault = message.had_fault();
        output_mode = message.output_mode();
    }
};

struct ConfigProbe {
    midi2pwm::pwm::ChannelConfiguration configuration{midi2pwm::pwm::ChannelConfiguration::FullBridge};
    std::uint16_t channel{0};
    std::uint16_t note{0};
    midi2pwm::pwm::OutputModeType output_mode{midi2pwm::pwm::OutputModeType::Instant};
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::ChannelConfig& message)
    {
        ++call_count;
        configuration = message.configuration();
        channel = message.channel_number();
        note = message.note();
        output_mode = message.output_mode();
    }
};

struct FaultLogProbe {
    std::uint32_t log_size{0};
    std::vector<std::uint32_t> timestamps;
    std::vector<midi2pwm::pwm::FaultType> faults;
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::FaultLog& message)
    {
        ++call_count;
        log_size = message.log_size();
        timestamps.clear();
        faults.clear();
        if (const auto* entries = message.entries()) {
            for (const auto* entry : *entries) {
                timestamps.push_back(entry->timestamp_ms());
                faults.push_back(entry->fault());
            }
        }
    }
};

struct FaultControlProbe {
    midi2pwm::pwm::FaultControlOperation operation{midi2pwm::pwm::FaultControlOperation::Reset};
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::FaultControlCommand& message)
    {
        ++call_count;
        operation = message.operation();
    }
};

struct HeartBeatProbe {
    bool request_telemetry{false};
    uint16_t epoch{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::HeartBeat& message)
    {
        ++call_count;
        request_telemetry = message.request_telemetry();
        epoch = message.epoch();
    }
};

struct ResponseProbe {
    midi2pwm::pwm::ResponseStatus status{midi2pwm::pwm::ResponseStatus::ACK};
    std::uint32_t error_code{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::pwm::Response& message)
    {
        ++call_count;
        status = message.status();
        error_code = message.error_code();
    }
};

struct PwmEndpointHarness {
    NullPwmWriter null_writer{};
    PwmLoopback loopback{};
    libcomm::PwmEndpoint receiver;
    libcomm::PwmEndpoint sender;

    PwmEndpointHarness()
        : receiver(libcomm::PwmEndpoint::WriteCallback::create<NullPwmWriter, &NullPwmWriter::Write>(null_writer))
        , sender(libcomm::PwmEndpoint::WriteCallback::create<PwmLoopback, &PwmLoopback::Write>(loopback))
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

struct FailingPwmWriter {
    std::size_t call_count{0};

    bool Write(const std::uint8_t*, std::size_t)
    {
        ++call_count;
        return false;
    }
};

void VerifyEnvelopeType(const flatbuffers::DetachedBuffer& buffer, midi2pwm::pwm::Message expected)
{
    REQUIRE(midi2pwm::pwm::EnvelopeBufferHasIdentifier(buffer.data()));
    const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
    REQUIRE(envelope != nullptr);
    REQUIRE(envelope->message_type() == expected);
}

} // namespace

TEST_CASE("PwmEndpoint routes telemetry envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    TelemetryProbe probe;
    harness.receiver.OnChannelTelemetry(libcomm::PwmEndpoint::ChannelTelemetryHandler::create<TelemetryProbe,
                                                                                               &TelemetryProbe::Handle>(
        probe));

    auto buffer = libcomm::BuildChannelTelemetryMessage(
        2U,
        midi2pwm::pwm::ChannelConfiguration::HalfBridge,
        midi2pwm::pwm::ChannelStatus::Active,
        57U,
        3.3F,
        1.2F,
        true,
        midi2pwm::pwm::OutputModeType::Instant,
        midi2pwm::pwm::ModeParametersUnionUnion());

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.channel == 2U);
    CHECK(probe.configuration == midi2pwm::pwm::ChannelConfiguration::HalfBridge);
    CHECK(probe.status == midi2pwm::pwm::ChannelStatus::Active);
    CHECK(probe.note == 57U);
    CHECK(probe.voltage == Approx(3.3F));
    CHECK(probe.current == Approx(1.2F));
    CHECK(probe.had_fault);
    CHECK(probe.output_mode == midi2pwm::pwm::OutputModeType::Instant);
}

TEST_CASE("PwmEndpoint routes config envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    ConfigProbe probe;
    harness.receiver.OnChannelConfig(
        libcomm::PwmEndpoint::ChannelConfigHandler::create<ConfigProbe, &ConfigProbe::Handle>(probe));

    auto buffer = libcomm::BuildChannelConfigMessage(
        4U,
        midi2pwm::pwm::ChannelConfiguration::FullBridge,
        72U,
        0.75F,
        0.1F,
        0.9F);

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.channel == 4U);
    CHECK(probe.configuration == midi2pwm::pwm::ChannelConfiguration::FullBridge);
    CHECK(probe.note == 72U);
}

TEST_CASE("PwmEndpoint routes fault log envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    FaultLogProbe probe;
    harness.receiver.OnFaultLog(
        libcomm::PwmEndpoint::FaultLogHandler::create<FaultLogProbe, &FaultLogProbe::Handle>(probe));

    const std::array<libcomm::FaultLogEntryData, 2> entries{{
        {100U, midi2pwm::pwm::FaultType::OverVoltage},
        {200U, midi2pwm::pwm::FaultType::OverCurrent},
    }};

    auto buffer = libcomm::BuildFaultLogMessage(10U, entries.data(), entries.size());
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.log_size == 10U);
    REQUIRE(probe.timestamps.size() == 2U);
    CHECK(probe.timestamps[0] == 100U);
    CHECK(probe.timestamps[1] == 200U);
    REQUIRE(probe.faults.size() == 2U);
    CHECK(probe.faults[0] == midi2pwm::pwm::FaultType::OverVoltage);
    CHECK(probe.faults[1] == midi2pwm::pwm::FaultType::OverCurrent);
}

TEST_CASE("PwmEndpoint routes fault control envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    FaultControlProbe probe;
    harness.receiver.OnFaultControl(
        libcomm::PwmEndpoint::FaultControlHandler::create<FaultControlProbe, &FaultControlProbe::Handle>(probe));

    auto buffer = libcomm::BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation::DisableAutoReset);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.operation == midi2pwm::pwm::FaultControlOperation::DisableAutoReset);
}

TEST_CASE("PwmEndpoint HandleIncoming tolerates missing handlers", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    auto buffer = libcomm::BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation::Fetch);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    CHECK(harness.loopback.call_count == 1);
}

TEST_CASE("PwmEndpoint HandleIncoming rejects invalid identifier", "[pwm_endpoint]")
{
    NullPwmWriter null_writer;
    auto write_callback =
        libcomm::PwmEndpoint::WriteCallback::create<NullPwmWriter, &NullPwmWriter::Write>(null_writer);
    libcomm::PwmEndpoint endpoint(write_callback);

    auto payload = libcomm::BuildChannelConfigMessage(
        1U, midi2pwm::pwm::ChannelConfiguration::HalfBridge, 60U, 0.4F, 0.1F, 0.9F);

    FrameCaptureWriter writer;
    auto frame_writer =
        libcomm::FrameTransport::WriteCallback::create<FrameCaptureWriter, &FrameCaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(frame_writer);
    REQUIRE(transport.Send(payload.data(), payload.size()));

    std::vector<std::uint8_t> corrupted = writer.buffer;
    std::size_t payload_start = libcomm::FrameTransport::kHeaderSize;
    corrupted[payload_start + 0] ^= 0xFFU;

    REQUIRE_FALSE(endpoint.HandleIncoming(corrupted.data(), corrupted.size()));
}

TEST_CASE("PwmEndpoint HandleIncoming rejects envelopes with invalid message type", "[pwm_endpoint]")
{
    NullPwmWriter null_writer;
    auto write_callback =
        libcomm::PwmEndpoint::WriteCallback::create<NullPwmWriter, &NullPwmWriter::Write>(null_writer);
    libcomm::PwmEndpoint endpoint(write_callback);

    flatbuffers::FlatBufferBuilder builder;
    auto envelope = midi2pwm::pwm::CreateEnvelope(builder, midi2pwm::pwm::Message::NONE);
    builder.Finish(envelope, midi2pwm::pwm::EnvelopeIdentifier());
    auto payload = builder.Release();

    FrameCaptureWriter writer;
    auto frame_writer =
        libcomm::FrameTransport::WriteCallback::create<FrameCaptureWriter, &FrameCaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(frame_writer);
    REQUIRE(transport.Send(payload.data(), payload.size()));

    REQUIRE_FALSE(endpoint.HandleIncoming(writer.buffer.data(), writer.buffer.size()));
}

TEST_CASE("PwmEndpoint Send propagates transport failures", "[pwm_endpoint]")
{
    FailingPwmWriter writer;
    auto write_callback =
        libcomm::PwmEndpoint::WriteCallback::create<FailingPwmWriter, &FailingPwmWriter::Write>(writer);
    libcomm::PwmEndpoint endpoint(write_callback);

    auto buffer = libcomm::BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation::Reset);
    REQUIRE_FALSE(endpoint.Send(std::move(buffer)));
    CHECK(writer.call_count == 1);
}

TEST_CASE("Pwm message builders populate envelopes", "[pwm_endpoint]")
{
    using midi2pwm::pwm::Message;

    SECTION("Channel telemetry builder")
    {
        auto buffer = libcomm::BuildChannelTelemetryMessage(
            5U,
            midi2pwm::pwm::ChannelConfiguration::FullBridge,
            midi2pwm::pwm::ChannelStatus::Fault,
            88U,
            4.1F,
            2.0F,
            false,
            midi2pwm::pwm::OutputModeType::Instant,
            midi2pwm::pwm::ModeParametersUnionUnion());
        VerifyEnvelopeType(buffer, Message::ChannelTelemetry);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_ChannelTelemetry();
        REQUIRE(message != nullptr);
        CHECK(message->channel_number() == 5U);
        CHECK(message->configuration() == midi2pwm::pwm::ChannelConfiguration::FullBridge);
        CHECK(message->status() == midi2pwm::pwm::ChannelStatus::Fault);
        CHECK(message->note() == 88U);
        CHECK(message->voltage() == Approx(4.1F));
        CHECK(message->current() == Approx(2.0F));
        CHECK_FALSE(message->had_fault());
    }

    SECTION("Channel config builder")
    {
        auto buffer =
            libcomm::BuildChannelConfigMessage(9U, midi2pwm::pwm::ChannelConfiguration::HalfBridge, 30U, 0.4F, -0.2F, 0.8F);
        VerifyEnvelopeType(buffer, Message::ChannelConfig);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_ChannelConfig();
        REQUIRE(message != nullptr);
        CHECK(message->channel_number() == 9U);
        CHECK(message->configuration() == midi2pwm::pwm::ChannelConfiguration::HalfBridge);
        CHECK(message->note() == 30U);
    }

    SECTION("Fault log builder truncates to 64 entries")
    {
        std::vector<libcomm::FaultLogEntryData> entries(80U);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            entries[i].timestamp_ms = static_cast<std::uint32_t>(i);
            entries[i].fault = midi2pwm::pwm::FaultType::OverVoltage;
        }

        auto buffer = libcomm::BuildFaultLogMessage(100U, entries.data(), entries.size());
        VerifyEnvelopeType(buffer, Message::FaultLog);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_FaultLog();
        REQUIRE(message != nullptr);
        REQUIRE(message->entries() != nullptr);
        CHECK(message->entries()->size() == 64U);
        CHECK(message->log_size() == 100U);
    }

    SECTION("Fault log builder allows null entries pointer")
    {
        auto buffer = libcomm::BuildFaultLogMessage(5U, nullptr, 10U);
        VerifyEnvelopeType(buffer, Message::FaultLog);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_FaultLog();
        REQUIRE(message != nullptr);
        CHECK(message->log_size() == 5U);
        CHECK(message->entries() != nullptr);
        CHECK(message->entries()->size() == 0U);
    }

    SECTION("Fault control builder")
    {
        auto buffer = libcomm::BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation::EnableAutoReset);
        VerifyEnvelopeType(buffer, Message::FaultControlCommand);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_FaultControlCommand();
        REQUIRE(message != nullptr);
        CHECK(message->operation() == midi2pwm::pwm::FaultControlOperation::EnableAutoReset);
    }

    SECTION("HeartBeat builder with request_telemetry = false")
    {
        auto buffer = libcomm::BuildHeartBeatMessage(false, 42);
        VerifyEnvelopeType(buffer, Message::HeartBeat);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_HeartBeat();
        REQUIRE(message != nullptr);
        CHECK(message->request_telemetry() == false);
        CHECK(message->epoch() == 42);
    }

    SECTION("HeartBeat builder with request_telemetry = true")
    {
        auto buffer = libcomm::BuildHeartBeatMessage(true, 1234);
        VerifyEnvelopeType(buffer, Message::HeartBeat);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_HeartBeat();
        REQUIRE(message != nullptr);
        CHECK(message->request_telemetry() == true);
        CHECK(message->epoch() == 1234);
    }

    SECTION("Response builder with ACK status")
    {
        auto buffer = libcomm::BuildResponseMessage(midi2pwm::pwm::ResponseStatus::ACK, 0);
        VerifyEnvelopeType(buffer, Message::Response);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_Response();
        REQUIRE(message != nullptr);
        CHECK(message->status() == midi2pwm::pwm::ResponseStatus::ACK);
        CHECK(message->error_code() == 0);
    }

    SECTION("Response builder with NACK status and error code")
    {
        auto buffer = libcomm::BuildResponseMessage(midi2pwm::pwm::ResponseStatus::NACK, 42);
        VerifyEnvelopeType(buffer, Message::Response);
        const auto* envelope = midi2pwm::pwm::GetEnvelope(buffer.data());
        const auto* message = envelope->message_as_Response();
        REQUIRE(message != nullptr);
        CHECK(message->status() == midi2pwm::pwm::ResponseStatus::NACK);
        CHECK(message->error_code() == 42);
    }

    SECTION("Channel telemetry supports all output modes (Instant, Ramped, Pulse, Toggle, ADSR, CCControl, PitchBend)")
    {
        auto buffer_instant = libcomm::BuildChannelTelemetryMessage(
            1U,
            midi2pwm::pwm::ChannelConfiguration::FullBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            60U,
            5.0F,
            0.5F,
            false,
            midi2pwm::pwm::OutputModeType::Instant,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_instant, Message::ChannelTelemetry);
        const auto* envelope_instant = midi2pwm::pwm::GetEnvelope(buffer_instant.data());
        const auto* message_instant = envelope_instant->message_as_ChannelTelemetry();
        REQUIRE(message_instant != nullptr);
        CHECK(message_instant->output_mode() == midi2pwm::pwm::OutputModeType::Instant);

        auto buffer_ramped = libcomm::BuildChannelTelemetryMessage(
            2U,
            midi2pwm::pwm::ChannelConfiguration::HalfBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            72U,
            3.3F,
            1.0F,
            false,
            midi2pwm::pwm::OutputModeType::Ramped,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_ramped, Message::ChannelTelemetry);
        const auto* envelope_ramped = midi2pwm::pwm::GetEnvelope(buffer_ramped.data());
        const auto* message_ramped = envelope_ramped->message_as_ChannelTelemetry();
        REQUIRE(message_ramped != nullptr);
        CHECK(message_ramped->output_mode() == midi2pwm::pwm::OutputModeType::Ramped);

        auto buffer_pulse = libcomm::BuildChannelTelemetryMessage(
            3U,
            midi2pwm::pwm::ChannelConfiguration::FullBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            48U,
            12.0F,
            2.5F,
            false,
            midi2pwm::pwm::OutputModeType::Pulse,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_pulse, Message::ChannelTelemetry);
        const auto* envelope_pulse = midi2pwm::pwm::GetEnvelope(buffer_pulse.data());
        const auto* message_pulse = envelope_pulse->message_as_ChannelTelemetry();
        REQUIRE(message_pulse != nullptr);
        CHECK(message_pulse->output_mode() == midi2pwm::pwm::OutputModeType::Pulse);

        auto buffer_toggle = libcomm::BuildChannelTelemetryMessage(
            4U,
            midi2pwm::pwm::ChannelConfiguration::HalfBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            36U,
            3.3F,
            0.2F,
            false,
            midi2pwm::pwm::OutputModeType::Toggle,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_toggle, Message::ChannelTelemetry);
        const auto* envelope_toggle = midi2pwm::pwm::GetEnvelope(buffer_toggle.data());
        const auto* message_toggle = envelope_toggle->message_as_ChannelTelemetry();
        REQUIRE(message_toggle != nullptr);
        CHECK(message_toggle->output_mode() == midi2pwm::pwm::OutputModeType::Toggle);

        auto buffer_adsr = libcomm::BuildChannelTelemetryMessage(
            5U,
            midi2pwm::pwm::ChannelConfiguration::FullBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            64U,
            12.0F,
            1.5F,
            false,
            midi2pwm::pwm::OutputModeType::ADSR,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_adsr, Message::ChannelTelemetry);
        const auto* envelope_adsr = midi2pwm::pwm::GetEnvelope(buffer_adsr.data());
        const auto* message_adsr = envelope_adsr->message_as_ChannelTelemetry();
        REQUIRE(message_adsr != nullptr);
        CHECK(message_adsr->output_mode() == midi2pwm::pwm::OutputModeType::ADSR);

        auto buffer_cccontrol = libcomm::BuildChannelTelemetryMessage(
            6U,
            midi2pwm::pwm::ChannelConfiguration::HalfBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            0U,
            5.0F,
            0.8F,
            false,
            midi2pwm::pwm::OutputModeType::CCControl,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_cccontrol, Message::ChannelTelemetry);
        const auto* envelope_cccontrol = midi2pwm::pwm::GetEnvelope(buffer_cccontrol.data());
        const auto* message_cccontrol = envelope_cccontrol->message_as_ChannelTelemetry();
        REQUIRE(message_cccontrol != nullptr);
        CHECK(message_cccontrol->output_mode() == midi2pwm::pwm::OutputModeType::CCControl);

        auto buffer_pitchbend = libcomm::BuildChannelTelemetryMessage(
            7U,
            midi2pwm::pwm::ChannelConfiguration::FullBridge,
            midi2pwm::pwm::ChannelStatus::Active,
            48U,
            9.0F,
            1.2F,
            false,
            midi2pwm::pwm::OutputModeType::PitchBend,
            midi2pwm::pwm::ModeParametersUnionUnion());

        VerifyEnvelopeType(buffer_pitchbend, Message::ChannelTelemetry);
        const auto* envelope_pitchbend = midi2pwm::pwm::GetEnvelope(buffer_pitchbend.data());
        const auto* message_pitchbend = envelope_pitchbend->message_as_ChannelTelemetry();
        REQUIRE(message_pitchbend != nullptr);
        CHECK(message_pitchbend->output_mode() == midi2pwm::pwm::OutputModeType::PitchBend);
    }
}

TEST_CASE("PwmEndpoint routes HeartBeat envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    HeartBeatProbe probe;
    harness.receiver.OnHeartBeat(libcomm::PwmEndpoint::HeartBeatHandler::create<HeartBeatProbe, &HeartBeatProbe::Handle>(probe));

    auto buffer = libcomm::BuildHeartBeatMessage(true, 999);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.request_telemetry == true);
    CHECK(probe.epoch == 999);
}

TEST_CASE("PwmEndpoint routes Response envelopes", "[pwm_endpoint]")
{
    PwmEndpointHarness harness;

    ResponseProbe probe;
    harness.receiver.OnResponse(libcomm::PwmEndpoint::ResponseHandler::create<ResponseProbe, &ResponseProbe::Handle>(probe));

    auto buffer = libcomm::BuildResponseMessage(midi2pwm::pwm::ResponseStatus::NACK, 123);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.status == midi2pwm::pwm::ResponseStatus::NACK);
    CHECK(probe.error_code == 123);
}
