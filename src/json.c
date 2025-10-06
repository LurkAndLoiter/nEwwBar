#include "../include/json.h"

#include <stdio.h>

// --- Utility: JSON string escaping ---
void print_json_str(const char *str) {
  putchar('"');
  for (const unsigned char *c = (unsigned char *)str; *c; ++c) {
    switch (*c) {
    case '\"':
      putchar('\\');
      putchar('\"');
      break;
    case '\\':
      putchar('\\');
      putchar('\\');
      break;
    case '\b':
      putchar('\\');
      putchar('b');
      break;
    case '\f':
      putchar('\\');
      putchar('f');
      break;
    case '\n':
      putchar('\\');
      putchar('n');
      break;
    case '\r':
      putchar('\\');
      putchar('r');
      break;
    case '\t':
      putchar('\\');
      putchar('t');
      break;
    case '/':
      putchar('\\');
      putchar('/');
      break;
    default:
      if (*c < 0x20) // control characters
        printf("\\u%04x", *c);
      else
        putchar(*c);
    }
  }
  putchar('"');
}
