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

// Structure to hold sink information
typedef struct {
  char *object_id;
  char *name;
  char *description;
  char *icon;
  bool muted;
  int volume;
  bool is_default;
} AudioSink;

// Context for storing state, including synchronization flags
typedef struct {
  pa_mainloop *pa_mainloop;
  pa_context *pa_context;
  AudioSink *sinks;
  size_t sink_count;
  char *default_sink;
  bool got_server_info;
  bool got_sink_info;
} AppContext;

// --- Memory management for AudioSink array ---
void free_sinks(AudioSink *sinks, size_t count) {
  if (!sinks)
    return;
  for (size_t i = 0; i < count; ++i) {
    free(sinks[i].object_id);
    free(sinks[i].name);
    free(sinks[i].description);
    free(sinks[i].icon);
  }
  free(sinks);
}

// --- Print all sinks as JSON array ---
void print_sinks(AppContext *app) {
  printf("[");
  for (size_t i = 0; i < app->sink_count; ++i) {
    AudioSink *sink = &app->sinks[i];
    if (i)
      printf(",");
    printf("{\"id\":");
    print_json_str(sink->object_id);
    printf(",\"mute\":%s", sink->muted ? "true" : "false");
    printf(",\"volume\":%d", sink->volume);
    printf(",\"default\":%s", sink->is_default ? "true" : "false");
    printf(",\"sink\":");
    print_json_str(sink->name);
    printf(",\"name\":");
    print_json_str(sink->description);
    printf(",\"icon\":");
    print_json_str(sink->icon);
    printf("}");
  }
  printf("]\n");
  fflush(stdout);
}

// --- Sink info callback: collects AudioSink array ---
void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                  void *userdata) {
  AppContext *app = (AppContext *)userdata;

  if (eol) {
    app->got_sink_info = true;
    if (app->got_server_info) {
      print_sinks(app);
      app->got_server_info = app->got_sink_info = false;
    }
    free_sinks(app->sinks, app->sink_count);
    app->sinks = NULL;
    app->sink_count = 0;
    return;
  }

  AudioSink *tmp =
      realloc(app->sinks, (app->sink_count + 1) * sizeof(AudioSink));
  if (!tmp) {
    fprintf(stderr, "realloc failed\n");
    exit(1);
  }
  app->sinks = tmp;
  AudioSink *sink = &app->sinks[app->sink_count++];

  const char *obj_id =
      i->proplist ? pa_proplist_gets(i->proplist, "object.id") : NULL;
  sink->object_id = obj_id ? strdup(obj_id) : strdup("unknown");
  sink->name = strdup(i->name ? i->name : "");
  sink->description = strdup(i->description ? i->description : "");
  const char *icon =
      i->proplist ? pa_proplist_gets(i->proplist, "device.icon_name") : NULL;
  sink->icon = strdup(icon ? icon : "audio-speakers");
  sink->muted = i->mute;
  double volume_percent =
      ((double)pa_cvolume_avg(&i->volume) * 100) / PA_VOLUME_NORM;
  sink->volume = (int)(volume_percent + 0.5);
  sink->is_default =
      app->default_sink && strcmp(i->name, app->default_sink) == 0;
}

// --- Server info callback for default sink ---
void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (app->default_sink)
    free(app->default_sink);
  app->default_sink = strdup(i->default_sink_name ? i->default_sink_name : "");
  app->got_server_info = true;
  if (app->got_sink_info) {
    print_sinks(app);
    app->got_server_info = app->got_sink_info = false;
  }
}

// --- Ensure both server and sink info are fresh after events ---
void refresh_info(pa_context *c, AppContext *app) {
  app->got_server_info = app->got_sink_info = false;
  pa_operation *op1 = pa_context_get_server_info(c, server_info_cb, app);
  pa_operation *op2 = pa_context_get_sink_info_list(c, sink_info_cb, app);
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
  if (fac == PA_SUBSCRIPTION_EVENT_SINK ||
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
        c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
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
      pa_context_new(pa_mainloop_get_api(app.pa_mainloop), "SinkMonitor");
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

  int ret = 0;
  if (pa_mainloop_run(app.pa_mainloop, &ret) < 0) {
    ret = 1;
  }

cleanup:
  free_sinks(app.sinks, app.sink_count);
  free(app.default_sink);
  if (app.pa_context)
    pa_context_unref(app.pa_context);
  if (app.pa_mainloop)
    pa_mainloop_free(app.pa_mainloop);
  return ret;
}
