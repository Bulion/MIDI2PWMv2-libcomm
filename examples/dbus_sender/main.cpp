#include "dbus_transport.h"
#include "libcomm/midi_endpoint.h"
#include "libcomm/pwm_endpoint.h"

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

    libcomm::MidiEndpoint endpoint(client.MakeWriteCallback(), true);
    libcomm::PwmEndpoint pwm_endpoint(client.MakeWriteCallback(), true);

    auto ping = libcomm::BuildPingMessage(1U, TimestampNow());
    if (!endpoint.Send(std::move(ping))) {
        std::fprintf(stderr, "[sender] Failed to send Ping message.\n");
        return 1;
    }
    std::printf("[sender] Ping message sent.\n");

    SleepMilliseconds(250U);

    auto note_on = libcomm::BuildNoteOnMessage(0U, 60U, 110U);
    if (!endpoint.Send(std::move(note_on))) {
        std::fprintf(stderr, "[sender] Failed to send NoteOn message.\n");
        return 1;
    }
    std::printf("[sender] NoteOn message sent.\n");

    SleepMilliseconds(50U);

    auto control = libcomm::BuildControlChangeMessage(0U, 74U, 100U);
    if (!endpoint.Send(std::move(control))) {
        std::fprintf(stderr, "[sender] Failed to send ControlChange message.\n");
        return 1;
    }
    std::printf("[sender] ControlChange message sent.\n");

    SleepMilliseconds(50U);

    auto realtime = libcomm::BuildSystemRealTimeMessage(midi2pwm::midi::SystemRealTimeType::TimingClock);
    if (!endpoint.Send(std::move(realtime))) {
        std::fprintf(stderr, "[sender] Failed to send SystemRealTime message.\n");
        return 1;
    }
    std::printf("[sender] SystemRealTime message sent.\n");

    const std::uint8_t manufacturer[] = {0x00, 0x20, 0x33};
    const std::uint8_t sysex_payload[] = {0x7D, 0x01, 0x02, 0x03};
    auto sysex =
        libcomm::BuildSystemExclusiveMessage(manufacturer, sizeof(manufacturer), sysex_payload, sizeof(sysex_payload));
    if (!endpoint.Send(std::move(sysex))) {
        std::fprintf(stderr, "[sender] Failed to send SysEx message.\n");
        return 1;
    }
    std::printf("[sender] SystemExclusive message sent.\n");

    SleepMilliseconds(50U);

    auto pwm_config = libcomm::BuildChannelConfigMessage(
        0U,
        midi2pwm::pwm::ChannelConfiguration::FullBridge,
        60U,
        0.5F,
        0.0F,
        1.0F);
    if (!pwm_endpoint.Send(std::move(pwm_config))) {
        std::fprintf(stderr, "[sender] Failed to send PWM ChannelConfig message.\n");
        return 1;
    }
    std::printf("[sender] PWM ChannelConfig message sent.\n");

    SleepMilliseconds(50U);

    auto pwm_telemetry = libcomm::BuildChannelTelemetryMessage(
        0U,
        midi2pwm::pwm::ChannelConfiguration::FullBridge,
        midi2pwm::pwm::ChannelStatus::Active,
        60U,
        12.3F,
        1.25F,
        0.75F,
        midi2pwm::pwm::Polarity::Forward,
        0.52F,
        0.05F,
        0.95F,
        false);
    if (!pwm_endpoint.Send(std::move(pwm_telemetry))) {
        std::fprintf(stderr, "[sender] Failed to send PWM telemetry message.\n");
        return 1;
    }
    std::printf("[sender] PWM telemetry message sent.\n");

    SleepMilliseconds(50U);

    const libcomm::FaultLogEntryData fault_entries[] = {
        {static_cast<std::uint32_t>(TimestampNow() & 0xFFFFFFFFULL), midi2pwm::pwm::FaultType::OverCurrent},
        {static_cast<std::uint32_t>((TimestampNow() + 42ULL) & 0xFFFFFFFFULL), midi2pwm::pwm::FaultType::Thermal},
    };
    auto pwm_fault_log = libcomm::BuildFaultLogMessage(2U, fault_entries, 2U);
    if (!pwm_endpoint.Send(std::move(pwm_fault_log))) {
        std::fprintf(stderr, "[sender] Failed to send PWM fault log message.\n");
        return 1;
    }
    std::printf("[sender] PWM fault log message sent.\n");

    SleepMilliseconds(50U);

    auto pwm_fault_control =
        libcomm::BuildFaultControlCommand(midi2pwm::pwm::FaultControlOperation::Reset);
    if (!pwm_endpoint.Send(std::move(pwm_fault_control))) {
        std::fprintf(stderr, "[sender] Failed to send PWM fault control command.\n");
        return 1;
    }
    std::printf("[sender] PWM fault control command sent.\n");

    return 0;
}
