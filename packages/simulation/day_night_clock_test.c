/* day_night_clock_test.c -- real tests for day_night_clock.c (BIG_O engine merge, phase 1).
 * Plain assert() harness, same convention as humanness_test.c/cutscene_effect_mod_test.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/dnc_test packages/simulation/day_night_clock_test.c \
 *       packages/simulation/day_night_clock.c packages/simulation/world_rules.c -lm && /tmp/dnc_test
 */
#include "day_night_clock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* init: minute-of-day matches start_hour, phase matches the real DAWN/DAY/DUSK/NIGHT bands
     * from world_rules.prn (05-07 DAWN, 07-19 DAY, 19-21 DUSK, else NIGHT). */
    {
        DayNightClock c;
        day_night_clock_init(&c, 42, 6);
        assert(day_night_clock_minute_of_day(&c) == 360);
        assert(day_night_clock_phase(&c) == DNC_DAWN);
        assert(day_night_clock_day(&c) == 1);
        printf("PASS: init at hour 6 -> minute 360, phase DAWN, day 1\n");
    }

    /* tick across every phase boundary in order over a full day, starting at midnight. */
    {
        DayNightClock c;
        day_night_clock_init(&c, 7, 0);
        DncPhase seen[4] = { DNC_NIGHT, DNC_NIGHT, DNC_NIGHT, DNC_NIGHT };
        int idx = 0;
        DncPhase last = day_night_clock_phase(&c);
        for (int m = 0; m < 1440; m++) {
            day_night_clock_tick(&c, 1);
            DncPhase p = day_night_clock_phase(&c);
            if (p != last && idx < 4) { seen[idx++] = p; last = p; }
        }
        assert(idx == 4);
        /* starting at midnight (NIGHT), the real band order is DAWN(300) DAY(420) DUSK(1140) NIGHT(1260) */
        assert(seen[0] == DNC_DAWN);
        assert(seen[1] == DNC_DAY);
        assert(seen[2] == DNC_DUSK);
        assert(seen[3] == DNC_NIGHT);
        printf("PASS: a full day ticks through all four phases in order\n");
    }

    /* day rolls over after 1440 minutes. */
    {
        DayNightClock c;
        day_night_clock_init(&c, 3, 0);
        assert(day_night_clock_day(&c) == 1);
        day_night_clock_tick(&c, 1440);
        assert(day_night_clock_day(&c) == 2);
        assert(day_night_clock_minute_of_day(&c) == 0);
        printf("PASS: day increments after 1440 minutes, minute-of-day wraps\n");
    }

    /* weather eventually transitions on its own (bounded duration per world_rules.prn) and cycles
     * CLEAR -> OVERCAST -> RAIN -> STORM -> CLEAR. */
    {
        DayNightClock c;
        day_night_clock_init(&c, 99, 0);
        assert(c.weather == DNC_CLEAR);
        int transitions = 0;
        DncWeather last = c.weather;
        for (int m = 0; m < 3000 && transitions < 5; m++) {
            day_night_clock_tick(&c, 1);
            if (c.weather != last) {
                /* real cycle order, never skips a step */
                DncWeather expect = (DncWeather)((last + 1) % 4);
                assert(c.weather == expect);
                last = c.weather;
                transitions++;
            }
        }
        assert(transitions >= 4); /* real, bounded durations (20-240 min) guarantee several transitions in 3000 minutes */
        printf("PASS: weather cycles CLEAR->OVERCAST->RAIN->STORM->CLEAR, never skipping a step\n");
    }

    /* force_weather: immediate, reschedules a real duration (not instantly re-triggering next tick). */
    {
        DayNightClock c;
        day_night_clock_init(&c, 5, 0);
        day_night_clock_force_weather(&c, DNC_STORM);
        assert(c.weather == DNC_STORM);
        assert(c.weather_ends > c.minutes);
        printf("PASS: force_weather sets weather immediately with a real forward-scheduled expiry\n");
    }

    /* name helpers never return null / handle real enum range. */
    {
        assert(strcmp(day_night_clock_phase_name(DNC_DUSK), "DUSK") == 0);
        assert(strcmp(day_night_clock_weather_name(DNC_STORM), "STORM") == 0);
        printf("PASS: phase/weather name helpers\n");
    }

    printf("ALL PASS\n");
    return 0;
}
