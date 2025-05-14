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

#include <playerctl/playerctl.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h> // Changed to use pa_glib_mainloop
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

// Structure to hold sink cache
typedef struct {
    uint32_t index;
    gchar *name;
} SinkCache;

// Structure to hold player data
typedef struct {
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
    gchar *url;
    int64_t length;
    // Shuffle and loop
    gboolean shuffle;
    gboolean shuffle_supported; // New field
    PlayerctlLoopStatus loop_status;
    gboolean loop_status_supported; // New field
    // PulseAudio fields
    gchar *sink_id;
    gchar *id;
    gchar *serial;
    guint32 volume;
    gboolean mute;
} PlayerData;

// Structure to hold PulseAudio context and data
typedef struct {
    pa_context *context;
    pa_glib_mainloop *mainloop; // Changed to pa_glib_mainloop
    GList *sink_cache;
    GList **players;
} PulseData;

// Store last JSON output for change detection
static gchar *last_json_output = NULL;

// Forward declaration of print_player_list
static void print_player_list(GList *players);

// Convert microseconds to HMS (MM:SS or H:MM:SS), or "live" for specified max
void to_hms(int64_t us, char *hms, size_t hms_size) {
    if (us == 9223372036854000000LL) {
        snprintf(hms, hms_size, "live");
        return;
    }

    if (us <= 0) {
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

// Helper function to convert loop status to string
static const char *loop_status_to_string(PlayerctlLoopStatus status) {
    switch (status) {
        case PLAYERCTL_LOOP_STATUS_NONE: return "None";
        case PLAYERCTL_LOOP_STATUS_TRACK: return "Track";
        case PLAYERCTL_LOOP_STATUS_PLAYLIST: return "Playlist";
        default: return "Unknown";
    }
}
/// ooooooooooooooooooooooooooooooooooooooooooooooooo
// Structure to hold timeout data
typedef struct {
    PlayerData *player_data;
    PulseData *pulse;
    guint check_count;
    guint max_checks;
} TimeoutData;

// Timeout callback to check can_* properties
static gboolean check_can_properties(gpointer user_data) {
    TimeoutData *timeout_data = user_data;
    PlayerData *data = timeout_data->player_data;
    PulseData *pulse = timeout_data->pulse;

    if (!data->player) {
        DEBUG_PRINT("Player %s: No player object, stopping timeout\n", data->name);
        g_free(timeout_data);
        return G_SOURCE_REMOVE;
    }

    // Store old values
    gboolean old_can_control = data->can_control;
    gboolean old_can_go_next = data->can_go_next;
    gboolean old_can_go_previous = data->can_go_previous;
    gboolean old_can_pause = data->can_pause;
    gboolean old_can_play = data->can_play;
    gboolean old_can_seek = data->can_seek;

    // Update can_* properties
    g_object_get(data->player,
                 "can-control", &data->can_control,
                 "can-go-next", &data->can_go_next,
                 "can-go-previous", &data->can_go_previous,
                 "can-pause", &data->can_pause,
                 "can-play", &data->can_play,
                 "can-seek", &data->can_seek,
                 NULL);

    // Check if any properties changed
    gboolean changed = (old_can_control != data->can_control ||
                       old_can_go_next != data->can_go_next ||
                       old_can_go_previous != data->can_go_previous ||
                       old_can_pause != data->can_pause ||
                       old_can_play != data->can_play ||
                       old_can_seek != data->can_seek);

    if (changed) {
        DEBUG_PRINT("Player %s: can_* properties updated (control=%d, next=%d, prev=%d, pause=%d, play=%d, seek=%d)\n",
                    data->name,
                    data->can_control, data->can_go_next, data->can_go_previous,
                    data->can_pause, data->can_play, data->can_seek);
        print_player_list(*pulse->players);
    }

    // Increment check count
    timeout_data->check_count++;

    // Stop polling after max_checks
    if (timeout_data->check_count >= timeout_data->max_checks) {
        DEBUG_PRINT("Player %s: Stopping can_* property checks after %u attempts\n",
                    data->name, timeout_data->check_count);
        g_free(timeout_data);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}
//LOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO

// Free a SinkCache entry
static void sink_cache_free(gpointer data) {
    SinkCache *cache = data;
    g_free(cache->name);
    g_free(cache);
}

// PulseAudio sink info callback
static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    PulseData *pulse = userdata;
    if (eol || !i) return;

    // Check if sink exists in cache
    for (GList *iter = pulse->sink_cache; iter; iter = iter->next) {
        SinkCache *cache = iter->data;
        if (cache->index == i->index) {
            g_free(cache->name);
            cache->name = g_strdup(i->name);
            DEBUG_PRINT("Updated sink: index=%u, name=%s\n", i->index, i->name);
            return;
        }
    }

    // Add new sink to cache
    SinkCache *cache = g_new0(SinkCache, 1);
    cache->index = i->index;
    cache->name = g_strdup(i->name);
    pulse->sink_cache = g_list_append(pulse->sink_cache, cache);
    DEBUG_PRINT("Cached sink: index=%u, name=%s\n", i->index, i->name);
}

// PulseAudio sink input info callback
static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    PulseData *pulse = userdata;
    if (eol || !i) return;

    if (i->corked) {
        DEBUG_PRINT("Skipping corked sink input: index=%u\n", i->index);
        return;
    }

    const char *app_name = pa_proplist_gets(i->proplist, "application.name");
    const char *media_name = pa_proplist_gets(i->proplist, "media.name");
    const char *binary_name = pa_proplist_gets(i->proplist, "application.process.binary");
    if (!app_name && !binary_name) {
        DEBUG_PRINT("Skipping sink input with no app_name or binary_name: index=%u\n", i->index);
        return;
    }

    // Log sink input details for debugging
    DEBUG_PRINT("Sink input: index=%u, app_name=%s, binary_name=%s, media_name=%s, corked=%d\n",
                i->index, app_name ? app_name : "null", binary_name ? binary_name : "null",
                media_name ? media_name : "null", i->corked);

    // Find matching player
    PlayerData *matched_player = NULL;
    for (GList *iter = *pulse->players; iter; iter = iter->next) {
        PlayerData *player = iter->data;
        if (app_name && player->name && strcasecmp(app_name, player->name) == 0) {
            matched_player = player;
            break;
        }
        if (app_name && player->instance && strcasecmp(app_name, player->instance) == 0) {
            matched_player = player;
            break;
        }
        if (binary_name && player->name && strcasecmp(binary_name, player->name) == 0) {
            matched_player = player;
            break;
        }
        if (media_name && player->name && strstr(media_name, player->name)) {
            matched_player = player;
            break;
        }
    }

    if (!matched_player) {
        DEBUG_PRINT("No player match for app_name=%s, binary_name=%s, media_name=%s\n",
                    app_name ? app_name : "null", binary_name ? binary_name : "null",
                    media_name ? media_name : "null");
        return;
    }

    // Look up sink name
    gchar *sink_name = NULL;
    for (GList *iter = pulse->sink_cache; iter; iter = iter->next) {
        SinkCache *cache = iter->data;
        if (cache->index == i->sink) {
            sink_name = cache->name;
            break;
        }
    }

    // Update player fields
    g_free(matched_player->sink_id);
    matched_player->sink_id = sink_name ? g_strdup(sink_name) : NULL;
    g_free(matched_player->id);
    matched_player->id = pa_proplist_gets(i->proplist, "object.id") ? g_strdup(pa_proplist_gets(i->proplist, "object.id")) : NULL;
    g_free(matched_player->serial);
    matched_player->serial = pa_proplist_gets(i->proplist, "object.serial") ? g_strdup(pa_proplist_gets(i->proplist, "object.serial")) : NULL;
    uint32_t volume = pa_cvolume_avg(&i->volume);
    matched_player->volume = (volume * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
    matched_player->mute = i->mute;

    DEBUG_PRINT("Updated player %s: sink_id=%s, id=%s, serial=%s, volume=%u, mute=%d\n",
                matched_player->name,
                matched_player->sink_id ? matched_player->sink_id : "null",
                matched_player->id ? matched_player->id : "null",
                matched_player->serial ? matched_player->serial : "null",
                matched_player->volume,
                matched_player->mute);

    print_player_list(*pulse->players);
}

// PulseAudio subscription callback
static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
    PulseData *pulse = userdata;
    pa_subscription_event_type_t facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_subscription_event_type_t type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (type != PA_SUBSCRIPTION_EVENT_CHANGE && type != PA_SUBSCRIPTION_EVENT_NEW) return;

    if (facility == PA_SUBSCRIPTION_EVENT_SINK) {
        pa_operation *op = pa_context_get_sink_info_by_index(c, idx, sink_info_cb, pulse);
        if (op) pa_operation_unref(op);
    } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        pa_operation *op = pa_context_get_sink_input_info(c, idx, sink_input_info_cb, pulse);
        if (op) pa_operation_unref(op);
    }
}

