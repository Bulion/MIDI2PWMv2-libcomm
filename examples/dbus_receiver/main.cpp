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

void PrintPing(const midi2pwm::comm::Ping &ping)
{
    std::printf(
        "[receiver] Ping received - sequence: %u, timestamp: %llu\n",
        static_cast<unsigned int>(ping.sequence()),
        static_cast<unsigned long long>(ping.timestamp()));
}

void PrintControlChange(const midi2pwm::comm::ControlChange &cc)
{
    std::printf(
        "[receiver] ControlChange received - channel: %u, controller: %u, value: %u\n",
        static_cast<unsigned int>(cc.channel()),
        static_cast<unsigned int>(cc.controller()),
        static_cast<unsigned int>(cc.value()));
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
    endpoint.OnControlChange(libcomm::Endpoint::ControlChangeHandler::create<&PrintControlChange>());

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
