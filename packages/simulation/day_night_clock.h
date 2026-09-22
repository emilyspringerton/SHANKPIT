#ifndef DAY_NIGHT_CLOCK_H
#define DAY_NIGHT_CLOCK_H

/* day_night_clock.h -- SHANKPIT's own day/night + weather clock, ported in from BIG_O
 * ("A SHANKPIT Story") as part of the real engine merge: BIG_O's systems become first-class
 * SHANKPIT tech, not a separate app. Every phase/weather transition is a call into PARENA-
 * generated rules (world_rules.c, from PARENA/stdlib/shankpit/world_rules.prn -- copied over from
 * PARENA/stdlib/big_o/world_rules.prn verbatim, the logic is identical) -- this file only holds
 * state, rolls dice, and ticks. See BIG_O/core/world.h for the original.
 *
 * Deliberate v0 scope cut vs. BIG_O's own World: no zombie population, no per-player area
 * assignment/harvest, no REFLUX alert wiring. Those are gameplay systems that arrive with the
 * witness/attention rules port (named, deferred, not this pass) -- this is just the clock and
 * weather that the sky/lighting renderer needs, standing alone so it has no dependency on a
 * player/Sim model that doesn't exist in SHANKPIT's own server shape. */

typedef enum { DNC_DAWN = 0, DNC_DAY, DNC_DUSK, DNC_NIGHT } DncPhase;
typedef enum { DNC_CLEAR = 0, DNC_OVERCAST, DNC_RAIN, DNC_STORM } DncWeather;

typedef struct {
    unsigned int rng;
    int minutes;         /* elapsed sim minutes since start */
    int start_minute;    /* minute-of-day the clock began at */
    DncWeather weather;
    int weather_ends;     /* absolute minutes value the current weather expires at */
} DayNightClock;

/* seed: any nonzero value for a deterministic run; start_hour: 0..23, the wall-clock hour the
 * clock begins at (matches BIG_O's own world_init signature, minus the Sim* it no longer needs). */
void day_night_clock_init(DayNightClock *c, unsigned int seed, int start_hour);

/* Advance the clock by `minutes` sim-minutes, rolling weather transitions as scheduled. */
void day_night_clock_tick(DayNightClock *c, int minutes);

/* GM/test hook: force a weather change immediately (also reschedules its duration). */
void day_night_clock_force_weather(DayNightClock *c, DncWeather w);

int day_night_clock_minute_of_day(const DayNightClock *c);
int day_night_clock_day(const DayNightClock *c);
DncPhase day_night_clock_phase(const DayNightClock *c);

const char *day_night_clock_phase_name(DncPhase p);
const char *day_night_clock_weather_name(DncWeather w);

#endif