// PulseAudio context state callback
static void context_state_cb(pa_context *c, void *userdata) {
    PulseData *pulse = userdata;
    pa_context_state_t state = pa_context_get_state(c);
    switch (state) {
        case PA_CONTEXT_READY:
            DEBUG_PRINT("PulseAudio context ready\n");
            pa_context_set_subscribe_callback(c, subscribe_cb, pulse);
            pa_operation *op_sub = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
            if (op_sub) pa_operation_unref(op_sub);
            pa_operation *op_sink = pa_context_get_sink_info_list(c, sink_info_cb, pulse);
            if (op_sink) pa_operation_unref(op_sink);
            pa_operation *op_input = pa_context_get_sink_input_info_list(c, sink_input_info_cb, pulse);
            if (op_input) pa_operation_unref(op_input);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            DEBUG_ERROR("PulseAudio context failed or terminated: %s\n", pa_strerror(pa_context_errno(c)));
            break;
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            DEBUG_PRINT("PulseAudio context state: %d\n", state);
            break;
        default:
            DEBUG_ERROR("Unknown PulseAudio context state: %d\n", state);
            break;
    }
}

// Helper function to update metadata and properties
static void update_metadata(PlayerData *data, PulseData *pulse) {
    // Free existing metadata
    g_free(data->title);
    g_free(data->album);
    g_free(data->artist);
    g_free(data->art_url);
    g_free(data->url);
    data->title = NULL;
    data->album = NULL;
    data->artist = NULL;
    data->art_url = NULL;
    data->url = NULL;
    data->length = 0;

    if (!data->player) {
        DEBUG_PRINT("No player for %s\n", data->name);
        return;
    }

    GError *error = NULL;

    // Title
    data->title = playerctl_player_get_title(data->player, &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get title for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    }

    // Album
    data->album = playerctl_player_get_album(data->player, &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get album for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    }

    // Artist
    data->artist = playerctl_player_get_artist(data->player, &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get artist for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    }

    // Art URL
    gchar *raw_art_url = playerctl_player_print_metadata_prop(data->player, "mpris:artUrl", &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get artUrl for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    } else if (raw_art_url) {
        if (g_str_has_prefix(raw_art_url, "file:///")) {
            data->art_url = g_strdup(raw_art_url + 7); // Skip "file://"
        } else if (g_str_has_prefix(raw_art_url, "https://i.scdn.co/image/")) {
            data->art_url = g_strconcat("/run/user/1000/album_art_cache", raw_art_url + 23, NULL); // Skip "https://i.scdn.co/image/"
        } else {
            data->art_url = g_strdup(raw_art_url);
        }
        g_free(raw_art_url);
    }

    // URL
    data->url = playerctl_player_print_metadata_prop(data->player, "xesam:url", &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get url for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    }

    // Length
    gchar *length_str = playerctl_player_print_metadata_prop(data->player, "mpris:length", &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to get length for %s: %s\n", data->name, error->message);
        g_error_free(error);
        error = NULL;
    } else if (length_str) {
        char *endptr;
        data->length = g_ascii_strtoll(length_str, &endptr, 10);
        if (endptr == length_str || *endptr != '\0') {
            DEBUG_ERROR("Failed to parse length for %s: %s\n", data->name, length_str);
            data->length = 0;
        }
        g_free(length_str);
    }

    // Shuffle (only if supported)
    if (data->shuffle_supported) {
        g_object_get(data->player, "shuffle", &data->shuffle, NULL);
    }

    // Loop Status (only if supported)
    if (data->loop_status_supported) {
        g_object_get(data->player, "loop-status", &data->loop_status, NULL);
    }

    // PulseAudio update
    if (pulse->context && pa_context_get_state(pulse->context) == PA_CONTEXT_READY) {
        pa_operation *op = pa_context_get_sink_input_info_list(pulse->context, sink_input_info_cb, pulse);
        if (op) pa_operation_unref(op);
    }

    DEBUG_PRINT("Updated metadata for %s: title=%s, album=%s, artist=%s, artUrl=%s, url=%s, length=%lld, shuffle=%s, loop=%s, sink_id=%s\n",
                data->name,
                data->title ? data->title : "none",
                data->album ? data->album : "none",
                data->artist ? data->artist : "none",
                data->art_url ? data->art_url : "none",
                data->url ? data->url : "none",
                data->length,
                data->shuffle_supported ? "true" : "false",
                data->shuffle_supported ? (data->shuffle ? "true" : "false") : "n/a",
                data->loop_status_supported ? "true" : "false",
                data->loop_status_supported ? loop_status_to_string(data->loop_status) : "n/a",
                data->sink_id ? data->sink_id : "none");
}

