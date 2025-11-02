/*  _               _        _              _ _          _ _
 * | |   _   _ _ __| | __   / \   _ __   __| | |    ___ (_) |_ ___ _ __
 * | |  | | | | '__| |/ /  / _ \ | '_ \ / _` | |   / _ \| | __/ _ \ '__|
 * | |__| |_| | |  |   <  / ___ \| | | | (_| | |__| (_) | | ||  __/ |
 * |_____\__,_|_|  |_|\_\/_/   \_\_| |_|\__,_|_____\___/|_|\__\___|_|
 * ____________________________________________________________________________
 * ----------------------------------------------------------------------------
 * Copyright 2025 LurkAndLoiter.
 * ____________________________________________________________________________
 *  __  __ ___ _____   _     _
 * |  \/  |_ _|_   _| | |   (_) ___ ___ _ __  ___  ___
 * | |\/| || |  | |   | |   | |/ __/ _ \ '_ \/ __|/ _ \
 * | |  | || |  | |   | |___| | (_|  __/ | | \__ \  __/
 * |_|  |_|___| |_|   |_____|_|\___\___|_| |_|___/\___|
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
 * ____________________________________________________________________________
 * ----------------------------------------------------------------------------
 * "Zetus Lupetus" "Omelette du fromage" "You're killing me smalls" "Ugh As If"
 * "Hey. Listen!" "Do a barrel roll!" "Dear Darla, I hate your stinking guts."
 * "If we listen to each other's hearts. We'll find we're never too far apart."
 * ____________________________________________________________________________
 */

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLUEZ_SERVICE "org.bluez"
#define ADAPTER_PATH "/org/bluez/hci0"
static const char *AGENT_PATH = "/org/example/agent";
#define TIMEOUT 30000 // 30 seconds timeout for D-Bus calls

// Function to send D-Bus method calls
DBusMessage *send_dbus_method_call(DBusConnection *conn, const char *dest,
                                   const char *path, const char *interface,
                                   const char *method) {
  (void)conn; // suppress unused paramater warning
  DBusMessage *msg =
      dbus_message_new_method_call(dest, path, interface, method);
  if (!msg) {
    fprintf(stderr, "Error: Cannot create D-Bus message\n");
    return NULL;
  }
  return msg;
}

// Function to get property value via D-Bus
dbus_bool_t get_property(DBusConnection *conn, const char *path,
                         const char *interface, const char *property,
                         DBusMessageIter *iter) {
  DBusMessage *msg = send_dbus_method_call(
      conn, BLUEZ_SERVICE, path, "org.freedesktop.DBus.Properties", "Get");
  if (!msg)
    return FALSE;

  DBusMessageIter args;
  dbus_message_iter_init_append(msg, &args);
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface))
    goto fail;
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property))
    goto fail;

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error getting %s: %s\n", property, err.message);
    dbus_error_free(&err);
    return FALSE;
  }

  if (!dbus_message_iter_init(reply, &args) ||
      dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) {
    fprintf(stderr, "Error: Invalid reply for %s\n", property);
    dbus_message_unref(reply);
    return FALSE;
  }

  dbus_message_iter_recurse(&args, iter);
  dbus_message_unref(reply);
  return TRUE;

fail:
  dbus_message_unref(msg);
  return FALSE;
}

// Function to set property via D-Bus
dbus_bool_t set_property(DBusConnection *conn, const char *path,
                         const char *interface, const char *property, int type,
                         void *value) {
  DBusMessage *msg = send_dbus_method_call(
      conn, BLUEZ_SERVICE, path, "org.freedesktop.DBus.Properties", "Set");
  if (!msg)
    return FALSE;

  DBusMessageIter args, variant;
  dbus_message_iter_init_append(msg, &args);
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface))
    goto fail;
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property))
    goto fail;
  if (!dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT,
                                        type == DBUS_TYPE_BOOLEAN ? "b" : "s",
                                        &variant))
    goto fail;
  if (!dbus_message_iter_append_basic(&variant, type, value))
    goto fail;
  if (!dbus_message_iter_close_container(&args, &variant))
    goto fail;

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error setting %s: %s\n", property, err.message);
    dbus_error_free(&err);
    return FALSE;
  }
  dbus_message_unref(reply);
  return TRUE;

