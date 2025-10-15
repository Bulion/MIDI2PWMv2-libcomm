#include "libcomm/frame_transport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "flatbuffers/base.h"

namespace libcomm {

namespace {

constexpr std::chrono::milliseconds ACKNOWLEDGMENT_TIMEOUT_DURATION_MILLISECONDS{200};

constexpr std::array<std::uint32_t, 256> generateCrc32LookupTable() {
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


}  // namespace

FrameTransport::FrameTransport(WriteCallback write, bool synchronous_ack)
    : write_(write), synchronous_ack_(synchronous_ack) {}

std::uint32_t FrameTransport::ComputeCrc32(const std::uint8_t* data, std::size_t length) {
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

bool FrameTransport::TransmitFrame(FrameType type,
                                   std::uint16_t sequenceNumber,
                                   const std::uint8_t* payloadData,
                                   std::size_t payloadSizeBytes) const {
    if (!write_) {
        return false;
    }

    if (payloadSizeBytes > kMaxFrameSize) {
        return false;
    }

    constexpr std::size_t MAXIMUM_TOTAL_BUFFER_SIZE_BYTES = kPrefixSize + kHeaderSize + kMaxFrameSize + kCrcSize;
    std::array<std::uint8_t, MAXIMUM_TOTAL_BUFFER_SIZE_BYTES> transmitBuffer{};

    std::size_t frameSizeBytesWithoutPrefix = kHeaderSize + payloadSizeBytes + kCrcSize;
    std::size_t totalTransmitSizeBytes = kPrefixSize + frameSizeBytesWithoutPrefix;

    std::copy(kFramePrefix.begin(), kFramePrefix.end(), transmitBuffer.begin());

    std::uint8_t* frameHeaderStart = transmitBuffer.data() + kPrefixSize;
    frameHeaderStart[0] = kProtocolVersion;
    frameHeaderStart[1] = static_cast<std::uint8_t>(type);

    std::uint16_t sequenceNumberLittleEndian = flatbuffers::EndianScalar(sequenceNumber);
    std::memcpy(&frameHeaderStart[2], &sequenceNumberLittleEndian, sizeof(sequenceNumberLittleEndian));

    std::uint32_t payloadSizeBytesLittleEndian = flatbuffers::EndianScalar(static_cast<std::uint32_t>(payloadSizeBytes));
    std::memcpy(&frameHeaderStart[4], &payloadSizeBytesLittleEndian, sizeof(payloadSizeBytesLittleEndian));

    if (payloadData && payloadSizeBytes > 0U) {
        std::memcpy(&frameHeaderStart[kHeaderSize], payloadData, payloadSizeBytes);
    }

    std::uint32_t computedCrc32Value = ComputeCrc32(frameHeaderStart, frameSizeBytesWithoutPrefix - kCrcSize);
    std::uint32_t crc32ValueLittleEndian = flatbuffers::EndianScalar(computedCrc32Value);
    std::memcpy(&frameHeaderStart[frameSizeBytesWithoutPrefix - kCrcSize], &crc32ValueLittleEndian, sizeof(crc32ValueLittleEndian));

    return write_(transmitBuffer.data(), totalTransmitSizeBytes);
}

std::uint16_t FrameTransport::NextSequence() {
    constexpr std::uint16_t SEQUENCE_NUMBER_ZERO_IS_RESERVED = 0U;

    etl::lock_guard<etl::mutex> lockGuard(mutex_);

    std::uint16_t allocatedSequenceNumber = next_sequence_;
    ++next_sequence_;

    if (next_sequence_ == SEQUENCE_NUMBER_ZERO_IS_RESERVED) {
        ++next_sequence_;
    }

    return allocatedSequenceNumber;
}

bool FrameTransport::ProcessAck(std::uint16_t receivedSequenceNumber, FrameType receivedFrameType) {
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

bool FrameTransport::Send(const std::uint8_t* payloadData,
                          std::size_t payloadSizeBytes,
                          std::size_t maximumRetryAttempts) {
    if (!payloadData && payloadSizeBytes > 0U) {
        return false;
    }

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

        bool transmissionSucceeded = TransmitFrame(FrameType::Data, allocatedSequenceNumber, payloadData, payloadSizeBytes);
        if (!transmissionSucceeded) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
            if (!synchronous_ack_) {
                etl::lock_guard<etl::mutex> lockGuard(mutex_);
                awaiting_ack_ = false;
            }
#endif
            continue;
        }

#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (synchronous_ack_) {
            etl::lock_guard<etl::mutex> lockGuard(mutex_);
            awaiting_ack_ = false;
            return true;
        }

        std::unique_lock<etl::mutex> uniqueLock(mutex_);
        bool acknowledgmentReceived = ack_cv_.wait_for(
            uniqueLock,
            ACKNOWLEDGMENT_TIMEOUT_DURATION_MILLISECONDS,
            [&]() { return !awaiting_ack_ && pending_sequence_ == allocatedSequenceNumber && ack_state_ != AckState::None; });

        if (!acknowledgmentReceived) {
            awaiting_ack_ = false;
            continue;
        }

        if (ack_state_ == AckState::Ack) {
            return true;
        }
#else
        (void)allocatedSequenceNumber;
        return true;
#endif
    }

