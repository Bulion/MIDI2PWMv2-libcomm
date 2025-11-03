#include <catch2/catch_test_macros.hpp>

#include "libcomm/stream_processor.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <flatbuffers/base.h>

namespace {

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

std::vector<std::uint8_t> BuildValidFrame(const std::uint8_t* payload, std::size_t payloadSize)
{
    std::vector<std::uint8_t> frame;
    frame.resize(8 + payloadSize + 4);

    frame[0] = 1;
    frame[1] = 0;

    std::uint16_t sequence = 1;
    std::memcpy(&frame[2], &sequence, sizeof(sequence));

    std::uint32_t payloadSizeLittleEndian = flatbuffers::EndianScalar(static_cast<std::uint32_t>(payloadSize));
    std::memcpy(&frame[4], &payloadSizeLittleEndian, sizeof(payloadSizeLittleEndian));

    if (payloadSize > 0) {
        std::memcpy(&frame[8], payload, payloadSize);
    }

    std::uint32_t crc = ComputeCrc32(frame.data(), 8 + payloadSize);
    std::uint32_t crcLittleEndian = flatbuffers::EndianScalar(crc);
    std::memcpy(&frame[8 + payloadSize], &crcLittleEndian, sizeof(crcLittleEndian));

    return frame;
}

struct FrameCapture {
    std::vector<std::vector<std::uint8_t>> frames;
    std::size_t call_count{0};

    void OnFrame(const std::uint8_t* data, std::size_t size)
    {
        ++call_count;
        frames.emplace_back(data, data + size);
    }
};

struct ErrorCapture {
    std::vector<int> error_types;
    std::vector<std::string> error_messages;
    std::size_t call_count{0};

    void OnError(int type, const char* message)
    {
        ++call_count;
        error_types.push_back(type);
        error_messages.emplace_back(message);
    }
};

}  // namespace

TEST_CASE("StreamProcessor: single complete frame", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture capture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(capture));

    std::uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    auto frame = BuildValidFrame(payload, sizeof(payload));

    processor.feed(frame.data(), frame.size());

    REQUIRE(capture.call_count == 1);
    REQUIRE(capture.frames.size() == 1);
    REQUIRE(capture.frames[0].size() == sizeof(payload));
    REQUIRE(std::memcmp(capture.frames[0].data(), payload, sizeof(payload)) == 0);
}

TEST_CASE("StreamProcessor: multiple frames in one feed", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture capture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(capture));

    std::uint8_t payload1[] = {0xAA, 0xBB};
    std::uint8_t payload2[] = {0xCC, 0xDD, 0xEE};

    auto frame1 = BuildValidFrame(payload1, sizeof(payload1));
    auto frame2 = BuildValidFrame(payload2, sizeof(payload2));

    std::vector<std::uint8_t> combined;
    combined.insert(combined.end(), frame1.begin(), frame1.end());
    combined.insert(combined.end(), frame2.begin(), frame2.end());

    processor.feed(combined.data(), combined.size());

    REQUIRE(capture.call_count == 2);
    REQUIRE(capture.frames.size() == 2);

    REQUIRE(capture.frames[0].size() == sizeof(payload1));
    REQUIRE(std::memcmp(capture.frames[0].data(), payload1, sizeof(payload1)) == 0);

    REQUIRE(capture.frames[1].size() == sizeof(payload2));
    REQUIRE(std::memcmp(capture.frames[1].data(), payload2, sizeof(payload2)) == 0);
}

TEST_CASE("StreamProcessor: fragmented frame", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture capture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(capture));

    std::uint8_t payload[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    auto frame = BuildValidFrame(payload, sizeof(payload));

    processor.feed(frame.data(), 5);
    REQUIRE(capture.call_count == 0);

    processor.feed(frame.data() + 5, frame.size() - 5);
    REQUIRE(capture.call_count == 1);
    REQUIRE(capture.frames.size() == 1);
    REQUIRE(capture.frames[0].size() == sizeof(payload));
    REQUIRE(std::memcmp(capture.frames[0].data(), payload, sizeof(payload)) == 0);
}

TEST_CASE("StreamProcessor: invalid CRC triggers sync recovery", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture frameCapture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(frameCapture));

    ErrorCapture errorCapture;
    processor.setErrorCallback(
        libcomm::StreamProcessor::ErrorCallback::create<ErrorCapture, &ErrorCapture::OnError>(errorCapture));

    std::uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    auto frame = BuildValidFrame(payload, sizeof(payload));

    frame[frame.size() - 1] ^= 0xFF;

    processor.feed(frame.data(), frame.size());

    REQUIRE(frameCapture.call_count == 0);
    REQUIRE(errorCapture.call_count >= 1);

    bool hasCrcError = false;
    for (auto errorType : errorCapture.error_types) {
        if (errorType == static_cast<int>(libcomm::StreamProcessor::ErrorType::CrcFailure)) {
            hasCrcError = true;
            break;
        }
    }
    REQUIRE(hasCrcError);
}

