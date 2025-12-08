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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
    printf(fmt "\n", ##__VA_ARGS__);                                           \
  } while (0)
#else
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)
#endif

#define BLUEZ_SERVICE "org.bluez"
#define BLUEZ_PATH "/org/bluez"
#define DBUS_INTERFACE "org.freedesktop.DBus.ObjectManager"
#define DEVICE_INTERFACE "org.bluez.Device1"
#define BATTERY_INTERFACE "org.bluez.Battery1"

typedef struct {
  char *key;
  char *value;
} KeyValuePair;

typedef struct {
  char *path;
  KeyValuePair *properties;
  int prop_count;
} Device;

static void free_device(Device *device) {
  if (device->path) {
    free(device->path);
  }
  for (int i = 0; i < device->prop_count; i++) {
    if (device->properties[i].key) {
      free(device->properties[i].key);
    }
    if (device->properties[i].value) {
      free(device->properties[i].value);
    }
  }
  if (device->properties) {
    free(device->properties);
  }
}

static char *get_property(DBusConnection *conn, const char *path,
                          const char *interface, const char *property) {
  DBusMessage *msg = dbus_message_new_method_call(
      BLUEZ_SERVICE, path, "org.freedesktop.DBus.Properties", "Get");
  if (!msg) {
    DEBUG_MSG("Failed to create Get message");
    return NULL;
  }

  if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface,
                                DBUS_TYPE_STRING, &property,
                                DBUS_TYPE_INVALID)) {
    DEBUG_MSG("Failed to append args to Get message");
    dbus_message_unref(msg);
    return NULL;
  }

  DBusPendingCall *pending = NULL;
  if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    DEBUG_MSG("Failed to send Get message");
    dbus_message_unref(msg);
    return NULL;
  }

  dbus_connection_flush(conn);
  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  if (!reply) {
    DEBUG_MSG("No reply from Properties.Get");
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    return NULL;
  }

  char *value = NULL;
  DBusMessageIter args;
  if (dbus_message_iter_init(reply, &args)) {
    DBusMessageIter variant;
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse(&args, &variant);
      int type = dbus_message_iter_get_arg_type(&variant);
      if (type == DBUS_TYPE_STRING) {
        char *str;
        dbus_message_iter_get_basic(&variant, &str);
        value = str ? strdup(str) : NULL;
      } else if (type == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t bool_val;
        dbus_message_iter_get_basic(&variant, &bool_val);
        value = strdup(bool_val ? "true" : "false");
      } else if (type == DBUS_TYPE_BYTE) {
        uint8_t byte_val;
        dbus_message_iter_get_basic(&variant, &byte_val);
        char buffer[4];
        snprintf(buffer, sizeof(buffer), "%u", byte_val);
        value = strdup(buffer);
      }
    }
  }

  dbus_message_unref(reply);
  dbus_pending_call_unref(pending);
  dbus_message_unref(msg);
  return value;
}

