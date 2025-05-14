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

#ifndef DEBUG
#define DEBUG 0
#endif

#include <stdio.h>
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define BLUEZ_SERVICE "org.bluez"
#define BLUEZ_PATH "/org/bluez/hci0"
#define BLUEZ_ADAPTER_INTERFACE "org.bluez.Adapter1"

static void log_debug(const char *message) {
#if DEBUG
  fprintf(stderr, "DEBUG: %s\n", message);
  fflush(stderr);
#endif
}

static void log_error(const char *message, DBusError *err) {
#if DEBUG
  if (err && dbus_error_is_set(err)) {
    fprintf(stderr, "ERROR: %s: %s\n", message, err->message);
  } else {
    fprintf(stderr, "ERROR: %s\n", message);
  }
  fflush(stderr);
#endif
}

static bool check_powered_state(DBusConnection *conn) {
  log_debug("Checking Powered state");
  if (!conn) {
    log_error("No D-Bus connection", NULL);
    return false;  // Error state
  }

  DBusMessage *msg = dbus_message_new_method_call(
      BLUEZ_SERVICE, BLUEZ_PATH, "org.freedesktop.DBus.Properties", "Get");
  if (!msg) {
    log_error("Failed to create Get message", NULL);
    return false;
  }

  const char *interface = BLUEZ_ADAPTER_INTERFACE;
  const char *prop = "Powered";
  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface,
                                DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
    log_error("Failed to append args to Get message", NULL);
    dbus_message_unref(msg);
    return false;
  }

  DBusPendingCall *pending = NULL;
  if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    log_error("Failed to send Get message", NULL);
    dbus_message_unref(msg);
    return false;
  }

  dbus_connection_flush(conn);
  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  if (!reply) {
    log_error("No reply from Properties.Get", NULL);
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    return false;
  }

  bool powered = false;  // Default to false for error or off
  DBusMessageIter args;
  if (dbus_message_iter_init(reply, &args)) {
    DBusMessageIter variant;
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse(&args, &variant);
      if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t value;
        dbus_message_iter_get_basic(&variant, &value);
        powered = value;
        log_debug(powered ? "Adapter is powered" : "Adapter is not powered");
      } else {
        log_error("Powered is not a boolean", NULL);
      }
    } else {
      log_error("Reply is not a variant", NULL);
    }
  } else {
    log_error("Failed to initialize reply iterator", NULL);
  }

  dbus_message_unref(reply);
  dbus_pending_call_unref(pending);
  dbus_message_unref(msg);
  return powered;
}

int main() {
  log_debug("Starting program");
  DBusError err;
  dbus_error_init(&err);

  log_debug("Connecting to system bus");
  DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (!conn) {
    log_error("Failed to connect to system bus", &err);
    dbus_error_free(&err);
    return 1;
  }
  log_debug("Connected to system bus");

  bool last_state = check_powered_state(conn);
  printf("%s\n", last_state ? "true" : "false");
  fflush(stdout);

  char match_rule[256];
  snprintf(match_rule, sizeof(match_rule),
           "type='signal',interface='org.freedesktop.DBus.Properties',"
           "path='%s',member='PropertiesChanged'",
           BLUEZ_PATH);
  log_debug("Adding match rule");
  dbus_bus_add_match(conn, match_rule, &err);
  if (dbus_error_is_set(&err)) {
    log_error("Failed to add match rule", &err);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
    return 1;
  }

  log_debug("Entering main loop");
  while (dbus_connection_read_write(conn, -1)) {
    DBusMessage *msg;
    while ((msg = dbus_connection_pop_message(conn)) != NULL) {
      if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged")) {
        log_debug("Received PropertiesChanged signal");
        bool new_state = check_powered_state(conn);
        if (new_state != last_state) {
          last_state = new_state;
          printf("%s\n", new_state ? "true" : "false");
          fflush(stdout);
        }
      }
      dbus_message_unref(msg);
    }
  }

  log_debug("Cleaning up");
  dbus_connection_unref(conn);
  dbus_error_free(&err);
  return 0;
}