fail:
  dbus_message_unref(msg);
  return FALSE;
}

// Function to check and set adapter properties
int configure_adapter(DBusConnection *conn) {
  DBusMessageIter iter;
  dbus_bool_t value;

  // Check if adapter is powered
  if (get_property(conn, ADAPTER_PATH, "org.bluez.Adapter1", "Powered",
                   &iter)) {
    dbus_message_iter_get_basic(&iter, &value);
    if (!value) {
      printf("Powering on adapter...\n");
      value = TRUE;
      if (!set_property(conn, ADAPTER_PATH, "org.bluez.Adapter1", "Powered",
                        DBUS_TYPE_BOOLEAN, &value)) {
        return -1;
      }
    }
  } else {
    return -1;
  }

  // Check if adapter is pairable
  if (get_property(conn, ADAPTER_PATH, "org.bluez.Adapter1", "Pairable",
                   &iter)) {
    dbus_message_iter_get_basic(&iter, &value);
    if (!value) {
      printf("Setting adapter to pairable...\n");
      value = TRUE;
      if (!set_property(conn, ADAPTER_PATH, "org.bluez.Adapter1", "Pairable",
                        DBUS_TYPE_BOOLEAN, &value)) {
        return -1;
      }
    }
  } else {
    return -1;
  }

  // Check if adapter is discoverable
  if (get_property(conn, ADAPTER_PATH, "org.bluez.Adapter1", "Discoverable",
                   &iter)) {
    dbus_message_iter_get_basic(&iter, &value);
    if (!value) {
      printf("Setting adapter to discoverable...\n");
      value = TRUE;
      if (!set_property(conn, ADAPTER_PATH, "org.bluez.Adapter1",
                        "Discoverable", DBUS_TYPE_BOOLEAN, &value)) {
        return -1;
      }
    }
  } else {
    return -1;
  }

  return 0;
}

// Agent signal handler for auto-accepting pairing requests
DBusHandlerResult agent_message_handler(DBusConnection *conn, DBusMessage *msg,
                                        void *data) {
  (void)data; // suppress unused paramater warning
  const char *interface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);

  if (strcmp(interface, "org.bluez.AgentManager1") != 0)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  DBusMessage *reply = NULL;
  if (strcmp(member, "RequestPinCode") == 0) {
    printf("Auto-accepting PIN code request\n");
    reply = dbus_message_new_method_return(msg);
    const char *pin = "0000"; // Default PIN (modify as needed)
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID);
  } else if (strcmp(member, "RequestPasskey") == 0) {
    printf("Auto-accepting passkey request\n");
    reply = dbus_message_new_method_return(msg);
    dbus_uint32_t passkey = 0; // Default passkey
    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &passkey,
                             DBUS_TYPE_INVALID);
  } else if (strcmp(member, "RequestConfirmation") == 0) {
    printf("Auto-accepting confirmation\n");
    reply = dbus_message_new_method_return(msg);
  } else if (strcmp(member, "RequestAuthorization") == 0) {
    printf("Auto-accepting authorization\n");
    reply = dbus_message_new_method_return(msg);
  } else if (strcmp(member, "AuthorizeService") == 0) {
    printf("Auto-accepting service authorization\n");
    reply = dbus_message_new_method_return(msg);
  }

  if (reply) {
    dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Register agent with BlueZ
int register_agent(DBusConnection *conn) {
  // Verify AGENT_PATH
  if (!AGENT_PATH || strlen(AGENT_PATH) == 0) {
    fprintf(stderr, "Error: AGENT_PATH is invalid or empty\n");
    return -1;
  }

  printf("Registering agent with path: %s (address: %p)\n", AGENT_PATH,
         AGENT_PATH);

  DBusMessage *msg =
      send_dbus_method_call(conn, BLUEZ_SERVICE, "/org/bluez",
                            "org.bluez.AgentManager1", "RegisterAgent");
  if (!msg) {
    fprintf(stderr,
            "Error: Failed to create D-Bus message for RegisterAgent\n");
    return -1;
  }

  DBusMessageIter args;
  dbus_message_iter_init_append(msg, &args);
  // Explicitly pass AGENT_PATH as a const char *
  const char *agent_path = AGENT_PATH;
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH,
                                      &agent_path)) {
    fprintf(stderr, "Error: Failed to append AGENT_PATH to message\n");
    dbus_message_unref(msg);
    return -1;
  }
  const char *capability = "NoInputNoOutput";
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &capability)) {
    fprintf(stderr, "Error: Failed to append capability to message\n");
    dbus_message_unref(msg);
    return -1;
  }

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error registering agent: %s\n", err.message);
    dbus_error_free(&err);
    return -1;
  }
  dbus_message_unref(reply);

  // Request default agent
  msg = send_dbus_method_call(conn, BLUEZ_SERVICE, "/org/bluez",
                              "org.bluez.AgentManager1", "RequestDefaultAgent");
  if (!msg) {
    fprintf(stderr,
            "Error: Failed to create D-Bus message for RequestDefaultAgent\n");
    return -1;
  }

  dbus_message_iter_init_append(msg, &args);
  if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH,
                                      &agent_path)) {
    fprintf(
        stderr,
        "Error: Failed to append AGENT_PATH to RequestDefaultAgent message\n");
    dbus_message_unref(msg);
    return -1;
  }

  reply = dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error setting default agent: %s\n", err.message);
    dbus_error_free(&err);
    return -1;
  }
  dbus_message_unref(reply);

  // Add filter for agent signals
  if (!dbus_connection_add_filter(conn, agent_message_handler, NULL, NULL)) {
    fprintf(stderr, "Error: Failed to add D-Bus filter for agent\n");
    return -1;
  }
  dbus_bus_add_match(conn, "type='method_call',interface='org.bluez.Agent1'",
                     &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error adding match rule: %s\n", err.message);
    dbus_error_free(&err);
    return -1;
  }

  return 0;
}

