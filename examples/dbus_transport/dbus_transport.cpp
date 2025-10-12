#include "dbus_transport.h"

#include <cstdio>

namespace libcomm::examples
{

namespace
{

void LogAndClearError(const char *context, DBusError *error)
{
    if (dbus_error_is_set(error)) {
        std::fprintf(stderr, "%s: %s - %s\n", context, error->name, error->message);
        dbus_error_free(error);
    }
}

bool AppendByteArray(DBusMessage *message, const std::uint8_t *data, std::size_t size)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(message, &iter);

    DBusMessageIter array_iter;
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &array_iter)) {
        return false;
    }

    void *mutable_data = const_cast<std::uint8_t *>(data);
    int length = static_cast<int>(size);
    if (!dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_BYTE, &mutable_data, length)) {
        dbus_message_iter_close_container(&iter, &array_iter);
        return false;
    }

    if (!dbus_message_iter_close_container(&iter, &array_iter)) {
        return false;
    }

    return true;
}

} // namespace

DBusClientTransport::DBusClientTransport(const DBusAddress &address)
    : address_(address)
{
    DBusError error;
    dbus_error_init(&error);
    connection_ = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection_) {
        LogAndClearError("DBusClientTransport: failed to connect to session bus", &error);
        return;
    }
    LogAndClearError("DBusClientTransport", &error);
    dbus_connection_set_exit_on_disconnect(connection_, FALSE);
}

DBusClientTransport::~DBusClientTransport()
{
    if (connection_) {
        dbus_connection_unref(connection_);
        connection_ = nullptr;
    }
}

bool DBusClientTransport::Valid() const
{
    return connection_ != nullptr;
}

Endpoint::WriteCallback DBusClientTransport::MakeWriteCallback() const
{
    return Endpoint::WriteCallback::create<const DBusClientTransport, &DBusClientTransport::Transmit>(*this);
}

bool DBusClientTransport::Transmit(const std::uint8_t *data, std::size_t size) const
{
    if (!connection_ || !data || size == 0U) {
        return false;
    }

    DBusMessage *message =
        dbus_message_new_method_call(address_.service, address_.object_path, address_.interface, address_.method);
    if (!message) {
        std::fprintf(stderr, "DBusClientTransport: failed to allocate message\n");
        return false;
    }

    if (!AppendByteArray(message, data, size)) {
        std::fprintf(stderr, "DBusClientTransport: failed to append payload\n");
        dbus_message_unref(message);
        return false;
    }

    DBusError error;
    dbus_error_init(&error);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection_, message, -1, &error);
    dbus_message_unref(message);

    if (!reply) {
        LogAndClearError("DBusClientTransport: send failed", &error);
        return false;
    }

    dbus_error_init(&error);
    dbus_bool_t success = FALSE;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID)) {
        LogAndClearError("DBusClientTransport: failed to parse reply", &error);
        dbus_message_unref(reply);
        return false;
    }

    dbus_message_unref(reply);
    return success == TRUE;
}

DBusServerTransport::DBusServerTransport(const DBusAddress &address)
    : address_(address)
{
}

DBusServerTransport::~DBusServerTransport()
{
    if (connection_) {
        dbus_connection_unregister_object_path(connection_, address_.object_path);
        dbus_connection_unref(connection_);
        connection_ = nullptr;
    }
}

bool DBusServerTransport::Start(RawMessageHandler handler)
{
    handler_ = handler;

    DBusError error;
    dbus_error_init(&error);
    connection_ = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection_) {
        LogAndClearError("DBusServerTransport: failed to connect to session bus", &error);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(connection_, FALSE);

    const int request_name_result =
        dbus_bus_request_name(connection_, address_.service, DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error)) {
        LogAndClearError("DBusServerTransport: failed to request name", &error);
        return false;
    }
    if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        std::fprintf(stderr, "DBusServerTransport: name already taken\n");
        return false;
    }

    static DBusObjectPathVTable vtable = {
        nullptr, &DBusServerTransport::HandleMessage, nullptr, nullptr, nullptr, nullptr};

    if (!dbus_connection_register_object_path(connection_, address_.object_path, &vtable, this)) {
        std::fprintf(stderr, "DBusServerTransport: failed to register object path\n");
        return false;
    }

    return true;
}

void DBusServerTransport::RunWhile(RunPredicate keep_running) const
{
    if (!connection_) {
        return;
    }

    while (!keep_running || keep_running()) {
        dbus_connection_read_write(connection_, 100);

        while (dbus_connection_dispatch(connection_) == DBUS_DISPATCH_DATA_REMAINS) {
            // Drain all queued messages.
        }
    }
}

void DBusServerTransport::RunForever() const
{
    RunWhile(nullptr);
}

DBusHandlerResult DBusServerTransport::HandleMessage(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    auto *self = static_cast<DBusServerTransport *>(user_data);
    if (!self || !message) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (!dbus_message_is_method_call(message, self->address_.interface, self->address_.method)) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const std::uint8_t *payload = nullptr;
    std::size_t payload_size = 0U;
    DBusMessageIter iter;
    if (dbus_message_iter_init(message, &iter)) {
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter array_iter;
            dbus_message_iter_recurse(&iter, &array_iter);
            if (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_BYTE) {
                void *raw = nullptr;
                int length = 0;
                dbus_message_iter_get_fixed_array(&array_iter, &raw, &length);
                if (raw && length > 0) {
                    payload = static_cast<std::uint8_t *>(raw);
                    payload_size = static_cast<std::size_t>(length);
                }
            }
        }
    }

    bool handled = false;
    if (payload && (payload_size > 0U) && self->handler_) {
        handled = self->handler_(payload, payload_size);
    }

    DBusMessage *reply = dbus_message_new_method_return(message);
    if (!reply) {
        std::fprintf(stderr, "DBusServerTransport: failed to allocate reply\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    dbus_bool_t result = handled ? TRUE : FALSE;
    if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &result, DBUS_TYPE_INVALID)) {
        dbus_message_unref(reply);
        std::fprintf(stderr, "DBusServerTransport: failed to append reply data\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    if (!dbus_connection_send(connection, reply, nullptr)) {
        dbus_message_unref(reply);
        std::fprintf(stderr, "DBusServerTransport: failed to send reply\n");
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_connection_flush(connection);
    dbus_message_unref(reply);

    return DBUS_HANDLER_RESULT_HANDLED;
}

} // namespace libcomm::examples
