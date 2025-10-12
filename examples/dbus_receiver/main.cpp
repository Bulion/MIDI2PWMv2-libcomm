#include "dbus_transport.h"
#include "libcomm/endpoint.h"
#include "libcomm/pwm_endpoint.h"

#include <csignal>
#include <cstdint>
#include <cstdio>

namespace
{

volatile bool g_running = true;

void HandleSignal(int)
{
    g_running = false;
}

const libcomm::examples::DBusAddress kAddress{
    "org.midi2pwm.libcomm", "/org/midi2pwm/libcomm/Endpoint", "org.midi2pwm.libcomm.Endpoint", "SendEnvelope"};

bool NullWrite(const std::uint8_t *, std::size_t)
{
    std::fprintf(stderr, "[libcomm] No outbound transport configured for receiver.\n");
    return false;
}

void PrintPing(const midi2pwm::midi::Ping &ping)
{
    std::printf(
        "[receiver] Ping received - sequence: %u, timestamp: %llu\n",
        static_cast<unsigned int>(ping.sequence()),
        static_cast<unsigned long long>(ping.timestamp()));
}

void PrintChannel(const midi2pwm::midi::ChannelMessage &message)
{
    std::printf(
        "[receiver] Channel message - type: %u, channel: %u, data1: %u, data2: %u\n",
        static_cast<unsigned int>(message.message_type()),
        static_cast<unsigned int>(message.channel()),
        static_cast<unsigned int>(message.data1()),
        static_cast<unsigned int>(message.data2()));
}

void PrintSystemCommon(const midi2pwm::midi::SystemCommonMessage &message)
{
    std::printf(
        "[receiver] System Common message - type: %u, value: %u\n",
        static_cast<unsigned int>(message.message_type()),
        static_cast<unsigned int>(message.value()));
}

void PrintSystemRealTime(const midi2pwm::midi::SystemRealTimeMessage &message)
{
    std::printf("[receiver] System Real-Time message - type: %u\n", static_cast<unsigned int>(message.message_type()));
}

void PrintSystemExclusive(const midi2pwm::midi::SystemExclusiveMessage &message)
{
    const auto *manufacturer = message.manufacturer_id();
    const auto *payload = message.payload();

    std::printf(
        "[receiver] SysEx message - manufacturer length: %u, payload length: %u\n",
        manufacturer ? static_cast<unsigned int>(manufacturer->size()) : 0U,
        payload ? static_cast<unsigned int>(payload->size()) : 0U);
}

void PrintChannelTelemetry(const midi2pwm::pwm::ChannelTelemetry &telemetry)
{
    std::printf(
        "[receiver] PWM telemetry - channel: %u, config: %u, status: %u, voltage: %.2f, current: %.2f, "
        "midpoint: %.2f, min: %.2f, max: %.2f, had_fault: %s\n",
        static_cast<unsigned int>(telemetry.channel_number()),
        static_cast<unsigned int>(telemetry.configuration()),
        static_cast<unsigned int>(telemetry.status()),
        telemetry.voltage(),
        telemetry.current(),
        telemetry.midpoint(),
        telemetry.min_point(),
        telemetry.max_point(),
        telemetry.had_fault() ? "true" : "false");
}

void PrintChannelConfig(const midi2pwm::pwm::ChannelConfig &config)
{
    std::printf(
        "[receiver] PWM config - channel: %u, config: %u, note: %u, midpoint: %.2f, min: %.2f, max: %.2f\n",
        static_cast<unsigned int>(config.channel_number()),
        static_cast<unsigned int>(config.configuration()),
        static_cast<unsigned int>(config.note()),
        config.midpoint(),
        config.min_point(),
        config.max_point());
}

void PrintFaultLog(const midi2pwm::pwm::FaultLog &log)
{
    const auto *entries = log.entries();
    std::printf("[receiver] PWM fault log - size: %u, reported entries: %u\n",
                static_cast<unsigned int>(log.log_size()),
                entries ? static_cast<unsigned int>(entries->size()) : 0U);
    if (entries) {
        for (flatbuffers::uoffset_t i = 0; i < entries->size(); ++i) {
            const auto *entry = entries->Get(i);
            if (!entry) {
                continue;
            }
            std::printf("  - entry[%u]: timestamp=%u, fault=%u\n",
                        static_cast<unsigned int>(i),
                        entry->timestamp_ms(),
                        static_cast<unsigned int>(entry->fault()));
        }
    }
}

void PrintFaultControl(const midi2pwm::pwm::FaultControlCommand &command)
{
    std::printf("[receiver] PWM fault control command - operation: %u\n",
                static_cast<unsigned int>(command.operation()));
}

bool KeepRunning()
{
    return g_running;
}

} // namespace

struct CombinedHandler
{
    libcomm::Endpoint &midi;
    libcomm::PwmEndpoint &pwm;

    bool operator()(const std::uint8_t *data, std::size_t size) const
    {
        if (midi.HandleIncoming(data, size)) {
            return true;
        }
        return pwm.HandleIncoming(data, size);
    }
};

int main()
{
    std::signal(SIGINT, HandleSignal);

    libcomm::examples::DBusServerTransport server(kAddress);
    libcomm::Endpoint endpoint(libcomm::Endpoint::WriteCallback::create<&NullWrite>());
    libcomm::PwmEndpoint pwm_endpoint(libcomm::PwmEndpoint::WriteCallback::create<&NullWrite>());

    endpoint.OnPing(libcomm::Endpoint::PingHandler::create<&PrintPing>());
    endpoint.OnChannelMessage(libcomm::Endpoint::ChannelMessageHandler::create<&PrintChannel>());
    endpoint.OnSystemCommon(libcomm::Endpoint::SystemCommonHandler::create<&PrintSystemCommon>());
    endpoint.OnSystemRealTime(libcomm::Endpoint::SystemRealTimeHandler::create<&PrintSystemRealTime>());
    endpoint.OnSystemExclusive(libcomm::Endpoint::SystemExclusiveHandler::create<&PrintSystemExclusive>());

    pwm_endpoint.OnChannelTelemetry(libcomm::PwmEndpoint::ChannelTelemetryHandler::create<&PrintChannelTelemetry>());
    pwm_endpoint.OnChannelConfig(libcomm::PwmEndpoint::ChannelConfigHandler::create<&PrintChannelConfig>());
    pwm_endpoint.OnFaultLog(libcomm::PwmEndpoint::FaultLogHandler::create<&PrintFaultLog>());
    pwm_endpoint.OnFaultControl(libcomm::PwmEndpoint::FaultControlHandler::create<&PrintFaultControl>());

    CombinedHandler handler{endpoint, pwm_endpoint};

    if (!server.Start(libcomm::examples::DBusServerTransport::RawMessageHandler::create(handler))) {
        std::fprintf(stderr, "[receiver] Failed to start D-Bus server transport.\n");
        return 1;
    }

    std::printf("[receiver] Listening on D-Bus. Press Ctrl+C to stop.\n");
    server.RunWhile(&KeepRunning);
    std::printf("[receiver] Shutting down.\n");

    return 0;
}