// Pair and connect to the device
int pair_and_connect(DBusConnection *conn, const char *device_addr) {
  char device_path[100];
  snprintf(device_path, sizeof(device_path), "/org/bluez/hci0/dev_%s",
           device_addr);

  // Pair device
  printf("Pairing with device %s...\n", device_addr);
  DBusMessage *msg = send_dbus_method_call(conn, BLUEZ_SERVICE, device_path,
                                           "org.bluez.Device1", "Pair");
  if (!msg)
    return -1;

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error pairing: %s\n", err.message);
    dbus_error_free(&err);
    return -1;
  }
  dbus_message_unref(reply);

  // Connect device
  printf("Connecting to device %s...\n", device_addr);
  msg = send_dbus_method_call(conn, BLUEZ_SERVICE, device_path,
                              "org.bluez.Device1", "Connect");
  if (!msg)
    return -1;

  reply = dbus_connection_send_with_reply_and_block(conn, msg, TIMEOUT, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error connecting: %s\n", err.message);
    dbus_error_free(&err);
    return -1;
  }
  dbus_message_unref(reply);

  printf("Successfully paired and connected to %s\n", device_addr);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <device_address>\n", argv[0]);
    return 1;
  }

  // Normalize device address (replace ':' with '_')
  char device_addr[18];
  strncpy(device_addr, argv[1], sizeof(device_addr));
  for (int i = 0; device_addr[i]; i++) {
    if (device_addr[i] == ':')
      device_addr[i] = '_';
  }

  DBusError err;
  dbus_error_init(&err);

  // Connect to the system bus
  DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Error connecting to system bus: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }
  dbus_connection_set_exit_on_disconnect(conn, FALSE);

  // Configure adapter (power on, pairable, discoverable)
  if (configure_adapter(conn) != 0) {
    fprintf(stderr, "Failed to configure adapter\n");
    dbus_connection_unref(conn);
    return 1;
  }

  // Register agent for auto-accepting pairing
  if (register_agent(conn) != 0) {
    fprintf(stderr, "Failed to register agent\n");
    dbus_connection_unref(conn);
    return 1;
  }

  // Pair and connect to the device
  if (pair_and_connect(conn, device_addr) != 0) {
    fprintf(stderr, "Failed to pair and connect\n");
    dbus_connection_unref(conn);
    return 1;
  }

  // Clean up
  dbus_connection_unref(conn);
  return 0;
}
