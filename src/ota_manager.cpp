#include "libcomm/ota_manager.h"
#include "libcomm/logging.h"

namespace libcomm
{

static constexpr const char* TAG = "OtaManager";

OtaManager::OtaManager(IFlashWriter& writer, midi2pwm::ota::Target target, SendProgressCallback sendProgress)
    : writer_(writer)
    , target_(target)
    , sendProgress_(sendProgress)
{
}

midi2pwm::ota::OtaStatus OtaManager::status() const
{
    return status_;
}

void OtaManager::transitionTo(midi2pwm::ota::OtaStatus newStatus, const char* errorMessage)
{
    LIBCOMM_LOG_INFO(TAG, "State transition: %u -> %u",
                     static_cast<unsigned int>(status_),
                     static_cast<unsigned int>(newStatus));
    status_ = newStatus;
    if (sendProgress_) {
        sendProgress_(target_, status_, receivedChunks_, totalChunks_, errorMessage);
    }
}

void OtaManager::handleBegin(const midi2pwm::ota::OtaBegin& msg)
{
    handleBegin(msg.firmware_size(), msg.firmware_crc32(), msg.total_chunks());
}

void OtaManager::handleBegin(std::uint32_t firmwareSize, std::uint32_t firmwareCrc32, std::uint16_t totalChunks)
{
    if (status_ != midi2pwm::ota::OtaStatus::Idle) {
        LIBCOMM_LOG_INFO(TAG, "Resetting from state %u for new OTA session",
                         static_cast<unsigned int>(status_));
        writer_.abort();
        status_ = midi2pwm::ota::OtaStatus::Idle;
        receivedChunks_ = 0;
        totalChunks_ = 0;
    }

    if (status_ != midi2pwm::ota::OtaStatus::Idle) {
        LIBCOMM_LOG_ERROR(TAG, "OtaBegin received in non-idle state %u",
                          static_cast<unsigned int>(status_));
        transitionTo(midi2pwm::ota::OtaStatus::Error, "OtaBegin received while not idle");
        return;
    }

    expectedCrc32_ = firmwareCrc32;
    totalChunks_ = totalChunks;
    receivedChunks_ = 0;

    transitionTo(midi2pwm::ota::OtaStatus::Preparing);

    if (!writer_.begin(firmwareSize)) {
        LIBCOMM_LOG_ERROR(TAG, "Flash erase failed");
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Flash erase failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    transitionTo(midi2pwm::ota::OtaStatus::Receiving);
}

void OtaManager::handleData(const midi2pwm::ota::OtaData& msg)
{
    const auto* chunkData = msg.data();
    if (!chunkData || chunkData->size() == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Empty chunk data at index %u",
                          static_cast<unsigned int>(msg.chunk_index()));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Empty chunk data");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }
    handleData(msg.chunk_index(), chunkData->data(), chunkData->size());
}

void OtaManager::handleData(std::uint16_t chunkIndex, const std::uint8_t* data, std::size_t dataSize)
{
    if (status_ != midi2pwm::ota::OtaStatus::Receiving) {
        LIBCOMM_LOG_ERROR(TAG, "OtaData received in state %u",
                          static_cast<unsigned int>(status_));
        return;
    }

    if (chunkIndex != receivedChunks_) {
        LIBCOMM_LOG_ERROR(TAG, "Unexpected chunk index: got %u expected %u",
                          static_cast<unsigned int>(chunkIndex),
                          static_cast<unsigned int>(receivedChunks_));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Unexpected chunk index");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    if (!data || dataSize == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Empty chunk data at index %u",
                          static_cast<unsigned int>(chunkIndex));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Empty chunk data");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    if (!writer_.writeChunk(chunkIndex, data, dataSize)) {
        LIBCOMM_LOG_ERROR(TAG, "Flash write failed at chunk %u",
                          static_cast<unsigned int>(chunkIndex));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Flash write failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    ++receivedChunks_;
    LIBCOMM_LOG_DEBUG(TAG, "Chunk %u/%u written",
                      static_cast<unsigned int>(receivedChunks_),
                      static_cast<unsigned int>(totalChunks_));

    constexpr std::uint16_t kProgressReportInterval = 50;
    bool isLastChunk = (receivedChunks_ == totalChunks_);
    bool isReportDue = (receivedChunks_ % kProgressReportInterval) == 0;

    if (sendProgress_ && (isLastChunk || isReportDue)) {
        sendProgress_(target_, status_, receivedChunks_, totalChunks_, nullptr);
    }
}

void OtaManager::handleEnd(const midi2pwm::ota::OtaEnd&)
{
    handleEnd();
}

void OtaManager::handleEnd()
{
    if (status_ != midi2pwm::ota::OtaStatus::Receiving) {
        LIBCOMM_LOG_ERROR(TAG, "OtaEnd received in state %u",
                          static_cast<unsigned int>(status_));
        return;
    }

    if (receivedChunks_ != totalChunks_) {
        LIBCOMM_LOG_ERROR(TAG, "Chunk count mismatch: received %u expected %u",
                          static_cast<unsigned int>(receivedChunks_),
                          static_cast<unsigned int>(totalChunks_));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Chunk count mismatch");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    if (!writer_.finish()) {
        LIBCOMM_LOG_ERROR(TAG, "Flash finalization failed");
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Flash finalization failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    transitionTo(midi2pwm::ota::OtaStatus::Verifying);

    if (!writer_.verify(expectedCrc32_)) {
        LIBCOMM_LOG_ERROR(TAG, "CRC32 verification failed");
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "CRC32 verification failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    transitionTo(midi2pwm::ota::OtaStatus::Applying);

    if (!writer_.activate()) {
        LIBCOMM_LOG_ERROR(TAG, "Slot activation failed");
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Slot activation failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    transitionTo(midi2pwm::ota::OtaStatus::Rebooting);
}

void OtaManager::handleAbort(const midi2pwm::ota::OtaAbort& msg)
{
    handleAbort(msg.reason() ? msg.reason()->c_str() : nullptr);
}

void OtaManager::handleAbort(const char* reason)
{
    if (status_ == midi2pwm::ota::OtaStatus::Idle) {
        LIBCOMM_LOG_WARN(TAG, "OtaAbort received in idle state, ignoring");
        return;
    }

    LIBCOMM_LOG_INFO(TAG, "OTA aborted: %s", reason ? reason : "no reason");
    writer_.abort();
    transitionTo(midi2pwm::ota::OtaStatus::Error, reason ? reason : "Aborted by host");
    status_ = midi2pwm::ota::OtaStatus::Idle;
}

} // namespace libcomm
