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

// gcc -o mpris_fetch mpris_fetch.c -I/usr/include/dbus-1.0
// -I/usr/lib64/dbus-1.0/include -ldbus-1 -lpulse
#include <ctype.h>
#include <dbus/dbus.h>
#include <pulse/pulseaudio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// PulseAudio context and mainloop
static pa_context *pa_ctx = NULL;
static pa_mainloop *pa_ml = NULL;

// D-Bus connection
static DBusConnection *conn = NULL;

// Player data structure
typedef struct {
  char *name;
  char *desktop_entry;
  char *sink_id;  // Will store sink name
  char *id;
  char *serial;
  int volume;
  int mute;
} Player;

// Sink cache structure
typedef struct {
  uint32_t index;  // Sink index (i->sink)
  char *name;     // Sink name (i->name)
} SinkCache;

// Global variables
static Player *players = NULL;
static int player_count = 0;
static char *last_json = NULL;
static SinkCache *sink_cache = NULL;
static size_t sink_cache_count = 0;

// Comparison function for qsort
static int compare_strings(const void *a, const void *b) {
  return strcasecmp(*(const char **)a, *(const char **)b);
}

// Check D-Bus errors
void check_dbus_error(DBusError *err, const char *context) {
  if (dbus_error_is_set(err)) {
    fprintf(stderr, "D-Bus Error in %s: %s\n", context, err->message);
    dbus_error_free(err);
  }
}

// Sink info callback for caching
static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                         void *userdata) {
  if (eol)
    return;
  if (!i)
    return;

  // Check if sink already exists in cache
  for (size_t j = 0; j < sink_cache_count; j++) {
    if (sink_cache[j].index == i->index) {
      free(sink_cache[j].name);
      sink_cache[j].name = strdup(i->name);
      return;
    }
  }

  // Add new sink to cache
  sink_cache = realloc(sink_cache, (sink_cache_count + 1) * sizeof(SinkCache));
  sink_cache[sink_cache_count].index = i->index;
  sink_cache[sink_cache_count].name = strdup(i->name);
  sink_cache_count++;
}

