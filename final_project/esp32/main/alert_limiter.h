#ifndef ALERT_LIMITER_H
#define ALERT_LIMITER_H

#include <stdbool.h>
#include <stdint.h>

// Small, dependency-light alert rate limiter.
//
// Key concepts:
// - cooldown: allow at most once per cooldown window per key
// - suppression count: counts how many times the same key was blocked by cooldown
// - once-per-boot: allow only the first time per key after reset
//
// Thread-safety: functions are safe to call from multiple tasks/handlers.

#ifdef __cplusplus
extern "C" {
#endif

// Returns true if the alert identified by `key` should be emitted now.
// If it returns true and `suppressed_out` is non-NULL, it will contain the number of suppressed
// occurrences since the last emit for this key, and will be reset to 0.
bool alert_limiter_allow(const char *key, uint32_t now_ms, uint32_t cooldown_ms, uint32_t *suppressed_out);

// Returns true only on the first call per key after boot.
bool alert_limiter_once(const char *key);

#ifdef __cplusplus
}
#endif

#endif // ALERT_LIMITER_H
