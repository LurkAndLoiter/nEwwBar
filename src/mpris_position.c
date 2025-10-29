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
#include <glib.h>
#include <inttypes.h>
#include <playerctl/playerctl.h>
#include <stdio.h>
#include <string.h>
#include "../include/json.h"

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

static const char *playback_status_to_string(PlayerctlPlaybackStatus status) {
  switch (status) {
    case PLAYERCTL_PLAYBACK_STATUS_PLAYING: return "Playing";
    case PLAYERCTL_PLAYBACK_STATUS_PAUSED: return "Paused";
    case PLAYERCTL_PLAYBACK_STATUS_STOPPED: return "Stopped";
    default: return "Unknown";
  }
}
#else
#define DEBUG_MSG(fmt, ...) do { } while (0)
#endif

typedef struct {
  gchar *name;
  gchar *instance;
  gint source;
  PlayerctlPlayer *player;
  gint64 local_seconds;
  gint hours;
  gint minutes;
  gint seconds;
  gint update_counter;
  PlayerctlPlaybackStatus playback_status;
  GList **players_ptr;
} PlayerData;

static gboolean on_position_check(gpointer user_data);
static void on_seeked(PlayerctlPlayer *player, gint64 position, gpointer user_data);
static void on_playback_status(PlayerctlPlayer *player, PlayerctlPlaybackStatus status, gpointer user_data);

static guint global_position_timeout_id = 0;

static void update_time_components(PlayerData *data) {
  data->hours = (gint)(data->local_seconds / 3600);
  data->minutes = (gint)((data->local_seconds % 3600) / 60);
  data->seconds = (gint)(data->local_seconds % 60);
}

static PlayerData *find_player_data(GList **players, PlayerctlPlayer *player) {
  for (GList *iter = *players; iter; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d->player == player) return d;
  }
  return NULL;
}

static GList *find_player_by_instance(GList *players, const gchar *instance) {
  for (GList *iter = players; iter; iter = iter->next) {
    PlayerData *data = iter->data;
    if (data->instance && instance && strcmp(data->instance, instance) == 0) {
      return iter;
    }
  }
  return NULL;
}

static void print_hms(PlayerData *data, char *buffer, size_t size) {
  if (data->hours > 0) {
    snprintf(buffer, size, "%d:%02d:%02d", data->hours, data->minutes, data->seconds);
  } else {
    snprintf(buffer, size, "%d:%02d", data->minutes, data->seconds);
  }
}

static void print_player_list(GList *players) {
  printf("{");
  gboolean first = TRUE;
  for (GList *iter = players; iter; iter = iter->next) {
    PlayerData *data = iter->data;
    if (!first) printf(",");
    first = FALSE;

    gchar *key = g_strdup_printf("org.mpris.MediaPlayer2.%s", data->instance);
    print_json_str(key);
    printf(":%" PRId64, data->local_seconds);
    g_free(key);

    printf(",");
    char hms[32];
    print_hms(data, hms, sizeof(hms));
    key = g_strdup_printf("org.mpris.MediaPlayer2.%sHMS", data->instance);
    print_json_str(key);
    printf(":");
    print_json_str(hms);
    g_free(key);
  }
  printf("}\n");
  fflush(stdout);
}

static PlayerData *player_data_new(PlayerctlPlayerName *name, GList **players) {
  PlayerData *data = g_new0(PlayerData, 1);
  data->name = g_strdup(name->name);
  data->instance = g_strdup(name->instance);
  data->source = name->source;
  data->local_seconds = 0;
  data->playback_status = PLAYERCTL_PLAYBACK_STATUS_STOPPED;
  data->players_ptr = players;

  GError *error = NULL;
  data->player = playerctl_player_new_from_name(name, &error);
  if (error) {
    DEBUG_MSG("Failed to create player for %s: %s", name->name, error->message);
    g_error_free(error);
  }

  if (data->player) {
    gint64 micros = playerctl_player_get_position(data->player, &error);
    if (error) {
      DEBUG_MSG("Failed to get initial position for %s: %s", data->name, error->message);
      g_error_free(error);
    } else {
      data->local_seconds = micros / 1000000;
    }

    update_time_components(data);
    data->update_counter = data->seconds;
    g_object_get(data->player, "playback-status", &data->playback_status, NULL);
    g_signal_connect(data->player, "playback-status", G_CALLBACK(on_playback_status), players);
    g_signal_connect(data->player, "seeked", G_CALLBACK(on_seeked), players);
  }

  DEBUG_MSG("Created PlayerData for %s (instance: %s, player: %p, initial position: %ld, status: %s)",
            data->name, data->instance, data->player, data->local_seconds,
            playback_status_to_string(data->playback_status));
  return data;
}

static void player_data_free(gpointer data) {
  PlayerData *player_data = data;
  if (player_data->player) g_object_unref(player_data->player);
  g_free(player_data->name);
  g_free(player_data->instance);
  g_free(player_data);
}

static void update_player_position(PlayerData *data, gint64 curr_sec, GList **players) {
  gint64 last_sec = data->local_seconds;
  data->local_seconds = curr_sec;
  update_time_components(data);
  data->update_counter = data->seconds;

  if (curr_sec != last_sec) {
    print_player_list(*players);
  }
}

