#include <catch2/catch_test_macros.hpp>

#include "libcomm/ota_endpoint.h"
#include "libcomm/frame_transport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct NullOtaWriter {
    bool Write(const std::uint8_t*, std::size_t)
    {
        return true;
    }
};

struct OtaLoopback {
    libcomm::OtaEndpoint* receiver{nullptr};
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

struct BeginProbe {
    midi2pwm::ota::Target target{midi2pwm::ota::Target::Stm32};
    std::uint32_t firmware_size{0};
    std::uint32_t firmware_crc32{0};
    std::string version_string;
    std::uint16_t total_chunks{0};
    std::size_t call_count{0};

    void Handle(const midi2pwm::ota::OtaBegin& msg)
    {
        ++call_count;
        target = msg.target();
        firmware_size = msg.firmware_size();
        firmware_crc32 = msg.firmware_crc32();
        if (msg.version_string()) {
            version_string = msg.version_string()->str();
        }
        total_chunks = msg.total_chunks();
    }
};

struct DataProbe {
    midi2pwm::ota::Target target{midi2pwm::ota::Target::Stm32};
    std::uint16_t chunk_index{0};
    std::vector<std::uint8_t> data;
    std::size_t call_count{0};

    void Handle(const midi2pwm::ota::OtaData& msg)
    {
        ++call_count;
        target = msg.target();
        chunk_index = msg.chunk_index();
        data.clear();
        if (msg.data()) {
            data.assign(msg.data()->begin(), msg.data()->end());
        }
    }
};

struct EndProbe {
    midi2pwm::ota::Target target{midi2pwm::ota::Target::Stm32};
    std::size_t call_count{0};

    void Handle(const midi2pwm::ota::OtaEnd& msg)
    {
        ++call_count;
        target = msg.target();
    }
};

struct ProgressProbe {
    midi2pwm::ota::Target target{midi2pwm::ota::Target::Stm32};
    midi2pwm::ota::OtaStatus status{midi2pwm::ota::OtaStatus::Idle};
    std::uint16_t chunks_received{0};
    std::uint16_t total_chunks{0};
    std::string error_message;
    std::size_t call_count{0};

    void Handle(const midi2pwm::ota::OtaProgress& msg)
    {
        ++call_count;
        target = msg.target();
        status = msg.status();
        chunks_received = msg.chunks_received();
        total_chunks = msg.total_chunks();
        if (msg.error_message()) {
            error_message = msg.error_message()->str();
        } else {
            error_message.clear();
        }
    }
};

struct AbortProbe {
    midi2pwm::ota::Target target{midi2pwm::ota::Target::Stm32};
    std::string reason;
    std::size_t call_count{0};

    void Handle(const midi2pwm::ota::OtaAbort& msg)
    {
        ++call_count;
        target = msg.target();
        if (msg.reason()) {
            reason = msg.reason()->str();
        } else {
            reason.clear();
        }
    }
};

struct OtaEndpointHarness {
    NullOtaWriter null_writer{};
    OtaLoopback loopback{};
    libcomm::OtaEndpoint receiver;
    libcomm::OtaEndpoint sender;

    OtaEndpointHarness()
        : receiver(libcomm::OtaEndpoint::WriteCallback::create<NullOtaWriter, &NullOtaWriter::Write>(null_writer))
        , sender(libcomm::OtaEndpoint::WriteCallback::create<OtaLoopback, &OtaLoopback::Write>(loopback))
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

struct FailingOtaWriter {
    std::size_t call_count{0};

    bool Write(const std::uint8_t*, std::size_t)
    {
        ++call_count;
        return false;
    }
};

} // namespace

TEST_CASE("OtaEndpoint routes OtaBegin messages", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    BeginProbe probe;
    harness.receiver.OnBegin(
        libcomm::OtaEndpoint::BeginHandler::create<BeginProbe, &BeginProbe::Handle>(probe));

    auto buffer = libcomm::BuildOtaBeginMessage(
        midi2pwm::ota::Target::Stm32, 228352U, 0xDEADBEEFU, "1.2.3", 60U);

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.target == midi2pwm::ota::Target::Stm32);
    CHECK(probe.firmware_size == 228352U);
    CHECK(probe.firmware_crc32 == 0xDEADBEEFU);
    CHECK(probe.version_string == "1.2.3");
    CHECK(probe.total_chunks == 60U);
}

TEST_CASE("OtaEndpoint routes OtaData messages", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    DataProbe probe;
    harness.receiver.OnData(
        libcomm::OtaEndpoint::DataHandler::create<DataProbe, &DataProbe::Handle>(probe));

    const std::vector<std::uint8_t> chunkData = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto buffer = libcomm::BuildOtaDataMessage(
        midi2pwm::ota::Target::Esp32, 7U, chunkData.data(), chunkData.size());

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.target == midi2pwm::ota::Target::Esp32);
    CHECK(probe.chunk_index == 7U);
    REQUIRE(probe.data.size() == 5U);
    CHECK(probe.data[0] == 0x01);
    CHECK(probe.data[4] == 0x05);
}

