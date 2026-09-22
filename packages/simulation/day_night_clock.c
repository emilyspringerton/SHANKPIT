#include "day_night_clock.h"

#include <string.h>

/* Prototypes for the PARENA-generated decision functions (world_rules.c, do-not-edit-by-hand --
 * see that file's own header). No shared .h between generated units in this codebase
 * (packages/simulation/cutscene_effect_mod.c sets the precedent: the generated .c is the whole
 * contract, callers just redeclare the prototypes they need). */
int phase_for_minute(int minute);
int weather_next(int w);
int weather_min_minutes(int w);
int weather_max_minutes(int w);
int weather_duration(int w, int roll);

static int dnc_roll(DayNightClock *c) {
    c->rng ^= c->rng << 13;
    c->rng ^= c->rng >> 17;
    c->rng ^= c->rng << 5;
    return (int)(c->rng % 100u);
}

static void dnc_schedule_weather(DayNightClock *c) {
    c->weather_ends = c->minutes + weather_duration((int)c->weather, dnc_roll(c));
}

void day_night_clock_init(DayNightClock *c, unsigned int seed, int start_hour) {
    memset(c, 0, sizeof(*c));
    c->rng = seed ? seed * 2654435761u | 1u : 0x9E3779B9u;
    c->start_minute = ((start_hour % 24) + 24) % 24 * 60;
    c->weather = DNC_CLEAR;
    dnc_schedule_weather(c);
}

int day_night_clock_minute_of_day(const DayNightClock *c) {
    return (c->start_minute + c->minutes) % 1440;
}

int day_night_clock_day(const DayNightClock *c) {
    return 1 + (c->start_minute + c->minutes) / 1440;
}

DncPhase day_night_clock_phase(const DayNightClock *c) {
    return (DncPhase)phase_for_minute(day_night_clock_minute_of_day(c));
}

void day_night_clock_force_weather(DayNightClock *c, DncWeather w) {
    c->weather = w;
    dnc_schedule_weather(c);
}

void day_night_clock_tick(DayNightClock *c, int minutes) {
    for (int m = 0; m < minutes; m++) {
        c->minutes++;
        if (c->minutes >= c->weather_ends) {
            c->weather = (DncWeather)weather_next((int)c->weather);
            dnc_schedule_weather(c);
        }
    }
}

const char *day_night_clock_phase_name(DncPhase p) {
    static const char *const names[4] = { "DAWN", "DAY", "DUSK", "NIGHT" };
    return (p >= 0 && p < 4) ? names[p] : "?";
}

const char *day_night_clock_weather_name(DncWeather w) {
    static const char *const names[4] = { "CLEAR", "OVERCAST", "RAIN", "STORM" };
    return (w >= 0 && w < 4) ? names[w] : "?";
}
