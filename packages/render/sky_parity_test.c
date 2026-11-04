/* sky_parity_test.c -- the shared sky fixture, asserted from the C side.
 *
 * PARENA/stdlib/shankpit/sky_rules.prn decides the clock, each weather's intensities, visibility through fog and
 * when lightning strikes. This reads PARENA/stdlib/shankpit/sky_parity.txt and asserts the generated C against it;
 * NOOBTOPIA's tests/sky_test.lua asserts the SAME file from the Luau side, which is the whole point of a shared
 * decision -- a drifting port goes red instead of two worlds disagreeing about what "storm" looks like.
 *
 * Columns: minutes weather tick | sun night visibility flash clock_hundredths
 *
 * Run: make test-sky-parity        (PARENA_DIR overrides where the fixture is read from)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sky_day_minutes(void);
int sky_wrap_minutes(int);
int sky_sun_permille(int);
int sky_night(int);
int sky_weather_count(void);
int sky_cover(int);
int sky_rain(int);
int sky_storm(int);
int sky_fog_wash(int);
int sky_base_visibility(int);
int sky_visibility(int, int);
int sky_flash_permille(int, int);
int sky_clock_hundredths(int);

static int g_checks = 0;
static int g_fails = 0;

static void expect(int got, int want, const char *what) {
    g_checks++;
    if (got != want) {
        g_fails++;
        printf("FAIL: %s (got %d want %d)\n", what, got, want);
    }
}

static void expect_true(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fails++; printf("FAIL: %s\n", what); }
}

int main(void) {
    static char path[512];
    const char *dir = getenv("PARENA_DIR");
    if (dir && *dir) snprintf(path, sizeof path, "%s/stdlib/shankpit/sky_parity.txt", dir);
    else snprintf(path, sizeof path, "../PARENA/stdlib/shankpit/sky_parity.txt");

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL: cannot open %s (set PARENA_DIR to the PARENA checkout)\n", path);
        return 1;
    }

    char line[512];
    int rows = 0;
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        int v[8];
        int n = 0;
        char *p = line;
        while (n < 8) {
            char *end = NULL;
            long got = strtol(p, &end, 10);
            if (end == p) break;
            v[n++] = (int)got;
            p = end;
        }
        if (n != 8) continue;
        rows++;
        int minutes = v[0], weather = v[1], tick = v[2];
        char tag[160];
        snprintf(tag, sizeof tag, "sky row %d (minutes %d weather %d tick %d)", rows, minutes, weather, tick);
        int sun = sky_sun_permille(minutes);
        expect(sun, v[3], tag);
        expect(sky_night(minutes), v[4], "night");
        expect(sky_visibility(weather, sun), v[5], "visibility");
        expect(sky_flash_permille(weather, tick), v[6], "flash");
        expect(sky_clock_hundredths(minutes), v[7], "clock hundredths");
    }
    fclose(f);
    expect_true(rows >= 20, "read at least 20 sky rows");

    /* The declared constants, so neither side can quietly renumber them. */
    expect(sky_day_minutes(), 1440, "a day is 1440 minutes");
    expect(sky_weather_count(), 5, "five weathers");
    expect(sky_wrap_minutes(-60), 1380, "a negative minute wraps backwards, not to zero");
    expect(sky_wrap_minutes(1500), 60, "and past midnight wraps forwards");

    if (g_fails) {
        printf("sky_parity: %d of %d FAILED\n", g_fails, g_checks);
        return 1;
    }
    printf("sky_parity OK (%d checks, %d rows)\n", g_checks, rows);
    return 0;
}
