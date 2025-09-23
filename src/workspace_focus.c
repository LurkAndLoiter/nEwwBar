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

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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


static void initialRun(void) {
  FILE *fp = popen("hyprctl monitors -j | jq '.[] | select(.focused == true) | .id'", "r");
  if (!fp) {
    DEBUG_MSG("popen failed");
    return;
  }
  char line[16];
  if (fgets(line, sizeof(line), fp)) {
    int id = atoi(line);
    printf("%i\n",id);
  }
  pclose(fp);
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

  size_t addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
  if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
    DEBUG_MSG("socket connect failed");
    close(sock);
    return 1;
  }

  initialRun();

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
    if (strncmp(buffer, "workspace>", 10) == 0 ||
        strncmp(buffer, "focusedmon>", 11) == 0) {
      /* Get last set of digits(workspaceID) in buffer */
      char *ptr = buffer;
      char *last_digits = NULL;
      while (*ptr) {
        if (isdigit(*ptr)) {
          last_digits = ptr;
          while (isdigit(*ptr))
            ptr++;
        } else {
          ptr++;
        }
      }
      if (last_digits) {
        char *end = last_digits;
        while (isdigit(*end)) end++;
        fwrite(last_digits, 1, end - last_digits, stdout);
        fputc('\n', stdout);
        fflush(stdout);
      } else {
        DEBUG_MSG("No digits found\n");
      }
    }
  }

  close(sock);
  return 0;
}
