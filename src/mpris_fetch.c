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
#include <locale.h>
#include <playerctl/playerctl.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
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

// Structure to hold player data
typedef struct {
  gchar *app_name;
  gchar *name;
  gchar *instance;
  gint source;
  PlayerctlPlayer *player;
  // Cached player properties
  gboolean can_control;
  gboolean can_go_next;
  gboolean can_go_previous;
  gboolean can_pause;
  gboolean can_play;
  gboolean can_seek;
  // Cached metadata
  gchar *title;
  gchar *album;
  gchar *artist;
  gchar *art_url;
  int64_t length;
  // Shuffle and loop
  int shuffle;
  int loop_status;
  int playback_status;
  // PulseAudio fields
  uint32_t index;
  uint32_t sink;
  guint32 volume;
  gboolean mute;
  gboolean has_playerctl;
  // Polling timeout ID for artUrl
  guint art_url_polling_id;
  // For reset
  gboolean updated;
} PlayerData;

// Structure to hold PulseAudio context and data
typedef struct {
  pa_context *context;
  pa_glib_mainloop *mainloop;
  GList **players;
} PulseData;

// Structure to hold new player can_* check timeout data
typedef struct {
  PlayerData *player_data;
  PulseData *pulse;
  guint check_count;
  guint max_checks;
} TimeoutData;

// Structure to hold artUrl polling data
typedef struct {
  PlayerData *player_data;
  PulseData *pulse;
  gchar *art_url;
  guint check_count;
  guint max_checks;
  guint timeout_id;
} ArtUrlPollingData;

static guint pending_updates = 0;
static guint debounce_timeout_id = 0;
static gboolean resetting = FALSE;

static void print_player_list(GList *players);
static void trigger_print(PulseData *pulse, gboolean force);
static gboolean debounce_print(gpointer user_data);
static void update_metadata(PlayerData *data, PulseData *pulse);
static void on_playback_status(PlayerctlPlayer *player, int status,
                               gpointer user_data);
static void on_metadata(PlayerctlPlayer *player, GVariant *metadata,
                        gpointer user_data);
static void on_shuffle(PlayerctlPlayer *player, gboolean shuffle,
                       gpointer user_data);
static void on_loop_status(PlayerctlPlayer *player, PlayerctlLoopStatus status,
                           gpointer user_data);
static void player_data_free(gpointer data);
static PlayerData *find_player_by_app_name(GList *players, const gchar *key);
static GList *find_player_by_instance(GList *players, const gchar *instance);
static void initialize_player_properties(PlayerData *data, PulseData *pulse);