// PulseAudio sink input callback
static void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i,
                               int eol, void *userdata) {
  if (eol)
    return;
  if (i->corked)
    return;

  const char *app_name = pa_proplist_gets(i->proplist, "application.name");
  if (!app_name)
    return;

  // Look up sink name from cache
  char *sink_name = NULL;
  for (size_t j = 0; j < sink_cache_count; j++) {
    if (sink_cache[j].index == i->sink) {
      sink_name = sink_cache[j].name;
      break;
    }
  }

  fprintf(stderr, "Sink Input: app_name=%s, sink=%u, sink_name=%s, corked=%d\n",
          app_name, i->sink, sink_name ? sink_name : "null", i->corked);

  for (int j = 0; j < player_count; j++) {
    if (strcasecmp(app_name, players[j].desktop_entry) == 0) {
      free(players[j].sink_id);
      players[j].sink_id = sink_name ? strdup(sink_name) : NULL;
      free(players[j].id);
      players[j].id = pa_proplist_gets(i->proplist, "object.id")
                          ? strdup(pa_proplist_gets(i->proplist, "object.id"))
                          : NULL;
      free(players[j].serial);
      players[j].serial =
          pa_proplist_gets(i->proplist, "object.serial")
              ? strdup(pa_proplist_gets(i->proplist, "object.serial"))
              : NULL;

      uint32_t volume = pa_cvolume_avg(&i->volume);
      players[j].volume = (volume * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM;
      players[j].mute = i->mute;
      break;
    }
  }
}

// Extract string array from D-Bus
char **extract_string_array(DBusMessageIter *iter, int *count) {
  if (!iter || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
    return NULL;
  DBusMessageIter array_iter;
  dbus_message_iter_recurse(iter, &array_iter);

  *count = 0;
  while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
    (*count)++;
    dbus_message_iter_next(&array_iter);
  }

  char **result = malloc(*count * sizeof(char *));
  dbus_message_iter_recurse(iter, &array_iter);
  for (int i = 0; i < *count; i++) {
    char *str;
    dbus_message_iter_get_basic(&array_iter, &str);
    result[i] = strdup(str);
    dbus_message_iter_next(&array_iter);
  }
  return result;
}

void free_string_array(char **array, int count) {
  for (int i = 0; i < count; i++)
    free(array[i]);
  free(array);
}

// Truncate player name
char *truncate_player_name(const char *full_name, char **desktop_entry) {
  const char *prefix = "org.mpris.MediaPlayer2.";
  if (strncmp(full_name, prefix, strlen(prefix)) == 0) {
    const char *name = full_name + strlen(prefix);
    char temp[256];
    strncpy(temp, name, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *dot = strchr(temp, '.');
    if (dot)
      *dot = '\0';
    *desktop_entry = strdup(temp);
    return strdup(full_name);
  }
  *desktop_entry = strdup(full_name);
  return strdup(full_name);
}

// Convert microseconds to HMS
void to_hms(dbus_int64_t us, char *hms, size_t hms_size) {
  if (us >= 9223372036854775807LL - 1000000LL) {
    snprintf(hms, hms_size, "\"live\"");
    return;
  }

  long long hours = us / 3600000000LL;
  long long minutes = (us / 60000000LL) % 60;
  long long seconds = (us / 1000000LL) % 60;

  if (hours > 9999) {
    snprintf(hms, hms_size, "\"%lld+ hours\"", hours);
  } else {
    snprintf(hms, hms_size, "\"%02lld:%02lld:%02lld\"", hours, minutes,
             seconds);
  }
}

// Strip prefixes
const char *strip_prefix(const char *key) {
  if (strncmp(key, "mpris:", 6) == 0)
    return key + 6;
  if (strncmp(key, "xesam:", 6) == 0)
    return key + 6;
  return key;
}

// Escape special characters in a string for JSON
char *json_escape(const char *input) {
  if (!input)
    return strdup("null");

  // Count the length needed for escaped string
  size_t len = 0;
  for (const char *c = input; *c; c++) {
    switch (*c) {
      case '\"':
      case '\\':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        len += 2;  // Escaped as \X
        break;
      default:
        len += 1;
    }
  }

  // Allocate escaped string
  char *output = malloc(len + 1);
  if (!output)
    return strdup("null");

  // Build escaped string
  char *out = output;
  for (const char *c = input; *c; c++) {
    switch (*c) {
      case '\"':
        *out++ = '\\';
        *out++ = '\"';
        break;
      case '\\':
        *out++ = '\\';
        *out++ = '\\';
        break;
      case '\b':
        *out++ = '\\';
        *out++ = 'b';
        break;
      case '\f':
        *out++ = '\\';
        *out++ = 'f';
        break;
      case '\n':
        *out++ = '\\';
        *out++ = 'n';
        break;
      case '\r':
        *out++ = '\\';
        *out++ = 'r';
        break;
      case '\t':
        *out++ = '\\';
        *out++ = 't';
        break;
      default:
        *out++ = *c;
        break;
    }
  }
  *out = '\0';
  return output;
}

// Modify artUrl
void print_art_url(char *buffer, size_t *pos, size_t max, const char *val) {
  char *url_to_print;
  if (strncmp(val, "file://", 7) == 0) {
    url_to_print = strdup(val + 7);
  } else if (strncmp(val, "https://i.scdn.co/image/", 24) == 0) {
    char temp[256];
    snprintf(temp, sizeof(temp), "/run/user/1000/album_art_cache/%s", val + 24);
    url_to_print = strdup(temp);
  } else {
    url_to_print = strdup(val);
  }

  char *escaped = json_escape(url_to_print);
  *pos += snprintf(buffer + *pos, max - *pos, "\"%s\"", escaped);
  free(escaped);
  free(url_to_print);
}

// Get D-Bus property
DBusMessage *get_property(DBusConnection *conn, const char *dest,
                          const char *interface, const char *property) {
  DBusMessage *msg =
      dbus_message_new_method_call(dest, "/org/mpris/MediaPlayer2",
                                   "org.freedesktop.DBus.Properties", "Get");
  if (!msg)
    return NULL;

  const char *iface = interface;
  const char *prop = property;
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING,
                           &prop, DBUS_TYPE_INVALID);

  DBusError err;
  dbus_error_init(&err);
  DBusMessage *reply =
      dbus_connection_send_with_reply_and_block(conn, msg, 1000, &err);
  dbus_message_unref(msg);
  if (dbus_error_is_set(&err)) {
    check_dbus_error(&err, property);
    return NULL;
  }
  return reply;
}

// Print simple property
void print_simple_property(char *buffer, size_t *pos, size_t max,
                           const char *prop_name, DBusMessageIter *iter) {
  if (!iter || dbus_message_iter_get_arg_type(iter) == DBUS_TYPE_INVALID) {
    *pos += snprintf(buffer + *pos, max - *pos, "\"%s\": null, ", prop_name);
    return;
  }

  int type = dbus_message_iter_get_arg_type(iter);
  *pos += snprintf(buffer + *pos, max - *pos, "\"%s\": ", prop_name);

  switch (type) {
  case DBUS_TYPE_BOOLEAN: {
    dbus_bool_t val;
    dbus_message_iter_get_basic(iter, &val);
    *pos += snprintf(buffer + *pos, max - *pos, "%s", val ? "true" : "false");
    break;
  }
  case DBUS_TYPE_DOUBLE: {
    double val;
    dbus_message_iter_get_basic(iter, &val);
    *pos += snprintf(buffer + *pos, max - *pos, "%.2f", val);
    break;
  }
  case DBUS_TYPE_INT64: {
    dbus_int64_t val;
    dbus_message_iter_get_basic(iter, &val);
    *pos += snprintf(buffer + *pos, max - *pos, "%ld", val);
    if (strcmp(prop_name, "Position") == 0) {
      char hms[16];
      to_hms(val, hms, sizeof(hms));
      *pos += snprintf(buffer + *pos, max - *pos, ", \"PositionHMS\": %s", hms);
    }
    break;
  }
  case DBUS_TYPE_STRING: {
    char *val;
    dbus_message_iter_get_basic(iter, &val);
    char *escaped = json_escape(val);
    *pos += snprintf(buffer + *pos, max - *pos, "\"%s\"", escaped);
    free(escaped);
    break;
  }
  case DBUS_TYPE_VARIANT: {
    DBusMessageIter sub_iter;
    dbus_message_iter_recurse(iter, &sub_iter);
    print_simple_property(buffer, pos, max, prop_name, &sub_iter);
    break;
  }
  default:
    *pos += snprintf(buffer + *pos, max - *pos, "\"Unhandled type %c\"", type);
    break;
  }
  *pos += snprintf(buffer + *pos, max - *pos, ", ");
}

// Print Metadata
void print_metadata(char *buffer, size_t *pos, size_t max,
                    DBusMessageIter *iter) {
  if (!iter || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
    return;

  DBusMessageIter array_iter;
  dbus_message_iter_recurse(iter, &array_iter);
  while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_DICT_ENTRY) {
    DBusMessageIter dict_iter;
    dbus_message_iter_recurse(&array_iter, &dict_iter);

    char *key;
    dbus_message_iter_get_basic(&dict_iter, &key);
    dbus_message_iter_next(&dict_iter);

    DBusMessageIter value_iter;
    dbus_message_iter_recurse(&dict_iter, &value_iter);
    int sub_type = dbus_message_iter_get_arg_type(&value_iter);

    const char *stripped_key = strip_prefix(key);
    *pos += snprintf(buffer + *pos, max - *pos, "\"%s\": ", stripped_key);
    switch (sub_type) {
    case DBUS_TYPE_STRING: {
      char *val;
      dbus_message_iter_get_basic(&value_iter, &val);
      if (strcmp(stripped_key, "artUrl") == 0) {
        print_art_url(buffer, pos, max, val);
      } else {
        char *escaped = json_escape(val);
        *pos += snprintf(buffer + *pos, max - *pos, "\"%s\"", escaped);
        free(escaped);
      }
      break;
    }
    case DBUS_TYPE_INT64: {
      dbus_int64_t val;
      dbus_message_iter_get_basic(&value_iter, &val);
      *pos += snprintf(buffer + *pos, max - *pos, "%ld", val);
      if (strcmp(stripped_key, "length") == 0) {
        char hms[32];
        to_hms(val, hms, sizeof(hms));
        *pos += snprintf(buffer + *pos, max - *pos, ", \"lengthHMS\": %s", hms);
      }
      break;
    }
    case DBUS_TYPE_UINT64: {
      dbus_uint64_t val;
      dbus_message_iter_get_basic(&value_iter, &val);
      *pos +=
          snprintf(buffer + *pos, max - *pos, "%llu", (unsigned long long)val);
      if (strcmp(stripped_key, "length") == 0) {
        char hms[32];
        to_hms(val, hms, sizeof(hms));
        *pos += snprintf(buffer + *pos, max - *pos, ", \"lengthHMS\": %s", hms);
      }
      break;
    }
    case DBUS_TYPE_INT32: {
      dbus_int32_t val;
      dbus_message_iter_get_basic(&value_iter, &val);
      *pos += snprintf(buffer + *pos, max - *pos, "%d", val);
      break;
    }
    case DBUS_TYPE_DOUBLE: {
      double val;
      dbus_message_iter_get_basic(&value_iter, &val);
      *pos += snprintf(buffer + *pos, max - *pos, "%.2f", val);
      break;
    }
    case DBUS_TYPE_ARRAY: {
      if (strcmp(stripped_key, "artist") == 0 ||
          strcmp(stripped_key, "albumArtist") == 0) {
        DBusMessageIter artist_iter;
        dbus_message_iter_recurse(&value_iter, &artist_iter);
        *pos += snprintf(buffer + *pos, max - *pos, "\"");
        int first = 1;
        while (dbus_message_iter_get_arg_type(&artist_iter) ==
               DBUS_TYPE_STRING) {
          char *artist;
          dbus_message_iter_get_basic(&artist_iter, &artist);
          if (!first)
            *pos += snprintf(buffer + *pos, max - *pos, ", ");
          char *escaped = json_escape(artist);
          *pos += snprintf(buffer + *pos, max - *pos, "%s", escaped);
          free(escaped);
          first = 0;
          dbus_message_iter_next(&artist_iter);
        }
        *pos += snprintf(buffer + *pos, max - *pos, "\"");
      } else {
        *pos += snprintf(buffer + *pos, max - *pos, "\"Array unsupported\"");
      }
      break;
    }
    default:
      *pos += snprintf(buffer + *pos, max - *pos, "\"Unhandled sub-type %c\"",
                       sub_type);
      break;
    }
    *pos += snprintf(buffer + *pos, max - *pos, ", ");
    dbus_message_iter_next(&array_iter);
  }
}

// Build JSON
char *build_json(DBusConnection *conn) {
  char *buffer = malloc(8192);
  if (!buffer)
    return NULL;
  size_t pos = 0;
  pos += snprintf(buffer + pos, 8192 - pos, "[");

  int first_player = 1;
  for (int i = 0; i < player_count; i++) {
    if (!first_player)
      pos += snprintf(buffer + pos, 8192 - pos, ",");
    char *name_escaped = json_escape(players[i].name);
    char *desktop_escaped = json_escape(players[i].desktop_entry);
    pos += snprintf(buffer + pos, 8192 - pos,
                    "  {\"Player\": \"%s\", \"DesktopEntry\": \"%s\", ",
                    name_escaped, desktop_escaped);
    free(name_escaped);
    free(desktop_escaped);

    const char *properties[] = {
        "CanControl", "CanGoNext",  "CanGoPrevious",  "CanPause", "CanPlay",
        "CanSeek",    "LoopStatus", "PlaybackStatus", "Position", "Shuffle"};
    int prop_count = sizeof(properties) / sizeof(properties[0]);

    for (int j = 0; j < prop_count; j++) {
      DBusMessage *prop_msg =
          get_property(conn, players[i].name, "org.mpris.MediaPlayer2.Player",
                       properties[j]);
      if (prop_msg) {
        DBusMessageIter prop_args;
        if (dbus_message_iter_init(prop_msg, &prop_args)) {
          DBusMessageIter prop_variant;
          dbus_message_iter_recurse(&prop_args, &prop_variant);
          print_simple_property(buffer, &pos, 8192, properties[j],
                                &prop_variant);
        }
        dbus_message_unref(prop_msg);
      }
    }

    DBusMessage *meta_msg = get_property(
        conn, players[i].name, "org.mpris.MediaPlayer2.Player", "Metadata");
    if (meta_msg) {
      DBusMessageIter meta_args;
      if (dbus_message_iter_init(meta_msg, &meta_args)) {
        DBusMessageIter meta_variant;
        dbus_message_iter_recurse(&meta_args, &meta_variant);
        print_metadata(buffer, &pos, 8192, &meta_variant);
      }
      dbus_message_unref(meta_msg);
    }

    char *sink_id_escaped = json_escape(players[i].sink_id ? players[i].sink_id : "null");
    char *id_escaped = json_escape(players[i].id ? players[i].id : "null");
    char *serial_escaped = json_escape(players[i].serial ? players[i].serial : "null");
    pos += snprintf(
        buffer + pos, 8192 - pos,
        "\"sinkID\": \"%s\", \"id\": \"%s\", \"serial\": \"%s\", \"volume\": %d, \"mute\": %s}",
        sink_id_escaped, id_escaped, serial_escaped, players[i].volume, players[i].mute ? "true" : "false");
    free(sink_id_escaped);
    free(id_escaped);
    free(serial_escaped);
    first_player = 0;
  }
  pos += snprintf(buffer + pos, 8192 - pos, "]");
  return buffer;
}

// Cleanup
void cleanup(int sig) {
  for (int i = 0; i < player_count; i++) {
    free(players[i].name);
    free(players[i].desktop_entry);
    free(players[i].sink_id);
    free(players[i].id);
    free(players[i].serial);
  }
  free(players);
  for (size_t i = 0; i < sink_cache_count; i++) {
    free(sink_cache[i].name);
  }
  free(sink_cache);
  free(last_json);
  if (pa_ctx) {
    pa_context_disconnect(pa_ctx);
    pa_context_unref(pa_ctx);
  }
  if (pa_ml)
    pa_mainloop_free(pa_ml);
  if (conn)
    dbus_connection_unref(conn);
  exit(0);
}

// Update player list
void update_player_list(DBusConnection *conn) {
  DBusMessage *msg =
      get_property(conn, "org.mpris.MediaPlayer2.playerctld",
                   "com.github.altdesktop.playerctld", "PlayerNames");
  if (!msg)
    return;

  DBusMessageIter args;
  if (!dbus_message_iter_init(msg, &args)) {
    dbus_message_unref(msg);
    return;
  }

  DBusMessageIter variant_iter;
  dbus_message_iter_recurse(&args, &variant_iter);
  int new_count = 0;
  char **new_names = extract_string_array(&variant_iter, &new_count);
  dbus_message_unref(msg);

  if (!new_names)
    return;

  qsort(new_names, new_count, sizeof(char *), compare_strings);

  Player *new_players = malloc(new_count * sizeof(Player));
  int new_player_idx = 0;

  for (int i = 0; i < new_count; i++) {
    int found = 0;
    for (int j = 0; j < player_count; j++) {
      if (strcmp(new_names[i], players[j].name) == 0) {
        new_players[new_player_idx] = players[j];
        found = 1;
        break;
      }
    }
    if (!found) {
      char *desktop_entry;
      new_players[new_player_idx].name =
          truncate_player_name(new_names[i], &desktop_entry);
      new_players[new_player_idx].desktop_entry = desktop_entry;
      new_players[new_player_idx].sink_id = NULL;
      new_players[new_player_idx].id = NULL;
      new_players[new_player_idx].serial = NULL;
      new_players[new_player_idx].volume = 0;
    }
    new_player_idx++;
  }

  for (int i = 0; i < player_count; i++) {
    int still_exists = 0;
    for (int j = 0; j < new_count; j++) {
      if (strcmp(players[i].name, new_names[j]) == 0) {
        still_exists = 1;
        break;
      }
    }
    if (!still_exists) {
      free(players[i].name);
      free(players[i].desktop_entry);
      free(players[i].sink_id);
      free(players[i].id);
      free(players[i].serial);
    }
  }
  free(players);

  players = new_players;
  player_count = new_count;
  free_string_array(new_names, new_count);
}

// Wait for PulseAudio connection with proper timeout
static int wait_for_pa_connection(pa_context *ctx, pa_mainloop *ml) {
  pa_context_state_t state;
  time_t start_time = time(NULL);
  int timeout_seconds = 5;

  while ((state = pa_context_get_state(ctx)) != PA_CONTEXT_READY) {
    if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
      fprintf(stderr, "PulseAudio connection failed: %s\n",
              pa_strerror(pa_context_errno(ctx)));
      return -1;
    }
    if (difftime(time(NULL), start_time) >= timeout_seconds) {
      fprintf(stderr, "PulseAudio connection timeout after %d seconds\n",
              timeout_seconds);
      return -1;
    }
    pa_mainloop_iterate(ml, 1, NULL);
  }
  return 0;
}