static Device *get_devices(DBusConnection *conn, int *device_count) {
  DBusMessage *msg = dbus_message_new_method_call(
      BLUEZ_SERVICE, "/", DBUS_INTERFACE, "GetManagedObjects");
  if (!msg) {
    DEBUG_MSG("Failed to create GetManagedObjects message");
    return NULL;
  }

  DBusPendingCall *pending = NULL;
  if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
    DEBUG_MSG("Failed to send GetManagedObjects message");
    dbus_message_unref(msg);
    return NULL;
  }

  dbus_connection_flush(conn);
  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  if (!reply) {
    DEBUG_MSG("No reply from GetManagedObjects");
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    return NULL;
  }

  Device *devices = NULL;
  *device_count = 0;
  DBusMessageIter args;
  if (dbus_message_iter_init(reply, &args) &&
      dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
    DBusMessageIter dict_array;
    dbus_message_iter_recurse(&args, &dict_array);

    // Count devices with Device1 interface
    while (dbus_message_iter_get_arg_type(&dict_array) != DBUS_TYPE_INVALID) {
      DBusMessageIter dict_entry;
      dbus_message_iter_recurse(&dict_array, &dict_entry);
      if (dbus_message_iter_get_arg_type(&dict_entry) ==
          DBUS_TYPE_OBJECT_PATH) {
        dbus_message_iter_next(&dict_entry);
        DBusMessageIter interfaces_array;
        dbus_message_iter_recurse(&dict_entry, &interfaces_array);
        while (dbus_message_iter_get_arg_type(&interfaces_array) !=
               DBUS_TYPE_INVALID) {
          DBusMessageIter interface_dict;
          dbus_message_iter_recurse(&interfaces_array, &interface_dict);
          char *interface;
          dbus_message_iter_get_basic(&interface_dict, &interface);
          if (strcmp(interface, DEVICE_INTERFACE) == 0) {
            (*device_count)++;
            break;
          }
          dbus_message_iter_next(&interfaces_array);
        }
      }
      dbus_message_iter_next(&dict_array);
    }

    if (*device_count > 0) {
      devices = calloc(*device_count, sizeof(Device));
      int index = 0;

      dbus_message_iter_recurse(&args, &dict_array);
      while (dbus_message_iter_get_arg_type(&dict_array) != DBUS_TYPE_INVALID &&
             index < *device_count) {
        DBusMessageIter dict_entry;
        dbus_message_iter_recurse(&dict_array, &dict_entry);
        char *path;
        dbus_message_iter_get_basic(&dict_entry, &path);
        dbus_message_iter_next(&dict_entry);

        DBusMessageIter interfaces_array;
        dbus_message_iter_recurse(&dict_entry, &interfaces_array);
        while (dbus_message_iter_get_arg_type(&interfaces_array) !=
               DBUS_TYPE_INVALID) {
          DBusMessageIter interface_dict;
          dbus_message_iter_recurse(&interfaces_array, &interface_dict);
          char *interface;
          dbus_message_iter_get_basic(&interface_dict, &interface);
          if (strcmp(interface, DEVICE_INTERFACE) == 0) {
            devices[index].path = strdup(path);
            const char *properties[] = {"Address",   "Alias",  "Icon",
                                        "Connected", "Paired", "Trusted",
                                        "Percentage"};
            devices[index].prop_count = 7; // All properties

            devices[index].properties =
                calloc(devices[index].prop_count, sizeof(KeyValuePair));

            // Get all properties (Device1 and Battery1)
            for (int i = 0; i < devices[index].prop_count; i++) {
              devices[index].properties[i].key = strdup(properties[i]);
              if (i < 6) { // Device1 properties
                devices[index].properties[i].value =
                    get_property(conn, path, DEVICE_INTERFACE, properties[i]);
              } else { // Battery1 property
                devices[index].properties[i].value =
                    get_property(conn, path, BATTERY_INTERFACE, properties[i]);
              }
            }

            index++;
            break;
          }
          dbus_message_iter_next(&interfaces_array);
        }
        dbus_message_iter_next(&dict_array);
      }
    }
  }

  dbus_message_unref(reply);
  dbus_pending_call_unref(pending);
  dbus_message_unref(msg);
  return devices;
}

static char *last_output = NULL; // Store the last printed JSON output