// Polling callback to check if artUrl file exists
static gboolean check_art_url_file(gpointer user_data) {
  ArtUrlPollingData *polling_data = user_data;
  PlayerData *data = polling_data->player_data;
  PulseData *pulse = polling_data->pulse;

  if (!data || !polling_data->art_url) {
    DEBUG_MSG("Player %s: Invalid polling data or artUrl, stopping polling",
              data->name);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  gboolean file_exists = g_file_test(polling_data->art_url, G_FILE_TEST_EXISTS);
  DEBUG_MSG("Player %s: Checking artUrl %s, exists=%d, attempt=%u/%u",
            data->name, polling_data->art_url, file_exists,
            polling_data->check_count + 1, polling_data->max_checks);

  if (file_exists) {
    DEBUG_MSG("Player %s: artUrl file %s now exists, updating JSON", data->name,
              polling_data->art_url);
    trigger_print(pulse, TRUE);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  polling_data->check_count++;
  if (polling_data->check_count >= polling_data->max_checks) {
    DEBUG_MSG("Player %s: Stopping artUrl polling for %s after %u attempts",
              data->name, polling_data->art_url, polling_data->check_count);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

// Timeout callback to check can_* properties
static gboolean check_can_properties(gpointer user_data) {
  TimeoutData *timeout_data = user_data;
  PlayerData *data = timeout_data->player_data;
  PulseData *pulse = timeout_data->pulse;

  if (!data->player) {
    DEBUG_MSG("Player %s: No player object, stopping timeout", data->name);
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
  }

  gboolean old_can_control = data->can_control;
  gboolean old_can_go_next = data->can_go_next;
  gboolean old_can_go_previous = data->can_go_previous;
  gboolean old_can_pause = data->can_pause;
  gboolean old_can_play = data->can_play;
  gboolean old_can_seek = data->can_seek;

  g_object_get(data->player, "can-control", &data->can_control, "can-go-next",
               &data->can_go_next, "can-go-previous", &data->can_go_previous,
               "can-pause", &data->can_pause, "can-play", &data->can_play,
               "can-seek", &data->can_seek, NULL);

  gboolean changed =
      (old_can_control != data->can_control ||
       old_can_go_next != data->can_go_next ||
       old_can_go_previous != data->can_go_previous ||
       old_can_pause != data->can_pause || old_can_play != data->can_play ||
       old_can_seek != data->can_seek);

  if (changed) {
    DEBUG_MSG("Player %s: can_* properties updated (control=%d, next=%d, "
              "prev=%d, pause=%d, play=%d, seek=%d)",
              data->name, data->can_control, data->can_go_next,
              data->can_go_previous, data->can_pause, data->can_play,
              data->can_seek);
    trigger_print(pulse, FALSE);
  }

  timeout_data->check_count++;
  if (timeout_data->check_count >= timeout_data->max_checks) {
    DEBUG_MSG("Player %s: Stopping can_* property checks after %u attempts",
              data->name, timeout_data->check_count);
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

// Helper function to find player by app_name or instance
static PlayerData *find_player_by_app_name(GList *players, const gchar *key) {
  for (GList *iter = players; iter; iter = iter->next) {
    PlayerData *data = iter->data;
    if (data->instance && key && strcmp(data->instance, key) == 0) {
      return data;
    }
    if (data->app_name && key && strcmp(data->app_name, key) == 0) {
      return data;
    }
  }
  return NULL;
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

// Initialize Playerctl properties and signals
static void initialize_player_properties(PlayerData *data, PulseData *pulse) {
  GError *error = NULL;

  g_object_get(data->player, "can-control", &data->can_control, "can-go-next",
               &data->can_go_next, "can-go-previous", &data->can_go_previous,
               "can-pause", &data->can_pause, "can-play", &data->can_play,
               "can-seek", &data->can_seek, NULL);

  // Initialize shuffle
  gboolean shuffle_value = FALSE;
  g_object_get(data->player, "shuffle", &shuffle_value, NULL);
  data->shuffle = shuffle_value ? 1 : 0;
  if (!shuffle_value) {
    playerctl_player_set_shuffle(data->player, TRUE, &error);
    if (error) {
      data->shuffle = -1;
      DEBUG_MSG("Error setting shuffle for %s: %s", data->name, error->message);
      shuffle_value = -1;
      g_error_free(error);
      error = NULL;
    } else {
      g_object_get(data->player, "shuffle", &shuffle_value, NULL);
      if (!shuffle_value) {
        data->shuffle = -1;
      } else {
        playerctl_player_set_shuffle(data->player, FALSE, NULL);
      }
    }
  }

  // Initialize loop status
  int loop_status = 0;
  g_object_get(data->player, "loop-status", &loop_status, NULL);
  data->loop_status = loop_status;
  if (!loop_status) {
    playerctl_player_set_loop_status(data->player, 1, &error);
    if (error) {
      data->loop_status = -1;
      DEBUG_MSG("Error setting loop status for %s: %s", data->name,
                error->message);
      loop_status = -1;
      g_error_free(error);
      error = NULL;
    } else {
      g_object_get(data->player, "loop-status", &loop_status, NULL);
      if (!loop_status) {
        data->loop_status = -1;
      } else {
        playerctl_player_set_loop_status(data->player, 0, NULL);
      }
    }
  }

  update_metadata(data, pulse);

  g_signal_connect(data->player, "playback-status",
                   G_CALLBACK(on_playback_status), pulse);
  g_signal_connect(data->player, "metadata", G_CALLBACK(on_metadata), pulse);
  if (shuffle_value != -1) {
    g_signal_connect(data->player, "shuffle", G_CALLBACK(on_shuffle), pulse);
  }
  if (loop_status != -1) {
    g_signal_connect(data->player, "loop-status", G_CALLBACK(on_loop_status),
                     pulse);
  }
}

static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i,
                               int eol, void *userdata) {
  PulseData *pulse = userdata;
  if (eol) {
    if (resetting) {
      GList *next;
      for (GList *iter = *pulse->players; iter; iter = next) {
        next = iter->next;
        PlayerData *d = iter->data;
        if (!d->updated) {
          *pulse->players = g_list_delete_link(*pulse->players, iter);
          player_data_free(d);
          trigger_print(pulse, FALSE);
        }
      }
      resetting = FALSE;
    }
    return;
  }

  if (!i)
    return;

  const char *app_name = pa_proplist_gets(i->proplist, "application.name");
  const char *media_name = pa_proplist_gets(i->proplist, "media.name");
  const char *binary_name =
      pa_proplist_gets(i->proplist, "application.process.binary");
  if (!app_name && !binary_name) {
    DEBUG_MSG("Skipping sink input with no app_name or binary_name: index=%u",
              i->index);
    return;
  }

  // Calculate normalized app_name
  gchar *normalized = g_ascii_strdown(app_name ? app_name : binary_name, -1);
  if (strcmp(normalized, "librewolf") == 0) {
    g_free(normalized);
    normalized = g_strdup("firefox");
  }

  // Find existing PlayerData by instance or app_name
  PlayerctlPlayerName *matched_name = NULL;
  GList *player_names = playerctl_list_players(NULL);
  for (GList *iter = player_names; iter; iter = iter->next) {
    PlayerctlPlayerName *player = iter->data;
    if ((app_name && player->name && strcasecmp(app_name, player->name) == 0) ||
        (app_name && player->instance &&
         strcasecmp(app_name, player->instance) == 0) ||
        (binary_name && player->name &&
         strcasecmp(binary_name, player->name) == 0) ||
        (media_name && player->name && strstr(media_name, player->name)) ||
        (app_name && player->name && strcasecmp(app_name, "libreWolf") == 0 &&
         strcasecmp(player->name, "firefox") == 0) ||
        (binary_name && player->name &&
         strcasecmp(binary_name, "librewolf") == 0 &&
         strcasecmp(player->name, "firefox") == 0)) {
      matched_name = player;
      break;
    }
  }

  PlayerData *player_data = NULL;
  if (matched_name) {
    GList *node =
        find_player_by_instance(*pulse->players, matched_name->instance);
    if (node) {
      player_data = node->data;
    }
  }
  if (!player_data) {
    player_data = find_player_by_app_name(*pulse->players, normalized);
  }

  if (!player_data) {
    player_data = g_new0(PlayerData, 1);
    player_data->app_name = g_strdup(normalized);
    player_data->name = g_strdup(app_name ? app_name : binary_name);
    *pulse->players = g_list_append(*pulse->players, player_data);
  }

  player_data->updated = TRUE;
  player_data->index = i->index;
  player_data->sink = i->sink;
  uint32_t volume = pa_cvolume_avg(&i->volume);
  player_data->volume = (volume * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
  player_data->mute = i->mute;

  if (matched_name && !player_data->player) {
    GError *error = NULL;
    player_data->name = g_strdup(matched_name->name);
    player_data->instance = g_strdup(matched_name->instance);
    player_data->source = matched_name->source;
    player_data->player = playerctl_player_new_from_name(matched_name, &error);
    player_data->has_playerctl = (error == NULL);
    if (error) {
      DEBUG_MSG("Failed to create player for %s: %s", matched_name->name,
                error->message);
      g_error_free(error);
      error = NULL;
    } else {
      initialize_player_properties(player_data, pulse);
    }
  }

  g_list_free_full(player_names, (GDestroyNotify)playerctl_player_name_free);
  g_free(normalized);

  DEBUG_MSG(
      "Updated player: index=%u, sink=%u, volume=%u, mute=%d, has_playerctl=%d",
      player_data->index, player_data->sink, player_data->volume,
      player_data->mute, player_data->has_playerctl);

  trigger_print(pulse, FALSE);
}
// PulseAudio subscription callback
static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                         uint32_t idx, void *userdata) {
  PulseData *pulse = userdata;
  pa_subscription_event_type_t facility =
      t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

  if (type != PA_SUBSCRIPTION_EVENT_CHANGE &&
      type != PA_SUBSCRIPTION_EVENT_NEW && type != PA_SUBSCRIPTION_EVENT_REMOVE)
    return;

  if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
    if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
      resetting = TRUE;
      for (GList *iter = *pulse->players; iter; iter = iter->next) {
        ((PlayerData *)iter->data)->updated = FALSE;
      }
      pa_operation *op =
          pa_context_get_sink_input_info_list(c, sink_input_info_cb, pulse);
      if (op)
        pa_operation_unref(op);
    } else {
      pa_operation *op =
          pa_context_get_sink_input_info(c, idx, sink_input_info_cb, pulse);
      if (op)
        pa_operation_unref(op);
    }
  }
}

// PulseAudio context state callback
static void context_state_cb(pa_context *c, void *userdata) {
  PulseData *pulse = userdata;
  pa_context_state_t state = pa_context_get_state(c);
  switch (state) {
  case PA_CONTEXT_READY:
    DEBUG_MSG("PulseAudio context ready");
    pa_context_set_subscribe_callback(c, subscribe_cb, pulse);
    pa_operation *op_sub =
        pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
    if (op_sub)
      pa_operation_unref(op_sub);
    resetting = TRUE;
    for (GList *iter = *pulse->players; iter; iter = iter->next) {
      ((PlayerData *)iter->data)->updated = FALSE;
    }
    pa_operation *op_input =
        pa_context_get_sink_input_info_list(c, sink_input_info_cb, pulse);
    if (op_input)
      pa_operation_unref(op_input);
    break;
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    DEBUG_MSG("PulseAudio context failed or terminated: %s",
              pa_strerror(pa_context_errno(c)));
    break;
  case PA_CONTEXT_CONNECTING:
  case PA_CONTEXT_AUTHORIZING:
  case PA_CONTEXT_SETTING_NAME:
    DEBUG_MSG("PulseAudio context state: %d", state);
    break;
  default:
    DEBUG_MSG("Unknown PulseAudio context state: %d", state);
    break;
  }
}

// Function to sanitize a string, ensuring it's valid UTF-8
static gchar *sanitize_utf8_string(const gchar *input) {
  if (!input)
    return NULL;
  if (g_utf8_validate(input, -1, NULL))
    return g_strdup(input);
  DEBUG_MSG("Invalid UTF-8 in string: %s", input);
  gchar *sanitized = g_utf8_make_valid(input, -1);
  DEBUG_MSG("Sanitized to: %s", sanitized);
  return sanitized;
}

// Helper function to update metadata and properties
static void update_metadata(PlayerData *data, PulseData *pulse) {
  g_free(data->title);
  g_free(data->album);
  g_free(data->artist);
  g_free(data->art_url);
  data->title = NULL;
  data->album = NULL;
  data->artist = NULL;
  data->art_url = NULL;
  data->length = 0;

  if (data->art_url_polling_id != 0) {
    g_source_remove(data->art_url_polling_id);
    data->art_url_polling_id = 0;
    DEBUG_MSG("Player %s: Cancelled existing artUrl polling", data->name);
  }

  if (!data->player)
    return;

  GError *error = NULL;

  gchar *raw_title = playerctl_player_get_title(data->player, &error);
  data->title = sanitize_utf8_string(raw_title);
  g_free(raw_title);
  if (error) {
    DEBUG_MSG("Failed to get title for %s: %s", data->name, error->message);
    g_error_free(error);
    error = NULL;
  }

  gchar *raw_album = playerctl_player_get_album(data->player, &error);
  data->album = sanitize_utf8_string(raw_album);
  g_free(raw_album);
  if (error) {
    DEBUG_MSG("Failed to get album for %s: %s", data->name, error->message);
    g_error_free(error);
    error = NULL;
  }

  gchar *raw_artist = playerctl_player_get_artist(data->player, &error);
  data->artist = sanitize_utf8_string(raw_artist);
  g_free(raw_artist);
  if (error) {
    DEBUG_MSG("Failed to get artist for %s: %s", data->name, error->message);
    g_error_free(error);
    error = NULL;
  }

  gchar *raw_art_url = playerctl_player_print_metadata_prop(
      data->player, "mpris:artUrl", &error);
  if (error) {
    DEBUG_MSG("Failed to get artUrl for %s: %s", data->name, error->message);
    g_error_free(error);
    error = NULL;
  } else if (raw_art_url) {
    if (g_str_has_prefix(raw_art_url, "file:///")) {
      data->art_url = g_strdup(raw_art_url + 7);
    } else if (g_str_has_prefix(raw_art_url, "https://i.scdn.co/image/")) {
      data->art_url =
          g_strconcat("/run/user/1000/album_art_cache", raw_art_url + 23, NULL);
    } else {
      data->art_url = g_strdup(raw_art_url);
    }
    g_free(raw_art_url);

    if (data->art_url && !g_file_test(data->art_url, G_FILE_TEST_EXISTS)) {
      DEBUG_MSG("Player %s: artUrl file %s does not exist, starting polling",
                data->name, data->art_url);
      ArtUrlPollingData *polling_data = g_new0(ArtUrlPollingData, 1);
      polling_data->player_data = data;
      polling_data->pulse = pulse;
      polling_data->art_url = g_strdup(data->art_url);
      polling_data->check_count = 0;
      polling_data->max_checks = 10;
      data->art_url_polling_id =
          g_timeout_add_seconds(1, check_art_url_file, polling_data);
      DEBUG_MSG("Player %s: Started polling for artUrl %s, timeout_id=%u",
                data->name, polling_data->art_url, data->art_url_polling_id);
    }
  }

  gchar *length_str = playerctl_player_print_metadata_prop(
      data->player, "mpris:length", &error);
  if (error) {
    DEBUG_MSG("Failed to get length for %s: %s", data->name, error->message);
    g_error_free(error);
    error = NULL;
  } else if (length_str) {
    char *endptr;
    data->length = g_ascii_strtoll(length_str, &endptr, 10);
    if (endptr == length_str || *endptr != '\0') {
      DEBUG_MSG("Failed to parse length for %s: %s", data->name, length_str);
      data->length = 0;
    }
    g_free(length_str);
  }

  g_object_get(data->player, "playback-status", &data->playback_status, NULL);

  if (data->shuffle != -1) {
    g_object_get(data->player, "shuffle", &data->shuffle, NULL);
  }

  if (data->loop_status != -1) {
    g_object_get(data->player, "loop-status", &data->loop_status, NULL);
  }

  if (pulse->context &&
      pa_context_get_state(pulse->context) == PA_CONTEXT_READY) {
    pa_operation *op = pa_context_get_sink_input_info_list(
        pulse->context, sink_input_info_cb, pulse);
    if (op)
      pa_operation_unref(op);
  }

  DEBUG_MSG(
      "Updated metadata for %s: title=%s, album=%s, artist=%s, artUrl=%s, "
      "length=%ld, shuffle=%i, loop=%i",
      data->name, data->title ? data->title : "none",
      data->album ? data->album : "none", data->artist ? data->artist : "none",
      data->art_url ? data->art_url : "none", data->shuffle, data->loop_status);
}

// Helper function to print the list of players as JSON
static void print_player_list(GList *players) {
  printf("[");
  gboolean first = TRUE;
  for (GList *iter = players; iter != NULL; iter = iter->next) {
    PlayerData *data = iter->data;
    if (!first) {
      printf(",");
    }
    first = FALSE;
    printf("{");
    printf("\"longName\":\"org.mpris.MediaPlayer2.%s\"",
           data->instance ? data->instance : "");
    printf(",\"name\":\"%s\"", data->name ? data->name : "");
    printf(",\"canControl\":%s", data->can_control ? "true" : "false");
    printf(",\"canGoNext\":%s", data->can_go_next ? "true" : "false");
    printf(",\"canGoPrevious\":%s", data->can_go_previous ? "true" : "false");
    printf(",\"canPause\":%s", data->can_pause ? "true" : "false");
    printf(",\"canPlay\":%s", data->can_play ? "true" : "false");
    printf(",\"canSeek\":%s", data->can_seek ? "true" : "false");
    printf(",\"playbackStatus\":%i", data->playback_status);
    printf(",\"title\":");
    print_json_str(data->title ? data->title : "");
    printf(",\"album\":");
    print_json_str(data->album ? data->album : "");
    printf(",\"artist\":");
    print_json_str(data->artist ? data->artist : "");
    printf(",\"artUrl\":\"%s\"", data->art_url ? data->art_url : "");
    printf(",\"length\":%" PRId64, data->length / 1000000);
    printf(",\"lengthHMS\":");
    char hms[32];
    to_hms(data->length, hms, sizeof(hms));
    print_json_str(hms);
    printf(",\"isShuffle\":%i", data->shuffle);
    printf(",\"loop\":%i", data->loop_status);
    printf(",\"index\":%u", data->index);
    printf(",\"sinkId\":%u", data->sink);
    printf(",\"volume\":%u", data->volume);
    printf(",\"isMute\":%s", data->mute ? "true" : "false");
    printf(",\"hasPlayerctl\":%s", data->has_playerctl ? "true" : "false");
    printf("}");
  }
  printf("]\n");
  fflush(stdout);
}

static void trigger_print(PulseData *pulse, gboolean force) {
  if (force) {
    if (debounce_timeout_id != 0) {
      g_source_remove(debounce_timeout_id);
      debounce_timeout_id = 0;
      pending_updates = 0;
    }
    print_player_list(*pulse->players);
  } else {
    pending_updates++;
    if (debounce_timeout_id == 0) {
      debounce_timeout_id = g_timeout_add(100, debounce_print, pulse);
    }
  }
}

static gboolean debounce_print(gpointer user_data) {
  PulseData *pulse = user_data;
  if (--pending_updates == 0) {
    print_player_list(*pulse->players);
    debounce_timeout_id = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

// Callback for the playback-status signal
static void on_playback_status(PlayerctlPlayer *player, int status,
                               gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d->player == player) {
      data = d;
      break;
    }
  }
  if (data && data->name && data->instance) {
    DEBUG_MSG("Player %s (instance: %s): Playback status changed to %i",
              data->name, data->instance, data->playback_status);
    update_metadata(data, pulse);
    trigger_print(pulse, FALSE);
  }
}

// Callback for the metadata signal
static void on_metadata(PlayerctlPlayer *player, GVariant *metadata,
                        gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d->player == player) {
      data = d;
      break;
    }
  }
  if (data && data->name && data->instance) {
    update_metadata(data, pulse);
    DEBUG_MSG("Player %s (instance: %s): Metadata signal fired (title: %s, "
              "length: %ld)",
              data->name, data->instance, data->title ? data->title : "none",
              data->length);
    trigger_print(pulse, FALSE);
  }
}

// Callback for the shuffle signal
static void on_shuffle(PlayerctlPlayer *player, gboolean shuffle,
                       gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d->player == player) {
      data = d;
      break;
    }
  }
  if (data && data->name && data->instance) {
    data->shuffle = shuffle;
    DEBUG_MSG("Player %s (instance: %s): Shuffle changed to %s", data->name,
              data->instance, shuffle ? "true" : "false");
    trigger_print(pulse, FALSE);
  }
}

