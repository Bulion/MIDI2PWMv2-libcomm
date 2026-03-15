#pragma once

#include <cstddef>
#include <cstdint>

#include <etl/delegate.h>

#include "flatbuffers/flatbuffers.h"
#include "libcomm/frame_transport.h"
#include "ota_messages_generated.h"

namespace libcomm {

class OtaEndpoint {
public:
    using WriteCallback = FrameTransport::WriteCallback;
    using BeginHandler = etl::delegate<void(const midi2pwm::ota::OtaBegin&)>;
    using DataHandler = etl::delegate<void(const midi2pwm::ota::OtaData&)>;
    using EndHandler = etl::delegate<void(const midi2pwm::ota::OtaEnd&)>;
    using ProgressHandler = etl::delegate<void(const midi2pwm::ota::OtaProgress&)>;
    using AbortHandler = etl::delegate<void(const midi2pwm::ota::OtaAbort&)>;

    explicit OtaEndpoint(WriteCallback write);

    OtaEndpoint(const OtaEndpoint&) = delete;
    OtaEndpoint& operator=(const OtaEndpoint&) = delete;
    ~OtaEndpoint() = default;

    bool Send(flatbuffers::DetachedBuffer&& buffer);
    bool HandleIncoming(const std::uint8_t* data, std::size_t size);

    void OnBegin(BeginHandler handler);
    void OnData(DataHandler handler);
    void OnEnd(EndHandler handler);
    void OnProgress(ProgressHandler handler);
    void OnAbort(AbortHandler handler);

    bool HandleFrame(const std::uint8_t* data, std::size_t size) const;

private:

    FrameTransport transport_;
    BeginHandler begin_handler_;
    DataHandler data_handler_;
    EndHandler end_handler_;
    ProgressHandler progress_handler_;
    AbortHandler abort_handler_;
};

flatbuffers::DetachedBuffer BuildOtaBeginMessage(midi2pwm::ota::Target target,
                                                  std::uint32_t firmwareSize,
                                                  std::uint32_t firmwareCrc32,
                                                  const char* versionString,
                                                  std::uint16_t totalChunks);

flatbuffers::DetachedBuffer BuildOtaDataMessage(midi2pwm::ota::Target target,
                                                 std::uint16_t chunkIndex,
                                                 const std::uint8_t* data,
                                                 std::size_t dataSize);

flatbuffers::DetachedBuffer BuildOtaEndMessage(midi2pwm::ota::Target target);

flatbuffers::DetachedBuffer BuildOtaProgressMessage(midi2pwm::ota::Target target,
                                                     midi2pwm::ota::OtaStatus status,
                                                     std::uint16_t chunksReceived,
                                                     std::uint16_t totalChunks,
                                                     const char* errorMessage = nullptr);

flatbuffers::DetachedBuffer BuildOtaAbortMessage(midi2pwm::ota::Target target,
                                                  const char* reason);

}  // namespace libcomm
