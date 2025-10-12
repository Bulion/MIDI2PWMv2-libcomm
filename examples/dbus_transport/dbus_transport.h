#pragma once

#include "libcomm/endpoint.h"

#include <cstddef>
#include <cstdint>
#include <dbus/dbus.h>
#include <etl/delegate.h>

namespace libcomm::examples
{

struct DBusAddress {
    const char *service;
    const char *object_path;
    const char *interface;
    const char *method;
};

class DBusClientTransport
{
public:
    explicit DBusClientTransport(const DBusAddress &address);
    ~DBusClientTransport();

    DBusClientTransport(const DBusClientTransport &) = delete;
    DBusClientTransport &operator=(const DBusClientTransport &) = delete;
    DBusClientTransport(DBusClientTransport &&) noexcept = delete;
    DBusClientTransport &operator=(DBusClientTransport &&) noexcept = delete;

    bool Valid() const;
    Endpoint::WriteCallback MakeWriteCallback() const;

private:
    bool Transmit(const std::uint8_t *data, std::size_t size) const;

    DBusConnection *connection_{nullptr};
    DBusAddress address_;
};

class DBusServerTransport
{
public:
    using RawMessageHandler = etl::delegate<bool(const std::uint8_t *, std::size_t)>;
    using RunPredicate = bool (*)(void);

    explicit DBusServerTransport(const DBusAddress &address);
    ~DBusServerTransport();

    DBusServerTransport(const DBusServerTransport &) = delete;
    DBusServerTransport &operator=(const DBusServerTransport &) = delete;
    DBusServerTransport(DBusServerTransport &&) noexcept = delete;
    DBusServerTransport &operator=(DBusServerTransport &&) noexcept = delete;

    bool Start(RawMessageHandler handler);
    void RunWhile(RunPredicate keep_running) const;
    void RunForever() const;

private:
    static DBusHandlerResult HandleMessage(DBusConnection *connection, DBusMessage *message, void *user_data);

    DBusConnection *connection_{nullptr};
    DBusAddress address_;
    RawMessageHandler handler_;
};

} // namespace libcomm::examples