    return false;
}

bool FrameTransport::HandleIncoming(const std::uint8_t* receivedData,
                                    std::size_t receivedSizeBytes,
                                    const DataHandler& dataHandler) {
    constexpr std::size_t MINIMUM_VALID_FRAME_SIZE_BYTES = kPrefixSize + kHeaderSize + kCrcSize;

    if (!receivedData || receivedSizeBytes < MINIMUM_VALID_FRAME_SIZE_BYTES) {
        return false;
    }

    bool prefixMatches = std::equal(kFramePrefix.begin(), kFramePrefix.end(), receivedData);
    if (!prefixMatches) {
        return false;
    }

    const std::uint8_t* frameHeaderStart = receivedData + kPrefixSize;
    std::uint8_t receivedProtocolVersion = frameHeaderStart[0];
    FrameType receivedFrameType = static_cast<FrameType>(frameHeaderStart[1]);

    std::uint16_t sequenceNumberLittleEndian = 0;
    std::memcpy(&sequenceNumberLittleEndian, &frameHeaderStart[2], sizeof(sequenceNumberLittleEndian));
    std::uint16_t receivedSequenceNumber = flatbuffers::EndianScalar(sequenceNumberLittleEndian);

    std::uint32_t payloadSizeBytesLittleEndian = 0;
    std::memcpy(&payloadSizeBytesLittleEndian, &frameHeaderStart[4], sizeof(payloadSizeBytesLittleEndian));
    std::uint32_t receivedPayloadSizeBytes = flatbuffers::EndianScalar(payloadSizeBytesLittleEndian);

    std::size_t expectedFrameSizeBytesWithoutPrefix = kHeaderSize + static_cast<std::size_t>(receivedPayloadSizeBytes) + kCrcSize;
    std::size_t expectedTotalSizeBytes = kPrefixSize + expectedFrameSizeBytesWithoutPrefix;

    bool versionAndSizeAreValid = (receivedProtocolVersion == kProtocolVersion) && (receivedSizeBytes == expectedTotalSizeBytes);
    if (!versionAndSizeAreValid) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
        return false;
    }

    std::uint32_t receivedCrc32ValueLittleEndian = *reinterpret_cast<const std::uint32_t*>(&frameHeaderStart[expectedFrameSizeBytesWithoutPrefix - kCrcSize]);
    std::uint32_t receivedCrc32Value = flatbuffers::EndianScalar(receivedCrc32ValueLittleEndian);
    std::uint32_t computedCrc32Value = ComputeCrc32(frameHeaderStart, expectedFrameSizeBytesWithoutPrefix - kCrcSize);

    bool crc32Matches = (receivedCrc32Value == computedCrc32Value);
    if (!crc32Matches) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
        return false;
    }

    bool isAcknowledgmentFrame = (receivedFrameType == FrameType::Ack) || (receivedFrameType == FrameType::Nack);
    if (isAcknowledgmentFrame) {
        return ProcessAck(receivedSequenceNumber, receivedFrameType);
    }

    bool isDataFrame = (receivedFrameType == FrameType::Data);
    if (!isDataFrame) {
#if LIBCOMM_HAS_CONDITION_VARIABLE
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, receivedSequenceNumber, nullptr, 0U);
        }
#endif
        return false;
    }

    const std::uint8_t* payloadDataStart = &frameHeaderStart[kHeaderSize];
    bool handlerSucceeded = dataHandler ? dataHandler(payloadDataStart, receivedPayloadSizeBytes) : false;

#if LIBCOMM_HAS_CONDITION_VARIABLE
    if (!synchronous_ack_) {
        FrameType acknowledgmentType = handlerSucceeded ? FrameType::Ack : FrameType::Nack;
        TransmitFrame(acknowledgmentType, receivedSequenceNumber, nullptr, 0U);
    }
#endif

    return handlerSucceeded;
}

}  // namespace libcomm