// Helper function to print the list of players as JSON
static void print_player_list(GList *players) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (GList *iter = players; iter != NULL; iter = iter->next) {
        PlayerData *data = iter->data;
        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "longName");
        gchar *long_name = g_strdup_printf("org.mpris.MediaPlayer2.%s", data->instance);
        json_builder_add_string_value(builder, long_name);
        g_free(long_name);

        json_builder_set_member_name(builder, "Name");
        json_builder_add_string_value(builder, data->name ? data->name : "");

        json_builder_set_member_name(builder, "CanControl");
        json_builder_add_boolean_value(builder, data->can_control);

        json_builder_set_member_name(builder, "CanGoNext");
        json_builder_add_boolean_value(builder, data->can_go_next);

        json_builder_set_member_name(builder, "CanGoPrevious");
        json_builder_add_boolean_value(builder, data->can_go_previous);

        json_builder_set_member_name(builder, "CanPause");
        json_builder_add_boolean_value(builder, data->can_pause);

        json_builder_set_member_name(builder, "CanPlay");
        json_builder_add_boolean_value(builder, data->can_play);

        json_builder_set_member_name(builder, "CanSeek");
        json_builder_add_boolean_value(builder, data->can_seek);

        json_builder_set_member_name(builder, "PlaybackStatus");
        PlayerctlPlaybackStatus status = PLAYERCTL_PLAYBACK_STATUS_STOPPED;
        if (data->player) {
            g_object_get(data->player, "playback-status", &status, NULL);
        }
        json_builder_add_string_value(builder, playback_status_to_string(status));

        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, data->title ? data->title : "");

        json_builder_set_member_name(builder, "album");
        json_builder_add_string_value(builder, data->album ? data->album : "");

        json_builder_set_member_name(builder, "artist");
        json_builder_add_string_value(builder, data->artist ? data->artist : "");

        json_builder_set_member_name(builder, "artUrl");
        json_builder_add_string_value(builder, data->art_url ? data->art_url : "");

        json_builder_set_member_name(builder, "url");
        json_builder_add_string_value(builder, data->url ? data->url : "");

        json_builder_set_member_name(builder, "length");
        json_builder_add_int_value(builder, data->length);

        json_builder_set_member_name(builder, "lengthHMS");
        char hms[32];
        to_hms(data->length, hms, sizeof(hms));
        json_builder_add_string_value(builder, hms);

        // Only include shuffle if supported
        if (data->shuffle_supported) {
            json_builder_set_member_name(builder, "shuffle");
            json_builder_add_boolean_value(builder, data->shuffle);
        }

        // Only include loop if supported
        if (data->loop_status_supported) {
            json_builder_set_member_name(builder, "loop");
            json_builder_add_string_value(builder, loop_status_to_string(data->loop_status));
        }

        json_builder_set_member_name(builder, "sinkID");
        json_builder_add_string_value(builder, data->sink_id ? data->sink_id : "");

        json_builder_set_member_name(builder, "id");
        json_builder_add_string_value(builder, data->id ? data->id : "");

        json_builder_set_member_name(builder, "serial");
        json_builder_add_string_value(builder, data->serial ? data->serial : "");

        json_builder_set_member_name(builder, "volume");
        json_builder_add_int_value(builder, data->volume);

        json_builder_set_member_name(builder, "mute");
        json_builder_add_boolean_value(builder, data->mute);

        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);

    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, FALSE);
    gchar *json_str = json_generator_to_data(generator, NULL);

    if (last_json_output == NULL || strcmp(json_str, last_json_output) != 0) {
        g_print("%s\n", json_str);
        g_free(last_json_output);
        last_json_output = g_strdup(json_str);
    }

    g_free(json_str);
    g_object_unref(generator);
    json_node_free(root);
    g_object_unref(builder);
}

