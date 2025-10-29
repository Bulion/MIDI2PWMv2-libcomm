#include <catch2/catch_test_macros.hpp>

#include "libcomm/frame_transport.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <algorithm>

#include <flatbuffers/base.h>

namespace {

struct CaptureWriter {
    std::vector<std::uint8_t> last_buffer;
    std::size_t call_count{0};
    bool succeed{true};

    bool Write(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        last_buffer.assign(data, data + size);
        return succeed;
    }
};

struct NullWriter {
    bool Write(const std::uint8_t*, std::size_t)
    {
        return true;
    }
};

struct CaptureHandler {
    std::vector<std::uint8_t> last_payload;
    std::size_t call_count{0};
    bool succeed{true};

    bool Handle(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        last_payload.assign(data, data + size);
        return succeed;
    }
};

std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t length)
{
    constexpr std::uint32_t CRC32_POLYNOMIAL_REVERSED = 0xEDB88320U;
    constexpr std::size_t TABLE_SIZE = 256U;
    constexpr std::size_t BITS_PER_BYTE = 8U;

    const auto& lookup_table = []() -> const std::array<std::uint32_t, TABLE_SIZE>& {
        static const std::array<std::uint32_t, TABLE_SIZE> table = []() {
            std::array<std::uint32_t, TABLE_SIZE> generated{};

            for (std::uint32_t byteValue = 0; byteValue < TABLE_SIZE; ++byteValue) {
                std::uint32_t accumulator = byteValue;

                for (std::uint32_t bit = 0; bit < BITS_PER_BYTE; ++bit) {
                    if ((accumulator & 1U) != 0U) {
                        accumulator = (accumulator >> 1U) ^ CRC32_POLYNOMIAL_REVERSED;
                    } else {
                        accumulator >>= 1U;
                    }
                }

                generated[byteValue] = accumulator;
            }

            return generated;
        }();

        return table;
    }();

    constexpr std::uint32_t CRC32_INITIAL_VALUE = 0xFFFFFFFFU;
    constexpr std::uint32_t CRC32_FINAL_XOR_VALUE = 0xFFFFFFFFU;
    constexpr std::uint32_t BYTE_MASK = 0xFFU;

    std::uint32_t crc = CRC32_INITIAL_VALUE;

    for (std::size_t index = 0; index < length; ++index) {
        std::uint8_t lookup_index = static_cast<std::uint8_t>((crc ^ data[index]) & BYTE_MASK);
        crc = (crc >> 8U) ^ lookup_table[lookup_index];
    }

    return crc ^ CRC32_FINAL_XOR_VALUE;
}

struct LoopbackPipe {
    libcomm::FrameTransport* receiver{nullptr};
    libcomm::FrameTransport::DataHandler handler{};
    std::vector<std::uint8_t> last_frame;
    std::size_t call_count{0};

    bool Write(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        last_frame.assign(data, data + size);
        REQUIRE(receiver != nullptr);
        return receiver->HandleIncoming(data, size, handler);
    }
};

} // namespace

TEST_CASE("FrameTransport::Send frames payload with header metadata", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(write_callback, true);

    const std::array<std::uint8_t, 3> payload{0x12U, 0x34U, 0x56U};

    REQUIRE(transport.Send(payload.data(), payload.size()));
    REQUIRE(writer.call_count == 1);

    const auto expected_transmit_size = libcomm::FrameTransport::kPrefixSize +
                                        libcomm::FrameTransport::kHeaderSize +
                                        payload.size() +
                                        libcomm::FrameTransport::kCrcSize;
    REQUIRE(writer.last_buffer.size() == expected_transmit_size);
    REQUIRE(std::equal(libcomm::FrameTransport::kFramePrefix.begin(),
                       libcomm::FrameTransport::kFramePrefix.end(),
                       writer.last_buffer.begin()));

    const std::uint8_t* frame_header = writer.last_buffer.data() + libcomm::FrameTransport::kPrefixSize;
    REQUIRE(frame_header[0] == 1U); // Protocol version
    REQUIRE(frame_header[1] == 0U); // FrameType::Data

    std::uint16_t sequence = 0;
    std::memcpy(&sequence, &frame_header[2], sizeof(sequence));
    sequence = flatbuffers::EndianScalar(sequence);
    REQUIRE(sequence == 1U);

    std::uint32_t payload_size = 0;
    std::memcpy(&payload_size, &frame_header[4], sizeof(payload_size));
    payload_size = flatbuffers::EndianScalar(payload_size);
    REQUIRE(payload_size == payload.size());

    const std::uint8_t* payload_start = frame_header + libcomm::FrameTransport::kHeaderSize;
    REQUIRE(std::equal(payload.begin(), payload.end(), payload_start));

    std::uint32_t crc32 = 0;
    std::memcpy(&crc32,
                &frame_header[libcomm::FrameTransport::kHeaderSize + payload.size()],
                sizeof(crc32));
    crc32 = flatbuffers::EndianScalar(crc32);
    const auto computed_crc32 =
        ComputeCrc32(frame_header, libcomm::FrameTransport::kHeaderSize + payload.size());
    REQUIRE(crc32 == computed_crc32);
}

