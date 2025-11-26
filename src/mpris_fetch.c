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
#include <dbus/dbus.h>
#include <json-glib/json-glib.h>
#include <playerctl/playerctl.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
    printf(fmt "\n", ##__VA_ARGS__);                                           \
  } while (0)
static const gchar *safe_str(const gchar *s) { return s ? s : ""; }
#else
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)
#endif

/* Constants to make behavior easier to tweak */
#define ART_POLL_INTERVAL_SEC 1
#define ART_POLL_MAX_CHECKS 10
#define CAN_CHECK_INTERVAL_SEC 1
#define CAN_CHECK_MAX_ATTEMPTS 2

/* Small helpers */
static unsigned int pa_volume_to_percent(const pa_cvolume *v) {
  guint32 avg = pa_cvolume_avg(v);
  return (avg * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
}

/* Structure to hold player data */
typedef struct {
  gchar *name;
  gchar *display_name;
  gchar *instance;
  gint source;
  PlayerctlPlayer *player;
  /* Cached MediaPlayer2 properties */
  gboolean can_quit;
  /* Cached player properties */
  gboolean can_control;
  gboolean can_go_next;
  gboolean can_go_previous;
  gboolean can_pause;
  gboolean can_play;
  gboolean can_seek;
  gint64 position;
  /* Cached metadata */
  gchar *title;
  gchar *album;
  gchar *artist;
  gchar *art_url;
  gint64 length;
  /* Shuffle and loop */
  gint shuffle;
  gint loop_status;
  gint playback_status;
  /* PulseAudio fields */
  guint32 index;
  guint32 sink;
  guint32 volume;
  gboolean mute;
  /* Polling timeout ID for artUrl */
  guint art_url_polling_id;
} PlayerData;

/* Structure to hold PulseAudio context and data */
typedef struct {
  pa_context *context;
  pa_glib_mainloop *mainloop;
  GList **players;
} PulseData;

/* Structure to hold artUrl polling data */
typedef struct {
  PlayerData *player_data;
  PulseData *pulse;
  gchar *art_url;
  guint check_count;
  guint max_checks;
} ArtUrlPollingData;

/* Store last JSON output for change detection */
static gchar *last_json_output = NULL;
static guint debounce_timeout_id = 0;

/* Forward declarations */
static void print_player_list(GList *players, gboolean force_output);
static void update_metadata(PlayerData *data, PulseData *pulse);
static int check_can_loop(PlayerData *data);
static int check_can_shuffle(PlayerData *data);

/* Polling callback to check if artUrl file exists */
static gboolean check_art_url_file(gpointer user_data) {
  ArtUrlPollingData *polling_data = user_data;
  if (!polling_data) {
    return G_SOURCE_REMOVE;
  }
  PlayerData *data = polling_data->player_data;
  PulseData *pulse = polling_data->pulse;

  if (!polling_data->art_url || !data) {
    DEBUG_MSG("check_art_url_file: invalid polling data (art_url=%p, data=%p)",
              (void *)polling_data->art_url, (void *)data);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  gboolean file_exists = g_file_test(polling_data->art_url, G_FILE_TEST_EXISTS);
  DEBUG_MSG("Player %s: Checking artUrl %s, exists=%d, attempt=%u/%u",
            safe_str(data->name), polling_data->art_url, file_exists,
            polling_data->check_count + 1, polling_data->max_checks);

  if (file_exists) {
    DEBUG_MSG("Player %s: artUrl file %s now exists, updating JSON",
              safe_str(data->name), polling_data->art_url);
    /* Force update so UI picks up the new file immediately */
    print_player_list(*pulse->players, TRUE);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  polling_data->check_count++;
  if (polling_data->check_count >= polling_data->max_checks) {
    DEBUG_MSG("Player %s: Stopping artUrl polling for %s after %u attempts",
              safe_str(data->name), polling_data->art_url,
              polling_data->check_count);
    g_free(polling_data->art_url);
    g_free(polling_data);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

// Convert seconds to HMS (MM:SS or H:MM:SS), or "live" for specified max
void to_hms(int64_t s, int64_t position, char *hms, size_t hms_size) {
  /* HTML video players treat "live" playback a few ways:
   * 0 is "live" position unchanging and -n is a decrementing duration;
   * position & duration increment together "live" if position == duration;
   * We include >= duration because position is handled independent from the
   * browser.Which results in over increments as duration isn't a subscribed
   * event meaning the position will increment but duration will be static.
   * This could be changed but would result unnecessary gtk redraws with no
   * visible UI changes.
   * */
  if (s < 0 || (s > 0 && position >= s) || s == INT64_MAX) {
    snprintf(hms, hms_size, "live");
    return;
  }
  if (s == 0) {
    snprintf(hms, hms_size, "0:00");
    return;
  }
  long hours = s / 3600;
  long minutes = (s / 60) % 60;
  long seconds = s % 60;
  if (hours > 0) {
    snprintf(hms, hms_size, "%ld:%02ld:%02ld", hours, minutes, seconds);
  } else {
    snprintf(hms, hms_size, "%ld:%02ld", minutes, seconds);
  }
}

static gboolean match_player(PulseData *pulse, const char *name, PlayerData **out_player)
{
  if (!name || !out_player) return FALSE;
  *out_player = NULL;

  for (GList *iter = *pulse->players; iter; iter = iter->next) {
    PlayerData *player = iter->data;
    if (player && player->name && g_ascii_strcasecmp(name, player->name) == 0) {
      *out_player = player;
      return TRUE;
    }
  }
  return FALSE;
}

/* PulseAudio sink input info callback */
static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i,
                               int eol, void *userdata) {
  (void)c; // suppress unused paramater warning
  PulseData *pulse = userdata;
  if (eol || !i || !pulse) {
    return;
  }

  if (i->corked) {
    DEBUG_MSG("Skipping corked sink input: index=%u", i->index);
    return;
  }

  const char *binary_name = pa_proplist_gets(i->proplist, "application.process.binary");
  const char *fallback_name = pa_proplist_gets(i->proplist, "application.name");

  if (!binary_name) {
    if (!fallback_name) {
      DEBUG_MSG("Skipping sink input with no binary_name or fallback_name: index=%u",
              i->index);
      return;
    } else {
      binary_name = fallback_name;
    }
  }

  DEBUG_MSG("Sink input: index=%u, binary_name=%s, fallback_name=%s",
            i->index, safe_str(binary_name), safe_str(fallback_name));

  /* Find matching player */
  PlayerData *matched_player = NULL;

  match_player(pulse, binary_name, &matched_player);

  /* Browser remap table */
  if (!matched_player) {
    if (g_ascii_strcasecmp(binary_name, "chrome") == 0 ||
        g_ascii_strcasecmp(binary_name, "opera") == 0) {
        binary_name = "chromium";
        match_player(pulse, binary_name, &matched_player);
    } else if (g_ascii_strcasecmp(binary_name, "librewolf") == 0 ||
                 g_ascii_strcasecmp(binary_name, "zen-bin") == 0) {
        binary_name = "firefox";
        match_player(pulse, binary_name, &matched_player);
    } else if (g_ascii_strcasecmp(binary_name, "msedge") == 0) {
        binary_name = "edge";
        match_player(pulse, binary_name, &matched_player);
    }
  }

  if (!matched_player) {
    /* Create default PlayerData for unrecognized sink input */
    PlayerData *default_player = g_new0(PlayerData, 1);
    default_player->name = g_strdup(binary_name ? binary_name : "Unknown");
    default_player->display_name = g_strdup(fallback_name);
    default_player->index = i->index;
    default_player->sink = i->sink;
    default_player->volume = pa_volume_to_percent(&i->volume);
    default_player->mute = i->mute;

    *pulse->players = g_list_append(*pulse->players, default_player);
  } else {
    /* Update existing player with sink input info */
    g_free(matched_player->display_name);
    matched_player->display_name = g_strdup(fallback_name);
    matched_player->index = i->index;
    matched_player->sink = i->sink;
    matched_player->volume = pa_volume_to_percent(&i->volume);
    matched_player->mute = i->mute;
  }

  print_player_list(*pulse->players, FALSE);
}

/* PulseAudio subscription callback */
static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                         guint32 idx, void *userdata) {
  PulseData *pulse = userdata;
  pa_subscription_event_type_t facility =
      t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

  if (type != PA_SUBSCRIPTION_EVENT_CHANGE && type != PA_SUBSCRIPTION_EVENT_NEW) {
    return;
  }

  if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
    pa_operation *op =
        pa_context_get_sink_input_info(c, idx, sink_input_info_cb, pulse);
    if (op) {
      pa_operation_unref(op);
    }
  }
}

/* PulseAudio context state callback */
static void context_state_cb(pa_context *c, void *userdata) {
  PulseData *pulse = userdata;
  pa_context_state_t state = pa_context_get_state(c);
  switch (state) {
  case PA_CONTEXT_READY:
    DEBUG_MSG("PulseAudio context ready");
    pa_context_set_subscribe_callback(c, subscribe_cb, pulse);
    {
      pa_operation *op_sub = pa_context_subscribe(
          c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL,
          NULL);
      if (op_sub) {
        pa_operation_unref(op_sub);
      }
    }
    {
      pa_operation *op_input =
          pa_context_get_sink_input_info_list(c, sink_input_info_cb, pulse);
      if (op_input) {
        pa_operation_unref(op_input);
      }
    }
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

/* Helper function to update metadata and properties */
static void update_metadata(PlayerData *data, PulseData *pulse) {
  if (!data) {
    return;
  }

  /* Free existing metadata */
  g_free(data->title);
  g_free(data->album);
  g_free(data->artist);
  g_free(data->art_url);
  data->title = data->album = data->artist = data->art_url = NULL;

  if (data->art_url_polling_id != 0) {
    g_source_remove(data->art_url_polling_id);
    data->art_url_polling_id = 0;
    DEBUG_MSG("Player %s: Cancelled existing artUrl polling",
              safe_str(data->name));
  }

  if (!data->player) {
    DEBUG_MSG("No player for %s", safe_str(data->name));
    return;
  }

  g_object_get(data->player, "can-control", &data->can_control, "can-go-next",
               &data->can_go_next, "can-go-previous", &data->can_go_previous,
               "can-pause", &data->can_pause, "can-play", &data->can_play,
               "can-seek", &data->can_seek, "playback-status",
               &data->playback_status, NULL);

  GError *error = NULL;

  data->title = playerctl_player_get_title(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get title for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  }

  data->album = playerctl_player_get_album(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get album for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  }

  data->artist = playerctl_player_get_artist(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get artist for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  }

  /* Art URL */
  gchar *raw_art_url = playerctl_player_print_metadata_prop(
      data->player, "mpris:artUrl", &error);
  if (error) {
    DEBUG_MSG("Failed to get artUrl for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  } else if (raw_art_url) {
    if (g_str_has_prefix(raw_art_url, "file:///")) {
      data->art_url = g_strdup(raw_art_url + 7); /* "file://" */
    } else if (g_str_has_prefix(raw_art_url, "https://i.scdn.co/image/")) {
      /* Map common Spotify CDN to local cache path */
      data->art_url =
          g_strconcat("/run/user/1000/album_art_cache", raw_art_url + 23, NULL);
    } else {
      data->art_url = g_strdup(raw_art_url);
    }
    g_free(raw_art_url);
    raw_art_url = NULL;

    if (data->art_url &&
        strlen(data->art_url) > 0 &&
        !g_str_has_prefix(data->art_url, "data:image/") &&
        !g_file_test(data->art_url, G_FILE_TEST_EXISTS))  {
      DEBUG_MSG("Player %s: artUrl file %s does not exist, starting polling",
                safe_str(data->name), data->art_url);
      ArtUrlPollingData *polling_data = g_new0(ArtUrlPollingData, 1);
      polling_data->player_data = data;
      polling_data->pulse = pulse;
      polling_data->art_url = g_strdup(data->art_url);
      polling_data->check_count = 0;
      polling_data->max_checks = ART_POLL_MAX_CHECKS;
      data->art_url_polling_id = g_timeout_add_seconds(
          ART_POLL_INTERVAL_SEC, check_art_url_file, polling_data);
      DEBUG_MSG("Player %s: Started polling for artUrl %s, timeout_id=%u",
                safe_str(data->name), polling_data->art_url,
                data->art_url_polling_id);
    }
  }

  /* Length */
  gchar *length_str = playerctl_player_print_metadata_prop(
      data->player, "mpris:length", &error);
  if (error) {
    DEBUG_MSG("Failed to get length for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  } else if (length_str) {
    char *endptr = NULL;
    /* microseconds to ceiling second */
    gint64 len = (gint64)strtod(length_str, &endptr);
    data->length = len <= INT64_MAX - 999999 ? (len + 999999) / 1000000 : INT64_MAX;
    if (endptr == length_str || *endptr != '\0') {
      DEBUG_MSG("Failed to parse length for %s: %s", safe_str(data->name),
                length_str);
      data->length = 0;
    }
    g_free(length_str);
    length_str = NULL;
  }

  /* Position (unfollowed we only use this for flagging LIVE) */
  data->position = playerctl_player_get_position(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get position for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  } else {
    /* microseconds to ceiling second */
    data->position = ((data->position + 999999) / 1000000);
  }

  /* PulseAudio update */
  if (pulse && pulse->context &&
      pa_context_get_state(pulse->context) == PA_CONTEXT_READY) {
    pa_operation *op = pa_context_get_sink_input_info_list(
        pulse->context, sink_input_info_cb, pulse);
    if (op) {
      pa_operation_unref(op);
    }
  }

  DEBUG_MSG("\nUpdated metadata for %s: title=%s, album=%s, artist=%s, "
            "artUrl=%s, length=%ld",
            safe_str(data->name), safe_str(data->title), safe_str(data->album),
            safe_str(data->artist), safe_str(data->art_url),
            (long)data->length);
}

static gboolean print_callback(gpointer user_data) {
  gchar *json_str = (gchar *)user_data;
  g_print("%s\n", json_str);
  g_free(last_json_output);
  last_json_output = g_strdup(json_str);
  g_free(json_str);
  debounce_timeout_id = 0;
  return FALSE;
}
/* Helper function to print the list of players as JSON */
static void print_player_list(GList *players, gboolean force_output) {
  JsonBuilder *builder = json_builder_new();
  json_builder_begin_array(builder);

  for (GList *iter = players; iter != NULL; iter = iter->next) {
    PlayerData *data = iter->data;
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "instance");
    json_builder_add_string_value(builder, data->instance);

    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, data->display_name ? data->display_name : data->name);

    json_builder_set_member_name(builder, "canQuit");
    json_builder_add_boolean_value(builder, data->can_quit);

    json_builder_set_member_name(builder, "canControl");
    json_builder_add_boolean_value(builder, data->can_control);

    json_builder_set_member_name(builder, "canGoNext");
    json_builder_add_boolean_value(builder, data->can_go_next);

    json_builder_set_member_name(builder, "canGoPrevious");
    json_builder_add_boolean_value(builder, data->can_go_previous);

    json_builder_set_member_name(builder, "canPause");
    json_builder_add_boolean_value(builder, data->can_pause);

    json_builder_set_member_name(builder, "canPlay");
    json_builder_add_boolean_value(builder, data->can_play);

    json_builder_set_member_name(builder, "canSeek");
    json_builder_add_boolean_value(builder, data->can_seek);

    json_builder_set_member_name(builder, "playbackStatus");
    json_builder_add_int_value(builder, data->playback_status);

    json_builder_set_member_name(builder, "title");
    json_builder_add_string_value(builder, data->title ? data->title : "");

    json_builder_set_member_name(builder, "album");
    json_builder_add_string_value(builder, data->album ? data->album : "");

    json_builder_set_member_name(builder, "artist");
    json_builder_add_string_value(builder, data->artist ? data->artist : "");

    json_builder_set_member_name(builder, "artUrl");
    json_builder_add_string_value(builder, data->art_url ? data->art_url : "");

    json_builder_set_member_name(builder, "position");
    json_builder_add_int_value(builder, data->position);

    json_builder_set_member_name(builder, "length");
    json_builder_add_int_value(builder, data->length);

    json_builder_set_member_name(builder, "lengthHMS");
    char hms[32] = "";
    to_hms(data->length, data->position, hms, sizeof(hms));
    json_builder_add_string_value(builder, hms);

    json_builder_set_member_name(builder, "shuffle");
    json_builder_add_int_value(builder, data->shuffle);

    json_builder_set_member_name(builder, "loop");
    json_builder_add_int_value(builder, data->loop_status);

    json_builder_set_member_name(builder, "index");
    json_builder_add_int_value(builder, data->index);

    json_builder_set_member_name(builder, "sinkId");
    json_builder_add_int_value(builder, data->sink);

    json_builder_set_member_name(builder, "volume");
    json_builder_add_int_value(builder, data->volume);

    json_builder_set_member_name(builder, "isMute");
    json_builder_add_boolean_value(builder, data->mute);

    json_builder_end_object(builder);
  }

  json_builder_end_array(builder);

  JsonNode *root = json_builder_get_root(builder);
  JsonGenerator *generator = json_generator_new();
  json_generator_set_root(generator, root);
  json_generator_set_pretty(generator, FALSE);
  gchar *json_str = json_generator_to_data(generator, NULL);

  /* should print? */
  if (!(force_output || last_json_output == NULL || strcmp(json_str, last_json_output) != 0)) {
    g_free(json_str);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return;
  }

  /* Debounce outputs */
  if (debounce_timeout_id != 0) {
    g_source_remove(debounce_timeout_id);
    debounce_timeout_id = 0;
  }

  if (force_output) {
    g_print("%s\n", json_str);
    g_free(last_json_output);
    last_json_output = g_strdup(json_str);
  } else {
    debounce_timeout_id = g_timeout_add(50, print_callback, g_strdup(json_str));
  }

  g_free(json_str);
  json_node_free(root);
  g_object_unref(generator);
  g_object_unref(builder);
}

static PlayerData *find_player_data(PulseData *pulse, PlayerctlPlayer *player) {
  if (!pulse) {
    return NULL;
  }
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d && d->player == player) {
      return d;
    }
  }
  return NULL;
}

/* Callback for the playback-status signal */
static void on_playback_status(PlayerctlPlayer *player,
                               PlayerctlPlaybackStatus status,
                               gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = find_player_data(pulse, player);
  if (data) {
    data->playback_status = status;
    DEBUG_MSG("\nUpdating playback status for %s", safe_str(data->name));
    print_player_list(*pulse->players, FALSE);
  }
}

/* Callback for the metadata signal */
static void on_metadata(PlayerctlPlayer *player, GVariant *metadata,
                        gpointer user_data) {
  (void)metadata; // suppress unused paramater warning
  PulseData *pulse = user_data;
  PlayerData *data = find_player_data(pulse, player);
  if (data) {
    update_metadata(data, pulse);
    DEBUG_MSG("\nUpdating Metadata for %s", safe_str(data->name));
    print_player_list(*pulse->players, FALSE);
  }
}

/* Callback for the shuffle signal */
static void on_shuffle(PlayerctlPlayer *player, gboolean shuffle,
                       gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = find_player_data(pulse, player);
  if (data) {
    data->shuffle = shuffle;
    DEBUG_MSG("\nUpdating shuffle status for %s: %d", safe_str(data->name), shuffle);
    print_player_list(*pulse->players, FALSE);
  }
}

/* Callback for the loop-status signal */
static void on_loop_status(PlayerctlPlayer *player, PlayerctlLoopStatus status,
                           gpointer user_data) {
  PulseData *pulse = user_data;
  PlayerData *data = find_player_data(pulse, player);
  if (data) {
    data->loop_status = status;
    DEBUG_MSG("\nUpdating loop status for %s: %d", safe_str(data->name), status);
    print_player_list(*pulse->players, FALSE);
  }
}

/* Determines if the player actually supports shuffle and follows state */
static int check_can_shuffle(PlayerData *data) {
  gboolean initial = FALSE;
  g_object_get(data->player, "shuffle", &initial, NULL);

  if (initial) {
    data->shuffle = initial;
    return EXIT_SUCCESS;
  }

  GError *error = NULL;
  playerctl_player_set_shuffle(data->player, TRUE, &error);
  if (error) {
    data->shuffle = -1;
    g_error_free(error);
    return EXIT_FAILURE;
  }

  gboolean after = FALSE;
  g_object_get(data->player, "shuffle", &after, NULL);
  if (after == initial) {
    data->shuffle = -1;
  } else {
    data->shuffle = initial;
  }
  playerctl_player_set_shuffle(data->player, initial, NULL);
  return EXIT_SUCCESS;
}

/* Determines if the player actually supports loop and follows state */
static int check_can_loop(PlayerData *data) {
  PlayerctlLoopStatus initial = PLAYERCTL_LOOP_STATUS_NONE;
  g_object_get(data->player, "loop-status", &initial, NULL);

  if (initial) {
    data->loop_status = initial;
    return EXIT_SUCCESS;
  }

  GError *error = NULL;
  playerctl_player_set_loop_status(data->player, PLAYERCTL_LOOP_STATUS_PLAYLIST, &error);
  if (error) {
    data->loop_status = -1;
    g_error_free(error);
    return EXIT_FAILURE;
  }

  PlayerctlLoopStatus after = PLAYERCTL_LOOP_STATUS_NONE;
  g_object_get(data->player, "loop-status", &after, NULL);
  if (after == initial) {
    data->loop_status = -1;
  } else {
    data->loop_status = initial;
  }
  playerctl_player_set_loop_status(data->player, initial, NULL);
  return EXIT_SUCCESS;
}

dbus_bool_t get_can_quit(const char *interface) {
    static const char *interface_str = "org.mpris.MediaPlayer2";
    static const char *property_str = "CanQuit";

    char dest[256];
    snprintf(dest, sizeof(dest), "org.mpris.MediaPlayer2.%s", interface);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
    DBusMessage *msg = dbus_message_new_method_call(
        dest, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "Get");
    dbus_message_append_args(msg,
        DBUS_TYPE_STRING, &interface_str,
        DBUS_TYPE_STRING, &property_str, DBUS_TYPE_INVALID);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, NULL);
    dbus_message_unref(msg);

    dbus_bool_t value = FALSE;
    if (reply) {
        DBusMessageIter iter, sub;
        dbus_message_iter_init(reply, &iter);
        dbus_message_iter_recurse(&iter, &sub);
        dbus_message_iter_get_basic(&sub, &value);
        dbus_message_unref(reply);
    }
    dbus_connection_unref(conn);
    return value;
}

/* Helper function to create PlayerData from PlayerctlPlayerName */
static PlayerData *player_data_new(PlayerctlPlayerName *name,
                                   PulseData *pulse, gboolean *is_new) {
  if (!name || !is_new) {
    return NULL;
  }
  *is_new = TRUE;
  PlayerData *existing = NULL;
  for (GList *iter = *pulse->players; iter; iter = iter->next) {
    PlayerData *d = iter->data;
    if (g_ascii_strcasecmp(d->name, name->name) == 0) {
      existing = d;
      break;
    }
  }
  PlayerData *data;
  if (existing) {
    data = existing;
    *is_new = FALSE;
    g_free(data->instance);
    data->instance = g_strdup(name->instance ? name->instance : "");
    data->can_quit = get_can_quit(data->instance);
    data->source = name->source;
  } else {
    data = g_new0(PlayerData, 1);
    data->name = g_strdup(name->name ? name->name : "Unknown");
    data->instance = g_strdup(name->instance ? name->instance : "");
    data->can_quit = get_can_quit(data->instance);
    data->source = name->source;
  }
  GError *error = NULL;
  data->player = playerctl_player_new_from_name(name, &error);
  if (error) {
    DEBUG_MSG("Failed to create player for %s: %s", safe_str(name->name),
              error->message);
    g_error_free(error);
  }
  if (data->player) {
    if (check_can_shuffle(data) == 0) {
      g_signal_connect(data->player, "shuffle", G_CALLBACK(on_shuffle), pulse);
    }
    if (check_can_loop(data) == 0 ) {
      g_signal_connect(data->player, "loop-status", G_CALLBACK(on_loop_status), pulse);
    }
    g_signal_connect(data->player, "playback-status",
                     G_CALLBACK(on_playback_status), pulse);
    g_signal_connect(data->player, "metadata", G_CALLBACK(on_metadata), pulse);
    update_metadata(data, pulse);
  }
  DEBUG_MSG("Created/Updated PlayerData for %s (instance: %s)",
            safe_str(data->name), safe_str(data->instance));
  return data;
}

/* Helper function to free PlayerData */
static void player_data_free(gpointer data) {
  PlayerData *player_data = data;
  if (!player_data) {
    return;
  }

  /* Cancel any active artUrl polling */
  if (player_data->art_url_polling_id != 0) {
    g_source_remove(player_data->art_url_polling_id);
    player_data->art_url_polling_id = 0;
    DEBUG_MSG("Player %s: Cancelled artUrl polling during free",
              safe_str(player_data->name));
  }

  if (player_data->player) {
    g_object_unref(player_data->player);
    player_data->player = NULL;
  }
  g_free(player_data->name);
  g_free(player_data->display_name);
  g_free(player_data->instance);
  g_free(player_data->title);
  g_free(player_data->album);
  g_free(player_data->artist);
  g_free(player_data->art_url);
  g_free(player_data);
}

/* Helper function to find a player by instance */
static GList *find_player_by_instance(GList *players, const gchar *instance) {
  if (!instance) {
    return NULL;
  }
  for (GList *iter = players; iter != NULL; iter = iter->next) {
    PlayerData *data = iter->data;
    if (data && data->instance && strcmp(data->instance, instance) == 0) {
      return iter;
    }
  }
  return NULL;
}

/* Callback for the name-appeared signal */
static void on_name_appeared(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, gpointer user_data) {
  (void)manager;
  PulseData *pulse = user_data;
  if (!pulse || !name) {
    return;
  }
  DEBUG_MSG("Received name-appeared for %s (instance: %s)",
            safe_str(name->name), safe_str(name->instance));
  if (find_player_by_instance(*pulse->players, name->instance) == NULL) {
    gboolean is_new;
    PlayerData *data = player_data_new(name, pulse, &is_new);
    if (data && is_new) {
      *pulse->players = g_list_append(*pulse->players, data);
    }
    print_player_list(*pulse->players, FALSE);
  } else {
    DEBUG_MSG("Player %s (instance: %s) already exists, skipping",
              safe_str(name->name), safe_str(name->instance));
  }
}

/* Callback for the name-vanished signal */
static void on_name_vanished(PlayerctlPlayerManager *manager,
                             PlayerctlPlayerName *name, gpointer user_data) {
  (void)manager; // suppress unused paramater warning
  PulseData *pulse = user_data;
  if (!pulse || !name) {
    return;
  }

  DEBUG_MSG("Received name-vanished for %s (instance: %s)",
            safe_str(name->name), safe_str(name->instance));
  GList *node = find_player_by_instance(*pulse->players, name->instance);
  if (node != NULL) {
    PlayerData *data = node->data;
    *pulse->players = g_list_delete_link(*pulse->players, node);
    DEBUG_MSG("Player vanished: %s (instance: %s, source: %d)",
              safe_str(data->name), safe_str(data->instance), data->source);
    player_data_free(data);
  } else {
    DEBUG_MSG("Player %s (instance: %s) not found in list",
              safe_str(name->name), safe_str(name->instance));
  }

  print_player_list(*pulse->players, FALSE);
}

/* Free PulseData */
static void pulse_data_free(PulseData *pulse) {
  if (!pulse) {
    return;
  }
  if (pulse->context) {
    pa_context_disconnect(pulse->context);
    pa_context_unref(pulse->context);
    pulse->context = NULL;
  }
  if (pulse->mainloop) {
    pa_glib_mainloop_free(pulse->mainloop);
    pulse->mainloop = NULL;
  }
  g_free(pulse);
}

/* Initialize PulseAudio */
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
  GError *error = NULL;

  /* Initialize GLib main loop */
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);

  /* Initialize playerctl manager */
  PlayerctlPlayerManager *manager = playerctl_player_manager_new(&error);
  if (error != NULL) {
    DEBUG_MSG("Failed to create player manager: %s", error->message);
    g_error_free(error);
    g_main_loop_unref(loop);
    return 1;
  }

  /* Initialize players list */
  GList *players = NULL;

  /* Initialize PulseAudio */
  PulseData *pulse = pulse_data_new(&players);
  if (!pulse) {
    DEBUG_MSG("Failed to initialize PulseAudio");
    g_object_unref(manager);
    g_main_loop_unref(loop);
    return 1;
  }

  /* Load initial players */
  GList *current_players = playerctl_list_players(&error);
  if (error != NULL) {
    DEBUG_MSG("Failed to list initial players: %s", error->message);
    g_error_free(error);
    error = NULL;
  } else {
    DEBUG_MSG("Found %d initial players", g_list_length(current_players));
    if (g_list_length(current_players) == 0) {
      print_player_list(players, FALSE);
    } else {
      for (GList *iter = current_players; iter != NULL; iter = iter->next) {
        PlayerctlPlayerName *n = (PlayerctlPlayerName *)iter->data;
        if (find_player_by_instance(players, n->instance) == NULL) {
          gboolean is_new;
          PlayerData *data = player_data_new(n, pulse, &is_new);
          if (data && is_new) {
            players = g_list_append(players, data);
          }
        }
      }
    }
    g_list_free_full(current_players,
                     (GDestroyNotify)playerctl_player_name_free);
  }

  /* Connect playerctl signals */
  g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared),
                   pulse);
  g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished),
                   pulse);

  DEBUG_MSG("Listening for player and PulseAudio events...");

  /* Run the main loop */
  g_main_loop_run(loop);

  /* Cleanup */
  g_list_free_full(players, player_data_free);
  g_free(last_json_output);
  g_main_loop_unref(loop);
  g_object_unref(manager);
  pulse_data_free(pulse);

  return 0;
}
