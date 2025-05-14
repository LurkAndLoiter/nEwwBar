/* Copyright © 2025, LurkAndLoiter and contributors.
 *
 * This file is part of LurkAndLoiter's eww config.
 *
 * LurkAndLoiter's eww is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or (at your 
 * option) any later version.
 *
 * This is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with playerctl If not, see <http://www.gnu.org/licenses/>.
*/

#include <playerctl/playerctl.h>
#include <json-glib/json-glib.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

// Define DEBUG mode
#ifndef DEBUG
#define DEBUG 0
#endif

// Debug macros
#if DEBUG
#define DEBUG_PRINT(...) g_print(__VA_ARGS__)
#define DEBUG_ERROR(...) g_printerr(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_ERROR(...)
#endif

// Structure to hold player data
typedef struct {
    gchar *name;
    gchar *instance;
    gint source;
    PlayerctlPlayer *player;
    gint64 last_position; // Store last known position
} PlayerData;

// Store last JSON output for change detection
static gchar *last_json_output = NULL;

// Convert microseconds to HMS (MM:SS or H:MM:SS)
void to_hms(int64_t us, char *hms, size_t hms_size) {
    if (us < 0) {
        snprintf(hms, hms_size, "0:00");
        return;
    }

    long long hours = us / 3600000000LL;
    long long minutes = (us / 60000000LL) % 60;
    long long seconds = (us / 1000000LL) % 60;

    if (hours > 0) {
        snprintf(hms, hms_size, "%lld:%02lld:%02lld", hours, minutes, seconds);
    } else {
        snprintf(hms, hms_size, "%lld:%02lld", minutes, seconds);
    }
}

// Helper function to convert playback status to string
static const char *playback_status_to_string(PlayerctlPlaybackStatus status) {
    switch (status) {
        case PLAYERCTL_PLAYBACK_STATUS_PLAYING: return "Playing";
        case PLAYERCTL_PLAYBACK_STATUS_PAUSED: return "Paused";
        case PLAYERCTL_PLAYBACK_STATUS_STOPPED: return "Stopped";
        default: return "Unknown";
    }
}

// Helper function to print the list of players as JSON
static void print_player_list(GList *players) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    // Build JSON object
    for (GList *iter = players; iter != NULL; iter = iter->next) {
        PlayerData *data = iter->data;
        // Add microsecond position
        gchar *key = g_strdup_printf("org.mpris.MediaPlayer2.%s", data->instance);
        json_builder_set_member_name(builder, key);
        json_builder_add_int_value(builder, data->last_position);
        g_free(key);

        // Add HMS position
        char hms[32];
        to_hms(data->last_position, hms, sizeof(hms));
        key = g_strdup_printf("org.mpris.MediaPlayer2.%sHMS", data->instance);
        json_builder_set_member_name(builder, key);
        json_builder_add_string_value(builder, hms);
        g_free(key);
    }

    json_builder_end_object(builder);

    // Serialize to JSON string
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, FALSE); // Compact output
    gchar *json_str = json_generator_to_data(generator, NULL);

    // Compare with last JSON output
    if (last_json_output == NULL || strcmp(json_str, last_json_output) != 0) {
        g_print("%s\n", json_str); // Always print JSON to stdout
        // Update last JSON output
        g_free(last_json_output);
        last_json_output = g_strdup(json_str);
    }

    // Clean up
    g_free(json_str);
    g_object_unref(generator);
    json_node_free(root);
    g_object_unref(builder);
}

