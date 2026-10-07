#pragma once
#include <stdint.h>
inline bool commandLong(const char *text, int32_t low, int32_t high, int32_t &value) {
  if (!text || !*text) return false;
  while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
  const bool negative = *text == '-';
  if (*text == '-' || *text == '+') ++text;
  if (*text < '0' || *text > '9') return false;
  uint32_t number = 0;
  do {
    const uint8_t digit = *text++ - '0';
    if (number > 214748364UL || (number == 214748364UL && digit > (negative ? 8 : 7))) return false;
    number = number * 10 + digit;
  } while (*text >= '0' && *text <= '9');
  if (*text) return false;
  const int32_t parsed = negative ? int32_t(0UL - number) : int32_t(number);
  if (parsed < low || parsed > high) return false;
  value = parsed;
  return true;
}
