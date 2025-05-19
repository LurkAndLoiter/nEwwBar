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

#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_WORKSPACES 6
#define BUFFER_SIZE 1024

#ifndef DEBUG
#define DEBUG 0
#endif

// Global variable to store last output
static char *last_output = NULL;

// Function to get workspace information
void print_workspaces() {
  // Execute hyprctl command and capture output
  FILE *fp = popen("hyprctl workspaces -j", "r");
  if (!fp) {
    if (DEBUG) {
      perror("popen failed");
    }
    return;
  }

  // Read the JSON output
  char buffer[BUFFER_SIZE];
  size_t bytes_read;
  char *json_str = NULL;
  size_t total_size = 0;

  while ((bytes_read = fread(buffer, 1, BUFFER_SIZE - 1, fp)) > 0) {
    buffer[bytes_read] = '\0';
    json_str = realloc(json_str, total_size + bytes_read + 1);
    memcpy(json_str + total_size, buffer, bytes_read);
    total_size += bytes_read;
    json_str[total_size] = '\0';
  }
  pclose(fp);

  if (!json_str) {
    if (DEBUG) {
      printf("Failed to read hyprctl output\n");
    }
    return;
  }

  // Parse JSON
  json_object *root = json_tokener_parse(json_str);
  if (!root) {
    if (DEBUG) {
      printf("Failed to parse JSON\n");
    }
    free(json_str);
    return;
  }

  // Create workspace windows object
  json_object *workspace_windows = json_object_new_object();
  int len = json_object_array_length(root);
  for (int i = 0; i < len; i++) {
    json_object *obj = json_object_array_get_idx(root, i);
    json_object *id = json_object_object_get(obj, "id");
    json_object *windows = json_object_object_get(obj, "windows");
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", json_object_get_int(id));
    json_object_object_add(workspace_windows, id_str, json_object_get(windows));
  }

  // Generate output for workspaces 1-6
  json_object *output = json_object_new_array();
  for (int i = 1; i <= MAX_WORKSPACES; i++) {
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", i);
    json_object *entry = json_object_new_object();
    json_object_object_add(entry, "id", json_object_new_string(id_str));

    json_object *win_count = json_object_object_get(workspace_windows, id_str);
    int windows = win_count ? json_object_get_int(win_count) : 0;
    json_object_object_add(entry, "windows", json_object_new_int(windows));

    json_object_array_add(output, entry);
  }

  // Convert to string and compare with last output
  const char *new_output = json_object_to_json_string_ext(output, JSON_C_TO_STRING_PLAIN);
  
  // Only print if different from last output
  if (!last_output || strcmp(new_output, last_output) != 0) {
    printf("%s\n", new_output);
    fflush(stdout);
    
    // Update last_output
    free(last_output);
    last_output = strdup(new_output);
  }

  // Cleanup
  json_object_put(root);
  json_object_put(workspace_windows);
  json_object_put(output);
  free(json_str);
}

int main() {
  // Get required environment variables
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  char *hyprland_instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!xdg_runtime || !hyprland_instance) {
    if (DEBUG) {
      fprintf(stderr, "Required environment variables not set\n");
    }
    return 1;
  }

  // Construct socket path
  char socket_path[256];
  snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
           xdg_runtime, hyprland_instance);

  // Create UNIX socket
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    if (DEBUG) {
      perror("socket creation failed");
    }
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  // Connect to socket
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    if (DEBUG) {
      perror("socket connect failed");
    }
    close(sock);
    return 1;
  }

  // Initial workspace print
  print_workspaces();

  // Main event loop
  char buffer[BUFFER_SIZE];
  while (1) {
    ssize_t bytes = read(sock, buffer, BUFFER_SIZE - 1);
    if (bytes <= 0) {
      if (bytes == 0) {
        if (DEBUG) {
          printf("Socket closed\n");
        }
      } else {
        if (DEBUG) {
          perror("socket read failed");
        }
      }
      break;
    }
    buffer[bytes] = '\0';

    // Check for relevant events
    if (strstr(buffer, "closewindow>>") ||
        strstr(buffer, "openwindow>>") ||
        strstr(buffer, "createworkspace>>") ||
        strstr(buffer, "destroyworkspace>>")) {
      print_workspaces();
    }
  }

  // Cleanup
  free(last_output);
  close(sock);
  return 0;
}
