#include "libcomm/ota_endpoint.h"
#include "libcomm/logging.h"

#include "flatbuffers/verifier.h"

namespace libcomm
{

static constexpr const char* TAG = "OtaEndpoint";

OtaEndpoint::OtaEndpoint(WriteCallback writeCallback)
    : transport_(writeCallback)
{
}

bool OtaEndpoint::Send(flatbuffers::DetachedBuffer &&serializedMessageBuffer)
{
    const std::uint8_t *bufferData = serializedMessageBuffer.data();
    std::size_t bufferSizeBytes = serializedMessageBuffer.size();

    return transport_.Send(bufferData, bufferSizeBytes);
}

bool OtaEndpoint::HandleIncoming(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    auto handler = FrameTransport::DataHandler::create<OtaEndpoint, &OtaEndpoint::HandleFrame>(*this);
    return transport_.HandleIncoming(receivedData, receivedSizeBytes, handler);
}

void OtaEndpoint::OnBegin(BeginHandler callbackHandler)
{
    begin_handler_ = callbackHandler;
}

void OtaEndpoint::OnData(DataHandler callbackHandler)
{
    data_handler_ = callbackHandler;
}

void OtaEndpoint::OnEnd(EndHandler callbackHandler)
{
    end_handler_ = callbackHandler;
}

void OtaEndpoint::OnProgress(ProgressHandler callbackHandler)
{
    progress_handler_ = callbackHandler;
}

void OtaEndpoint::OnAbort(AbortHandler callbackHandler)
{
    abort_handler_ = callbackHandler;
}

bool OtaEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame: null data or zero size");
        return false;
    }

    bool hasValidIdentifier = midi2pwm::ota::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidIdentifier) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid FlatBuffers identifier in OTA message");
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::ota::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        LIBCOMM_LOG_ERROR(TAG, "FlatBuffers verification failed for OTA envelope");
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::ota::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        LIBCOMM_LOG_ERROR(TAG, "Failed to deserialize OTA envelope");
        return false;
    }

    switch (deserializedEnvelope->message_type()) {
    case midi2pwm::ota::Message::OtaBegin: {
        const auto *beginMessage = deserializedEnvelope->message_as_OtaBegin();
        if (beginMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received OtaBegin: target=%u size=%u crc=0x%08X chunks=%u",
                             static_cast<unsigned int>(beginMessage->target()),
                             static_cast<unsigned int>(beginMessage->firmware_size()),
                             static_cast<unsigned int>(beginMessage->firmware_crc32()),
                             static_cast<unsigned int>(beginMessage->total_chunks()));
            if (begin_handler_) {
                begin_handler_(*beginMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for OtaBegin");
            }
        }
        return true;
    }
    case midi2pwm::ota::Message::OtaData: {
        const auto *dataMessage = deserializedEnvelope->message_as_OtaData();
        if (dataMessage) {
            LIBCOMM_LOG_DEBUG(TAG, "Received OtaData: target=%u chunk=%u size=%u",
                              static_cast<unsigned int>(dataMessage->target()),
                              static_cast<unsigned int>(dataMessage->chunk_index()),
                              dataMessage->data() ? static_cast<unsigned int>(dataMessage->data()->size()) : 0U);
            if (data_handler_) {
                data_handler_(*dataMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for OtaData");
            }
        }
        return true;
    }
    case midi2pwm::ota::Message::OtaEnd: {
        const auto *endMessage = deserializedEnvelope->message_as_OtaEnd();
        if (endMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received OtaEnd: target=%u",
                             static_cast<unsigned int>(endMessage->target()));
            if (end_handler_) {
                end_handler_(*endMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for OtaEnd");
            }
        }
        return true;
    }
    case midi2pwm::ota::Message::OtaProgress: {
        const auto *progressMessage = deserializedEnvelope->message_as_OtaProgress();
        if (progressMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received OtaProgress: target=%u status=%u received=%u/%u",
                             static_cast<unsigned int>(progressMessage->target()),
                             static_cast<unsigned int>(progressMessage->status()),
                             static_cast<unsigned int>(progressMessage->chunks_received()),
                             static_cast<unsigned int>(progressMessage->total_chunks()));
            if (progress_handler_) {
                progress_handler_(*progressMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for OtaProgress");
            }
        }
        return true;
    }
    case midi2pwm::ota::Message::OtaAbort: {
        const auto *abortMessage = deserializedEnvelope->message_as_OtaAbort();
        if (abortMessage) {
            LIBCOMM_LOG_INFO(TAG, "Received OtaAbort: target=%u reason=%s",
                             static_cast<unsigned int>(abortMessage->target()),
                             abortMessage->reason() ? abortMessage->reason()->c_str() : "none");
            if (abort_handler_) {
                abort_handler_(*abortMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for OtaAbort");
            }
        }
        return true;
    }
    default:
        LIBCOMM_LOG_ERROR(TAG, "Unknown OTA message type: %u",
                          static_cast<unsigned int>(deserializedEnvelope->message_type()));
        return false;
    }
}

namespace
{

flatbuffers::DetachedBuffer buildSerializedOtaMessageEnvelope(
    flatbuffers::FlatBufferBuilder &flatBuffersBuilder,
    midi2pwm::ota::Message messageType,
    flatbuffers::Offset<void> serializedPayloadOffset)
{
    auto serializedEnvelope = midi2pwm::ota::CreateEnvelope(flatBuffersBuilder, messageType, serializedPayloadOffset);
    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::ota::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace

flatbuffers::DetachedBuffer BuildOtaBeginMessage(midi2pwm::ota::Target target,
                                                  std::uint32_t firmwareSize,
                                                  std::uint32_t firmwareCrc32,
                                                  const char* versionString,
                                                  std::uint16_t totalChunks)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto versionOffset = versionString ? flatBuffersBuilder.CreateString(versionString)
                                       : flatbuffers::Offset<flatbuffers::String>();

    auto serializedBeginMessage = midi2pwm::ota::CreateOtaBegin(
        flatBuffersBuilder, target, firmwareSize, firmwareCrc32, versionOffset, totalChunks);

    return buildSerializedOtaMessageEnvelope(
        flatBuffersBuilder, midi2pwm::ota::Message::OtaBegin, serializedBeginMessage.Union());
}

flatbuffers::DetachedBuffer BuildOtaDataMessage(midi2pwm::ota::Target target,
                                                 std::uint16_t chunkIndex,
                                                 const std::uint8_t* data,
                                                 std::size_t dataSize)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto dataOffset = (data && dataSize > 0U) ? flatBuffersBuilder.CreateVector(data, dataSize)
                                              : flatbuffers::Offset<flatbuffers::Vector<uint8_t>>();

    auto serializedDataMessage = midi2pwm::ota::CreateOtaData(
        flatBuffersBuilder, target, chunkIndex, dataOffset);

    return buildSerializedOtaMessageEnvelope(
        flatBuffersBuilder, midi2pwm::ota::Message::OtaData, serializedDataMessage.Union());
}

flatbuffers::DetachedBuffer BuildOtaEndMessage(midi2pwm::ota::Target target)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto serializedEndMessage = midi2pwm::ota::CreateOtaEnd(flatBuffersBuilder, target);

    return buildSerializedOtaMessageEnvelope(
        flatBuffersBuilder, midi2pwm::ota::Message::OtaEnd, serializedEndMessage.Union());
}

