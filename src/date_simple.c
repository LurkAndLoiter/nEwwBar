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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define JSON_STR_LEN 128 // 83 bytes; set to 128 as buffer safety

#ifdef DEBUG
#define DEBUG_MSG(std, fmt, ...) do { fprintf(std ,fmt "\n", ##__VA_ARGS__); } while (0)
#else
#define DEBUG_MSG(fmt, ...)
#endif

static const char *days_of_week[] = {"Sunday",    "Monday",   "Tuesday",
                                     "Wednesday", "Thursday", "Friday",
                                     "Saturday"};
static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

int main(void) {
  DEBUG_MSG("DEBUG enabled.");
  struct tm prev_time = {0};

  while (1) {
    time_t rawtime;
    if (time(&rawtime) == (time_t)-1) {
      DEBUG_MSG(stderr, "ERROR: Failed to get system time");
      sleep(1);
      continue;
    }

    struct tm *local_time = localtime(&rawtime);
    if (local_time == NULL) {
      DEBUG_MSG(stderr, "ERROR: Failed to convert to local time");
      sleep(1);
      continue;
    }

    struct tm curr_time = *local_time;

    // Calculate sleep time to next minute boundary
    int sleep_seconds = 60 - curr_time.tm_sec;
    if (sleep_seconds <= 0) {
      sleep_seconds = 1;
    }

    // Output JSON if time has changed (or on first valid iteration)
    if (curr_time.tm_min != prev_time.tm_min ||
        prev_time.tm_year == 0 ||
        curr_time.tm_hour != prev_time.tm_hour ||
        curr_time.tm_mday != prev_time.tm_mday ||
        curr_time.tm_mon != prev_time.tm_mon ||
        curr_time.tm_year != prev_time.tm_year ||
        curr_time.tm_wday != prev_time.tm_wday) {

      char json_str[JSON_STR_LEN];
      int result =
          snprintf(json_str, sizeof(json_str),
                   "{\"DayOfWeek\": \"%s\", "
                   "\"Month\": \"%s\", "
                   "\"Day\": \"%02d\", "
                   "\"Year\": \"%d\", "
                   "\"H\": \"%02d\", "
                   "\"M\": \"%02d\"}\n",
                   days_of_week[curr_time.tm_wday], months[curr_time.tm_mon],
                   curr_time.tm_mday, curr_time.tm_year + 1900,
                   curr_time.tm_hour, curr_time.tm_min);

      if (result < 0 || result >= (int)sizeof(json_str)) {
        DEBUG_MSG(stderr, "ERROR: Failed to create JSON string");
        sleep(1);
        continue;
      }

      printf("%s", json_str);
      fflush(stdout);
      prev_time = curr_time;
    }

    // Sleep until the next minute boundary
    DEBUG_MSG(stdout, "DEBUG: Sleeping for %d seconds", sleep_seconds);
    sleep(sleep_seconds);
  }

  return EXIT_SUCCESS;
}