// Callback for the loop-status signal
static void on_loop_status(PlayerctlPlayer *player, PlayerctlLoopStatus status,
                           gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d->player == player) {
      data = d;
      break;
    }
  }
  if (data && data->name && data->instance) {
    data->loop_status = status;
    DEBUG_MSG("Player %s (instance: %s): Loop status changed to %d", data->name,
              data->instance, data->loop_status);
    trigger_print(pulse, FALSE);
  }
}

// Helper function to create PlayerData from PlayerctlPlayerName
static PlayerData *player_data_new(PlayerctlPlayerName *name,
                                   PulseData *pulse) {
  GError *error = NULL;
  PlayerData *data = g_new0(PlayerData, 1);
  data->name = g_strdup(name->name);
  data->instance = g_strdup(name->instance);
  data->source = name->source;
  data->player = playerctl_player_new_from_name(name, &error);
  data->has_playerctl = (error == NULL);
  if (error) {
    DEBUG_MSG("Failed to create player for %s: %s", name->name, error->message);
    g_error_free(error);
    error = NULL;
  }
  if (data->player) {
    initialize_player_properties(data, pulse);
  }
  DEBUG_MSG("Created PlayerData for %s (instance: %s, player: %p, title: %s, "
            "length: %ld)",
            data->name, data->instance, data->player,
            data->title ? data->title : "none", data->length);
  return data;
}

