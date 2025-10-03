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

#include "../include/json.h"
#include <pulse/pulseaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// AudioSource: holds details about a PulseAudio input (source)
typedef struct {
  uint32_t index;
  char *name;
  char *description;
  char *icon;
  bool muted;
  int volume;
  bool is_default;
  pa_source_state_t state;
} AudioSource;

// AppContext: holds all application state
typedef struct {
  pa_mainloop *pa_mainloop;
  pa_context *pa_context;
  AudioSource *sources;
  size_t source_count;
  char *default_source;
  bool got_server_info;
  bool got_source_info;
  bool want_reprint;
} AppContext;

// --- Utility: state to string ---
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

// --- Memory management for AudioSource array ---
void free_sources(AudioSource *sources, size_t count) {
  if (!sources)
    return;
  for (size_t i = 0; i < count; ++i) {
    free(sources[i].name);
    free(sources[i].description);
    free(sources[i].icon);
  }
  free(sources);
}

// --- Print all sources as JSON array ---
void print_sources(AppContext *app) {
  printf("[");
  for (size_t i = 0; i < app->source_count; ++i) {
    AudioSource *src = &app->sources[i];
    if (i)
      printf(",");
    printf("{\"id\":%u,", src->index);
    printf("\"mute\":%s,", src->muted ? "true" : "false");
    printf("\"volume\":%d,", src->volume);
    printf("\"default\":%s,", src->is_default ? "true" : "false");
    printf("\"source\":");
    print_json_str(src->name);
    printf(",");
    printf("\"name\":");
    print_json_str(src->description);
    printf(",");
    printf("\"icon\":");
    print_json_str(src->icon);
    printf(",");
    printf("\"state\":");
    print_json_str(state_to_string(src->state));
    printf("}");
  }
  printf("]\n");
  fflush(stdout);
}

// --- Source info callback: collects AudioSource array ---
void source_info_cb(pa_context *c, const pa_source_info *i, int eol,
                    void *userdata) {
  AppContext *app = (AppContext *)userdata;

  if (eol) {
    app->got_source_info = true;
    if (app->got_server_info) {
      print_sources(app);
      app->got_server_info = app->got_source_info = false;
    }
    free_sources(app->sources, app->source_count);
    app->sources = NULL;
    app->source_count = 0;
    return;
  }

  // Reallocate for new source
  AudioSource *tmp =
      realloc(app->sources, (app->source_count + 1) * sizeof(AudioSource));
  if (!tmp) {
    fprintf(stderr, "realloc failed\n");
    exit(1);
  }
  app->sources = tmp;
  AudioSource *src = &app->sources[app->source_count++];
  src->index = i->index;
  src->name = strdup(i->name ? i->name : "");
  src->description = strdup(i->description ? i->description : "");
  src->icon = strdup(
      i->proplist ? pa_proplist_gets(i->proplist, "device.icon_name")
                        ? pa_proplist_gets(i->proplist, "device.icon_name")
                        : "audio-input-microphone"
                  : "audio-input-microphone");
  src->muted = i->mute;
  src->volume = (int)((pa_cvolume_avg(&i->volume) * 100) / PA_VOLUME_NORM);
  src->state = i->state;
  src->is_default =
      app->default_source && strcmp(i->name, app->default_source) == 0;
}

// --- Server info callback: gets default source name ---
void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (app->default_source)
    free(app->default_source);
  app->default_source =
      strdup(i->default_source_name ? i->default_source_name : "");
  app->got_server_info = true;
  // If source info is already fetched, print. Otherwise, wait for
  // source_info_cb.
  if (app->got_source_info) {
    print_sources(app);
    app->got_server_info = app->got_source_info = false;
  }
}

// --- Ensure both server and source info are fresh after events ---
void refresh_info(pa_context *c, AppContext *app) {
  app->got_server_info = app->got_source_info = false;
  pa_operation *op1 = pa_context_get_server_info(c, server_info_cb, app);
  pa_operation *op2 = pa_context_get_source_info_list(c, source_info_cb, app);
  if (op1)
    pa_operation_unref(op1);
  if (op2)
    pa_operation_unref(op2);
}

// --- Subscription callback: handle all relevant events ---
void subscription_cb(pa_context *c, pa_subscription_event_type_t t,
                     uint32_t idx, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  pa_subscription_event_type_t fac = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  if (fac == PA_SUBSCRIPTION_EVENT_SOURCE ||
      fac == PA_SUBSCRIPTION_EVENT_SERVER) {
    refresh_info(c, app);
  }
}

// --- State callback: initial setup and reconnect handling ---
void pa_state_cb(pa_context *c, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  switch (pa_context_get_state(c)) {
  case PA_CONTEXT_READY: {
    pa_context_set_subscribe_callback(c, subscription_cb, app);
    pa_operation *op = pa_context_subscribe(
        c, PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER, NULL,
        NULL);
    if (op)
      pa_operation_unref(op);
    refresh_info(c, app);
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

int main(void) {
  AppContext app = {0};

  // Initialize PulseAudio
  app.pa_mainloop = pa_mainloop_new();
  if (!app.pa_mainloop) {
    fprintf(stderr, "Failed to create PulseAudio mainloop\n");
    return 1;
  }
  app.pa_context =
      pa_context_new(pa_mainloop_get_api(app.pa_mainloop), "AudioMonitor");
  if (!app.pa_context) {
    fprintf(stderr, "Failed to create PulseAudio context\n");
    pa_mainloop_free(app.pa_mainloop);
    return 1;
  }

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
  free_sources(app.sources, app.source_count);
  free(app.default_source);
  if (app.pa_context)
    pa_context_unref(app.pa_context);
  if (app.pa_mainloop)
    pa_mainloop_free(app.pa_mainloop);
  return ret;
}
