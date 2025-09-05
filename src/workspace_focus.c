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

#define BUFFER_SIZE 1024

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...) do { printf(fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define DEBUG_MSG(fmt, ...) do { } while (0)
#endif

void print_initial_workspace() {
  // hyprctl monitors -j | jq '.[] | select(.focused == true) | .id'
  FILE *fp = popen("hyprctl monitors -j", "r");
  if (!fp) {
    DEBUG_MSG("popen failed");
    return;
  }

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
    DEBUG_MSG("Failed to read hyprctl output\n");
    return;
  }

  json_object *root = json_tokener_parse(json_str);
  if (!root) {
    DEBUG_MSG("Failed to parse JSON\n");
    free(json_str);
    return;
  }

  int array_len = json_object_array_length(root);
  for (int i = 0; i < array_len; i++) {
    json_object *monitor = json_object_array_get_idx(root, i);
    json_object *focused = json_object_object_get(monitor, "focused");
    if (json_object_get_boolean(focused)) {
      json_object *id = json_object_object_get(monitor, "id");
      printf("%d\n", json_object_get_int(id));
      fflush(stdout);
      break;
    }
  }

  json_object_put(root);
  free(json_str);
}

int main() {
  DEBUG_MSG("DEBUG enabled");
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  char *hyprland_instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!xdg_runtime || !hyprland_instance) {
    DEBUG_MSG("Required environment variables not set");
    return 1;
  }

  char socket_path[256];
  snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
           xdg_runtime, hyprland_instance);

  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    DEBUG_MSG("socket creation failed");
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    DEBUG_MSG("socket connect failed");
    close(sock);
    return 1;
  }

  print_initial_workspace();

  char buffer[BUFFER_SIZE];
  char line[BUFFER_SIZE];
  size_t line_pos = 0;

  while (1) {
    ssize_t bytes = read(sock, buffer, BUFFER_SIZE - 1);
    if (bytes <= 0) {
      if (bytes == 0) {
        DEBUG_MSG("Socket closed");
      } else {
        DEBUG_MSG("socket read failed");
      }
      break;
    }
    buffer[bytes] = '\0';

    for (ssize_t i = 0; i < bytes; i++) {
      if (buffer[i] == '\n') {
        line[line_pos] = '\0';
        DEBUG_MSG("Raw event: %s\n", line);

        if (strncmp(line, "workspace>>", 11) == 0) {
          char *id_str = line + 11;
          char *comma = strchr(id_str, ',');
          if (comma)
            *comma = '\0';  // Truncate at comma if present
          printf("%s\n", id_str);
          fflush(stdout);
        } else if (strncmp(line, "focusedmon>>", 12) == 0) {
          char *fields = line + 12;
          char *comma1 = strchr(fields, ',');
          if (comma1) {
            *comma1 = '\0';  // Split at first comma
            char *workspace_id = comma1 + 1;
            char *comma2 = strchr(workspace_id, ',');
            if (comma2)
              *comma2 = '\0';  // Handle extra commas
            printf("%s\n", workspace_id);
            fflush(stdout);
          }
        }
        // Reset line_pos and clear line for the next event
        line_pos = 0;
        memset(line, 0, BUFFER_SIZE);  // Clear residual data
      } else if (line_pos < BUFFER_SIZE - 1) {
        line[line_pos++] = buffer[i];
      }
    }
  }

  close(sock);
  return 0;
}