// Callback for the playback-status signal
static void on_playback_status(PlayerctlPlayer *player, PlayerctlPlaybackStatus status, gpointer user_data) {
    GList **players = user_data;
    // Find the corresponding PlayerData
    PlayerData *data = NULL;
    for (GList *iter = *players; iter != NULL; iter = iter->next) {
        PlayerData *d = iter->data;
        if (d->player == player) {
            data = d;
            break;
        }
    }
    if (data && data->name && data->instance) {
        // Update position if status is Paused or Stopped
        if (status == PLAYERCTL_PLAYBACK_STATUS_PAUSED || status == PLAYERCTL_PLAYBACK_STATUS_STOPPED) {
            GError *error = NULL;
            data->last_position = playerctl_player_get_position(data->player, &error);
            if (error != NULL) {
                DEBUG_ERROR("Failed to get position for %s on status change: %s\n", data->name, error->message);
                g_error_free(error);
                data->last_position = -1;
            }
        }
        DEBUG_PRINT("Player %s (instance: %s): Playback status changed to %s (position: %lld)\n",
                    data->name, data->instance, playback_status_to_string(status), data->last_position);
        DEBUG_PRINT("Playback status: List pointer %p, length %d\n", *players, g_list_length(*players));
        print_player_list(*players);
    } else {
        DEBUG_PRINT("Playback status: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Helper function to create PlayerData from PlayerctlPlayerName
static PlayerData *player_data_new(PlayerctlPlayerName *name, GList **players) {
    GError *error = NULL;
    PlayerData *data = g_new0(PlayerData, 1);
    data->name = g_strdup(name->name);
    data->instance = g_strdup(name->instance);
    data->source = name->source;
    data->last_position = -1; // Initialize last position
    data->player = playerctl_player_new_from_name(name, &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to create player for %s: %s\n", name->name, error->message);
        g_error_free(error);
    }
    if (data->player) {
        // Fetch initial position
        error = NULL;
        data->last_position = playerctl_player_get_position(data->player, &error);
        if (error != NULL) {
            DEBUG_ERROR("Failed to get initial position for %s: %s\n", name->name, error->message);
            g_error_free(error);
            data->last_position = -1;
        }
        // Connect to playback-status signal
        g_signal_connect(data->player, "playback-status", G_CALLBACK(on_playback_status), players);
    }
    DEBUG_PRINT("Created PlayerData for %s (instance: %s, player: %p, initial position: %lld)\n", 
                data->name, data->instance, data->player, data->last_position);
    return data;
}

// Helper function to free PlayerData
static void player_data_free(gpointer data) {
    PlayerData *player_data = data;
    if (player_data->player) {
        g_object_unref(player_data->player);
    }
    g_free(player_data->name);
    g_free(player_data->instance);
    g_free(player_data);
}

// Helper function to find a player by instance
static GList *find_player_by_instance(GList *players, const gchar *instance) {
    for (GList *iter = players; iter != NULL; iter = iter->next) {
        PlayerData *data = iter->data;
        if (data->instance && instance && strcmp(data->instance, instance) == 0) {
            return iter;
        }
    }
    return NULL;
}

// Callback for the seeked signal
static void on_seeked(PlayerctlPlayer *player, gint64 position, gpointer user_data) {
    GList **players = user_data;
    // Find the corresponding PlayerData
    PlayerData *data = NULL;
    for (GList *iter = *players; iter != NULL; iter = iter->next) {
        PlayerData *d = iter->data;
        if (d->player == player) {
            data = d;
            break;
        }
    }
    if (data && data->name && data->instance) {
        // Update last position
        GError *error = NULL;
        data->last_position = playerctl_player_get_position(data->player, &error);
        if (error != NULL) {
            DEBUG_ERROR("Failed to get position for %s: %s\n", data->name, error->message);
            g_error_free(error);
            data->last_position = -1;
        }
        DEBUG_PRINT("Player %s (instance: %s): Seeked to %lld microseconds\n",
                    data->name, data->instance, data->last_position);
        DEBUG_PRINT("Seeked: List pointer %p, length %d\n", *players, g_list_length(*players));
        print_player_list(*players);
    } else {
        DEBUG_PRINT("Seeked: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Periodic callback to check positions of playing players
static gboolean check_positions(gpointer user_data) {
    GList **players = user_data;
    DEBUG_PRINT("Checking positions: List pointer %p, length %d\n", 
                *players, g_list_length(*players));
    if (*players == NULL) {
        DEBUG_PRINT("No players found.\n");
    } else {
        DEBUG_PRINT("Current players (%d total):\n", g_list_length(*players));
        int playing_count = 0;
        for (GList *iter = *players; iter != NULL; iter = iter->next) {
            PlayerData *data = iter->data;
            PlayerctlPlaybackStatus status = PLAYERCTL_PLAYBACK_STATUS_STOPPED;
            gint64 position = data->last_position; // Use last known position by default
            if (data->player) {
                g_object_get(data->player, "playback-status", &status, NULL);
                if (status == PLAYERCTL_PLAYBACK_STATUS_PLAYING) {
                    GError *error = NULL;
                    position = playerctl_player_get_position(data->player, &error);
                    if (error != NULL) {
                        DEBUG_ERROR("Failed to get position for %s: %s\n", data->name, error->message);
                        g_error_free(error);
                        position = -1;
                    }
                    data->last_position = position; // Update last position for playing players
                    playing_count++;
                }
            }
            DEBUG_PRINT("  - %s (instance: %s, source: %d, position: %lld microseconds, status: %s)\n",
                        data->name, data->instance, data->source, position, playback_status_to_string(status));
        }
        if (playing_count == 0) {
            DEBUG_PRINT("  No players are currently playing.\n");
        }
        print_player_list(*players); // Output JSON with updated positions
    }
    return G_SOURCE_CONTINUE; // Keep the timeout running
}

// Callback for the name-appeared signal
static void on_name_appeared(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, GList **players) {
    DEBUG_PRINT("Received name-appeared for %s (instance: %s)\n", name->name, name->instance);
    // Check if player already exists to avoid duplicates
    if (find_player_by_instance(*players, name->instance) == NULL) {
        PlayerData *data = player_data_new(name, players);
        *players = g_list_append(*players, data);
        DEBUG_PRINT("Player appeared: %s (instance: %s, source: %d)\n",
                    name->name, name->instance, name->source);
        // Connect to seeked signal if player was created
        if (data->player) {
            g_signal_connect(data->player, "seeked", G_CALLBACK(on_seeked), players);
        }
        DEBUG_PRINT("List after adding %s:\n", name->instance);
        print_player_list(*players);
    } else {
        DEBUG_PRINT("Player %s (instance: %s) already exists, skipping\n", name->name, name->instance);
    }
}

// Callback for the name-vanished signal
static void on_name_vanished(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, GList **players) {
    DEBUG_PRINT("Received name-vanished for %s (instance: %s)\n", name->name, name->instance);
    GList *node = find_player_by_instance(*players, name->instance);
    if (node != NULL) {
        PlayerData *data = node->data;
        *players = g_list_delete_link(*players, node);
        DEBUG_PRINT("Player vanished: %s (instance: %s, source: %d)\n",
                    name->name, name->instance, name->source);
        DEBUG_PRINT("List after removing %s:\n", name->instance);
        print_player_list(*players);
        player_data_free(data);
    } else {
        DEBUG_PRINT("Player %s (instance: %s) not found in list\n", name->name, name->instance);
    }
}

int main(int argc, char *argv[]) {
    GError *error = NULL;

    // Create a new PlayerctlPlayerManager
    PlayerctlPlayerManager *manager = playerctl_player_manager_new(&error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to create player manager: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    // Initialize the player list with current players
    GList *players = NULL;
    GList *current_players = playerctl_list_players(&error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to list initial players: %s\n", error->message);
        g_error_free(error);
    } else {
        DEBUG_PRINT("Found %d initial players\n", g_list_length(current_players));
        for (GList *iter = current_players; iter != NULL; iter = iter->next) {
            PlayerctlPlayerName *name = iter->data;
            PlayerData *data = player_data_new(name, &players);
            players = g_list_append(players, data);
            // Connect to seeked signal if player was created
            if (data->player) {
                g_signal_connect(data->player, "seeked", G_CALLBACK(on_seeked), &players);
            }
        }
        g_list_free_full(current_players, (GDestroyNotify)playerctl_player_name_free);
    }

    // Print initial player list
    DEBUG_PRINT("Initial player list:\n");
    print_player_list(players);

    // Connect to the name-appeared signal
    g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared), &players);

    // Connect to the name-vanished signal
    g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished), &players);

    // Set up a periodic timeout to check positions (every 1 second)
    g_timeout_add_seconds(1, check_positions, &players);

    // Create and run the GLib main loop
    DEBUG_PRINT("Listening for player events...\n");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    // Cleanup (unreachable in this example, but included for completeness)
    g_list_free_full(players, player_data_free);
    g_free(last_json_output); // Free last JSON output
    g_main_loop_unref(loop);
    g_object_unref(manager);

    return 0;
}