// Callback for the playback-status signal
static void on_playback_status(PlayerctlPlayer *player, PlayerctlPlaybackStatus status, gpointer user_data) {
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
        DEBUG_PRINT("Player %s (instance: %s): Playback status changed to %s\n",
                    data->name, data->instance, playback_status_to_string(status));
        update_metadata(data, pulse);
        print_player_list(*pulse->players);
    } else {
        DEBUG_PRINT("Playback status: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Callback for the metadata signal
static void on_metadata(PlayerctlPlayer *player, GVariant *metadata, gpointer user_data) {
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
        DEBUG_PRINT("Player %s (instance: %s): Metadata signal fired (title: %s, length: %lld)\n",
                    data->name, data->instance, data->title ? data->title : "none", data->length);
        print_player_list(*pulse->players);
    } else {
        DEBUG_PRINT("Metadata: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Callback for the shuffle signal
static void on_shuffle(PlayerctlPlayer *player, gboolean shuffle, gpointer user_data) {
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
        DEBUG_PRINT("Player %s (instance: %s): Shuffle changed to %s\n",
                    data->name, data->instance, shuffle ? "true" : "false");
        print_player_list(*pulse->players);
    } else {
        DEBUG_PRINT("Shuffle: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Callback for the loop-status signal
static void on_loop_status(PlayerctlPlayer *player, PlayerctlLoopStatus status, gpointer user_data) {
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
        DEBUG_PRINT("Player %s (instance: %s): Loop status changed to %s\n",
                    data->name, data->instance, loop_status_to_string(status));
        print_player_list(*pulse->players);
    } else {
        DEBUG_PRINT("Loop status: Invalid PlayerData (name: %p, instance: %p)\n",
                    data ? data->name : NULL, data ? data->instance : NULL);
    }
}

// Helper function to create PlayerData from PlayerctlPlayerName
static PlayerData *player_data_new(PlayerctlPlayerName *name, PulseData *pulse) {
    GError *error = NULL;
    PlayerData *data = g_new0(PlayerData, 1);
    data->name = g_strdup(name->name);
    data->instance = g_strdup(name->instance);
    data->source = name->source; // TODO Purge? what is this. Can it be useful?
    data->player = playerctl_player_new_from_name(name, &error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to create player for %s: %s\n", name->name, error->message);
        g_error_free(error);
    }
    if (data->player) {
        g_object_get(data->player,
                     "can-control", &data->can_control,
                     "can-go-next", &data->can_go_next,
                     "can-go-previous", &data->can_go_previous,
                     "can-pause", &data->can_pause,
                     "can-play", &data->can_play,
                     "can-seek", &data->can_seek,
                     NULL);

        // Test Shuffle property support by attempting to set it
        error = NULL;
        playerctl_player_set_shuffle(data->player, FALSE, &error);
        data->shuffle_supported = (error == NULL);
        if (!data->shuffle_supported) {
            data->shuffle = FALSE; // Default, won't be included in JSON
            DEBUG_ERROR("Shuffle not supported for %s: %s\n", data->name, error ? error->message : "Unknown error");
            if (error) g_error_free(error);
        } else {
            // Get current shuffle value if supported
            g_object_get(data->player, "shuffle", &data->shuffle, NULL);
        }

        // Test LoopStatus property support by attempting to set it
        error = NULL;
        playerctl_player_set_loop_status(data->player, PLAYERCTL_LOOP_STATUS_NONE, &error);
        data->loop_status_supported = (error == NULL);
        if (!data->loop_status_supported) {
            data->loop_status = PLAYERCTL_LOOP_STATUS_NONE; // Default, won't be included in JSON
            DEBUG_ERROR("LoopStatus not supported for %s: %s\n", data->name, error ? error->message : "Unknown error");
            if (error) g_error_free(error);
        } else {
            // Get current loop status if supported
            g_object_get(data->player, "loop-status", &data->loop_status, NULL);
        }

        update_metadata(data, pulse);

        g_signal_connect(data->player, "playback-status", G_CALLBACK(on_playback_status), pulse);
        g_signal_connect(data->player, "metadata", G_CALLBACK(on_metadata), pulse);
        if (data->shuffle_supported) {
            g_signal_connect(data->player, "shuffle", G_CALLBACK(on_shuffle), pulse);
        }
        if (data->loop_status_supported) {
            g_signal_connect(data->player, "loop-status", G_CALLBACK(on_loop_status), pulse);
        }
    }
    DEBUG_PRINT("Created PlayerData for %s (instance: %s, player: %p, title: %s, length: %lld, shuffle_supported: %s, loop_supported: %s, sink_id: %s)\n",
                data->name, data->instance, data->player, data->title ? data->title : "none", data->length,
                data->shuffle_supported ? "true" : "false",
                data->loop_status_supported ? "true" : "false",
                data->sink_id ? data->sink_id : "none");
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
    g_free(player_data->title);
    g_free(player_data->album);
    g_free(player_data->artist);
    g_free(player_data->art_url);
    g_free(player_data->url);
    g_free(player_data->sink_id);
    g_free(player_data->id);
    g_free(player_data->serial);
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

// Callback for the name-appeared signal
static void on_name_appeared(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, gpointer user_data) {
    PulseData *pulse = user_data;
    DEBUG_PRINT("Received name-appeared for %s (instance: %s)\n", name->name, name->instance);
    if (find_player_by_instance(*pulse->players, name->instance) == NULL) {
        PlayerData *data = player_data_new(name, pulse);
        *pulse->players = g_list_append(*pulse->players, data);
        DEBUG_PRINT("Player appeared: %s (instance: %s, source: %d)\n",
                    name->name, name->instance, name->source);

        // Schedule timeout to check can_* properties
        TimeoutData *timeout_data = g_new0(TimeoutData, 1);
        timeout_data->player_data = data;
        timeout_data->pulse = pulse;
        timeout_data->check_count = 0;
        timeout_data->max_checks = 2; // Check 2 times (2 seconds total)
        g_timeout_add_seconds(1, check_can_properties, timeout_data);

        for (GList *iter = *pulse->players; iter != NULL; iter = iter->next) {
            PlayerData *d = iter->data;
            if (d->player) update_metadata(d, pulse);
        }
        print_player_list(*pulse->players);
    } else {
        DEBUG_PRINT("Player %s (instance: %s) already exists, skipping\n", name->name, name->instance);
    }
}

// Callback for the name-vanished signal
static void on_name_vanished(PlayerctlPlayerManager *manager, PlayerctlPlayerName *name, gpointer user_data) {
    PulseData *pulse = user_data;
    DEBUG_PRINT("Received name-vanished for %s (instance: %s)\n", name->name, name->instance);
    GList *node = find_player_by_instance(*pulse->players, name->instance);
    if (node != NULL) {
        PlayerData *data = node->data;
        *pulse->players = g_list_delete_link(*pulse->players, node);
        DEBUG_PRINT("Player vanished: %s (instance: %s, source: %d)\n",
                    data->name, data->instance, data->source);
        print_player_list(*pulse->players);
        player_data_free(data);
    } else {
        DEBUG_PRINT("Player %s (instance: %s) not found in list\n", name->name, name->instance);
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
    g_list_free_full(pulse->sink_cache, sink_cache_free);
    g_free(pulse);
}

// Initialize PulseAudio
static PulseData *pulse_data_new(GList **players) {
    PulseData *pulse = g_new0(PulseData, 1);
    pulse->players = players;
    pulse->mainloop = pa_glib_mainloop_new(NULL); // Use GLib main loop
    if (!pulse->mainloop) {
        DEBUG_ERROR("Failed to create PulseAudio GLib mainloop\n");
        pulse_data_free(pulse);
        return NULL;
    }
    pulse->context = pa_context_new(pa_glib_mainloop_get_api(pulse->mainloop), "mprisFetch");
    if (!pulse->context) {
        DEBUG_ERROR("Failed to create PulseAudio context\n");
        pulse_data_free(pulse);
        return NULL;
    }
    pa_context_set_state_callback(pulse->context, context_state_cb, pulse);
    if (pa_context_connect(pulse->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        DEBUG_ERROR("Failed to connect PulseAudio context: %s\n", pa_strerror(pa_context_errno(pulse->context)));
        pulse_data_free(pulse);
        return NULL;
    }
    return pulse;
}

int main(int argc, char *argv[]) {
    GError *error = NULL;

    // Initialize GLib main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    // Initialize playerctl
    PlayerctlPlayerManager *manager = playerctl_player_manager_new(&error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to create player manager: %s\n", error->message);
        g_error_free(error);
        g_main_loop_unref(loop);
        return 1;
    }

    // Initialize players list
    GList *players = NULL;

    // Initialize PulseAudio
    PulseData *pulse = pulse_data_new(&players);
    if (!pulse) {
        DEBUG_ERROR("Failed to initialize PulseAudio\n");
        g_object_unref(manager);
        g_main_loop_unref(loop);
        return 1;
    }

    // Load initial players
    GList *current_players = playerctl_list_players(&error);
    if (error != NULL) {
        DEBUG_ERROR("Failed to list initial players: %s\n", error->message);
        g_error_free(error);
    } else {
        DEBUG_PRINT("Found %d initial players\n", g_list_length(current_players));
        for (GList *iter = current_players; iter != NULL; iter = iter->next) {
            PlayerData *data = player_data_new((PlayerctlPlayerName *)iter->data, pulse);
            players = g_list_append(players, data);
        }
        g_list_free_full(current_players, (GDestroyNotify)playerctl_player_name_free);
    }

    // Print initial player list
    print_player_list(players);

    // Connect playerctl signals
    g_signal_connect(manager, "name-appeared", G_CALLBACK(on_name_appeared), pulse);
    g_signal_connect(manager, "name-vanished", G_CALLBACK(on_name_vanished), pulse);

    DEBUG_PRINT("Listening for player and PulseAudio events...\n");

    // Run the main loop
    g_main_loop_run(loop);

    // Cleanup
    g_list_free_full(players, player_data_free);
    g_free(last_json_output);
    g_main_loop_unref(loop);
    g_object_unref(manager);
    pulse_data_free(pulse);

    return 0;
}
