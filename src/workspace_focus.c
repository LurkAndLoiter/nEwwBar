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

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

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

static void initialRun(void) {
  FILE *fp = popen(
      "hyprctl monitors -j | jq '.[] | select(.focused == true) | .id'", "r");
  if (!fp) {
    DEBUG_MSG("popen failed");
    return;
  }
  char line[16];
  if (fgets(line, sizeof(line), fp)) {
    int id = atoi(line);
    printf("%i\n", id);
  }
  pclose(fp);
}

int main(void) {
  DEBUG_MSG("DEBUG enabled.");
  const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  const char *hyprland_instance = getenv("HYPRLAND_INSTANCE_SIGNATURE");
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

  size_t addr_len =
      offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
  if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
    DEBUG_MSG("socket connect failed");
    close(sock);
    return 1;
  }

  initialRun();

  while (1) {
    char buffer[BUFFER_SIZE];
    size_t buffer_len = 0;

    if (buffer_len >= BUFFER_SIZE - 1) {
      DEBUG_MSG("Buffer full, discarding old data");
      buffer_len = 0;
    }

    ssize_t bytes =
        read(sock, buffer + buffer_len, BUFFER_SIZE - buffer_len - 1);
    if (bytes <= 0) {
      if (bytes == 0) {
        DEBUG_MSG("Socket closed");
      }
      else
        DEBUG_MSG("socket read failed.");
      break;
    }
    buffer_len += bytes;
    buffer[buffer_len] = '\0';

    char *line = buffer;
    char *next_line;
    while ((next_line = strchr(line, '\n'))) {
      *next_line = '\0';
      if (strncmp(line, "workspace>>", 10) == 0 ||
          strncmp(line, "focusedmon>>", 11) == 0) {
        char *ptr = strrchr(line, ',');
        if (!ptr) {
          ptr = strrchr(line, '>');
        }
        if (ptr) {
          printf("%s\n", ptr + 1);
          fflush(stdout);
        }
      }
      line = next_line + 1;
    }

    buffer_len = strlen(line);
    if (buffer_len > 0) {
      memmove(buffer, line, buffer_len + 1);
    } else {
      buffer_len = 0;
    }
  }

  close(sock);
  return 0;
}
