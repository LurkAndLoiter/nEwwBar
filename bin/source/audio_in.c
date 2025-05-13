/*  Copyright © 2025, LurkAndLoiter and contributors.
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

#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure to hold source information
typedef struct {
  uint32_t index;  // PulseAudio index
  char *name;
  char *description;
  char *icon;
  bool muted;
  int volume;  // 0-100 range
  bool is_default;
  pa_source_state_t state;  // Add source state
} AudioSource;

// Context for storing state
typedef struct {
  pa_mainloop *pa_mainloop;
  pa_context *pa_context;
  AudioSource *sources;
  size_t source_count;
  char *default_source;
} AppContext;

// Forward declarations
void print_sources(AppContext *app);

// Callback for source info
void source_info_cb(pa_context *c, const pa_source_info *i, int eol,
                    void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (eol) {
    print_sources(app);
    // Reset the source list after printing
    for (size_t j = 0; j < app->source_count; j++) {
      free(app->sources[j].name);
      free(app->sources[j].description);
      free(app->sources[j].icon);
    }
    free(app->sources);
    app->sources = NULL;
    app->source_count = 0;
    return;
  }

  app->sources =
      realloc(app->sources, (app->source_count + 1) * sizeof(AudioSource));
  AudioSource *src = &app->sources[app->source_count++];

  src->index = i->index;
  src->name = strdup(i->name);
  src->description = strdup(i->description ? i->description : "");
  src->icon =
      strdup(i->proplist ? pa_proplist_gets(i->proplist, "device.icon_name")
                         : "audio-input-microphone");
  src->muted = i->mute;
  src->volume = (int)((pa_cvolume_avg(&i->volume) * 100) /
                      PA_VOLUME_NORM);  // Convert to 0-100
  src->is_default =
      app->default_source && strcmp(i->name, app->default_source) == 0;
  src->state = i->state;  // Store the state
}

// Server info callback for default source
void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (app->default_source)
    free(app->default_source);
  app->default_source = strdup(i->default_source_name);
}

// Subscription callback
void subscription_cb(pa_context *c, pa_subscription_event_type_t t,
                     uint32_t idx, void *userdata) {
  if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE ||
      (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) !=
          PA_SUBSCRIPTION_EVENT_SOURCE) {
    return;
  }
  AppContext *app = (AppContext *)userdata;
  pa_operation *op = pa_context_get_source_info_list(c, source_info_cb, app);
  if (op)
    pa_operation_unref(op);
}

// Helper function to convert state to string
const char *state_to_string(pa_source_state_t state) {
  switch (state) {
  case PA_SOURCE_RUNNING:
    return "running";
  case PA_SOURCE_IDLE:
    return "idle";
  case PA_SOURCE_SUSPENDED:
    return "suspended";
  case PA_SOURCE_INVALID_STATE:
    return "invalid";
  default:
    return "unknown";
  }
}

// Print sources as JSON
void print_sources(AppContext *app) {
  printf("[");
  for (size_t i = 0; i < app->source_count; i++) {
    AudioSource *src = &app->sources[i];
    if (i > 0)
      printf(",");
    printf("{\"id\": %u, \"mute\": \"%s\", \"volume\": %d, \"default\": %s, "
           "\"source\": \"%s\", \"name\": \"%s\", \"icon\": \"%s\", \"state\": "
           "\"%s\"}",
           src->index, src->muted ? "yes" : "no", src->volume,
           src->is_default ? "true" : "false", src->name, src->description,
           src->icon,
           state_to_string(src->state));  // Add state to JSON
  }
  printf("]\n");
  fflush(stdout);
}

// PulseAudio state callback
void pa_state_cb(pa_context *c, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  switch (pa_context_get_state(c)) {
  case PA_CONTEXT_READY: {
    pa_operation *op1 = pa_context_get_server_info(c, server_info_cb, app);
    pa_operation *op2 = pa_context_get_source_info_list(c, source_info_cb, app);
    if (op1)
      pa_operation_unref(op1);
    if (op2)
      pa_operation_unref(op2);
    pa_context_set_subscribe_callback(c, subscription_cb, app);
    pa_operation *op3 =
        pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SOURCE, NULL, NULL);
    if (op3)
      pa_operation_unref(op3);
    break;
  }
  case PA_CONTEXT_FAILED:
  case PA_CONTEXT_TERMINATED:
    pa_mainloop_quit(app->pa_mainloop, 1);
    break;
  default:
    break;
  }
}

int main() {
  AppContext app = {0};

  // Initialize PulseAudio
  app.pa_mainloop = pa_mainloop_new();
  app.pa_context =
      pa_context_new(pa_mainloop_get_api(app.pa_mainloop), "AudioMonitor");
  pa_context_set_state_callback(app.pa_context, pa_state_cb, &app);
  if (pa_context_connect(app.pa_context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
    fprintf(stderr, "PulseAudio connect failed: %s\n",
            pa_strerror(pa_context_errno(app.pa_context)));
    goto cleanup;
  }

  // Run main loop
  int ret = 0;
  if (pa_mainloop_run(app.pa_mainloop, &ret) < 0) {
    ret = 1;
  }

cleanup:
  for (size_t i = 0; i < app.source_count; i++) {
    free(app.sources[i].name);
    free(app.sources[i].description);
    free(app.sources[i].icon);
  }
  free(app.sources);
  free(app.default_source);
  if (app.pa_context)
    pa_context_unref(app.pa_context);
  if (app.pa_mainloop)
    pa_mainloop_free(app.pa_mainloop);
  return ret;
}
