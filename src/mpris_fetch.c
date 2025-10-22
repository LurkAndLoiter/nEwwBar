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
#include <json-glib/json-glib.h>
#include <playerctl/playerctl.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>

#include "../include/hms.h"

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
  gchar *instance;
  gint source;
  PlayerctlPlayer *player;
  /* Cached player properties */
  gboolean can_control;
  gboolean can_go_next;
  gboolean can_go_previous;
  gboolean can_pause;
  gboolean can_play;
  gboolean can_seek;
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
  gint hasPlayer;
  /* Polling timeout ID for artUrl */
  guint art_url_polling_id;
} PlayerData;

/* Structure to hold PulseAudio context and data */
typedef struct {
  pa_context *context;
  pa_glib_mainloop *mainloop;
  GList **players;
} PulseData;

/* Structure to hold new player can_* check timeout data */
typedef struct {
  PlayerData *player_data;
  PulseData *pulse;
  guint check_count;
  guint max_checks;
} TimeoutData;

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

/* Forward declarations */
static void print_player_list(GList *players, gboolean force_output);
static void update_metadata(PlayerData *data, PulseData *pulse);

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

/* Timeout callback to check can_* properties */
static gboolean check_can_properties(gpointer user_data) {
  TimeoutData *timeout_data = user_data;
  if (!timeout_data) {
    return G_SOURCE_REMOVE;
  }
  PlayerData *data = timeout_data->player_data;
  PulseData *pulse = timeout_data->pulse;

  if (!data) {
    DEBUG_MSG("check_can_properties: missing PlayerData, stopping");
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
  }

  if (!data->player) {
    DEBUG_MSG("Player %s: No player object available, stopping timeout",
              safe_str(data->name));
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
  }

  /* Store old values */
  gboolean old_can_control = data->can_control;
  gboolean old_can_go_next = data->can_go_next;
  gboolean old_can_go_previous = data->can_go_previous;
  gboolean old_can_pause = data->can_pause;
  gboolean old_can_play = data->can_play;
  gboolean old_can_seek = data->can_seek;

  /* Update can_* properties */
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
              safe_str(data->name), data->can_control, data->can_go_next,
              data->can_go_previous, data->can_pause, data->can_play,
              data->can_seek);
    print_player_list(*pulse->players, FALSE);
  }

  timeout_data->check_count++;
  if (timeout_data->check_count >= timeout_data->max_checks) {
    DEBUG_MSG("Player %s: Stopping can_* property checks after %u attempts",
              safe_str(data->name), timeout_data->check_count);
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

/* PulseAudio sink input info callback */
static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i,
                               int eol, void *userdata) {
  PulseData *pulse = userdata;
  if (eol || !i || !pulse)
    return;

  if (i->corked) {
    DEBUG_MSG("Skipping corked sink input: index=%u", i->index);
    return;
  }

  const char *app_name = pa_proplist_gets(i->proplist, "application.name");
  const char *media_name = pa_proplist_gets(i->proplist, "media.name");
  const char *binary_name =
      pa_proplist_gets(i->proplist, "application.process.binary");

  if (!app_name && !binary_name) {
    DEBUG_MSG("Skipping sink input with no app_name or binary_name: index=%u",
              i->index);
    return;
  }

  DEBUG_MSG("Sink input: index=%u, app_name=%s, binary_name=%s, media_name=%s",
            i->index, safe_str(app_name), safe_str(binary_name),
            safe_str(media_name));

  /* Find matching player */
  PlayerData *matched_player = NULL;
  for (GList *iter = *pulse->players; iter; iter = iter->next) {
    PlayerData *player = iter->data;
    if (!player)
      continue;

    if (app_name && player->name &&
        g_ascii_strcasecmp(app_name, player->name) == 0) {
      matched_player = player;
      break;
    }
    if (app_name && player->instance &&
        g_ascii_strcasecmp(app_name, player->instance) == 0) {
      matched_player = player;
      break;
    }
    if (binary_name && player->name &&
        g_ascii_strcasecmp(binary_name, player->name) == 0) {
      matched_player = player;
      break;
    }
    if (media_name && player->name && strstr(media_name, player->name)) {
      matched_player = player;
      break;
    }
    /* Special casing for LibreWolf -> Firefox mapping */
    if (app_name && player->name &&
        g_ascii_strcasecmp(app_name, "LibreWolf") == 0 &&
        g_ascii_strcasecmp(player->name, "Firefox") == 0) {
      matched_player = player;
      break;
    }
    if (binary_name && player->name &&
        g_ascii_strcasecmp(binary_name, "librewolf") == 0 &&
        g_ascii_strcasecmp(player->name, "Firefox") == 0) {
      matched_player = player;
      break;
    }
  }

  if (!matched_player) {
    /* Create default PlayerData for unrecognized sink input */
    PlayerData *default_player = g_new0(PlayerData, 1);
    default_player->name =
        g_strdup(app_name ? app_name : (binary_name ? binary_name : "Unknown"));
    default_player->index = i->index;
    default_player->sink = i->sink;
    default_player->volume = pa_volume_to_percent(&i->volume);
    default_player->mute = i->mute;
    default_player->hasPlayer = 0;

    *pulse->players = g_list_append(*pulse->players, default_player);

    DEBUG_MSG("Created default player %s: index=%u, sink=%u, volume=%u, "
              "mute=%u, hasPlayer=%d",
              safe_str(default_player->name), default_player->index,
              default_player->sink, default_player->volume,
              default_player->mute, default_player->hasPlayer);
  } else {
    /* Update existing player with sink input info */
    matched_player->index = i->index;
    matched_player->sink = i->sink;
    matched_player->volume = pa_volume_to_percent(&i->volume);
    matched_player->mute = i->mute;
    matched_player->hasPlayer =
        matched_player->hasPlayer ? matched_player->hasPlayer : 1;

    DEBUG_MSG("Updated player %s: index=%u, sink=%u, volume=%u, mute=%u, "
              "hasPlayer=%d",
              safe_str(matched_player->name), matched_player->index,
              matched_player->sink, matched_player->volume,
              matched_player->mute, matched_player->hasPlayer);
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

  if (type != PA_SUBSCRIPTION_EVENT_CHANGE && type != PA_SUBSCRIPTION_EVENT_NEW)
    return;

  if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
    pa_operation *op =
        pa_context_get_sink_input_info(c, idx, sink_input_info_cb, pulse);
    if (op)
      pa_operation_unref(op);
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
      if (op_sub)
        pa_operation_unref(op_sub);
    }
    {
      pa_operation *op_input =
          pa_context_get_sink_input_info_list(c, sink_input_info_cb, pulse);
      if (op_input)
        pa_operation_unref(op_input);
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
  if (!data)
    return;

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

  GError *error = NULL;

  /*TODO remove if fixed upstream workaround for youtube forces a page update */
  gint64 position = playerctl_player_get_position(data->player, &error);
  if (error) {
    DEBUG_MSG("Failed to get position for %s: %s", safe_str(data->name),
              error->message);
    g_error_free(error);
    error = NULL;
  }
  if (position < 100000) {
    playerctl_player_set_position(data->player, position, &error);
    if (error) {
      DEBUG_MSG("Failed to set position for %s: %s", safe_str(data->name),
                error->message);
      g_error_free(error);
      error = NULL;
    }
  }

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

    if (data->art_url && !g_file_test(data->art_url, G_FILE_TEST_EXISTS)) {
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
    data->length = g_ascii_strtoll(length_str, &endptr, 10);
    if (endptr == length_str || *endptr != '\0') {
      DEBUG_MSG("Failed to parse length for %s: %s", safe_str(data->name),
                length_str);
      data->length = 0;
    }
    g_free(length_str);
  }

  /* Shuffle (only if supported) */
  if (data->shuffle != -1) {
    g_object_get(data->player, "shuffle", &data->shuffle, NULL);
  }

  /* Loop Status (only if supported) */
  if (data->loop_status != -1) {
    g_object_get(data->player, "loop-status", &data->loop_status, NULL);
  }

  g_object_get(data->player, "playback-status", &data->playback_status, NULL);

  /* PulseAudio update */
  if (pulse && pulse->context &&
      pa_context_get_state(pulse->context) == PA_CONTEXT_READY) {
    pa_operation *op = pa_context_get_sink_input_info_list(
        pulse->context, sink_input_info_cb, pulse);
    if (op)
      pa_operation_unref(op);
  }

  DEBUG_MSG("Updated metadata for %s: title=%s, album=%s, artist=%s, "
            "artUrl=%s, length=%ld",
            safe_str(data->name), safe_str(data->title), safe_str(data->album),
            safe_str(data->artist), safe_str(data->art_url),
            (long)data->length);
}

/* Helper function to print the list of players as JSON */
static void print_player_list(GList *players, gboolean force_output) {
  JsonBuilder *builder = json_builder_new();
  json_builder_begin_array(builder);

  for (GList *iter = players; iter != NULL; iter = iter->next) {
    PlayerData *data = iter->data;
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "longName");
    gchar *long_name =
        g_strdup_printf("org.mpris.MediaPlayer2.%s",
                        data->instance ? data->instance
                                       : (data->name ? data->name : "Unknown"));
    json_builder_add_string_value(builder, long_name);
    g_free(long_name);

    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, data->name ? data->name : "");

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

    json_builder_set_member_name(builder, "length");
    json_builder_add_int_value(builder, (gint)(data->length / 1000000));

    json_builder_set_member_name(builder, "lengthHMS");
    char hms[32] = "";
    to_hms(data->length, hms, sizeof(hms));
    json_builder_add_string_value(builder, hms);

    json_builder_set_member_name(builder, "isShuffle");
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

    json_builder_set_member_name(builder, "hasPlayer");
    json_builder_add_boolean_value(builder, data->hasPlayer);

    json_builder_end_object(builder);
  }

  json_builder_end_array(builder);

  JsonNode *root = json_builder_get_root(builder);
  JsonGenerator *generator = json_generator_new();
  json_generator_set_root(generator, root);
  json_generator_set_pretty(generator, FALSE);
  gchar *json_str = json_generator_to_data(generator, NULL);

  if (force_output || last_json_output == NULL ||
      strcmp(json_str, last_json_output) != 0) {
    g_print("%s\n", json_str);
    g_free(last_json_output);
    last_json_output = g_strdup(json_str);
  }

  g_free(json_str);
  g_object_unref(generator);
  json_node_free(root);
  g_object_unref(builder);
}

