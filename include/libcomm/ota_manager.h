#pragma once

#include <cstdint>

#include <etl/delegate.h>

#include "libcomm/flash_writer.h"
#include "ota_messages_generated.h"

namespace libcomm {

class OtaManager {
public:
    using SendProgressCallback = etl::delegate<void(midi2pwm::ota::Target, midi2pwm::ota::OtaStatus,
                                                     std::uint16_t, std::uint16_t, const char*)>;

    OtaManager(IFlashWriter& writer, midi2pwm::ota::Target target, SendProgressCallback sendProgress);

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    void handleBegin(const midi2pwm::ota::OtaBegin& msg);
    void handleData(const midi2pwm::ota::OtaData& msg);
    void handleEnd(const midi2pwm::ota::OtaEnd& msg);
    void handleAbort(const midi2pwm::ota::OtaAbort& msg);

    void handleBegin(std::uint32_t firmwareSize, std::uint32_t firmwareCrc32, std::uint16_t totalChunks);
    void handleData(std::uint16_t chunkIndex, const std::uint8_t* data, std::size_t dataSize);
    void handleEnd();
    void handleAbort(const char* reason);

    midi2pwm::ota::OtaStatus status() const;
    std::uint16_t totalChunks() const { return totalChunks_; }

private:
    void transitionTo(midi2pwm::ota::OtaStatus newStatus, const char* errorMessage = nullptr);

    IFlashWriter& writer_;
    midi2pwm::ota::Target target_;
    SendProgressCallback sendProgress_;
    midi2pwm::ota::OtaStatus status_{midi2pwm::ota::OtaStatus::Idle};
    std::uint32_t expectedCrc32_{0};
    std::uint16_t totalChunks_{0};
    std::uint16_t receivedChunks_{0};
};

}  // namespace libcomm
