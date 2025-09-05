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

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...) do { printf(fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define DEBUG_MSG(fmt, ...) do { } while (0)
#endif

// D-Bus constants
#define BLUEZ_SERVICE_NAME "org.bluez"
#define ADAPTER_OBJECT_PATH "/org/bluez/hci0"
#define ADAPTER_INTERFACE "org.bluez.Adapter1"
#define PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"

// Properties to monitor
static const char *properties[] = {
    "Powered",
    "Pairable",
    "Discovering",
    "Discoverable"
    // Note: Connectable is not a standard BlueZ Adapter1 property; omitted
};

// Function to add a variant to JSON builder
static void variant_to_json(JsonBuilder *builder, const char *prop_name, GVariant *value) {
    json_builder_set_member_name(builder, prop_name);
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        json_builder_add_boolean_value(builder, g_variant_get_boolean(value));
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        json_builder_add_string_value(builder, g_variant_get_string(value, NULL));
    } else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) {
        json_builder_add_int_value(builder, g_variant_get_int32(value));
    } else {
        json_builder_add_string_value(builder, "unsupported_type");
        DEBUG_MSG("Unsupported type for property %s: %s\n", prop_name, g_variant_get_type_string(value));
    }
}

// Function to create JSON from adapter properties
static void create_json_from_properties(GDBusProxy *proxy) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    for (guint i = 0; i < G_N_ELEMENTS(properties); i++) {
        const char *prop = properties[i];
        GError *error = NULL;
        GVariant *result = g_dbus_proxy_call_sync(
            proxy,
            "Get",
            g_variant_new("(ss)", ADAPTER_INTERFACE, prop),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error
        );

        if (error) {
            DEBUG_MSG("Failed to get property %s: %s\n", prop, error->message);
            g_error_free(error);
            continue;
        }

        GVariant *prop_value = g_variant_get_child_value(result, 0);
        GVariant *unboxed_value = g_variant_get_variant(prop_value);
        variant_to_json(builder, prop, unboxed_value);

        g_variant_unref(unboxed_value);
        g_variant_unref(prop_value);
        g_variant_unref(result);
    }

    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, FALSE); // Single-line output
    gchar *json_str = json_generator_to_data(generator, NULL);

    // Output JSON as a single line with newline
    printf("%s\n", json_str);
    fflush(stdout); // Ensure immediate write to stdout

    g_free(json_str);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
}

// Callback for PropertiesChanged signal
static void on_properties_changed(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data
) {
    GDBusProxy *proxy = G_DBUS_PROXY(user_data);
    const gchar *interface;
    GVariantIter *changed_properties;
    GVariantIter *invalidated_properties;

    g_variant_get(parameters, "(&sa{sv}as)", &interface, &changed_properties, &invalidated_properties);

    // Check if the changed properties are ones we care about
    gboolean relevant_change = FALSE;
    const gchar *property_name;
    GVariant *property_value;

    while (g_variant_iter_next(changed_properties, "{&sv}", &property_name, &property_value)) {
        for (guint i = 0; i < G_N_ELEMENTS(properties); i++) {
            if (g_strcmp0(property_name, properties[i]) == 0) {
                relevant_change = TRUE;
                break;
            }
        }
        g_variant_unref(property_value);
    }

    g_variant_iter_free(changed_properties);
    g_variant_iter_free(invalidated_properties);

    if (relevant_change) {
        create_json_from_properties(proxy);
    }
}

// Function to check if adapter exists
static gboolean check_adapter_exists(GDBusConnection *conn, GError **error) {
    GVariant *result = g_dbus_connection_call_sync(
        conn,
        BLUEZ_SERVICE_NAME,
        ADAPTER_OBJECT_PATH,
        "org.freedesktop.DBus.Introspectable",
        "Introspect",
        NULL,
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        error
    );

    if (result) {
        g_variant_unref(result);
        return TRUE;
    }
    return FALSE;
}

int main(void) {
    DEBUG_MSG("DEBUG enabled"); 
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    GError *error = NULL;

    // Connect to the system bus
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!conn) {
        DEBUG_MSG("Failed to connect to system bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    // Check if adapter exists
    if (!check_adapter_exists(conn, &error)) {
        DEBUG_MSG("Adapter %s not found: %s\n", ADAPTER_OBJECT_PATH, error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        g_object_unref(conn);
        return 1;
    }

    // Create a proxy for the Properties interface
    GDBusProxy *proxy = g_dbus_proxy_new_sync(
        conn,
        G_DBUS_PROXY_FLAGS_NONE,
        NULL,
        BLUEZ_SERVICE_NAME,
        ADAPTER_OBJECT_PATH,
        PROPERTIES_INTERFACE,
        NULL,
        &error
    );

    if (!proxy) {
        DEBUG_MSG("Failed to create proxy: %s\n", error->message);
        g_error_free(error);
        g_object_unref(conn);
        return 1;
    }

    // Subscribe to PropertiesChanged signal
    g_dbus_connection_signal_subscribe(
        conn,
        BLUEZ_SERVICE_NAME,
        PROPERTIES_INTERFACE,
        "PropertiesChanged",
        ADAPTER_OBJECT_PATH,
        ADAPTER_INTERFACE,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_properties_changed,
        proxy,
        NULL
    );

    // Generate initial JSON
    create_json_from_properties(proxy);

    // Run the main loop
    g_main_loop_run(loop);

    // Cleanup
    g_object_unref(proxy);
    g_object_unref(conn);
    g_main_loop_unref(loop);

    return 0;
}