static void print_devices(Device *devices, int device_count) {
  // Build the JSON output into a dynamic buffer
  size_t buffer_size = 1024; // Initial buffer size
  char *buffer = malloc(buffer_size);
  if (!buffer) {
    DEBUG_MSG("Failed to allocate buffer for JSON output");
    return;
  }
  size_t offset = 0;

  offset += snprintf(buffer + offset, buffer_size - offset, "[");
  for (int i = 0; i < device_count; i++) {
    if (i > 0) {
      offset += snprintf(buffer + offset, buffer_size - offset, ",");
    }
    offset += snprintf(buffer + offset, buffer_size - offset, "{");
    int first = 1;
    const char *output_keys[] = {"id",     "Name",    "Icon",   "Connected",
                                 "Paired", "Trusted", "Battery"};
    for (int j = 0; j < devices[i].prop_count; j++) {
      if (!first) {
        offset += snprintf(buffer + offset, buffer_size - offset, ",");
      }
      first = 0;
      const char *value = devices[i].properties[j].value
                              ? devices[i].properties[j].value
                              : "999";
      if (strcmp(devices[i].properties[j].key, "Connected") == 0 ||
          strcmp(devices[i].properties[j].key, "Paired") == 0 ||
          strcmp(devices[i].properties[j].key, "Trusted") == 0) {
        offset += snprintf(buffer + offset, buffer_size - offset, "\"%s\": %s",
                           output_keys[j], value[0] ? value : "false");
      } else if (strcmp(devices[i].properties[j].key, "Percentage") == 0) {
        offset += snprintf(buffer + offset, buffer_size - offset, "\"%s\": %s",
                           output_keys[j], value[0] ? value : "999");
      } else {
        offset +=
            snprintf(buffer + offset, buffer_size - offset, "\"%s\": \"%s\"",
                     output_keys[j], value[0] ? value : "null");
      }

      // Resize buffer if needed
      if (offset >= buffer_size - 1) {
        buffer_size *= 2;
        char *new_buffer = realloc(buffer, buffer_size);
        if (!new_buffer) {
          DEBUG_MSG("Failed to reallocate buffer for JSON output");
          free(buffer);
          return;
        }
        buffer = new_buffer;
      }
    }
    offset += snprintf(buffer + offset, buffer_size - offset, "}");
  }
  offset += snprintf(buffer + offset, buffer_size - offset, "]");

  // Compare with last output
  if (!last_output || strcmp(buffer, last_output) != 0) {
    // Print and update last_output
    printf("%s\n", buffer);
    fflush(stdout);
    free(last_output);            // Free previous output
    last_output = strdup(buffer); // Store new output
  }

  free(buffer);
}

int main(void) {
  DEBUG_MSG("Debug enabled");
  DBusError err;
  dbus_error_init(&err);

  DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
  if (!conn) {
    DEBUG_MSG("Failed to connect to system bus");
    dbus_error_free(&err);
    return 1;
  }

  // Initial device list
  int device_count = 0;
  Device *devices = get_devices(conn, &device_count);
  if (device_count > 0) {
    print_devices(devices, device_count);
  } else {
    // Handle empty device list
    if (!last_output || strcmp("[]", last_output) != 0) {
      printf("[]\n");
      fflush(stdout);
      free(last_output);
      last_output = strdup("[]");
    }
  }

  // Add match rules for PropertiesChanged and InterfacesAdded/Removed
  dbus_bus_add_match(conn,
                     "type='signal',interface='org.freedesktop.DBus.Properties'"
                     ",member='PropertiesChanged'",
                     &err);
  dbus_bus_add_match(conn,
                     "type='signal',interface='org.freedesktop.DBus."
                     "ObjectManager',member='InterfacesAdded'",
                     &err);
  dbus_bus_add_match(conn,
                     "type='signal',interface='org.freedesktop.DBus."
                     "ObjectManager',member='InterfacesRemoved'",
                     &err);
  if (dbus_error_is_set(&err)) {
    DEBUG_MSG("Failed to add match rules");
    for (int i = 0; i < device_count; i++)
      free_device(&devices[i]);
    if (devices) {
      free(devices);
    }
    free(last_output); // Clean up last_output
    dbus_connection_unref(conn);
    dbus_error_free(&err);
    return 1;
  }

  while (dbus_connection_read_write(conn, -1)) {
    DBusMessage *msg;
    while ((msg = dbus_connection_pop_message(conn)) != NULL) {
      if (dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged") ||
          dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager",
                                 "InterfacesAdded") ||
          dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager",
                                 "InterfacesRemoved")) {

        // Free previous devices and refresh the list
        for (int i = 0; i < device_count; i++) {
          free_device(&devices[i]);
        }
        if (devices) {
          free(devices);
        }

        devices = get_devices(conn, &device_count);
        if (device_count > 0) {
          print_devices(devices, device_count);
        } else {
          // Handle empty device list
          if (!last_output || strcmp("[]", last_output) != 0) {
            printf("[]\n");
            fflush(stdout);
            free(last_output);
            last_output = strdup("[]");
          }
        }
      }
      dbus_message_unref(msg);
    }
  }

  // Cleanup
  for (int i = 0; i < device_count; i++) {
    free_device(&devices[i]);
  }
  if (devices) {
    free(devices);
  }
  free(last_output); // Clean up last_output
  dbus_connection_unref(conn);
  dbus_error_free(&err);

  return 0;
}
