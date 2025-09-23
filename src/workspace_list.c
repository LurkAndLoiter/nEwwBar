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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_WORKSPACES 6
#define BUFFER_SIZE 64 

#ifdef DEBUG
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
    printf(fmt "\n", ##__VA_ARGS__);                                           \
  } while (0)
#else
#define DEBUG_MSG(fmt, ...)                                                    \
  do {                                                                         \
  } while (0)
#endif

typedef struct {
  int WorkspaceID;
  bool hasWindows;
  bool prevhasWindows;
} Workspace;

Workspace workspaces[MAX_WORKSPACES] = {0};

void print_json() {
  printf("[");
  for (int i = 0; i < MAX_WORKSPACES; i++) {
    if (i) printf(",");
    printf("{\"WorkspaceID\": %i,", workspaces[i].WorkspaceID);
    printf("\"hasWindows\": %s", (workspaces[i].hasWindows ? "true" : "false"));
    printf("}");
  }
  printf("]\n");
  fflush(stdout);
}

static void update_workspaces(void) {
  // Execute hyprctl command and capture output
  FILE *fp = popen("hyprctl workspaces -j | jq '.[] | .id, .windows'", "r");
  if (!fp) {
    DEBUG_MSG("popen failed");
    return;
  }

  for (int i = 0; i < MAX_WORKSPACES; i++){
    workspaces[i].prevhasWindows = workspaces[i].hasWindows;
    workspaces[i].hasWindows = false;
  }

  char line[16];
  int id;
  while (fgets(line, 16, fp)) {
    id = atoi(line);
    DEBUG_MSG("%s:%i\n", line, id);
    if (fgets(line, 16, fp)) {
      int windows = atoi(line);
      DEBUG_MSG("%s:%i\n", line, windows);
      if (id >= 1 && id <= MAX_WORKSPACES) {
        workspaces[id - 1].hasWindows = (windows > 0);
      }
    }
  }

  pclose(fp);

  bool outputFlag = false;
  for (int i = 0; i < MAX_WORKSPACES; i++) {
    DEBUG_MSG("ID: %i, hasWindows: %i\n", workspaces[i].WorkspaceID, 
        workspaces[i].hasWindows);
    if (workspaces[i].hasWindows != workspaces[i].prevhasWindows) {
        outputFlag = true;
    }
  }

  if (outputFlag) { print_json(); }
}


int main() {
  DEBUG_MSG("DEBUG enabled.");
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

  for (int i = 0; i < MAX_WORKSPACES; i++) {
    workspaces[i].WorkspaceID = i + 1;
    workspaces[i].hasWindows = false;
    workspaces[i].prevhasWindows = false;
  }

  update_workspaces(); // initialize

  char buffer[BUFFER_SIZE];
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
    if (strncmp(buffer, "closewindow>", 12) == 0 ||
               strncmp(buffer, "openwindow>", 11) == 0 ||
               strncmp(buffer, "movewindow>", 11) == 0) {
      DEBUG_MSG("CAUGHT:windows %s\n", buffer);
      update_workspaces();
    }
  }

  close(sock);
  return 0;
}
