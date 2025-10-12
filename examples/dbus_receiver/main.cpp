#include "dbus_transport.h"
#include "libcomm/endpoint.h"

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

bool KeepRunning()
{
    return g_running;
}

} // namespace

int main()
{
    std::signal(SIGINT, HandleSignal);

    libcomm::examples::DBusServerTransport server(kAddress);
    libcomm::Endpoint endpoint(libcomm::Endpoint::WriteCallback::create<&NullWrite>());

    endpoint.OnPing(libcomm::Endpoint::PingHandler::create<&PrintPing>());
    endpoint.OnChannelMessage(libcomm::Endpoint::ChannelMessageHandler::create<&PrintChannel>());
    endpoint.OnSystemCommon(libcomm::Endpoint::SystemCommonHandler::create<&PrintSystemCommon>());
    endpoint.OnSystemRealTime(libcomm::Endpoint::SystemRealTimeHandler::create<&PrintSystemRealTime>());
    endpoint.OnSystemExclusive(libcomm::Endpoint::SystemExclusiveHandler::create<&PrintSystemExclusive>());

    if (!server.Start(libcomm::examples::DBusServerTransport::RawMessageHandler::
                          create<const libcomm::Endpoint, &libcomm::Endpoint::HandleIncoming>(endpoint))) {
        std::fprintf(stderr, "[receiver] Failed to start D-Bus server transport.\n");
        return 1;
    }

    std::printf("[receiver] Listening on D-Bus. Press Ctrl+C to stop.\n");
    server.RunWhile(&KeepRunning);
    std::printf("[receiver] Shutting down.\n");

    return 0;
}