TEST_CASE("OtaEndpoint routes OtaEnd messages", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    EndProbe probe;
    harness.receiver.OnEnd(
        libcomm::OtaEndpoint::EndHandler::create<EndProbe, &EndProbe::Handle>(probe));

    auto buffer = libcomm::BuildOtaEndMessage(midi2pwm::ota::Target::Esp32);

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.target == midi2pwm::ota::Target::Esp32);
}

TEST_CASE("OtaEndpoint routes OtaProgress messages", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    ProgressProbe probe;
    harness.receiver.OnProgress(
        libcomm::OtaEndpoint::ProgressHandler::create<ProgressProbe, &ProgressProbe::Handle>(probe));

    auto buffer = libcomm::BuildOtaProgressMessage(
        midi2pwm::ota::Target::Stm32, midi2pwm::ota::OtaStatus::Receiving, 15U, 60U);

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.target == midi2pwm::ota::Target::Stm32);
    CHECK(probe.status == midi2pwm::ota::OtaStatus::Receiving);
    CHECK(probe.chunks_received == 15U);
    CHECK(probe.total_chunks == 60U);
    CHECK(probe.error_message.empty());
}

TEST_CASE("OtaEndpoint routes OtaProgress with error message", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    ProgressProbe probe;
    harness.receiver.OnProgress(
        libcomm::OtaEndpoint::ProgressHandler::create<ProgressProbe, &ProgressProbe::Handle>(probe));

    auto buffer = libcomm::BuildOtaProgressMessage(
        midi2pwm::ota::Target::Esp32, midi2pwm::ota::OtaStatus::Error, 0U, 0U, "CRC mismatch");

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.status == midi2pwm::ota::OtaStatus::Error);
    CHECK(probe.error_message == "CRC mismatch");
}

TEST_CASE("OtaEndpoint routes OtaAbort messages", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    AbortProbe probe;
    harness.receiver.OnAbort(
        libcomm::OtaEndpoint::AbortHandler::create<AbortProbe, &AbortProbe::Handle>(probe));

    auto buffer = libcomm::BuildOtaAbortMessage(midi2pwm::ota::Target::Stm32, "User cancelled");

    REQUIRE(harness.sender.Send(std::move(buffer)));

    REQUIRE(probe.call_count == 1);
    CHECK(probe.target == midi2pwm::ota::Target::Stm32);
    CHECK(probe.reason == "User cancelled");
}

TEST_CASE("OtaEndpoint HandleIncoming tolerates missing handlers", "[ota_endpoint]")
{
    OtaEndpointHarness harness;

    auto buffer = libcomm::BuildOtaBeginMessage(
        midi2pwm::ota::Target::Stm32, 1024U, 0U, "1.0.0", 1U);
    REQUIRE(harness.sender.Send(std::move(buffer)));

    CHECK(harness.loopback.call_count == 1);
}

