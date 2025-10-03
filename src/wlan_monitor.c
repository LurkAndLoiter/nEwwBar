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

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NM_BUS_NAME "org.freedesktop.NetworkManager"
#define NM_OBJ_PATH "/org/freedesktop/NetworkManager"
#define WIRELESS_IFACE "wlan0"

static GDBusConnection *connection = NULL;
static guint subscription_id = 0;
static guint strength_subscription_id = 0;
static char *current_ap_path = NULL;
static char *device_path = NULL;
static char last_output[64] = ""; // Buffer for last JSON output

static char *get_device_path() {
  GError *error = NULL;
  GVariant *result;
  char *path = NULL;

  result = g_dbus_connection_call_sync(
      connection, NM_BUS_NAME, NM_OBJ_PATH, NM_BUS_NAME, "GetDeviceByIpIface",
      g_variant_new("(s)", WIRELESS_IFACE), G_VARIANT_TYPE("(o)"),
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error) {
    fprintf(stderr, "Failed to get device: %s\n", error->message);
    g_error_free(error);
    return NULL;
  }

  g_variant_get(result, "(o)", &path);
  g_variant_unref(result);
  return path;
}

static char *get_active_access_point(const char *dev_path) {
  GError *error = NULL;
  GVariant *result;
  char *ap_path = NULL;

  result = g_dbus_connection_call_sync(
      connection, NM_BUS_NAME, dev_path, "org.freedesktop.DBus.Properties",
      "Get",
      g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device.Wireless",
                    "ActiveAccessPoint"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error) {
    fprintf(stderr, "Failed to get ActiveAccessPoint: %s\n", error->message);
    g_error_free(error);
    return NULL;
  }

  GVariant *value;
  g_variant_get(result, "(v)", &value);
  ap_path = g_variant_dup_string(value, NULL);
  g_variant_unref(value);
  g_variant_unref(result);

  if (strcmp(ap_path, "/") == 0 || strlen(ap_path) == 0) {
    g_free(ap_path);
    return NULL;
  }

  return ap_path;
}

static guint8 get_strength(const char *ap_path) {
  GError *error = NULL;
  GVariant *result;

  result = g_dbus_connection_call_sync(
      connection, NM_BUS_NAME, ap_path, "org.freedesktop.DBus.Properties",
      "Get",
      g_variant_new("(ss)", "org.freedesktop.NetworkManager.AccessPoint",
                    "Strength"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error) {
    fprintf(stderr, "Failed to get strength: %s\n", error->message);
    g_error_free(error);
    return 0;
  }

  GVariant *value;
  g_variant_get(result, "(v)", &value);
  guint8 strength = g_variant_get_byte(value);
  g_variant_unref(value);
  g_variant_unref(result);
  return strength;
}

static guint get_state(const char *dev_path) {
  GError *error = NULL;
  GVariant *result;

  result = g_dbus_connection_call_sync(
      connection, NM_BUS_NAME, dev_path, "org.freedesktop.DBus.Properties",
      "Get",
      g_variant_new("(ss)", "org.freedesktop.NetworkManager.Device", "State"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (error) {
    fprintf(stderr, "Failed to get state: %s\n", error->message);
    g_error_free(error);
    return 0;
  }

  GVariant *value;
  g_variant_get(result, "(v)", &value);
  guint state = g_variant_get_uint32(value);
  g_variant_unref(value);
  g_variant_unref(result);
  return state;
}

static void print_json(guint state, guint8 strength) {
  char new_output[64];
  snprintf(new_output, sizeof(new_output), "{\"state\": %u, \"strength\": %u}",
           state, strength);

  // Only print if different from last output
  if (strcmp(new_output, last_output) != 0) {
    printf("%s\n", new_output);
    fflush(stdout);
    strncpy(last_output, new_output, sizeof(last_output) - 1);
    last_output[sizeof(last_output) - 1] = '\0'; // Ensure null termination
  }
}

static void on_strength_changed(GDBusConnection *conn, const gchar *sender_name,
                                const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *signal_name, GVariant *parameters,
                                gpointer user_data) {
  guint state = get_state(device_path);
  if (state == 100) {
    guint8 strength = get_strength(current_ap_path);
    print_json(state, strength);
  }
}

static void on_state_changed(GDBusConnection *conn, const gchar *sender_name,
                             const gchar *object_path,
                             const gchar *interface_name,
                             const gchar *signal_name, GVariant *parameters,
                             gpointer user_data) {
  guint state = get_state(device_path);
  guint8 strength = 0;

  if (state == 100) {
    if (current_ap_path)
      g_free(current_ap_path);
    current_ap_path = get_active_access_point(device_path);

    if (current_ap_path) {
      strength = get_strength(current_ap_path);
      if (!strength_subscription_id) {
        strength_subscription_id = g_dbus_connection_signal_subscribe(
            connection, NM_BUS_NAME, "org.freedesktop.DBus.Properties",
            "PropertiesChanged", current_ap_path,
            "org.freedesktop.NetworkManager.AccessPoint",
            G_DBUS_SIGNAL_FLAGS_NONE, on_strength_changed, NULL, NULL);
      }
    }
  } else {
    if (strength_subscription_id) {
      g_dbus_connection_signal_unsubscribe(connection,
                                           strength_subscription_id);
      strength_subscription_id = 0;
    }
    if (current_ap_path) {
      g_free(current_ap_path);
      current_ap_path = NULL;
    }
  }

  print_json(state, strength);
}

int main() {
  GError *error = NULL;
  GMainLoop *loop;

  connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (error) {
    fprintf(stderr, "Failed to connect to system bus: %s\n", error->message);
    g_error_free(error);
    return 1;
  }

  device_path = get_device_path();
  if (!device_path) {
    fprintf(stderr, "No device path for %s\n", WIRELESS_IFACE);
    g_object_unref(connection);
    return 1;
  }

  // Get initial state and strength
  guint state = get_state(device_path);
  guint8 strength = 0;

  if (state == 100) {
    current_ap_path = get_active_access_point(device_path);
    if (current_ap_path) {
      strength = get_strength(current_ap_path);
    }
  }
  print_json(state, strength);

  // Subscribe to state changes
  subscription_id = g_dbus_connection_signal_subscribe(
      connection, NM_BUS_NAME, "org.freedesktop.DBus.Properties",
      "PropertiesChanged", device_path, "org.freedesktop.NetworkManager.Device",
      G_DBUS_SIGNAL_FLAGS_NONE, on_state_changed, NULL, NULL);

  if (state == 100 && current_ap_path) {
    strength_subscription_id = g_dbus_connection_signal_subscribe(
        connection, NM_BUS_NAME, "org.freedesktop.DBus.Properties",
        "PropertiesChanged", current_ap_path,
        "org.freedesktop.NetworkManager.AccessPoint", G_DBUS_SIGNAL_FLAGS_NONE,
        on_strength_changed, NULL, NULL);
  }

  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  // Cleanup
  if (subscription_id)
    g_dbus_connection_signal_unsubscribe(connection, subscription_id);
  if (strength_subscription_id)
    g_dbus_connection_signal_unsubscribe(connection, strength_subscription_id);
  if (current_ap_path)
    g_free(current_ap_path);
  g_free(device_path);
  g_object_unref(connection);
  g_main_loop_unref(loop);

  return 0;
}
