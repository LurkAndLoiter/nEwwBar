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

#include "../include/hms.h"
#include "../include/json.h"

#include <glib.h>
#include <inttypes.h>
#include <playerctl/playerctl.h>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
    printf(fmt "\n", ##__VA_ARGS__);                                           \
  } while (0)

// Helper function to convert playback status to string
static const char *playback_status_to_string(PlayerctlPlaybackStatus status) {
  switch (status) {
  case PLAYERCTL_PLAYBACK_STATUS_PLAYING:
    return "Playing";
  case PLAYERCTL_PLAYBACK_STATUS_PAUSED:
    return "Paused";
  case PLAYERCTL_PLAYBACK_STATUS_STOPPED:
    return "Stopped";
  default:
    return "Unknown";
  }
}
#else
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)
#endif

// Structure to hold player data
typedef struct {
  gchar *name;
  gchar *instance;
  gint source;
  PlayerctlPlayer *player;
  gint64 last_position; // Store last known position
} PlayerData;

static guint pending_updates = 0;
static guint debounce_timeout_id = 0;

static void print_player_list(GList *players);
static void trigger_print(GList **players);
static gboolean debounce_print(gpointer user_data);

// Helper function to print the list of players as JSON
static void print_player_list(GList *players) {
  printf("{");
  gboolean first = TRUE;
  for (GList *iter = players; iter != NULL; iter = iter->next) {
    PlayerData *data = iter->data;
    if (!first) {
      printf(",");
    }
    first = FALSE;
    // Add microsecond position
    gchar *key = g_strdup_printf("org.mpris.MediaPlayer2.%s", data->instance);
    print_json_str(key);
    printf(":%" PRId64, data->last_position / 1000000);
    g_free(key);

    // Add HMS position
    printf(",");
    char hms[32];
    to_hms(data->last_position, hms, sizeof(hms));
    key = g_strdup_printf("org.mpris.MediaPlayer2.%sHMS", data->instance);
    print_json_str(key);
    printf(":");
    print_json_str(hms);
    g_free(key);
  }
  printf("}\n");
  fflush(stdout);
}

// Callback for the playback-status signal
static void on_playback_status(PlayerctlPlayer *player,
                               PlayerctlPlaybackStatus status,
                               gpointer user_data) {
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
    if (status == PLAYERCTL_PLAYBACK_STATUS_PAUSED ||
        status == PLAYERCTL_PLAYBACK_STATUS_STOPPED) {
      GError *error = NULL;
      data->last_position = playerctl_player_get_position(data->player, &error);
      if (error != NULL) {
        DEBUG_MSG("Failed to get position for %s on status change: %s",
                  data->name, error->message);
        g_error_free(error);
        data->last_position = -1;
      }
    }
    DEBUG_MSG("Player %s (instance: %s): Playback status changed to %s "
              "(position: %ld)",
              data->name, data->instance, playback_status_to_string(status),
              data->last_position);
    DEBUG_MSG("Playback status: List pointer %p, length %d", *players,
              g_list_length(*players));
    trigger_print(players);
  } else {
    DEBUG_MSG("Playback status: Invalid PlayerData (name: %p, instance: %p)",
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
    DEBUG_MSG("Failed to create player for %s: %s", name->name, error->message);
    g_error_free(error);
  }
  if (data->player) {
    // Fetch initial position
    error = NULL;
    data->last_position = playerctl_player_get_position(data->player, &error);
    if (error != NULL) {
      DEBUG_MSG("Failed to get initial position for %s: %s", name->name,
                error->message);
      g_error_free(error);
      data->last_position = -1;
    }
    // Connect to playback-status signal
    g_signal_connect(data->player, "playback-status",
                     G_CALLBACK(on_playback_status), players);
  }
  DEBUG_MSG("Created PlayerData for %s (instance: %s, player: %p, initial "
            "position: %ld)",
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
static void on_seeked(PlayerctlPlayer *player, gint64 position,
                      gpointer user_data) {
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
      DEBUG_MSG("Failed to get position for %s: %s", data->name,
                error->message);
      g_error_free(error);
      data->last_position = -1;
    }
    DEBUG_MSG("Player %s (instance: %s): Seeked to %ld microseconds",
              data->name, data->instance, data->last_position);
    DEBUG_MSG("Seeked: List pointer %p, length %d", *players,
              g_list_length(*players));
    trigger_print(players);
  } else {
    DEBUG_MSG("Seeked: Invalid PlayerData (name: %p, instance: %p)",
              data ? data->name : NULL, data ? data->instance : NULL);
  }
}