TEST_CASE("FrameTransport::Send rejects payloads exceeding maximum frame size", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(write_callback, true);

    std::vector<std::uint8_t> payload(libcomm::FrameTransport::kMaxFrameSize + 1U, 0xAAU);

    REQUIRE_FALSE(transport.Send(payload.data(), payload.size()));
    REQUIRE(writer.call_count == 0);
}

TEST_CASE("FrameTransport::Send propagates write failures", "[frame_transport]")
{
    CaptureWriter writer;
    writer.succeed = false;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(write_callback, true);

    const std::array<std::uint8_t, 2> payload{0x01U, 0x02U};

    REQUIRE_FALSE(transport.Send(payload.data(), payload.size()));
    REQUIRE(writer.call_count == 3);
}

TEST_CASE("FrameTransport::HandleIncoming dispatches payloads to handler", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport sender(write_callback, true);

    const std::array<std::uint8_t, 4> payload{0x9DU, 0x40U, 0x7FU, 0x00U};
    REQUIRE(sender.Send(payload.data(), payload.size()));

    NullWriter null_writer;
    auto noop_writer =
        libcomm::FrameTransport::WriteCallback::create<NullWriter, &NullWriter::Write>(null_writer);
    libcomm::FrameTransport receiver(noop_writer, true);

    CaptureHandler handler;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    REQUIRE(receiver.HandleIncoming(writer.last_buffer.data(), writer.last_buffer.size(), data_handler));
    REQUIRE(handler.call_count == 1);
    REQUIRE(handler.last_payload == std::vector<std::uint8_t>(payload.begin(), payload.end()));
}

TEST_CASE("FrameTransport::HandleIncoming rejects frames with invalid CRC", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport sender(write_callback, true);

    const std::array<std::uint8_t, 2> payload{0xFEU, 0x01U};
    REQUIRE(sender.Send(payload.data(), payload.size()));

    NullWriter null_writer;
    auto noop_writer =
        libcomm::FrameTransport::WriteCallback::create<NullWriter, &NullWriter::Write>(null_writer);
    libcomm::FrameTransport receiver(noop_writer, true);

    CaptureHandler handler;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    std::vector<std::uint8_t> corrupted = writer.last_buffer;
    corrupted.back() ^= 0xFFU;

    REQUIRE_FALSE(receiver.HandleIncoming(corrupted.data(), corrupted.size(), data_handler));
    REQUIRE(handler.call_count == 0);
}

