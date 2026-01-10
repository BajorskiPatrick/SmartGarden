#include "alert_limiter.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#ifndef ALERT_LIMITER_MAX_KEYS
#define ALERT_LIMITER_MAX_KEYS 48
#endif

#ifndef ALERT_LIMITER_KEY_MAX
#define ALERT_LIMITER_KEY_MAX 64
#endif

typedef struct {
    bool in_use;
    char key[ALERT_LIMITER_KEY_MAX];
    uint32_t last_emit_ms;
    uint32_t suppressed;
    bool once_emitted;
} alert_limiter_entry_t;

static alert_limiter_entry_t s_entries[ALERT_LIMITER_MAX_KEYS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static alert_limiter_entry_t *find_or_alloc(const char *key) {
    if (!key || key[0] == '\0') return NULL;

    // 1) Find existing
    for (int i = 0; i < ALERT_LIMITER_MAX_KEYS; i++) {
        if (s_entries[i].in_use && strncmp(s_entries[i].key, key, ALERT_LIMITER_KEY_MAX) == 0) {
            return &s_entries[i];
        }
    }

    // 2) Allocate new
    for (int i = 0; i < ALERT_LIMITER_MAX_KEYS; i++) {
        if (!s_entries[i].in_use) {
            s_entries[i].in_use = true;
            s_entries[i].key[0] = '\0';
            strlcpy(s_entries[i].key, key, sizeof(s_entries[i].key));
            s_entries[i].last_emit_ms = 0;
            s_entries[i].suppressed = 0;
            s_entries[i].once_emitted = false;
            return &s_entries[i];
        }
    }

    return NULL;
}

bool alert_limiter_allow(const char *key, uint32_t now_ms, uint32_t cooldown_ms, uint32_t *suppressed_out) {
    bool allow = false;
    uint32_t suppressed = 0;

    portENTER_CRITICAL(&s_mux);

    alert_limiter_entry_t *e = find_or_alloc(key);
    if (!e) {
        portEXIT_CRITICAL(&s_mux);
        return true; // fail-open
    }

    // Handle wraparound-safe time comparison.
    uint32_t elapsed = now_ms - e->last_emit_ms;

    if (e->last_emit_ms == 0 || elapsed >= cooldown_ms) {
        allow = true;
        suppressed = e->suppressed;
        e->suppressed = 0;
        e->last_emit_ms = now_ms;
    } else {
        e->suppressed++;
    }

    portEXIT_CRITICAL(&s_mux);

    if (allow && suppressed_out) {
        *suppressed_out = suppressed;
    }

    return allow;
}

bool alert_limiter_once(const char *key) {
    bool allow = false;

    portENTER_CRITICAL(&s_mux);

    alert_limiter_entry_t *e = find_or_alloc(key);
    if (!e) {
        portEXIT_CRITICAL(&s_mux);
        return true; // fail-open
    }

    if (!e->once_emitted) {
        e->once_emitted = true;
        allow = true;
    }

    portEXIT_CRITICAL(&s_mux);
    return allow;
}
