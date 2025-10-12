#include "dbus_transport.h"
#include "libcomm/endpoint.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <time.h>
#include <utility>

namespace
{

const libcomm::examples::DBusAddress kAddress{
    "org.midi2pwm.libcomm", "/org/midi2pwm/libcomm/Endpoint", "org.midi2pwm.libcomm.Endpoint", "SendEnvelope"};

std::uint64_t TimestampNow()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    const std::uint64_t seconds = static_cast<std::uint64_t>(ts.tv_sec);
    const std::uint64_t nanos = static_cast<std::uint64_t>(ts.tv_nsec);
    return (seconds * 1000ULL) + (nanos / 1000000ULL);
}

void SleepMilliseconds(unsigned int milliseconds)
{
    struct timespec req;
    req.tv_sec = milliseconds / 1000U;
    req.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    nanosleep(&req, nullptr);
}

} // namespace

int main()
{
    libcomm::examples::DBusClientTransport client(kAddress);
    if (!client.Valid()) {
        std::fprintf(stderr, "[sender] Failed to initialise D-Bus client transport.\n");
        return 1;
    }

    libcomm::Endpoint endpoint(client.MakeWriteCallback());

    auto ping = libcomm::BuildPingMessage(1U, TimestampNow());
    if (!endpoint.Send(std::move(ping))) {
        std::fprintf(stderr, "[sender] Failed to send Ping message.\n");
        return 1;
    }
    std::printf("[sender] Ping message sent.\n");

    SleepMilliseconds(250U);

    auto control = libcomm::BuildControlChangeMessage(0U, 74U, 100U);
    if (!endpoint.Send(std::move(control))) {
        std::fprintf(stderr, "[sender] Failed to send ControlChange message.\n");
        return 1;
    }
    std::printf("[sender] ControlChange message sent.\n");

    return 0;
}
