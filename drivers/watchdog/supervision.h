// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <stdbool.h>
#include <stdint.h>

// All access is serialized by the platform driver's lock. Time is ticks.
typedef struct wdt_supervision {
    bool sos_armed;
    bool reset_latched;
    uint64_t sos_last_kick;
    uint64_t sos_timeout;
} wdt_supervision_t;

static inline bool wdt_supervision_fail(wdt_supervision_t* state) {
    if (state->reset_latched) {
        return false;
    }
    state->reset_latched = true;
    return true;
}

static inline bool wdt_supervision_kick(wdt_supervision_t* state, uint64_t now) {
    if (state->reset_latched) {
        return false;
    }
    state->sos_armed = true;
    state->sos_last_kick = now;
    return true;
}

static inline bool wdt_supervision_check(wdt_supervision_t* state,
                                        uint64_t now, bool suspended) {
    if (state->reset_latched || suspended || !state->sos_armed ||
        now <= state->sos_last_kick ||
        now - state->sos_last_kick <= state->sos_timeout) {
        return false;
    }
    return wdt_supervision_fail(state);
}

static inline void wdt_supervision_resume(wdt_supervision_t* state, uint64_t now) {
    // Resume refreshes an existing obligation, it never arms one or clears
    // a failure. A late resume cannot cancel a reset already requested.
    if (!state->reset_latched && state->sos_armed) {
        state->sos_last_kick = now;
    }
}