flatbuffers::DetachedBuffer BuildOtaProgressMessage(midi2pwm::ota::Target target,
                                                     midi2pwm::ota::OtaStatus status,
                                                     std::uint16_t chunksReceived,
                                                     std::uint16_t totalChunks,
                                                     const char* errorMessage)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto errorOffset = errorMessage ? flatBuffersBuilder.CreateString(errorMessage)
                                    : flatbuffers::Offset<flatbuffers::String>();

    auto serializedProgressMessage = midi2pwm::ota::CreateOtaProgress(
        flatBuffersBuilder, target, status, chunksReceived, totalChunks, errorOffset);

    return buildSerializedOtaMessageEnvelope(
        flatBuffersBuilder, midi2pwm::ota::Message::OtaProgress, serializedProgressMessage.Union());
}

flatbuffers::DetachedBuffer BuildOtaAbortMessage(midi2pwm::ota::Target target,
                                                  const char* reason)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto reasonOffset = reason ? flatBuffersBuilder.CreateString(reason)
                               : flatbuffers::Offset<flatbuffers::String>();

    auto serializedAbortMessage = midi2pwm::ota::CreateOtaAbort(
        flatBuffersBuilder, target, reasonOffset);

    return buildSerializedOtaMessageEnvelope(
        flatBuffersBuilder, midi2pwm::ota::Message::OtaAbort, serializedAbortMessage.Union());
}

} // namespace libcomm