TEST_CASE("OtaEndpoint HandleIncoming rejects invalid identifier", "[ota_endpoint]")
{
    NullOtaWriter null_writer;
    auto write_callback =
        libcomm::OtaEndpoint::WriteCallback::create<NullOtaWriter, &NullOtaWriter::Write>(null_writer);
    libcomm::OtaEndpoint endpoint(write_callback);

    auto payload = libcomm::BuildOtaEndMessage(midi2pwm::ota::Target::Stm32);

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

TEST_CASE("OtaEndpoint HandleIncoming rejects envelopes with NONE message type", "[ota_endpoint]")
{
    NullOtaWriter null_writer;
    auto write_callback =
        libcomm::OtaEndpoint::WriteCallback::create<NullOtaWriter, &NullOtaWriter::Write>(null_writer);
    libcomm::OtaEndpoint endpoint(write_callback);

    flatbuffers::FlatBufferBuilder builder;
    auto envelope = midi2pwm::ota::CreateEnvelope(builder, midi2pwm::ota::Message::NONE);
    builder.Finish(envelope, midi2pwm::ota::EnvelopeIdentifier());
    auto payload = builder.Release();

    FrameCaptureWriter writer;
    auto frame_writer =
        libcomm::FrameTransport::WriteCallback::create<FrameCaptureWriter, &FrameCaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(frame_writer);
    REQUIRE(transport.Send(payload.data(), payload.size()));

    REQUIRE_FALSE(endpoint.HandleIncoming(writer.buffer.data(), writer.buffer.size()));
}

TEST_CASE("OtaEndpoint Send propagates transport failures", "[ota_endpoint]")
{
    FailingOtaWriter writer;
    auto write_callback =
        libcomm::OtaEndpoint::WriteCallback::create<FailingOtaWriter, &FailingOtaWriter::Write>(writer);
    libcomm::OtaEndpoint endpoint(write_callback);

    auto buffer = libcomm::BuildOtaEndMessage(midi2pwm::ota::Target::Stm32);
    REQUIRE_FALSE(endpoint.Send(std::move(buffer)));
    CHECK(writer.call_count == 1);
}

TEST_CASE("OTA message builders populate envelopes", "[ota_endpoint]")
{
    SECTION("OtaBegin builder")
    {
        auto buffer = libcomm::BuildOtaBeginMessage(
            midi2pwm::ota::Target::Esp32, 1572864U, 0xCAFEBABEU, "2.0.0-rc1", 414U);

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        REQUIRE(envelope->message_type() == midi2pwm::ota::Message::OtaBegin);

        const auto* msg = envelope->message_as_OtaBegin();
        REQUIRE(msg != nullptr);
        CHECK(msg->target() == midi2pwm::ota::Target::Esp32);
        CHECK(msg->firmware_size() == 1572864U);
        CHECK(msg->firmware_crc32() == 0xCAFEBABEU);
        REQUIRE(msg->version_string() != nullptr);
        CHECK(std::string(msg->version_string()->c_str()) == "2.0.0-rc1");
        CHECK(msg->total_chunks() == 414U);
    }

    SECTION("OtaData builder")
    {
        const std::vector<std::uint8_t> testData = {0xAA, 0xBB, 0xCC};
        auto buffer = libcomm::BuildOtaDataMessage(
            midi2pwm::ota::Target::Stm32, 42U, testData.data(), testData.size());

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        REQUIRE(envelope->message_type() == midi2pwm::ota::Message::OtaData);

        const auto* msg = envelope->message_as_OtaData();
        REQUIRE(msg != nullptr);
        CHECK(msg->target() == midi2pwm::ota::Target::Stm32);
        CHECK(msg->chunk_index() == 42U);
        REQUIRE(msg->data() != nullptr);
        REQUIRE(msg->data()->size() == 3U);
        CHECK(msg->data()->Get(0) == 0xAA);
        CHECK(msg->data()->Get(2) == 0xCC);
    }

    SECTION("OtaData builder with null data")
    {
        auto buffer = libcomm::BuildOtaDataMessage(
            midi2pwm::ota::Target::Stm32, 0U, nullptr, 0U);

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        const auto* msg = envelope->message_as_OtaData();
        REQUIRE(msg != nullptr);
        CHECK(msg->data() == nullptr);
    }

    SECTION("OtaEnd builder")
    {
        auto buffer = libcomm::BuildOtaEndMessage(midi2pwm::ota::Target::Esp32);

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        REQUIRE(envelope->message_type() == midi2pwm::ota::Message::OtaEnd);

        const auto* msg = envelope->message_as_OtaEnd();
        REQUIRE(msg != nullptr);
        CHECK(msg->target() == midi2pwm::ota::Target::Esp32);
    }

    SECTION("OtaProgress builder without error")
    {
        auto buffer = libcomm::BuildOtaProgressMessage(
            midi2pwm::ota::Target::Stm32, midi2pwm::ota::OtaStatus::Verifying, 60U, 60U);

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        REQUIRE(envelope->message_type() == midi2pwm::ota::Message::OtaProgress);

        const auto* msg = envelope->message_as_OtaProgress();
        REQUIRE(msg != nullptr);
        CHECK(msg->target() == midi2pwm::ota::Target::Stm32);
        CHECK(msg->status() == midi2pwm::ota::OtaStatus::Verifying);
        CHECK(msg->chunks_received() == 60U);
        CHECK(msg->total_chunks() == 60U);
        CHECK(msg->error_message() == nullptr);
    }

    SECTION("OtaProgress builder with error")
    {
        auto buffer = libcomm::BuildOtaProgressMessage(
            midi2pwm::ota::Target::Esp32, midi2pwm::ota::OtaStatus::Error, 5U, 100U, "Write failed");

        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        const auto* msg = envelope->message_as_OtaProgress();
        REQUIRE(msg != nullptr);
        CHECK(msg->status() == midi2pwm::ota::OtaStatus::Error);
        REQUIRE(msg->error_message() != nullptr);
        CHECK(std::string(msg->error_message()->c_str()) == "Write failed");
    }

    SECTION("OtaAbort builder")
    {
        auto buffer = libcomm::BuildOtaAbortMessage(midi2pwm::ota::Target::Stm32, "Timeout");

        REQUIRE(midi2pwm::ota::EnvelopeBufferHasIdentifier(buffer.data()));
        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        REQUIRE(envelope != nullptr);
        REQUIRE(envelope->message_type() == midi2pwm::ota::Message::OtaAbort);

        const auto* msg = envelope->message_as_OtaAbort();
        REQUIRE(msg != nullptr);
        CHECK(msg->target() == midi2pwm::ota::Target::Stm32);
        REQUIRE(msg->reason() != nullptr);
        CHECK(std::string(msg->reason()->c_str()) == "Timeout");
    }

    SECTION("OtaBegin builder with null version string")
    {
        auto buffer = libcomm::BuildOtaBeginMessage(
            midi2pwm::ota::Target::Stm32, 1024U, 0U, nullptr, 1U);

        const auto* envelope = midi2pwm::ota::GetEnvelope(buffer.data());
        const auto* msg = envelope->message_as_OtaBegin();
        REQUIRE(msg != nullptr);
        CHECK(msg->version_string() == nullptr);
    }
}