TEST_CASE("StreamProcessor: oversized frame triggers error", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture frameCapture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(frameCapture));

    ErrorCapture errorCapture;
    processor.setErrorCallback(
        libcomm::StreamProcessor::ErrorCallback::create<ErrorCapture, &ErrorCapture::OnError>(errorCapture));

    std::uint8_t header[8] = {0};
    header[0] = 1;
    header[1] = 0;

    std::uint32_t invalidSize = 0xFFFFFFFF;
    std::uint32_t invalidSizeLittleEndian = flatbuffers::EndianScalar(invalidSize);
    std::memcpy(&header[4], &invalidSizeLittleEndian, sizeof(invalidSizeLittleEndian));

    processor.feed(header, sizeof(header));

    REQUIRE(frameCapture.call_count == 0);
    REQUIRE(errorCapture.call_count >= 1);

    bool hasInvalidSizeError = false;
    for (auto errorType : errorCapture.error_types) {
        if (errorType == static_cast<int>(libcomm::StreamProcessor::ErrorType::InvalidFrameSize)) {
            hasInvalidSizeError = true;
            break;
        }
    }
    REQUIRE(hasInvalidSizeError);
}

TEST_CASE("StreamProcessor: buffer overflow triggers error and recovery", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture frameCapture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(frameCapture));

    ErrorCapture errorCapture;
    processor.setErrorCallback(
        libcomm::StreamProcessor::ErrorCallback::create<ErrorCapture, &ErrorCapture::OnError>(errorCapture));

    std::vector<std::uint8_t> largeData(4200, 0xAA);

    processor.feed(largeData.data(), largeData.size());

    REQUIRE(errorCapture.call_count >= 1);

    bool hasOverflowError = false;
    for (auto errorType : errorCapture.error_types) {
        if (errorType == static_cast<int>(libcomm::StreamProcessor::ErrorType::BufferOverflow)) {
            hasOverflowError = true;
            break;
        }
    }
    REQUIRE(hasOverflowError);
}

TEST_CASE("StreamProcessor: sync recovery finds valid frame", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture frameCapture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(frameCapture));

    ErrorCapture errorCapture;
    processor.setErrorCallback(
        libcomm::StreamProcessor::ErrorCallback::create<ErrorCapture, &ErrorCapture::OnError>(errorCapture));

    std::uint8_t garbage[] = {0xFF, 0xAA, 0xBB, 0xCC};
    std::uint8_t payload[] = {0x11, 0x22, 0x33};
    auto validFrame = BuildValidFrame(payload, sizeof(payload));

    std::vector<std::uint8_t> combined;
    combined.insert(combined.end(), garbage, garbage + sizeof(garbage));
    combined.insert(combined.end(), validFrame.begin(), validFrame.end());

    processor.feed(combined.data(), combined.size());

    REQUIRE(frameCapture.call_count == 1);
    REQUIRE(frameCapture.frames.size() == 1);
    REQUIRE(frameCapture.frames[0].size() == sizeof(payload));
}

TEST_CASE("StreamProcessor: reset clears buffer", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture capture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(capture));

    std::uint8_t payload[] = {0x01, 0x02, 0x03};
    auto frame = BuildValidFrame(payload, sizeof(payload));

    processor.feed(frame.data(), 5);
    REQUIRE(processor.getBufferUsage() == 5);

    processor.reset();
    REQUIRE(processor.getBufferUsage() == 0);

    processor.feed(frame.data(), frame.size());
    REQUIRE(capture.call_count == 1);
}

TEST_CASE("StreamProcessor: getBufferUsage tracks accumulation", "[stream_processor]")
{
    libcomm::StreamProcessor processor;

    FrameCapture capture;
    processor.setFrameCallback(
        libcomm::StreamProcessor::FrameCallback::create<FrameCapture, &FrameCapture::OnFrame>(capture));

    REQUIRE(processor.getBufferUsage() == 0);

    std::uint8_t payload[] = {0xAA, 0xBB};
    auto frame = BuildValidFrame(payload, sizeof(payload));

    processor.feed(frame.data(), 4);
    REQUIRE(processor.getBufferUsage() == 4);

    processor.feed(frame.data() + 4, frame.size() - 4);
    REQUIRE(processor.getBufferUsage() == 0);
    REQUIRE(capture.call_count == 1);
}
