#pragma once

#include "comm_generated.h"
#include "flatbuffers/flatbuffers.h"

#include <cstddef>
#include <cstdint>
#include <etl/delegate.h>

namespace libcomm
{

class Endpoint
{
public:
    using WriteCallback = etl::delegate<bool(const std::uint8_t *, std::size_t)>;
    using PingHandler = etl::delegate<void(const midi2pwm::comm::Ping &)>;
    using ControlChangeHandler = etl::delegate<void(const midi2pwm::comm::ControlChange &)>;

    explicit Endpoint(WriteCallback write);

    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;
    ~Endpoint() = default;

    bool Send(flatbuffers::DetachedBuffer &&buffer) const;
    bool HandleIncoming(const std::uint8_t *data, std::size_t size) const;

    void OnPing(PingHandler handler);
    void OnControlChange(ControlChangeHandler handler);

private:
    WriteCallback write_;
    PingHandler ping_handler_;
    ControlChangeHandler control_change_handler_;
};

flatbuffers::DetachedBuffer BuildPingMessage(std::uint32_t sequence, std::uint64_t timestamp);
flatbuffers::DetachedBuffer
BuildControlChangeMessage(std::uint8_t channel, std::uint8_t controller, std::uint8_t value);

} // namespace libcomm
