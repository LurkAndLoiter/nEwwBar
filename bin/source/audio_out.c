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

// Context for storing state
typedef struct {
  pa_mainloop *pa_mainloop;
  pa_context *pa_context;
  AudioSink *sinks;
  size_t sink_count;
  char *default_sink;
} AppContext;

// Forward declarations
void print_sinks(AppContext *app);

// Callback for sink info
void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol,
                  void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (eol) {
    print_sinks(app);
    for (size_t j = 0; j < app->sink_count; j++) {
      free(app->sinks[j].object_id);
      free(app->sinks[j].name);
      free(app->sinks[j].description);
      free(app->sinks[j].icon);
    }
    free(app->sinks);
    app->sinks = NULL;
    app->sink_count = 0;
    return;
  }

  app->sinks = realloc(app->sinks, (app->sink_count + 1) * sizeof(AudioSink));
  AudioSink *sink = &app->sinks[app->sink_count++];

  const char *obj_id = pa_proplist_gets(i->proplist, "object.id");
  sink->object_id = obj_id ? strdup(obj_id) : strdup("unknown");
  sink->name = strdup(i->name);
  sink->description = strdup(i->description ? i->description : "");
  sink->icon = strdup(pa_proplist_gets(i->proplist, "device.icon_name")
                          ? pa_proplist_gets(i->proplist, "device.icon_name")
                          : "audio-speakers");
  sink->muted = i->mute;
  double volume_percent =
      ((double)pa_cvolume_avg(&i->volume) * 100) / PA_VOLUME_NORM;
  sink->volume = (int)(volume_percent + 0.5);
  sink->is_default =
      app->default_sink && strcmp(i->name, app->default_sink) == 0;
}

// Server info callback for default sink
void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
  AppContext *app = (AppContext *)userdata;
  if (app->default_sink)
    free(app->default_sink);
  app->default_sink = strdup(i->default_sink_name);
  // After updating default sink, refresh the sink list
  pa_operation *op = pa_context_get_sink_info_list(c, sink_info_cb, app);
  if (op)
    pa_operation_unref(op);
}

// Subscription callback
void subscription_cb(pa_context *c, pa_subscription_event_type_t t,
                     uint32_t idx, void *userdata) {
  AppContext *app = (AppContext *)userdata;

  // Check if it's a sink-related change event
  if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK) {
    // Refresh sink list for any sink change
    pa_operation *op1 = pa_context_get_sink_info_list(c, sink_info_cb, app);
    if (op1)
      pa_operation_unref(op1);
  }
  // Check if it's a server change (which includes default sink changes)
  if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) ==
      PA_SUBSCRIPTION_EVENT_SERVER) {
    // Refresh default sink info when server changes (includes default sink
    // changes)
    pa_operation *op2 = pa_context_get_server_info(c, server_info_cb, app);
    if (op2)
      pa_operation_unref(op2);
  }
}

// Print sinks as JSON
void print_sinks(AppContext *app) {
  printf("[");
  for (size_t i = 0; i < app->sink_count; i++) {
    AudioSink *sink = &app->sinks[i];
    if (i > 0)
      printf(",");
    printf("{\"id\": \"%s\", \"mute\": %s, \"volume\": %d, \"default\": "
           "%s, \"sink\": \"%s\", \"name\": \"%s\", \"icon\": \"%s\"}",
           sink->object_id, sink->muted ? "true" : "false", sink->volume,
           sink->is_default ? "true" : "false", sink->name, sink->description,
           sink->icon);
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
    pa_operation *op2 = pa_context_get_sink_info_list(c, sink_info_cb, app);
    if (op1)
      pa_operation_unref(op1);
    if (op2)
      pa_operation_unref(op2);
    pa_context_set_subscribe_callback(c, subscription_cb, app);
    // Subscribe to both sink and server events
    pa_operation *op3 = pa_context_subscribe(
        c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
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

  app.pa_mainloop = pa_mainloop_new();
  app.pa_context =
      pa_context_new(pa_mainloop_get_api(app.pa_mainloop), "SinkMonitor");
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
  for (size_t i = 0; i < app.sink_count; i++) {
    free(app.sinks[i].object_id);
    free(app.sinks[i].name);
    free(app.sinks[i].description);
    free(app.sinks[i].icon);
  }
  free(app.sinks);
  free(app.default_sink);
  if (app.pa_context)
    pa_context_unref(app.pa_context);
  if (app.pa_mainloop)
    pa_mainloop_free(app.pa_mainloop);
  return ret;
}