/* Callback for the playback-status signal */
static void on_playback_status(PlayerctlPlayer *player,
                               PlayerctlPlaybackStatus status,
                               gpointer user_data) {
  PulseData *pulse = user_data;
  if (!pulse)
    return;

  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d && d->player == player) {
      data = d;
      break;
    }
  }

  if (data) {
    DEBUG_MSG("Player %s (instance: %s): Playback status changed to %i",
              safe_str(data->name), safe_str(data->instance), status);
    update_metadata(data, pulse);
    print_player_list(*pulse->players, FALSE);
  } else {
    DEBUG_MSG("Playback status: PlayerData not found for player object");
  }
}

/* Callback for the metadata signal */
static void on_metadata(PlayerctlPlayer *player, GVariant *metadata,
                        gpointer user_data) {
  PulseData *pulse = user_data;
  if (!pulse)
    return;

  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d && d->player == player) {
      data = d;
      break;
    }
  }

  if (data) {
    update_metadata(data, pulse);
    DEBUG_MSG("Player %s (instance: %s): Metadata signal fired (title: %s, "
              "length: %ld)",
              safe_str(data->name), safe_str(data->instance),
              safe_str(data->title), (long)data->length);
    print_player_list(*pulse->players, FALSE);
  } else {
    DEBUG_MSG("Metadata: PlayerData not found for player object");
  }
}

