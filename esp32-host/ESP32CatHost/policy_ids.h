#pragma once
#include <stdint.h>

// Zero is the protocol's unidentified-command sentinel, never a pending ID.
constexpr bool matchesPending(uint32_t reply, uint32_t pending) {
  return reply != 0 && pending != 0 && reply == pending;
}
static_assert(!matchesPending(0, 0), "unidentified error must not match idle motor");
static_assert(!matchesPending(0, 42), "unidentified error must not match active motor");
static_assert(!matchesPending(42, 0), "completed request must not match idle slot");
static_assert(!matchesPending(41, 42), "different request must not match");
static_assert(matchesPending(42, 42), "identified pending request must match");
static_assert(matchesPending(65535, 65535), "full Uno ID range must match");