// Helper function to free PlayerData
static void player_data_free(gpointer data) {
  PlayerData *player_data = data;
  if (player_data->art_url_polling_id != 0) {
    g_source_remove(player_data->art_url_polling_id);
    player_data->art_url_polling_id = 0;
    DEBUG_MSG("Player %s: Cancelled artUrl polling during free",
              player_data->name);
  }
  if (player_data->player) {
    g_object_unref(player_data->player);
  }
  g_free(player_data->app_name);
  g_free(player_data->name);
  g_free(player_data->instance);
  g_free(player_data->title);
  g_free(player_data->album);
  g_free(player_data->artist);
  g_free(player_data->art_url);
  g_free(player_data);
}

// Callback for the name-appeared signal
static void on_name_appeared(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, gpointer user_data) {
  PulseData *pulse = user_data;
  DEBUG_MSG("Received name-appeared for %s (instance: %s)", name->name,
            name->instance);

  gchar *normalized = g_ascii_strdown(name->name, -1);
  if (strcmp(normalized, "librewolf") == 0) {
    g_free(normalized);
    normalized = g_strdup("firefox");
  }

  // Check for existing player by instance or app_name
  GList *node = find_player_by_instance(*pulse->players, name->instance);
  PlayerData *player_data =
      node ? node->data : find_player_by_app_name(*pulse->players, normalized);

  if (player_data) {
    if (!player_data->player) {
      GError *error = NULL;
      g_free(player_data->name);
      g_free(player_data->instance);
      player_data->name = g_strdup(name->name);
      player_data->instance = g_strdup(name->instance);
      player_data->source = name->source;
      player_data->player = playerctl_player_new_from_name(name, &error);
      player_data->has_playerctl = (error == NULL);
      if (error) {
        DEBUG_MSG("Failed to create player for %s: %s", name->name,
                  error->message);
        g_error_free(error);
        error = NULL;
      } else {
        initialize_player_properties(player_data, pulse);
        trigger_print(pulse, FALSE);
      }
    }
  } else {
    player_data = player_data_new(name, pulse);
    player_data->app_name = g_strdup(normalized);
    *pulse->players = g_list_append(*pulse->players, player_data);
    trigger_print(pulse, FALSE);

    TimeoutData *timeout_data = g_new0(TimeoutData, 1);
    timeout_data->player_data = player_data;
    timeout_data->pulse = pulse;
    timeout_data->check_count = 0;
    timeout_data->max_checks = 2;
    g_timeout_add_seconds(1, check_can_properties, timeout_data);
  }
  g_free(normalized);
}