/* Callback for the shuffle signal */
static void on_shuffle(PlayerctlPlayer *player, gboolean shuffle,
                       gpointer user_data) {
  PulseData *pulse = user_data;
  if (!pulse)
    return;

  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d && d->player == player) {
      data = d;
      break;
    }
  }

  if (data) {
    data->shuffle = shuffle;
    DEBUG_MSG("Player %s (instance: %s): Shuffle changed to %s",
              safe_str(data->name), safe_str(data->instance),
              shuffle ? "true" : "false");
    print_player_list(*pulse->players, FALSE);
  } else {
    DEBUG_MSG("Shuffle: PlayerData not found for player object");
  }
}

/* Callback for the loop-status signal */
static void on_loop_status(PlayerctlPlayer *player, PlayerctlLoopStatus status,
                           gpointer user_data) {
  PulseData *pulse = user_data;
  if (!pulse)
    return;

  PlayerData *data = NULL;
  for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
    PlayerData *d = iter->data;
    if (d && d->player == player) {
      data = d;
      break;
    }
  }

  if (data) {
    data->loop_status = status;
    DEBUG_MSG("Player %s (instance: %s): Loop status changed to %i",
              safe_str(data->name), safe_str(data->instance),
              data->loop_status);
    print_player_list(*pulse->players, FALSE);
  } else {
    DEBUG_MSG("Loop status: PlayerData not found for player object");
  }
}

