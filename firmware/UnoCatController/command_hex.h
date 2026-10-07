#pragma once
#include <stdint.h>
inline int8_t commandHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c |= 32;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
// Exactly two hex digits, with the same upper/lower-case acceptance as before.
inline bool commandCrcByte(const char *s, uint8_t &value) {
  if (!s[0] || !s[1] || s[2]) return false;
  int8_t high = commandHexDigit(s[0]), low = commandHexDigit(s[1]);
  if (high < 0 || low < 0) return false;
  value = (high << 4) | low;
  return true;
}
