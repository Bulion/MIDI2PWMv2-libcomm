#include "libcomm/frame_transport.h"

#include <array>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#include "flatbuffers/base.h"

namespace libcomm {

namespace {

constexpr std::chrono::milliseconds kAckTimeout{200};

std::array<std::uint32_t, 256> BuildCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256U; ++i) {
        std::uint32_t crc = i;
        for (std::uint32_t j = 0; j < 8U; ++j) {
            if (crc & 1U) {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            } else {
                crc >>= 1U;
            }
        }
        table[i] = crc;
    }
    return table;
}

const std::array<std::uint32_t, 256> kCrcTable = BuildCrcTable();

}  // namespace

FrameTransport::FrameTransport(WriteCallback write, bool synchronous_ack)
    : write_(write), synchronous_ack_(synchronous_ack) {}

std::uint32_t FrameTransport::ComputeCrc32(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8U) ^ kCrcTable[index];
    }
    return crc ^ 0xFFFFFFFFU;
}

bool FrameTransport::TransmitFrame(FrameType type,
                                   std::uint16_t sequence,
                                   const std::uint8_t* payload,
                                   std::size_t size) const {
    if (!write_) {
        return false;
    }

    if (size > kMaxFrameSize) {
        return false;
    }

    const std::size_t total_size = kHeaderSize + size + kCrcSize;
    std::vector<std::uint8_t> buffer(total_size, 0);

    buffer[0] = kProtocolVersion;
    buffer[1] = static_cast<std::uint8_t>(type);

    const std::uint16_t sequence_le = flatbuffers::EndianScalar(sequence);
    std::memcpy(&buffer[2], &sequence_le, sizeof(sequence_le));

    const std::uint32_t size_le = flatbuffers::EndianScalar(static_cast<std::uint32_t>(size));
    std::memcpy(&buffer[4], &size_le, sizeof(size_le));

    if (payload && size > 0U) {
        std::memcpy(&buffer[kHeaderSize], payload, size);
    }

    const std::uint32_t crc = ComputeCrc32(buffer.data(), total_size - kCrcSize);
    const std::uint32_t crc_le = flatbuffers::EndianScalar(crc);
    std::memcpy(&buffer[total_size - kCrcSize], &crc_le, sizeof(crc_le));

    return write_(buffer.data(), buffer.size());
}

std::uint16_t FrameTransport::NextSequence() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint16_t sequence = next_sequence_;
    ++next_sequence_;
    if (next_sequence_ == 0U) {
        ++next_sequence_;
    }
    return sequence;
}

bool FrameTransport::ProcessAck(std::uint16_t sequence, FrameType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!awaiting_ack_ || sequence != pending_sequence_) {
        return false;
    }

    ack_state_ = (type == FrameType::Ack) ? AckState::Ack : AckState::Nack;
    awaiting_ack_ = false;
    ack_cv_.notify_all();
    return true;
}

bool FrameTransport::Send(const std::uint8_t* payload, std::size_t size, std::size_t max_retries) {
    if (!payload && size > 0U) {
        return false;
    }

    for (std::size_t attempt = 0; attempt < max_retries; ++attempt) {
        const std::uint16_t sequence = NextSequence();


        if (!synchronous_ack_) {
            std::lock_guard<std::mutex> lock(mutex_);
            awaiting_ack_ = true;
            pending_sequence_ = sequence;
            ack_state_ = AckState::None;
        }

        const bool write_ok = TransmitFrame(FrameType::Data, sequence, payload, size);
        if (!write_ok) {
            std::lock_guard<std::mutex> lock(mutex_);
            awaiting_ack_ = false;
            continue;
        }

        if (synchronous_ack_) {
            std::lock_guard<std::mutex> lock(mutex_);
            awaiting_ack_ = false;
            return true;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        const bool acknowledged = ack_cv_.wait_for(
            lock,
            kAckTimeout,
            [&]() { return !awaiting_ack_ && pending_sequence_ == sequence && ack_state_ != AckState::None; });

        if (!acknowledged) {
            awaiting_ack_ = false;
            continue;
        }

        if (ack_state_ == AckState::Ack) {
            return true;
        }
    }

    return false;
}

bool FrameTransport::HandleIncoming(const std::uint8_t* data,
                                    std::size_t size,
                                    const DataHandler& handler) {
    if (!data || size < (kHeaderSize + kCrcSize)) {
        return false;
    }

    const std::uint8_t version = data[0];
    const FrameType type = static_cast<FrameType>(data[1]);

    std::uint16_t sequence_le = 0;
    std::memcpy(&sequence_le, &data[2], sizeof(sequence_le));
    const std::uint16_t sequence = flatbuffers::EndianScalar(sequence_le);

    std::uint32_t payload_size_le = 0;
    std::memcpy(&payload_size_le, &data[4], sizeof(payload_size_le));
    const std::uint32_t payload_size = flatbuffers::EndianScalar(payload_size_le);

    const std::size_t expected_size = kHeaderSize + static_cast<std::size_t>(payload_size) + kCrcSize;
    if (version != kProtocolVersion || size != expected_size) {
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, sequence, nullptr, 0U);
        }
        return false;
    }

    const std::uint32_t received_crc_le = *reinterpret_cast<const std::uint32_t*>(&data[expected_size - kCrcSize]);
    const std::uint32_t received_crc = flatbuffers::EndianScalar(received_crc_le);
    const std::uint32_t computed_crc = ComputeCrc32(data, expected_size - kCrcSize);
    if (received_crc != computed_crc) {
        if (!synchronous_ack_) {
            TransmitFrame(FrameType::Nack, sequence, nullptr, 0U);
        }
        return false;
    }

    if (type == FrameType::Ack || type == FrameType::Nack) {
        return ProcessAck(sequence, type);
    }

    if (type != FrameType::Data) {
        TransmitFrame(FrameType::Nack, sequence, nullptr, 0U);
        return false;
    }

    const std::uint8_t* payload = &data[kHeaderSize];
    const bool ok = handler ? handler(payload, payload_size) : false;
    if (!synchronous_ack_) {
        TransmitFrame(ok ? FrameType::Ack : FrameType::Nack, sequence, nullptr, 0U);
    }
    return ok;
}

}  // namespace libcomm