/* Helper function to create PlayerData from PlayerctlPlayerName */
static PlayerData *player_data_new(PlayerctlPlayerName *name,
                                   PulseData *pulse) {
  if (!name)
    return NULL;

  GError *error = NULL;
  PlayerData *data = g_new0(PlayerData, 1);
  data->name = g_strdup(name->name ? name->name : "Unknown");
  data->instance = g_strdup(name->instance ? name->instance : "");
  data->source = name->source;
  data->player = playerctl_player_new_from_name(name, &error);
  data->hasPlayer = -1;

  if (error) {
    DEBUG_MSG("Failed to create player for %s: %s", safe_str(name->name),
              error->message);
    g_error_free(error);
    error = NULL;
  }

  if (data->player) {
    g_object_get(data->player, "can-control", &data->can_control, "can-go-next",
                 &data->can_go_next, "can-go-previous", &data->can_go_previous,
                 "can-pause", &data->can_pause, "can-play", &data->can_play,
                 "can-seek", &data->can_seek, NULL);

    /* Initialize shuffle */
    gboolean shuffle_value = FALSE;
    g_object_get(data->player, "shuffle", &shuffle_value, NULL);
    data->shuffle = shuffle_value ? 1 : 0;
    if (!shuffle_value) {
      playerctl_player_set_shuffle(data->player, TRUE, &error);
      if (error) {
        data->shuffle = -1;
        DEBUG_MSG("Error setting shuffle for %s: %s", safe_str(data->name),
                  error->message);
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

    /* Initialize loop status */
    int loop_status = 0;
    g_object_get(data->player, "loop-status", &loop_status, NULL);
    data->loop_status = loop_status;
    if (!loop_status) {
      playerctl_player_set_loop_status(data->player, 1, &error);
      if (error) {
        data->loop_status = -1;
        DEBUG_MSG("Error setting loop status for %s: %s", safe_str(data->name),
                  error->message);
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
    if (data->shuffle != -1) {
      g_signal_connect(data->player, "shuffle", G_CALLBACK(on_shuffle), pulse);
    }
    if (data->loop_status != -1) {
      g_signal_connect(data->player, "loop-status", G_CALLBACK(on_loop_status),
                       pulse);
    }
  }

  DEBUG_MSG("Created PlayerData for %s (instance: %s)", safe_str(data->name),
            safe_str(data->instance));
  return data;
}

/* Helper function to free PlayerData */
static void player_data_free(gpointer data) {
  PlayerData *player_data = data;
  if (!player_data)
    return;

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
  g_free(player_data->instance);
  g_free(player_data->title);
  g_free(player_data->album);
  g_free(player_data->artist);
  g_free(player_data->art_url);
  g_free(player_data);
}

/* Helper function to find a player by instance */
static GList *find_player_by_instance(GList *players, const gchar *instance) {
  if (!instance)
    return NULL;
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
  PulseData *pulse = user_data;
  if (!pulse || !name)
    return;

  DEBUG_MSG("Received name-appeared for %s (instance: %s)",
            safe_str(name->name), safe_str(name->instance));
  if (find_player_by_instance(*pulse->players, name->instance) == NULL) {
    PlayerData *data = player_data_new(name, pulse);
    *pulse->players = g_list_append(*pulse->players, data);
    DEBUG_MSG("Player appeared: %s (instance: %s, source: %d)",
              safe_str(name->name), safe_str(name->instance), name->source);

    /* Schedule timeout to check can_* properties */
    TimeoutData *timeout_data = g_new0(TimeoutData, 1);
    timeout_data->player_data = data;
    timeout_data->pulse = pulse;
    timeout_data->check_count = 0;
    timeout_data->max_checks = CAN_CHECK_MAX_ATTEMPTS;
    g_timeout_add_seconds(CAN_CHECK_INTERVAL_SEC, check_can_properties,
                          timeout_data);

    /* Update all players metadata once new player appears */
    for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
      PlayerData *d = iter->data;
      if (d && d->player)
        update_metadata(d, pulse);
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
  PulseData *pulse = user_data;
  if (!pulse || !name)
    return;

  DEBUG_MSG("Received name-vanished for %s (instance: %s)",
            safe_str(name->name), safe_str(name->instance));
  GList *node = find_player_by_instance(*pulse->players, name->instance);
  if (node != NULL) {
    PlayerData *data = node->data;
    *pulse->players = g_list_delete_link(*pulse->players, node);
    DEBUG_MSG("Player vanished: %s (instance: %s, source: %d)",
              safe_str(data->name), safe_str(data->instance), data->source);
    print_player_list(*pulse->players, FALSE);
    player_data_free(data);
  } else {
    DEBUG_MSG("Player %s (instance: %s) not found in list",
              safe_str(name->name), safe_str(name->instance));
  }
}

/* Free PulseData */
static void pulse_data_free(PulseData *pulse) {
  if (!pulse)
    return;
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
    for (GList *iter = current_players; iter != NULL; iter = iter->next) {
      PlayerData *data =
          player_data_new((PlayerctlPlayerName *)iter->data, pulse);
      players = g_list_append(players, data);
    }
    g_list_free_full(current_players,
                     (GDestroyNotify)playerctl_player_name_free);
  }

  /* Print initial player list */
  print_player_list(players, FALSE);

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
