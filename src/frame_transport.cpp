#include "libcomm/frame_transport.h"
#include "libcomm/logging.h"

#include "flatbuffers/base.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace libcomm
{

static constexpr const char* TAG = "FrameTransport";

namespace
{

constexpr std::chrono::milliseconds ACKNOWLEDGMENT_TIMEOUT_DURATION_MILLISECONDS{200};

constexpr std::array<std::uint32_t, 256> generateCrc32LookupTable()
{
    std::array<std::uint32_t, 256> lookupTable{};

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

constexpr std::array<std::uint32_t, 256> CRC32_LOOKUP_TABLE = generateCrc32LookupTable();

} // namespace

FrameTransport::FrameTransport(WriteCallback write, bool synchronous_ack)
    : write_(write)
    , synchronous_ack_(synchronous_ack)
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
    std::array<std::uint8_t, MAXIMUM_TOTAL_BUFFER_SIZE_BYTES> transmitBuffer{};

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

bool FrameTransport::ProcessAck(std::uint16_t receivedSequenceNumber, FrameType receivedFrameType)
{
#if LIBCOMM_HAS_CONDITION_VARIABLE
    etl::lock_guard<etl::mutex> lockGuard(mutex_);

    bool isWaitingForThisAcknowledgment = awaiting_ack_ && (receivedSequenceNumber == pending_sequence_);
    if (!isWaitingForThisAcknowledgment) {
        return false;
    }

    ack_state_ = (receivedFrameType == FrameType::Ack) ? AckState::Ack : AckState::Nack;
    awaiting_ack_ = false;
    ack_cv_.notify_all();

    return true;
#else
    (void)receivedSequenceNumber;
    (void)receivedFrameType;
    return false;
#endif
}

bool FrameTransport::Send(
    const std::uint8_t *payloadData, std::size_t payloadSizeBytes, std::size_t maximumRetryAttempts)
{
    if (!payloadData && payloadSizeBytes > 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid parameters: null payload with non-zero size");
        return false;
    }

    LIBCOMM_LOG_DEBUG(TAG, "Send called with payload_size=%u max_retries=%u", static_cast<unsigned int>(payloadSizeBytes), static_cast<unsigned int>(maximumRetryAttempts));

    for (std::size_t attemptNumber = 0; attemptNumber < maximumRetryAttempts; ++attemptNumber) {
        std::uint16_t allocatedSequenceNumber = NextSequence();

        if (!synchronous_ack_) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
            etl::lock_guard<etl::mutex> lockGuard(mutex_);
            awaiting_ack_ = true;
            pending_sequence_ = allocatedSequenceNumber;
            ack_state_ = AckState::None;
#endif
        }

        bool transmissionSucceeded =
            TransmitFrame(FrameType::Data, allocatedSequenceNumber, payloadData, payloadSizeBytes);
        if (!transmissionSucceeded) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
            if (!synchronous_ack_) {
                etl::lock_guard<etl::mutex> lockGuard(mutex_);
                awaiting_ack_ = false;
            }
#endif
            LIBCOMM_LOG_WARN(TAG, "Transmission failed on attempt %u/%u for seq=%u", static_cast<unsigned int>(attemptNumber + 1), static_cast<unsigned int>(maximumRetryAttempts), allocatedSequenceNumber);
            continue;
        }

#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (synchronous_ack_) {
            etl::lock_guard<etl::mutex> lockGuard(mutex_);
            awaiting_ack_ = false;
            LIBCOMM_LOG_INFO(TAG, "Frame sent successfully seq=%u (synchronous mode)", allocatedSequenceNumber);
            return true;
        }

        std::unique_lock<etl::mutex> uniqueLock(mutex_);
        bool acknowledgmentReceived = ack_cv_.wait_for(uniqueLock, ACKNOWLEDGMENT_TIMEOUT_DURATION_MILLISECONDS, [&]() {
            return !awaiting_ack_ && pending_sequence_ == allocatedSequenceNumber && ack_state_ != AckState::None;
        });

        if (!acknowledgmentReceived) {
            awaiting_ack_ = false;
            LIBCOMM_LOG_WARN(TAG, "ACK timeout for seq=%u on attempt %u/%u", allocatedSequenceNumber, static_cast<unsigned int>(attemptNumber + 1), static_cast<unsigned int>(maximumRetryAttempts));
            continue;
        }

        if (ack_state_ == AckState::Ack) {
            LIBCOMM_LOG_INFO(TAG, "Frame sent successfully seq=%u (received ACK)", allocatedSequenceNumber);
            return true;
        }

        LIBCOMM_LOG_WARN(TAG, "Received NACK for seq=%u on attempt %u/%u", allocatedSequenceNumber, static_cast<unsigned int>(attemptNumber + 1), static_cast<unsigned int>(maximumRetryAttempts));
#else
        (void)allocatedSequenceNumber;
        return true;
#endif
    }

    LIBCOMM_LOG_ERROR(TAG, "Send failed after %u retry attempts", static_cast<unsigned int>(maximumRetryAttempts));
    return false;
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
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
        return false;
    }

    std::uint32_t receivedCrc32ValueLittleEndian =
        *reinterpret_cast<const std::uint32_t *>(&receivedData[expectedTotalSizeBytes - kCrcSize]);
    std::uint32_t receivedCrc32Value = flatbuffers::EndianScalar(receivedCrc32ValueLittleEndian);
    std::uint32_t computedCrc32Value = ComputeCrc32(receivedData, expectedTotalSizeBytes - kCrcSize);

    bool crc32Matches = (receivedCrc32Value == computedCrc32Value);
    if (!crc32Matches) {
        LIBCOMM_LOG_ERROR(TAG, "CRC mismatch for seq=%u: expected %x, got %x", receivedSequenceNumber, computedCrc32Value, receivedCrc32Value);
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
        return false;
    }

    bool isAcknowledgmentFrame = (receivedFrameType == FrameType::Ack) || (receivedFrameType == FrameType::Nack);
    if (isAcknowledgmentFrame) {
        LIBCOMM_LOG_DEBUG(TAG, "Received %s for seq=%u", (receivedFrameType == FrameType::Ack) ? "ACK" : "NACK", receivedSequenceNumber);
        return ProcessAck(receivedSequenceNumber, receivedFrameType);
    }

    bool isDataFrame = (receivedFrameType == FrameType::Data);
    if (!isDataFrame) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame type %u for seq=%u", static_cast<unsigned int>(receivedFrameType), receivedSequenceNumber);
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
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

#if LIBCOMM_HAS_CONDITION_VARIABLE
    if (!synchronous_ack_) {
        FrameType acknowledgmentType = handlerSucceeded ? FrameType::Ack : FrameType::Nack;
        TransmitFrame(acknowledgmentType, receivedSequenceNumber, nullptr, 0U);
    }
#endif

    return handlerSucceeded;
}

} // namespace libcomm