// Callback for the name-vanished signal
static void on_name_vanished(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, gpointer user_data) {
  PulseData *pulse = user_data;
  DEBUG_MSG("Received name-vanished for %s (instance: %s)", name->name,
            name->instance);
  GList *node = find_player_by_instance(*pulse->players, name->instance);
  if (node != NULL) {
    PlayerData *data = node->data;
    if (data->player) {
      g_object_unref(data->player);
      data->player = NULL;
      g_free(data->instance);
      data->instance = NULL;
      data->source = 0;
      data->has_playerctl = FALSE;
      update_metadata(data, pulse);
      DEBUG_MSG("Player vanished but sink input remains: %s", data->name);
      trigger_print(pulse, FALSE);
      if (data->index == 0) {
        *pulse->players = g_list_remove(*pulse->players, data);
        player_data_free(data);
        trigger_print(pulse, FALSE);
      }
    }
  }
}

// Free PulseData
static void pulse_data_free(PulseData *pulse) {
  if (pulse->context) {
    pa_context_disconnect(pulse->context);
    pa_context_unref(pulse->context);
  }
  if (pulse->mainloop) {
    pa_glib_mainloop_free(pulse->mainloop);
  }
  g_free(pulse);
}

// Initialize PulseAudio
static PulseData *pulse_data_new(GList **players) {
  PulseData *pulse = g_new0(PulseData, 1);
  pulse->players = players;
  pulse->mainloop = pa_glib_mainloop_new(NULL);
  if (!pulse->mainloop) {
    DEBUG_MSG("Failed to create PulseAudio GLib mainloop");
    pulse_data_free(pulse);
    return NULL;
  }
  pulse->context =
      pa_context_new(pa_glib_mainloop_get_api(pulse->mainloop), "mprisFetch");
  if (!pulse->context) {
    DEBUG_MSG("Failed to create PulseAudio context");
    pulse_data_free(pulse);
    return NULL;
  }
  pa_context_set_state_callback(pulse->context, context_state_cb, pulse);
  if (pa_context_connect(pulse->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
    DEBUG_MSG("Failed to connect PulseAudio context: %s",
              pa_strerror(pa_context_errno(pulse->context)));
    pulse_data_free(pulse);
    return NULL;
  }
  return pulse;
}

int main(void) {
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

  PlayerctlPlayerManager *manager = playerctl_player_manager_new(NULL);
  if (!manager) {
    DEBUG_MSG("Failed to create player manager");
    g_main_loop_unref(loop);
    return 1;
  }

  GList *players = NULL;
  PulseData *pulse = pulse_data_new(&players);
  if (!pulse) {
    DEBUG_MSG("Failed to initialize PulseAudio");
    g_object_unref(manager);
    g_main_loop_unref(loop);
    return 1;
  }

  GList *current_players = playerctl_list_players(NULL);
  if (current_players) {
    DEBUG_MSG("Found %d initial players", g_list_length(current_players));
    for (GList *iter = current_players; iter != NULL; iter = iter->next) {
      on_name_appeared(manager, (PlayerctlPlayerName *)iter->data, pulse);
    }
    g_list_free_full(current_players,
                     (GDestroyNotify)playerctl_player_name_free);
  }

  trigger_print(pulse, FALSE);

  g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared),
                   pulse);
  g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished),
                   pulse);

  DEBUG_MSG("Listening for player and PulseAudio events...");

  g_main_loop_run(loop);

  g_list_free_full(players, player_data_free);
  g_main_loop_unref(loop);
  g_object_unref(manager);
  pulse_data_free(pulse);

  return 0;
}
