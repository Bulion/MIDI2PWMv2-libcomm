#include "libcomm/frame_transport.h"
#include "libcomm/logging.h"

#include "flatbuffers/base.h"

#include <etl/array.h>

#include <algorithm>
#include <cstring>

namespace libcomm
{

static constexpr const char* TAG = "FrameTransport";

namespace
{

etl::array<std::uint32_t, 256> generateCrc32LookupTable()
{
    etl::array<std::uint32_t, 256> lookupTable{};

    constexpr std::uint32_t CRC32_POLYNOMIAL_REVERSED = 0xEDB88320U;
    constexpr std::size_t BITS_PER_BYTE = 8;
    constexpr std::size_t TABLE_SIZE_ENTRIES = 256;

    for (std::uint32_t byteValue = 0; byteValue < TABLE_SIZE_ENTRIES; ++byteValue) {
        std::uint32_t crc32Accumulator = byteValue;

        for (std::uint32_t bitPosition = 0; bitPosition < BITS_PER_BYTE; ++bitPosition) {
            bool lowestBitIsSet = (crc32Accumulator & 1U) != 0;

            if (lowestBitIsSet) {
                crc32Accumulator = (crc32Accumulator >> 1U) ^ CRC32_POLYNOMIAL_REVERSED;
            } else {
                crc32Accumulator >>= 1U;
            }
        }

        lookupTable[byteValue] = crc32Accumulator;
    }

    return lookupTable;
}

const etl::array<std::uint32_t, 256> CRC32_LOOKUP_TABLE = generateCrc32LookupTable();

} // namespace

FrameTransport::FrameTransport(WriteCallback write)
    : write_(write)
{
}

std::uint32_t FrameTransport::ComputeCrc32(const std::uint8_t *data, std::size_t length)
{
    constexpr std::uint32_t CRC32_INITIAL_VALUE = 0xFFFFFFFFU;
    constexpr std::uint32_t CRC32_FINAL_XOR_VALUE = 0xFFFFFFFFU;
    constexpr std::uint8_t BYTE_MASK = 0xFFU;

    std::uint32_t crc32Accumulator = CRC32_INITIAL_VALUE;

    for (std::size_t byteIndex = 0; byteIndex < length; ++byteIndex) {
        std::uint8_t tableLookupIndex = static_cast<std::uint8_t>((crc32Accumulator ^ data[byteIndex]) & BYTE_MASK);
        crc32Accumulator = (crc32Accumulator >> 8U) ^ CRC32_LOOKUP_TABLE[tableLookupIndex];
    }

    return crc32Accumulator ^ CRC32_FINAL_XOR_VALUE;
}

bool FrameTransport::TransmitFrame(
    FrameType type, std::uint16_t sequenceNumber, const std::uint8_t *payloadData, std::size_t payloadSizeBytes) const
{
    if (!write_) {
        LIBCOMM_LOG_ERROR(TAG, "Write callback not set");
        return false;
    }

    if (payloadSizeBytes > kMaxFrameSize) {
        LIBCOMM_LOG_ERROR(TAG, "Payload size %u exceeds maximum %u", static_cast<unsigned int>(payloadSizeBytes), static_cast<unsigned int>(kMaxFrameSize));
        return false;
    }

    constexpr std::size_t MAXIMUM_TOTAL_BUFFER_SIZE_BYTES = kHeaderSize + kMaxFrameSize + kCrcSize;
    etl::array<std::uint8_t, MAXIMUM_TOTAL_BUFFER_SIZE_BYTES> transmitBuffer{};

    std::size_t totalTransmitSizeBytes = kHeaderSize + payloadSizeBytes + kCrcSize;

    transmitBuffer[0] = kProtocolVersion;
    transmitBuffer[1] = static_cast<std::uint8_t>(type);

    std::uint16_t sequenceNumberLittleEndian = flatbuffers::EndianScalar(sequenceNumber);
    std::memcpy(&transmitBuffer[2], &sequenceNumberLittleEndian, sizeof(sequenceNumberLittleEndian));

    std::uint32_t payloadSizeBytesLittleEndian =
        flatbuffers::EndianScalar(static_cast<std::uint32_t>(payloadSizeBytes));
    std::memcpy(&transmitBuffer[4], &payloadSizeBytesLittleEndian, sizeof(payloadSizeBytesLittleEndian));

    if (payloadData && payloadSizeBytes > 0U) {
        std::memcpy(&transmitBuffer[kHeaderSize], payloadData, payloadSizeBytes);
    }

    std::uint32_t computedCrc32Value = ComputeCrc32(transmitBuffer.data(), totalTransmitSizeBytes - kCrcSize);
    std::uint32_t crc32ValueLittleEndian = flatbuffers::EndianScalar(computedCrc32Value);
    std::memcpy(
        &transmitBuffer[totalTransmitSizeBytes - kCrcSize],
        &crc32ValueLittleEndian,
        sizeof(crc32ValueLittleEndian));

    LIBCOMM_LOG_DEBUG(TAG, "Transmitting frame type=%u seq=%u payload_size=%u", static_cast<unsigned int>(type), sequenceNumber, static_cast<unsigned int>(payloadSizeBytes));

    bool writeSucceeded = write_(transmitBuffer.data(), totalTransmitSizeBytes);
    if (!writeSucceeded) {
        LIBCOMM_LOG_ERROR(TAG, "Write callback failed for frame seq=%u", sequenceNumber);
    }

    return writeSucceeded;
}

std::uint16_t FrameTransport::NextSequence()
{
    constexpr std::uint16_t SEQUENCE_NUMBER_ZERO_IS_RESERVED = 0U;

    std::uint16_t allocatedSequenceNumber = 0;

    {
        etl::lock_guard<etl::mutex> lockGuard(mutex_);

        allocatedSequenceNumber = next_sequence_;
        ++next_sequence_;

        if (next_sequence_ == SEQUENCE_NUMBER_ZERO_IS_RESERVED) {
            ++next_sequence_;
        }
    }

    return allocatedSequenceNumber;
}


bool FrameTransport::Send(const std::uint8_t *payloadData, std::size_t payloadSizeBytes)
{
    if (!payloadData && payloadSizeBytes > 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid parameters: null payload with non-zero size");
        return false;
    }

    LIBCOMM_LOG_DEBUG(TAG, "Send called with payload_size=%u", static_cast<unsigned int>(payloadSizeBytes));

    std::uint16_t allocatedSequenceNumber = NextSequence();

    bool transmissionSucceeded =
        TransmitFrame(FrameType::Data, allocatedSequenceNumber, payloadData, payloadSizeBytes);

    if (transmissionSucceeded) {
        LIBCOMM_LOG_INFO(TAG, "Frame sent successfully seq=%u", allocatedSequenceNumber);
    } else {
        LIBCOMM_LOG_ERROR(TAG, "Transmission failed for seq=%u", allocatedSequenceNumber);
    }

    return transmissionSucceeded;
}

bool FrameTransport::HandleIncoming(
    const std::uint8_t *receivedData, std::size_t receivedSizeBytes, const DataHandler &dataHandler)
{
    constexpr std::size_t MINIMUM_VALID_FRAME_SIZE_BYTES = kHeaderSize + kCrcSize;

    if (!receivedData || receivedSizeBytes < MINIMUM_VALID_FRAME_SIZE_BYTES) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid incoming frame: null data or size %u below minimum %u", static_cast<unsigned int>(receivedSizeBytes), static_cast<unsigned int>(MINIMUM_VALID_FRAME_SIZE_BYTES));
        return false;
    }