static void adjust_global_timer(GList **players) {
  gboolean any_playing = FALSE;
  for (GList *iter = *players; iter; iter = iter->next) {
    PlayerData *data = iter->data;
    if (data->playback_status == PLAYERCTL_PLAYBACK_STATUS_PLAYING) {
      any_playing = TRUE;
      break;
    }
  }

  if (any_playing && global_position_timeout_id == 0) {
    global_position_timeout_id = g_timeout_add(1000, on_position_check, players);
  } else if (!any_playing && global_position_timeout_id != 0) {
    g_source_remove(global_position_timeout_id);
    global_position_timeout_id = 0;
  }
}

static void on_playback_status(PlayerctlPlayer *player, PlayerctlPlaybackStatus status, gpointer user_data) {
  GList **players = user_data;
  PlayerData *data = find_player_data(players, player);
  if (!data || !data->name || !data->instance) return;

  data->playback_status = status;
  GError *error = NULL;
  gint64 pos_micros = playerctl_player_get_position(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get position for %s on status change: %s", data->name, error->message);
    g_error_free(error);
    pos_micros = data->local_seconds * 1000000;
  }

  update_player_position(data, pos_micros / 1000000, players);
  DEBUG_MSG("Player %s (instance: %s): Playback status changed to %s (position: %ld)",
            data->name, data->instance, playback_status_to_string(status), data->local_seconds);
  adjust_global_timer(players);
}

static void on_seeked(PlayerctlPlayer *player, gint64 position, gpointer user_data) {
  GList **players = user_data;
  PlayerData *data = find_player_data(players, player);
  if (!data || !data->name || !data->instance) return;

  update_player_position(data, position / 1000000, players);
  DEBUG_MSG("Player %s (instance: %s): Seeked to %ld seconds",
            data->name, data->instance, data->local_seconds);
  adjust_global_timer(players);
}

static gboolean on_position_check(gpointer user_data) {
  GList **players = user_data;
  global_position_timeout_id = 0;
  gboolean any_changed = FALSE;

  for (GList *iter = *players; iter; iter = iter->next) {
    PlayerData *data = iter->data;
    if (!data->player || data->playback_status != PLAYERCTL_PLAYBACK_STATUS_PLAYING) continue;

    gint64 old_seconds = data->local_seconds;
    data->local_seconds += 1;
    data->seconds += 1;
    if (data->seconds >= 60) {
      data->seconds = 0;
      data->minutes += 1;
      if (data->minutes >= 60) {
        data->minutes = 0;
        data->hours += 1;
      }
    }

    data->update_counter = (data->update_counter + 1) % 60;
    if (data->update_counter == 0) {
      GError *error = NULL;
      gint64 micros = playerctl_player_get_position(data->player, &error);
      if (error) {
        DEBUG_MSG("Failed to get position for %s: %s", data->name, error->message);
        g_error_free(error);
      } else {
        data->local_seconds = micros / 1000000;
        update_time_components(data);
      }
    }

    if (data->local_seconds != old_seconds) any_changed = TRUE;
  }

  if (any_changed) print_player_list(*players);
  adjust_global_timer(players);
  return FALSE;
}

static void on_name_appeared(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, gpointer user_data) {
  (void)manager;
  GList **players = user_data;
  DEBUG_MSG("Received name-appeared for %s (instance: %s)", name->name, name->instance);

  if (!find_player_by_instance(*players, name->instance)) {
    PlayerData *data = player_data_new(name, players);
    *players = g_list_append(*players, data);
    DEBUG_MSG("Player appeared: %s (instance: %s, source: %d)", name->name, name->instance, name->source);
    print_player_list(*players);
    adjust_global_timer(players);
  } else {
    DEBUG_MSG("Player %s (instance: %s) already exists, skipping", name->name, name->instance);
  }
}

static void on_name_vanished(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, gpointer user_data) {
  (void)manager;
  GList **players = user_data;
  DEBUG_MSG("Received name-vanished for %s (instance: %s)", name->name, name->instance);

  GList *node = find_player_by_instance(*players, name->instance);
  if (node) {
    PlayerData *data = node->data;
    *players = g_list_delete_link(*players, node);
    DEBUG_MSG("Player vanished: %s (instance: %s, source: %d)", name->name, name->instance, name->source);
    print_player_list(*players);
    player_data_free(data);
    adjust_global_timer(players);
  } else {
    DEBUG_MSG("Player %s (instance: %s) not found in list", name->name, name->instance);
  }
}

int main(void) {
  g_usleep(500);
  GError *error = NULL;
  PlayerctlPlayerManager *manager = playerctl_player_manager_new(&error);
  if (error) {
    DEBUG_MSG("Failed to create player manager: %s", error->message);
    g_error_free(error);
    return 1;
  }

  GList *players = NULL;
  GList *current_players = playerctl_list_players(&error);
  if (error) {
    DEBUG_MSG("Failed to list initial players: %s", error->message);
    g_error_free(error);
  } else {
    DEBUG_MSG("Found %d initial players", g_list_length(current_players));
    for (GList *iter = current_players; iter; iter = iter->next) {
      PlayerData *data = player_data_new(iter->data, &players);
      players = g_list_append(players, data);
    }
    g_list_free_full(current_players, (GDestroyNotify)playerctl_player_name_free);
  }

  print_player_list(players);
  adjust_global_timer(&players);
  g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared), &players);
  g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished), &players);
  DEBUG_MSG("Listening for player events...");

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);

  if (global_position_timeout_id != 0) g_source_remove(global_position_timeout_id);
  g_list_free_full(players, player_data_free);
  g_main_loop_unref(loop);
  g_object_unref(manager);
  return 0;
}
