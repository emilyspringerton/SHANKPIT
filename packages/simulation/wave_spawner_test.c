/* wave_spawner_test.c -- `make test-wave-spawner`. The wave timer, with no server and no display.
 *
 * Two jobs. First, parity: the C defaults in wave_rules.h must equal the numbers PARENA emits into
 * wave_rules.c, sample for sample -- that is the whole reason the header may carry defaults at all.
 * Second, the properties a host gets wrong: a capped world must not burn its wave, a clock that goes
 * backwards must not burst, and the session must end after its last wave instead of running forever.
 *
 * Build/run: make test-wave-spawner
 */
#include "wave_spawner.h"
#include "wave_rules_host.h"

#include <stdarg.h>
#include <stdio.h>

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", buf);
    } else {
        printf("ok    %s\n", buf);
    }
}

int main(void) {
    /* ---- parity: header defaults vs the generated PARENA copy ---- */
    ok(g_wr_rules.first_delay_ms() == wave_first_delay_ms(), "first delay: defaults vs PARENA");
    ok(g_wr_rules.interval_ms() == wave_interval_ms(), "interval: defaults vs PARENA");
    ok(g_wr_rules.count() == wave_count(), "wave count: defaults vs PARENA");
    ok(g_wr_rules.size() == wave_size(), "wave size: defaults vs PARENA");
    ok(g_wr_rules.live_cap() == wave_live_cap(), "live cap: defaults vs PARENA");
    ok(g_wr_rules.spawn_radius_milli() == wave_spawn_radius_milli(), "spawn radius: defaults vs PARENA");
    ok(g_wr_rules.archetype() == wave_archetype(), "archetype: defaults vs PARENA");
    ok(g_wr_rules.should_spawn(0, 0, 0, 0) == wave_should_spawn(0, 0, 0, 0),
       "should_spawn(not due): defaults vs PARENA");
    ok(g_wr_rules.should_spawn(0, 0, 100, 50) == wave_should_spawn(0, 0, 100, 50),
       "should_spawn(due): defaults vs PARENA");
    ok(g_wr_rules.should_spawn(0, g_wr_rules.live_cap(), 100, 50) ==
           wave_should_spawn(0, wave_live_cap(), 100, 50),
       "should_spawn(capped): defaults vs PARENA");
    ok(g_wr_rules.should_spawn(g_wr_rules.count(), 0, 100, 50) ==
           wave_should_spawn(wave_count(), 0, 100, 50),
       "should_spawn(past the last wave): defaults vs PARENA");
    for (int w = -1; w <= 12; w++) {
        g_checks += 2;
        if (g_wr_rules.size_for(w) != wave_size_for(w)) {
            g_failures++;
            printf("FAIL  size_for(%d): defaults %d vs PARENA %d\n", w, g_wr_rules.size_for(w),
                   wave_size_for(w));
        }
        if (g_wr_rules.interval_for(w) != wave_interval_for(w)) {
            g_failures++;
            printf("FAIL  interval_for(%d): defaults %d vs PARENA %d\n", w, g_wr_rules.interval_for(w),
                   wave_interval_for(w));
        }
    }
    ok(g_wr_rules.size_for(0) == g_wr_rules.size(), "the first wave is the base size");
    ok(g_wr_rules.size_for(30) <= 8, "size is capped at 8 (got %d)", g_wr_rules.size_for(30));
    ok(g_wr_rules.interval_for(0) == g_wr_rules.interval_ms(), "the first interval is the base interval");
    ok(g_wr_rules.interval_for(20) == 15000, "the cadence floors at 15 s (got %d)", g_wr_rules.interval_for(20));

    /* ---- the timer ---- */
    WaveRunner r = {0};
    wave_runner_reset(&r, 0);
    ok(r.enabled == 0, "a fresh runner is disarmed -- no level spawns waves by accident");
    ok(wave_runner_tick(&r, 100000, 0) == 0, "a disarmed runner emitted a wave");

    r.enabled = 1;
    wave_runner_reset(&r, 0);
    int first_due = r.due_ms;
    ok(first_due == g_wr_rules.first_delay_ms(), "first wave due at the first delay (got %d)", first_due);
    ok(wave_runner_tick(&r, first_due - 1, 0) == 0, "a wave arrived early");
    int size0 = wave_runner_tick(&r, first_due, 0);
    ok(size0 == g_wr_rules.size_for(0), "first wave size %d, want %d", size0, g_wr_rules.size_for(0));
    ok(r.wave == 1, "the wave counter did not advance (got %d)", r.wave);
    ok(r.due_ms == first_due + g_wr_rules.interval_for(1),
       "next wave due at %d, want %d", r.due_ms, first_due + g_wr_rules.interval_for(1));
    ok(r.spawned == size0, "spawned tally %d, want %d", r.spawned, size0);

    /* ---- THE property: a capped world leaves the wave due instead of eating it ---- */
    {
        WaveRunner c = {0};
        c.enabled = 1;
        wave_runner_reset(&c, 0);
        int due = c.due_ms;
        ok(wave_runner_tick(&c, due + 60000, g_wr_rules.live_cap()) == 0, "spawned a wave at the live cap");
        ok(c.wave == 0, "a capped world advanced the wave number (it ate the wave): %d", c.wave);
        ok(c.due_ms == due, "a capped world moved the schedule");
        ok(wave_runner_tick(&c, due + 60000, g_wr_rules.live_cap() - 1) > 0,
           "room freed up and the held wave still did not arrive");
        ok(c.wave == 1, "the held wave did not start where it left off (got %d)", c.wave);
    }

    /* ---- the clock going backwards is a no-op, not a burst ---- */
    {
        WaveRunner b = {0};
        b.enabled = 1;
        wave_runner_reset(&b, 500000);
        ok(wave_runner_tick(&b, 1000, 0) == 0, "a backwards clock emitted a wave");
        ok(b.wave == 0, "a backwards clock advanced the wave number");
        ok(wave_runner_tick(&b, 500000 + g_wr_rules.first_delay_ms(), 0) > 0,
           "the wave never arrived after the clock recovered");
    }

    /* ---- a whole session: exactly wave_count waves, then silence ---- */
    {
        WaveRunner s = {0};
        s.enabled = 1;
        wave_runner_reset(&s, 0);
        int t = 0, emitted = 0, waves = 0;
        for (int step = 0; step < 100000; step++) {
            t += 1000;
            int n = wave_runner_tick(&s, t, 0);
            if (n > 0) {
                waves++;
                emitted += n;
            }
        }
        ok(waves == g_wr_rules.count(), "a 100 000 s session ran %d waves, want %d", waves, g_wr_rules.count());
        ok(emitted > 0, "a session emitted nothing");
        ok(wave_runner_remaining(&s) == 0, "waves still remaining after the last one");
        ok(wave_runner_tick(&s, t + 999999, 0) == 0, "waves continued past the last one");
        ok(g_wr_rules.interval_for(g_wr_rules.count() - 1) < g_wr_rules.interval_for(0),
           "the cadence does not tighten over the session");
    }

    if (g_failures) {
        printf("wave_spawner: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("wave_spawner: %d checks, all passed\n", g_checks);
    return 0;
}
