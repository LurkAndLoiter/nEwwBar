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

#define NM_DBUS_SERVICE "org.freedesktop.NetworkManager"
#define NM_DBUS_PATH "/org/freedesktop/NetworkManager"
#define NM_DBUS_INTERFACE "org.freedesktop.NetworkManager"
#define WIRELESS_INTERFACE "org.freedesktop.NetworkManager.Device.Wireless"
#define AP_INTERFACE "org.freedesktop.NetworkManager.AccessPoint"
#define ACTIVE_CONN_INTERFACE "org.freedesktop.NetworkManager.Connection.Active"

// Function to convert DBus byte array to string
char *byte_array_to_string(DBusMessageIter *iter) {
  DBusMessageIter array_iter;

  if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY ||
      dbus_message_iter_get_element_type(iter) != DBUS_TYPE_BYTE) {
    fprintf(stderr, "Expected byte array for SSID\n");
    return strdup("");
  }

  dbus_message_iter_recurse(iter, &array_iter);

  int len = 0;
  DBusMessageIter count_iter = array_iter;
  while (dbus_message_iter_get_arg_type(&count_iter) != DBUS_TYPE_INVALID) {
    len++;
    dbus_message_iter_next(&count_iter);
  }

  if (len == 0) {
    return strdup("");
  }

  char *str = malloc(len + 1);
  if (!str)
    return strdup("");

  int i = 0;
  while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
    unsigned char byte;
    dbus_message_iter_get_basic(&array_iter, &byte);
    str[i++] = (char)byte;
    dbus_message_iter_next(&array_iter);
  }
  str[len] = '\0';
  return str;
}

// Function to get the active connection's SpecificObject
char *get_active_specific_object(DBusConnection *conn) {
  DBusMessage *msg;
  DBusPendingCall *pending;
  char *primary_conn_path = NULL;
  char *specific_object = NULL;

  // Get PrimaryConnection
  msg = dbus_message_new_method_call(NM_DBUS_SERVICE, NM_DBUS_PATH,
                                     "org.freedesktop.DBus.Properties", "Get");
  const char *prop_iface = NM_DBUS_INTERFACE;
  const char *prop_name = "PrimaryConnection";
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &prop_iface, DBUS_TYPE_STRING,
                           &prop_name, DBUS_TYPE_INVALID);

  if (dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    dbus_connection_flush(conn);
    dbus_pending_call_block(pending);
    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    DBusMessageIter args, variant;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse(&args, &variant);
      dbus_message_iter_get_basic(&variant, &primary_conn_path);
      if (primary_conn_path && strcmp(primary_conn_path, "/") != 0) {
        primary_conn_path = strdup(primary_conn_path);
      } else {
        primary_conn_path = NULL;
      }
    }
    dbus_message_unref(reply);
  }
  dbus_message_unref(msg);

  if (!primary_conn_path) {
    return NULL;
  }

  // Get SpecificObject from the active connection
  msg = dbus_message_new_method_call(NM_DBUS_SERVICE, primary_conn_path,
                                     "org.freedesktop.DBus.Properties", "Get");
  prop_iface = ACTIVE_CONN_INTERFACE;
  prop_name = "SpecificObject";
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &prop_iface, DBUS_TYPE_STRING,
                           &prop_name, DBUS_TYPE_INVALID);

  if (dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    dbus_connection_flush(conn);
    dbus_pending_call_block(pending);
    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);

    DBusMessageIter args, variant;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse(&args, &variant);
      dbus_message_iter_get_basic(&variant, &specific_object);
      if (specific_object && strcmp(specific_object, "/") != 0) {
        specific_object = strdup(specific_object);
      } else {
        specific_object = NULL;
      }
    }
    dbus_message_unref(reply);
  }
  dbus_message_unref(msg);

  free(primary_conn_path);
  return specific_object;
}

