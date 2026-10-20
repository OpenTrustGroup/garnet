// SPDX-License-Identifier: BSD-3-Clause
#include "supervision.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    wdt_supervision_t state = {.sos_timeout = 30};
    // Sink has no SOS obligation, irrespective of how long the host runs.
    assert(!wdt_supervision_check(&state, 100000, false));
    wdt_supervision_resume(&state, 100);
    assert(!state.sos_armed);
    assert(wdt_supervision_kick(&state, 100));
    assert(!wdt_supervision_check(&state, 130, false));
    assert(!wdt_supervision_check(&state, 90, false));
    assert(!wdt_supervision_check(&state, 200, true));
    wdt_supervision_resume(&state, 200);
    assert(!wdt_supervision_check(&state, 230, false));
    assert(wdt_supervision_check(&state, 231, false));
    // Only the first failure requests SMC. Late kick/resume never recover it.
    assert(state.reset_latched);
    assert(!wdt_supervision_check(&state, 240, false));
    assert(!wdt_supervision_kick(&state, 250));
    wdt_supervision_resume(&state, 260);
    assert(state.reset_latched && state.sos_last_kick == 200);
    assert(!wdt_supervision_fail(&state));

    wdt_supervision_t gm_failure = {.sos_timeout = 30};
    assert(wdt_supervision_fail(&gm_failure));
    assert(!wdt_supervision_kick(&gm_failure, 1));
    wdt_supervision_resume(&gm_failure, 1);
    assert(gm_failure.reset_latched && !gm_failure.sos_armed);
    // Large guest intervals do not require changing the hardware deadline.
    wdt_supervision_t slow_sos = {.sos_timeout = 62};
    assert(wdt_supervision_kick(&slow_sos, 10));
    assert(!wdt_supervision_check(&slow_sos, 71, false));
    assert(wdt_supervision_check(&slow_sos, 73, false));
    puts("platform watchdog supervision: PASS");
    return 0;
}
