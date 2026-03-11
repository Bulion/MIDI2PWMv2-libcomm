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
    if (status_ != midi2pwm::ota::OtaStatus::Idle) {
        LIBCOMM_LOG_ERROR(TAG, "OtaBegin received in non-idle state %u",
                          static_cast<unsigned int>(status_));
        transitionTo(midi2pwm::ota::OtaStatus::Error, "OtaBegin received while not idle");
        return;
    }

    expectedCrc32_ = msg.firmware_crc32();
    totalChunks_ = msg.total_chunks();
    receivedChunks_ = 0;

    transitionTo(midi2pwm::ota::OtaStatus::Preparing);

    if (!writer_.begin(msg.firmware_size())) {
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
    if (status_ != midi2pwm::ota::OtaStatus::Receiving) {
        LIBCOMM_LOG_ERROR(TAG, "OtaData received in state %u",
                          static_cast<unsigned int>(status_));
        return;
    }

    if (msg.chunk_index() != receivedChunks_) {
        LIBCOMM_LOG_ERROR(TAG, "Unexpected chunk index: got %u expected %u",
                          static_cast<unsigned int>(msg.chunk_index()),
                          static_cast<unsigned int>(receivedChunks_));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Unexpected chunk index");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    const auto* chunkData = msg.data();
    if (!chunkData || chunkData->size() == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Empty chunk data at index %u",
                          static_cast<unsigned int>(msg.chunk_index()));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Empty chunk data");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    if (!writer_.writeChunk(msg.chunk_index(), chunkData->data(), chunkData->size())) {
        LIBCOMM_LOG_ERROR(TAG, "Flash write failed at chunk %u",
                          static_cast<unsigned int>(msg.chunk_index()));
        writer_.abort();
        transitionTo(midi2pwm::ota::OtaStatus::Error, "Flash write failed");
        status_ = midi2pwm::ota::OtaStatus::Idle;
        return;
    }

    ++receivedChunks_;
    LIBCOMM_LOG_DEBUG(TAG, "Chunk %u/%u written",
                      static_cast<unsigned int>(receivedChunks_),
                      static_cast<unsigned int>(totalChunks_));

    if (sendProgress_) {
        sendProgress_(target_, status_, receivedChunks_, totalChunks_, nullptr);
    }
}

void OtaManager::handleEnd(const midi2pwm::ota::OtaEnd&)
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
    if (status_ == midi2pwm::ota::OtaStatus::Idle) {
        LIBCOMM_LOG_WARN(TAG, "OtaAbort received in idle state, ignoring");
        return;
    }

    LIBCOMM_LOG_INFO(TAG, "OTA aborted: %s",
                     msg.reason() ? msg.reason()->c_str() : "no reason");
    writer_.abort();
    transitionTo(midi2pwm::ota::OtaStatus::Error,
                 msg.reason() ? msg.reason()->c_str() : "Aborted by host");
    status_ = midi2pwm::ota::OtaStatus::Idle;
}

} // namespace libcomm
