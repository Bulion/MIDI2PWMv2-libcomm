#include "libcomm/log_endpoint.h"
#include "libcomm/logging.h"

#include "flatbuffers/verifier.h"

namespace libcomm
{

static constexpr const char* TAG = "LogEndpoint";

LogEndpoint::LogEndpoint(WriteCallback writeCallback)
    : transport_(writeCallback)
{
}

bool LogEndpoint::Send(flatbuffers::DetachedBuffer &&serializedMessageBuffer)
{
    const std::uint8_t *bufferData = serializedMessageBuffer.data();
    std::size_t bufferSizeBytes = serializedMessageBuffer.size();

    return transport_.Send(bufferData, bufferSizeBytes);
}

bool LogEndpoint::HandleIncoming(const std::uint8_t *receivedData, std::size_t receivedSizeBytes)
{
    auto handler = FrameTransport::DataHandler::create<LogEndpoint, &LogEndpoint::HandleFrame>(*this);
    return transport_.HandleIncoming(receivedData, receivedSizeBytes, handler);
}

void LogEndpoint::OnLogForward(LogForwardHandler callbackHandler)
{
    log_forward_handler_ = callbackHandler;
}

bool LogEndpoint::HandleFrame(const std::uint8_t *framePayloadData, std::size_t framePayloadSizeBytes) const
{
    if (!framePayloadData || framePayloadSizeBytes == 0U) {
        LIBCOMM_LOG_ERROR(TAG, "Invalid frame: null data or zero size");
        return false;
    }

    bool hasValidFlatBuffersIdentifier = midi2pwm::log::EnvelopeBufferHasIdentifier(framePayloadData);
    if (!hasValidFlatBuffersIdentifier) {
        return false;
    }

    flatbuffers::Verifier flatBuffersVerifier(framePayloadData, framePayloadSizeBytes);
    bool envelopeIsValid = midi2pwm::log::VerifyEnvelopeBuffer(flatBuffersVerifier);
    if (!envelopeIsValid) {
        LIBCOMM_LOG_ERROR(TAG, "FlatBuffers verification failed for Log envelope");
        return false;
    }

    const auto *deserializedEnvelope = midi2pwm::log::GetEnvelope(framePayloadData);
    if (!deserializedEnvelope) {
        LIBCOMM_LOG_ERROR(TAG, "Failed to deserialize Log envelope");
        return false;
    }

    switch (deserializedEnvelope->message_type()) {
    case midi2pwm::log::Message::LogForward: {
        const auto *logForwardMessage = deserializedEnvelope->message_as_LogForward();
        if (logForwardMessage) {
            LIBCOMM_LOG_DEBUG(TAG, "Received LogForward: level=%u", static_cast<unsigned int>(logForwardMessage->level()));
            if (log_forward_handler_) {
                log_forward_handler_(*logForwardMessage);
            } else {
                LIBCOMM_LOG_WARN(TAG, "No handler registered for LogForward");
            }
        }
        return true;
    }
    default:
        LIBCOMM_LOG_ERROR(TAG, "Unknown Log message type: %u", static_cast<unsigned int>(deserializedEnvelope->message_type()));
        return false;
    }
}

flatbuffers::DetachedBuffer BuildLogForwardMessage(
    midi2pwm::log::LogLevel level,
    const char* tag,
    const char* message)
{
    flatbuffers::FlatBufferBuilder flatBuffersBuilder;

    auto tagOffset = flatBuffersBuilder.CreateString(tag ? tag : "");
    auto messageOffset = flatBuffersBuilder.CreateString(message ? message : "");

    auto serializedLogForward = midi2pwm::log::CreateLogForward(
        flatBuffersBuilder, level, tagOffset, messageOffset);
    auto serializedEnvelope = midi2pwm::log::CreateEnvelope(
        flatBuffersBuilder, midi2pwm::log::Message::LogForward, serializedLogForward.Union());

    flatBuffersBuilder.Finish(serializedEnvelope, midi2pwm::log::EnvelopeIdentifier());

    return flatBuffersBuilder.Release();
}

} // namespace libcomm