TEST_CASE("FrameTransport asynchronous send succeeds when peer acknowledges", "[frame_transport]")
{
    CaptureHandler handler;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    LoopbackPipe pipe_sender_to_receiver;
    LoopbackPipe pipe_receiver_to_sender;

    auto write_to_receiver =
        libcomm::FrameTransport::WriteCallback::create<LoopbackPipe, &LoopbackPipe::Write>(pipe_sender_to_receiver);
    auto write_to_sender =
        libcomm::FrameTransport::WriteCallback::create<LoopbackPipe, &LoopbackPipe::Write>(pipe_receiver_to_sender);

    libcomm::FrameTransport sender(write_to_receiver, false);
    libcomm::FrameTransport receiver(write_to_sender, false);

    pipe_sender_to_receiver.receiver = &receiver;
    pipe_sender_to_receiver.handler = data_handler;

    pipe_receiver_to_sender.receiver = &sender;
    pipe_receiver_to_sender.handler = libcomm::FrameTransport::DataHandler();

    const std::array<std::uint8_t, 3> payload{0x10U, 0x20U, 0x30U};

    REQUIRE(sender.Send(payload.data(), payload.size()));
    REQUIRE(handler.call_count == 1);
    REQUIRE(pipe_receiver_to_sender.call_count >= 1);

    const std::uint8_t* ack_header =
        pipe_receiver_to_sender.last_frame.data() + libcomm::FrameTransport::kPrefixSize;
    REQUIRE(ack_header[1] == 1U); // FrameType::Ack
}

TEST_CASE("FrameTransport asynchronous send retries and fails after receiving NACKs", "[frame_transport]")
{
    CaptureHandler handler;
    handler.succeed = false;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    LoopbackPipe pipe_sender_to_receiver;
    LoopbackPipe pipe_receiver_to_sender;

    auto write_to_receiver =
        libcomm::FrameTransport::WriteCallback::create<LoopbackPipe, &LoopbackPipe::Write>(pipe_sender_to_receiver);
    auto write_to_sender =
        libcomm::FrameTransport::WriteCallback::create<LoopbackPipe, &LoopbackPipe::Write>(pipe_receiver_to_sender);

    libcomm::FrameTransport sender(write_to_receiver, false);
    libcomm::FrameTransport receiver(write_to_sender, false);

    pipe_sender_to_receiver.receiver = &receiver;
    pipe_sender_to_receiver.handler = data_handler;

    pipe_receiver_to_sender.receiver = &sender;
    pipe_receiver_to_sender.handler = libcomm::FrameTransport::DataHandler();

    const std::array<std::uint8_t, 1> payload{0xFFU};

    REQUIRE_FALSE(sender.Send(payload.data(), payload.size()));
    REQUIRE(handler.call_count == 3);
    REQUIRE(pipe_receiver_to_sender.call_count == 3);

    const std::uint8_t* nack_header =
        pipe_receiver_to_sender.last_frame.data() + libcomm::FrameTransport::kPrefixSize;
    REQUIRE(nack_header[1] == 2U); // FrameType::Nack
}

TEST_CASE("FrameTransport::HandleIncoming rejects frames with invalid prefix", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(write_callback, true);

    const std::array<std::uint8_t, 2> payload{0x01U, 0x02U};
    REQUIRE(transport.Send(payload.data(), payload.size()));

    std::vector<std::uint8_t> corrupted = writer.last_buffer;
    corrupted[0] ^= 0xFFU;

    CaptureHandler handler;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    REQUIRE_FALSE(transport.HandleIncoming(corrupted.data(), corrupted.size(), data_handler));
    REQUIRE(handler.call_count == 0);
}

TEST_CASE("FrameTransport::HandleIncoming rejects frames with mismatched protocol version", "[frame_transport]")
{
    CaptureWriter writer;
    auto write_callback =
        libcomm::FrameTransport::WriteCallback::create<CaptureWriter, &CaptureWriter::Write>(writer);
    libcomm::FrameTransport transport(write_callback, true);

    const std::array<std::uint8_t, 1> payload{0xAAU};
    REQUIRE(transport.Send(payload.data(), payload.size()));

    std::vector<std::uint8_t> corrupted = writer.last_buffer;
    std::size_t header_index = libcomm::FrameTransport::kPrefixSize;
    corrupted[header_index] = static_cast<std::uint8_t>(corrupted[header_index] + 1U);

    CaptureHandler handler;
    auto data_handler =
        libcomm::FrameTransport::DataHandler::create<CaptureHandler, &CaptureHandler::Handle>(handler);

    REQUIRE_FALSE(transport.HandleIncoming(corrupted.data(), corrupted.size(), data_handler));
    REQUIRE(handler.call_count == 0);
}
