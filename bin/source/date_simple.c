#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define JSON_STR_LEN 256
#define INITIAL_TIME_CHECK_INTERVAL 1  // Initial check interval (seconds)
#define UPDATED_TIME_CHECK_INTERVAL 60 // Check interval after first change (seconds)

// Define DEBUG only if not defined via command line
#ifndef DEBUG
#define DEBUG 0 // Default to 0 (debug disabled) if not specified
#endif

// Arrays for day of week and month names
static const char *days_of_week[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Structure to hold time values for comparison
typedef struct {
    int wday; // Day of week (0-6)
    int mon;  // Month (0-11)
    int day;  // Day of month (1-31)
    int year; // Year (e.g., 2025)
    int hour; // Hour (0-23)
    int min;  // Minute (0-59)
} TimeValues;

int main(void) {
    TimeValues prev_time = {0}; // Initialize to zero (will be set on first run)
    int time_check_interval = INITIAL_TIME_CHECK_INTERVAL; // Start with 1-second checks
    int is_first_json_output = 1; // Flag to output first JSON

    while (1) {
        // Get current time
        time_t rawtime;
        if (time(&rawtime) == (time_t)-1) {
            fprintf(stderr, "ERROR: Failed to get system time\n");
            sleep(time_check_interval);
            continue;
        }

        // Convert to local time
        struct tm *local_time = localtime(&rawtime);
        if (local_time == NULL) {
            fprintf(stderr, "ERROR: Failed to convert to local time\n");
            sleep(time_check_interval);
            continue;
        }

        // Extract current time values
        TimeValues curr_time = {
            local_time->tm_wday,
            local_time->tm_mon,
            local_time->tm_mday,
            local_time->tm_year + 1900,
            local_time->tm_hour,
            local_time->tm_min
        };

        // Debug message for polling interval
        if (DEBUG) {
            fprintf(stderr, "DEBUG: Polling Interval: %d\n", time_check_interval);
        }

        // Output first JSON immediately
        if (is_first_json_output) {
            char json_str[JSON_STR_LEN];
            int result = snprintf(json_str, sizeof(json_str),
                                 "{\"DayOfWeek\": \"%s\", "
                                  "\"Month\": \"%s\", "
                                  "\"Day\": \"%02d\", "
                                  "\"Year\": \"%d\", "
                                  "\"H\": \"%02d\", "
                                  "\"M\": \"%02d\"}\n",
                                 days_of_week[curr_time.wday],
                                 months[curr_time.mon],
                                 curr_time.day,
                                 curr_time.year,
                                 curr_time.hour,
                                 curr_time.min);

            if (result < 0 || result >= (int)sizeof(json_str)) {
                fprintf(stderr, "ERROR: Failed to create JSON string\n");
                sleep(time_check_interval);
                continue;
            }

            printf("%s", json_str);
            fflush(stdout); // Ensure immediate output
            prev_time = curr_time; // Set prev_time for subsequent comparisons
            is_first_json_output = 0;
            sleep(time_check_interval);
            continue;
        }

        // Check if any time value has changed
        if (curr_time.wday != prev_time.wday ||
            curr_time.mon != prev_time.mon ||
            curr_time.day != prev_time.day ||
            curr_time.year != prev_time.year ||
            curr_time.hour != prev_time.hour ||
            curr_time.min != prev_time.min) {

            // Update previous time
            prev_time = curr_time;

            // Create JSON string with zero-padded integers
            char json_str[JSON_STR_LEN];
            int result = snprintf(json_str, sizeof(json_str),
                                 "{\"DayOfWeek\": \"%s\", "
                                  "\"Month\": \"%s\", "
                                  "\"Day\": \"%02d\", "
                                  "\"Year\": \"%d\", "
                                  "\"H\": \"%02d\", "
                                  "\"M\": \"%02d\"}\n",
                                 days_of_week[curr_time.wday],
                                 months[curr_time.mon],
                                 curr_time.day,
                                 curr_time.year,
                                 curr_time.hour,
                                 curr_time.min);

            if (result < 0 || result >= (int)sizeof(json_str)) {
                fprintf(stderr, "ERROR: Failed to create JSON string\n");
                sleep(time_check_interval);
                continue;
            }

            // Output JSON string to stdout
            printf("%s", json_str);
            fflush(stdout); // Ensure immediate output

            // Switch to 60-second interval after the next change
            time_check_interval = UPDATED_TIME_CHECK_INTERVAL;
        }

        // Sleep to avoid excessive CPU usage
        sleep(time_check_interval);
    }

    return EXIT_SUCCESS; // Unreachable, but included for completeness
}
