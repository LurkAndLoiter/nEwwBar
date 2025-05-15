/*
 * Copyright 2025 LurkAndLoiter.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // For sleep()

void check_dbus_error(DBusError *err, const char *context) {
    if (dbus_error_is_set(err)) {
        fprintf(stderr, "%s: %s\n", context, err->message);
        dbus_error_free(err);
        exit(1);
    }
}

dbus_bool_t get_discovering_state(DBusConnection *conn, const char *adapter_path) {
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusError err;
    dbus_bool_t discovering = FALSE;
    DBusMessageIter args;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call(
        "org.bluez",
        adapter_path,
        "org.freedesktop.DBus.Properties",
        "Get"
    );
    if (!msg) {
        fprintf(stderr, "Failed to create Discovering property message\n");
        return FALSE;
    }

    const char *interface = "org.bluez.Adapter1";
    const char *property = "Discovering";
    dbus_message_append_args(msg,
        DBUS_TYPE_STRING, &interface,
        DBUS_TYPE_STRING, &property,
        DBUS_TYPE_INVALID);

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply) {
        fprintf(stderr, "Failed to get Discovering: %s\n", err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    if (dbus_message_iter_init(reply, &args)) {
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            dbus_message_iter_recurse(&args, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN) {
                dbus_message_iter_get_basic(&variant, &discovering);
            }
        }
    }

    dbus_message_unref(reply);
    return discovering;
}

int call_start_discovery(DBusConnection *conn, const char *adapter_path) {
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusError err;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call(
        "org.bluez",
        adapter_path,
        "org.bluez.Adapter1",
        "StartDiscovery"
    );
    if (!msg) {
        fprintf(stderr, "Failed to create StartDiscovery message\n");
        return 1;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 3000, &err);
    dbus_message_unref(msg);
    if (!reply) {
        fprintf(stderr, "StartDiscovery failed: %s\n", err.message);
        dbus_error_free(&err);
        return 1;
    }

    dbus_message_unref(reply);
    return 0;
}

int main(int argc, char *argv[]) {
    DBusConnection *conn = NULL;
    DBusError err;
    const char *adapter_path = "/org/bluez/hci0"; // Adjust if needed
    int duration;

    // Check command-line argument
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <seconds>\n", argv[0]);
        return 1;
    }

    duration = atoi(argv[1]);
    if (duration <= 0) {
        fprintf(stderr, "Invalid duration: %s (must be a positive integer)\n", argv[1]);
        return 1;
    }

    // Initialize D-Bus error
    dbus_error_init(&err);

    // Connect to system bus
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    check_dbus_error(&err, "Connection Error");
    if (!conn) {
        fprintf(stderr, "Connection Null\n");
        return 1;
    }

    // Call StartDiscovery
    printf("Starting discovery for %d seconds...\n", duration);
    if (call_start_discovery(conn, adapter_path) != 0) {
        dbus_connection_unref(conn);
        return 1;
    }

    // Check initial state
    dbus_bool_t discovering = get_discovering_state(conn, adapter_path);
    printf("Adapter %s: Discovering = %s\n", adapter_path, discovering ? "true" : "false");

    // Keep program running for specified duration, checking state periodically
    for (int i = 0; i < duration; i++) {
        sleep(1);
        discovering = get_discovering_state(conn, adapter_path);
        printf("Adapter %s after %d/%d seconds: Discovering = %s\n",
               adapter_path, i + 1, duration, discovering ? "true" : "false");
        if (!discovering) {
            printf("Discovery stopped unexpectedly\n");
            break;
        }
    }

    // Clean up
    dbus_connection_unref(conn);
    printf("Discovery completed\n");
    return 0;
}