    LIBCOMM_LOG_DEBUG(TAG, "Handling incoming frame of size %u", static_cast<unsigned int>(receivedSizeBytes));

    std::uint8_t receivedProtocolVersion = receivedData[0];
    FrameType receivedFrameType = static_cast<FrameType>(receivedData[1]);

    std::uint16_t sequenceNumberLittleEndian = 0;
    std::memcpy(&sequenceNumberLittleEndian, &receivedData[2], sizeof(sequenceNumberLittleEndian));
    std::uint16_t receivedSequenceNumber = flatbuffers::EndianScalar(sequenceNumberLittleEndian);

    std::uint32_t payloadSizeBytesLittleEndian = 0;
    std::memcpy(&payloadSizeBytesLittleEndian, &receivedData[4], sizeof(payloadSizeBytesLittleEndian));
    std::uint32_t receivedPayloadSizeBytes = flatbuffers::EndianScalar(payloadSizeBytesLittleEndian);

    std::size_t expectedTotalSizeBytes = kHeaderSize + static_cast<std::size_t>(receivedPayloadSizeBytes) + kCrcSize;

    bool versionAndSizeAreValid =
        (receivedProtocolVersion == kProtocolVersion) && (receivedSizeBytes == expectedTotalSizeBytes);
    if (!versionAndSizeAreValid) {
        if (receivedProtocolVersion != kProtocolVersion) {
            LIBCOMM_LOG_ERROR(TAG, "Protocol version mismatch: expected %u, got %u", kProtocolVersion, receivedProtocolVersion);
        } else {
            LIBCOMM_LOG_ERROR(TAG, "Frame size mismatch: expected %u, got %u", static_cast<unsigned int>(expectedTotalSizeBytes), static_cast<unsigned int>(receivedSizeBytes));
        }
        return false;
    }

    std::uint32_t receivedCrc32ValueLittleEndian =
        *reinterpret_cast<const std::uint32_t *>(&receivedData[expectedTotalSizeBytes - kCrcSize]);
    std::uint32_t receivedCrc32Value = flatbuffers::EndianScalar(receivedCrc32ValueLittleEndian);
    std::uint32_t computedCrc32Value = ComputeCrc32(receivedData, expectedTotalSizeBytes - kCrcSize);

    bool crc32Matches = (receivedCrc32Value == computedCrc32Value);
    if (!crc32Matches) {
        LIBCOMM_LOG_ERROR(TAG, "CRC mismatch for seq=%u: expected %x, got %x", receivedSequenceNumber, computedCrc32Value, receivedCrc32Value);
        return false;
    }

    if (receivedFrameType != FrameType::Data) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame type %u for seq=%u", static_cast<unsigned int>(receivedFrameType), receivedSequenceNumber);
        return false;
    }

    LIBCOMM_LOG_DEBUG(TAG, "Received data frame seq=%u payload_size=%u", receivedSequenceNumber, static_cast<unsigned int>(receivedPayloadSizeBytes));

    const std::uint8_t *payloadDataStart = &receivedData[kHeaderSize];
    bool handlerSucceeded = dataHandler ? dataHandler(payloadDataStart, receivedPayloadSizeBytes) : false;

    if (!handlerSucceeded) {
        if (!dataHandler) {
            LIBCOMM_LOG_WARN(TAG, "No data handler registered for seq=%u", receivedSequenceNumber);
        } else {
            LIBCOMM_LOG_WARN(TAG, "Data handler failed for seq=%u", receivedSequenceNumber);
        }
    }

    return handlerSucceeded;
}

} // namespace libcomm