// Function to get and print access points
void print_access_points(DBusConnection *conn, const char *device_path) {
  DBusMessage *msg;
  DBusPendingCall *pending;
  char *active_ap_path = get_active_specific_object(conn);

  msg = dbus_message_new_method_call(NM_DBUS_SERVICE, device_path,
                                     WIRELESS_INTERFACE, "GetAccessPoints");

  if (!msg) {
    fprintf(stderr, "Failed to create message\n");
    free(active_ap_path);
    return;
  }

  if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    fprintf(stderr, "Failed to send message\n");
    dbus_message_unref(msg);
    free(active_ap_path);
    return;
  }

  dbus_connection_flush(conn);
  dbus_message_unref(msg);

  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);

  if (!reply) {
    fprintf(stderr, "No reply received\n");
    free(active_ap_path);
    return;
  }

  DBusMessageIter args;
  if (dbus_message_iter_init(reply, &args)) {
    if (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&args)) {
      DBusMessageIter array_iter;
      dbus_message_iter_recurse(&args, &array_iter);

      printf("[");
      int first = 1;

      while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
        char *ap_path;
        dbus_message_iter_get_basic(&array_iter, &ap_path);
        int is_connected =
            (active_ap_path && strcmp(ap_path, active_ap_path) == 0);

        DBusMessage *prop_msg = dbus_message_new_method_call(
            NM_DBUS_SERVICE, ap_path, "org.freedesktop.DBus.Properties",
            "GetAll");

        const char *interface = AP_INTERFACE;
        dbus_message_append_args(prop_msg, DBUS_TYPE_STRING, &interface,
                                 DBUS_TYPE_INVALID);

        DBusPendingCall *prop_pending;
        if (dbus_connection_send_with_reply(conn, prop_msg, &prop_pending,
                                            -1)) {
          dbus_connection_flush(conn);
          dbus_pending_call_block(prop_pending);
          DBusMessage *prop_reply = dbus_pending_call_steal_reply(prop_pending);
          dbus_pending_call_unref(prop_pending);

          DBusMessageIter prop_args;
          if (dbus_message_iter_init(prop_reply, &prop_args)) {
            DBusMessageIter dict_iter;
            dbus_message_iter_recurse(&prop_args, &dict_iter);

            char *ssid = NULL;
            dbus_uint32_t freq = 0;
            unsigned char strength = 0;

            while (dbus_message_iter_get_arg_type(&dict_iter) !=
                   DBUS_TYPE_INVALID) {
              DBusMessageIter entry_iter, value_iter;
              char *prop_name;

              dbus_message_iter_recurse(&dict_iter, &entry_iter);
              dbus_message_iter_get_basic(&entry_iter, &prop_name);
              dbus_message_iter_next(&entry_iter);
              dbus_message_iter_recurse(&entry_iter, &value_iter);

              if (strcmp(prop_name, "Ssid") == 0) {
                ssid = byte_array_to_string(&value_iter);
              } else if (strcmp(prop_name, "Frequency") == 0) {
                dbus_message_iter_get_basic(&value_iter, &freq);
              } else if (strcmp(prop_name, "Strength") == 0) {
                dbus_message_iter_get_basic(&value_iter, &strength);
              }

              dbus_message_iter_next(&dict_iter);
            }

            if (!first)
              printf(",");
            first = 0;

            printf("{\"SSID\": \"%s\", \"Frequency\": %u, \"Strength\": %u, "
                   "\"connected\": %s}",
                   ssid ? ssid : "", freq, strength,
                   is_connected ? "true" : "false");

            if (ssid)
              free(ssid);
          }
          dbus_message_unref(prop_reply);
        }
        dbus_message_unref(prop_msg);
        dbus_message_iter_next(&array_iter);
      }
      printf("]\n");
      fflush(stdout);
    }
  }
  dbus_message_unref(reply);
  free(active_ap_path);
}

// Signal handler for DBus signals
static DBusHandlerResult signal_handler(DBusConnection *conn, DBusMessage *msg,
                                        void *user_data) {
  const char *device_path = (const char *)user_data;

  if (dbus_message_is_signal(msg, WIRELESS_INTERFACE, "AccessPointAdded") ||
      dbus_message_is_signal(msg, WIRELESS_INTERFACE, "AccessPointRemoved")) {
    print_access_points(conn, device_path);
  } else if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
                                    "PropertiesChanged")) {
    DBusMessageIter args;
    const char *interface_name;

    // Check if the signal is for the NetworkManager interface
    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
      dbus_message_iter_get_basic(&args, &interface_name);
      if (strcmp(interface_name, NM_DBUS_INTERFACE) == 0) {
        DBusMessageIter changed_props;
        // Move to the dictionary of changed properties
        dbus_message_iter_next(&args);
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
          dbus_message_iter_recurse(&args, &changed_props);
          // Iterate through the dictionary
          while (dbus_message_iter_get_arg_type(&changed_props) !=
                 DBUS_TYPE_INVALID) {
            DBusMessageIter dict_entry;
            const char *prop_name;

            dbus_message_iter_recurse(&changed_props, &dict_entry);
            if (dbus_message_iter_get_arg_type(&dict_entry) ==
                DBUS_TYPE_STRING) {
              dbus_message_iter_get_basic(&dict_entry, &prop_name);
              // Check if the property is ActiveConnections
              if (strcmp(prop_name, "Connectivity") == 0) {
                print_access_points(conn, device_path);
                break;
              }
            }
            dbus_message_iter_next(&changed_props);
          }
        }
      }
    }
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

int main() {
  DBusConnection *conn;
  DBusError err;
  DBusMessage *msg;
  DBusPendingCall *pending;
  char *device_path = NULL;

  dbus_error_init(&err);
  conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Connection Error: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }

  msg = dbus_message_new_method_call(NM_DBUS_SERVICE, NM_DBUS_PATH,
                                     NM_DBUS_INTERFACE, "GetDeviceByIpIface");

  const char *iface = "wlan0";
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);

  if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    fprintf(stderr, "Failed to send message\n");
    dbus_message_unref(msg);
    return 1;
  }

  dbus_connection_flush(conn);
  dbus_message_unref(msg);

  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);

  DBusMessageIter args;
  if (dbus_message_iter_init(reply, &args)) {
    dbus_message_iter_get_basic(&args, &device_path);
    device_path = strdup(device_path);
  }
  dbus_message_unref(reply);

  if (!device_path) {
    fprintf(stderr, "Failed to get device path\n");
    return 1;
  }

  char match_rule[256];
  // Match rule for AccessPointAdded and AccessPointRemoved
  snprintf(match_rule, sizeof(match_rule),
           "type='signal',interface='%s',path='%s'", WIRELESS_INTERFACE,
           device_path);
  dbus_bus_add_match(conn, match_rule, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Match Error: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }

  // Match rule for PropertiesChanged
  snprintf(match_rule, sizeof(match_rule),
           "type='signal',interface='org.freedesktop.DBus.Properties',path='%s'"
           ",member='PropertiesChanged'",
           NM_DBUS_PATH);
  dbus_bus_add_match(conn, match_rule, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "Match Error: %s\n", err.message);
    dbus_error_free(&err);
    return 1;
  }

  if (!dbus_connection_add_filter(conn, signal_handler, device_path, NULL)) {
    fprintf(stderr, "Failed to add filter\n");
    return 1;
  }

  print_access_points(conn, device_path);

  while (dbus_connection_read_write_dispatch(conn, -1))
    ;

  free(device_path);
  dbus_connection_unref(conn);
  return 0;
}