// Reset PulseAudio data for all players
static void reset_pulseaudio_data() {
  for (int i = 0; i < player_count; i++) {
    free(players[i].sink_id);
    players[i].sink_id = NULL;
    free(players[i].id);
    players[i].id = NULL;
    free(players[i].serial);
    players[i].serial = NULL;
    players[i].volume = 0;
    players[i].mute = 0;
  }
}

int main() {
  signal(SIGINT, cleanup);
  signal(SIGTERM, cleanup);

  // D-Bus setup
  DBusError err;
  dbus_error_init(&err);
  conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (!conn) {
    check_dbus_error(&err, "bus_get");
    return 1;
  }

  // PulseAudio setup
  pa_ml = pa_mainloop_new();
  if (!pa_ml) {
    fprintf(stderr, "Failed to create PulseAudio mainloop\n");
    cleanup(0);
  }
  pa_ctx = pa_context_new(pa_mainloop_get_api(pa_ml), "mpris_fetch");
  if (!pa_ctx) {
    fprintf(stderr, "Failed to create PulseAudio context\n");
    cleanup(0);
  }
  if (pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
    fprintf(stderr, "PulseAudio connection failed: %s\n",
            pa_strerror(pa_context_errno(pa_ctx)));
    cleanup(0);
  }

  // Wait for PulseAudio to be ready
  if (wait_for_pa_connection(pa_ctx, pa_ml) < 0) {
    cleanup(0);
  }

  // Main polling loop
  while (1) {
    // Update player list from D-Bus
    update_player_list(conn);

    // Update sink cache
    pa_operation *sink_op =
        pa_context_get_sink_info_list(pa_ctx, sink_info_cb, NULL);
    if (sink_op) {
      while (pa_operation_get_state(sink_op) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(pa_ml, 1, NULL);
      }
      pa_operation_unref(sink_op);
    }

    if (player_count == 0) {
      char *new_json = build_json(conn);
      if (!last_json || strcmp(new_json, last_json) != 0) {
        printf("%s\n", new_json);
        fflush(stdout);
        free(last_json);
        last_json = new_json;
      } else {
        free(new_json);
      }
      sleep(1);
      continue;
    }

    // Reset PulseAudio data before fetching new values
    reset_pulseaudio_data();

    // Fetch PulseAudio data
    pa_operation *op =
        pa_context_get_sink_input_info_list(pa_ctx, sink_input_info_cb, NULL);
    if (op) {
      while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_mainloop_iterate(pa_ml, 1, NULL);
      }
      pa_operation_unref(op);
    } else {
      fprintf(stderr, "Failed to get PulseAudio sink input info\n");
    }

    // Build and compare JSON
    char *new_json = build_json(conn);
    if (!new_json) {
      sleep(1);
      continue;
    }

    if (!last_json || strcmp(new_json, last_json) != 0) {
      printf("%s\n", new_json);
      fflush(stdout);
      free(last_json);
      last_json = new_json;
    } else {
      free(new_json);
    }

    sleep(1);
  }

  cleanup(0);
  return 0;
}