// Periodic callback to check positions of playing players
static gboolean check_positions(gpointer user_data) {
  GList **players = user_data;
  DEBUG_MSG("Checking positions: List pointer %p, length %d", *players,
            g_list_length(*players));
  if (*players == NULL) {
    DEBUG_MSG("No players found.");
  } else {
    DEBUG_MSG("Current players (%d total):", g_list_length(*players));
    int playing_count = 0;
    for (GList *iter = *players; iter != NULL; iter = iter->next) {
      PlayerData *data = iter->data;
      PlayerctlPlaybackStatus status = PLAYERCTL_PLAYBACK_STATUS_STOPPED;
      gint64 position =
          data->last_position; // Use last known position by default
      if (data->player) {
        g_object_get(data->player, "playback-status", &status, NULL);
        if (status == PLAYERCTL_PLAYBACK_STATUS_PLAYING) {
          GError *error = NULL;
          position = playerctl_player_get_position(data->player, &error);
          if (error != NULL) {
            DEBUG_MSG("Failed to get position for %s: %s", data->name,
                      error->message);
            g_error_free(error);
            position = -1;
          }
          data->last_position =
              position; // Update last position for playing players
          playing_count++;
        }
      }
      DEBUG_MSG("  - %s (instance: %s, source: %d, position: %ld microseconds, "
                "status: %s)",
                data->name, data->instance, data->source, position,
                playback_status_to_string(status));
    }
    if (playing_count == 0) {
      DEBUG_MSG("  No players are currently playing.");
    }
    trigger_print(players); // Output JSON with updated positions
  }
  return G_SOURCE_CONTINUE; // Keep the timeout running
}

// Callback for the name-appeared signal
static void on_name_appeared(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, GList **players) {
  DEBUG_MSG("Received name-appeared for %s (instance: %s)", name->name,
            name->instance);
  // Check if player already exists to avoid duplicates
  if (find_player_by_instance(*players, name->instance) == NULL) {
    PlayerData *data = player_data_new(name, players);
    *players = g_list_append(*players, data);
    DEBUG_MSG("Player appeared: %s (instance: %s, source: %d)", name->name,
              name->instance, name->source);
    // Connect to seeked signal if player was created
    if (data->player) {
      g_signal_connect(data->player, "seeked", G_CALLBACK(on_seeked), players);
    }
    DEBUG_MSG("List after adding %s:", name->instance);
    trigger_print(players);
  } else {
    DEBUG_MSG("Player %s (instance: %s) already exists, skipping", name->name,
              name->instance);
  }
}

// Callback for the name-vanished signal
static void on_name_vanished(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, GList **players) {
  DEBUG_MSG("Received name-vanished for %s (instance: %s)", name->name,
            name->instance);
  GList *node = find_player_by_instance(*players, name->instance);
  if (node != NULL) {
    PlayerData *data = node->data;
    *players = g_list_delete_link(*players, node);
    DEBUG_MSG("Player vanished: %s (instance: %s, source: %d)", name->name,
              name->instance, name->source);
    DEBUG_MSG("List after removing %s:", name->instance);
    trigger_print(players);
    player_data_free(data);
  } else {
    DEBUG_MSG("Player %s (instance: %s) not found in list", name->name,
              name->instance);
  }
}

static void trigger_print(GList **players) {
  pending_updates++;
  if (debounce_timeout_id == 0) {
    debounce_timeout_id = g_timeout_add(100, debounce_print, players);
  }
}

static gboolean debounce_print(gpointer user_data) {
  GList **players = user_data;
  if (--pending_updates == 0) {
    print_player_list(*players);
    debounce_timeout_id = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
  GError *error = NULL;

  // Create a new PlayerctlPlayerManager
  PlayerctlPlayerManager *manager = playerctl_player_manager_new(&error);
  if (error != NULL) {
    DEBUG_MSG("Failed to create player manager: %s", error->message);
    g_error_free(error);
    return 1;
  }

  // Initialize the player list with current players
  GList *players = NULL;
  GList *current_players = playerctl_list_players(&error);
  if (error != NULL) {
    DEBUG_MSG("Failed to list initial players: %s", error->message);
    g_error_free(error);
  } else {
    DEBUG_MSG("Found %d initial players", g_list_length(current_players));
    for (GList *iter = current_players; iter != NULL; iter = iter->next) {
      PlayerctlPlayerName *name = iter->data;
      PlayerData *data = player_data_new(name, &players);
      players = g_list_append(players, data);
      // Connect to seeked signal if player was created
      if (data->player) {
        g_signal_connect(data->player, "seeked", G_CALLBACK(on_seeked),
                         &players);
      }
    }
    g_list_free_full(current_players,
                     (GDestroyNotify)playerctl_player_name_free);
  }

  // Print initial player list
  DEBUG_MSG("Initial player list:");
  trigger_print(&players);

  // Connect to the name-appeared signal
  g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared),
                   &players);

  // Connect to the name-vanished signal
  g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished),
                   &players);

  // Set up a periodic timeout to check positions (every 1 second)
  g_timeout_add_seconds(1, check_positions, &players);

  // Create and run the GLib main loop
  DEBUG_MSG("Listening for player events...");
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  // Cleanup (unreachable in this example, but included for completeness)
  g_list_free_full(players, player_data_free);
  g_main_loop_unref(loop);
  g_object_unref(manager);

  return 0;
}
