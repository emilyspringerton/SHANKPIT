/* sky_weather_fog_test.c -- real tests for the weather fog model in sky_weather_cfg.h (pure math,
 * no GL). Plain assert() harness, same convention as packages/simulation/day_night_clock_test.c.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/skw_fog_test packages/render/sky_weather_fog_test.c -lm && /tmp/skw_fog_test
 */
#include "sky_weather_cfg.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Same total density sky_weather_update computes, for a daytime (night=0) sky at `minute`. */
static float day_density(const SkyWeatherConfig *c, int w, float minute) {
    const SkwWeatherProfile *p = &c->weather[w];
    return c->fog_base + p->fog + c->fog_dawn * skw_dawn_fog_weight(minute, p->rain, p->storm);
}

int main(void) {
    SkyWeatherConfig c;
    sky_weather_cfg_defaults(&c);

    /* Koschmieder: at exactly the visibility distance, 2% of the contrast survives. */
    for (float v = 50.0f; v < 5000.0f; v *= 1.7f) {
        float t = skw_fog_transmittance(v, v);
        assert(fabsf(t - 0.02f) < 0.001f);
        assert(skw_fog_transmittance(v, 0.0f) == 1.0f);
        assert(skw_fog_transmittance(v, v * 0.5f) > skw_fog_transmittance(v, v));
    }

    /* Worse weather = shorter visibility, at noon (no dawn fog). */
    float vis[4];
    for (int w = 0; w < 4; w++) vis[w] = skw_fog_visibility(&c, day_density(&c, w, 720.0f));
    printf("noon visibility: clear %.0f  overcast %.0f  rain %.0f  storm %.0f\n", vis[0], vis[1], vis[2], vis[3]);
    assert(vis[0] > vis[1] && vis[1] > vis[2] && vis[2] > vis[3]);
    assert(vis[0] > 2500.0f);                 /* a clear day stays clear at gameplay ranges */
    assert(vis[3] < 700.0f);                  /* a storm genuinely closes in */
    assert(skw_fog_wash(vis[0]) == 0.0f);     /* clear noon: sky is not washed out at all */

    /* Dawn radiation fog: forms before sunrise on a clear morning, gone by noon, scrubbed by storms. */
    assert(skw_dawn_fog_weight(360.0f, 0, 0) == 1.0f);
    assert(skw_dawn_fog_weight(720.0f, 0, 0) == 0.0f);
    assert(skw_dawn_fog_weight(120.0f, 0, 0) == 0.0f);
    assert(skw_dawn_fog_weight(360.0f, 1, 1) == 0.0f);
    float dawn_clear = skw_fog_visibility(&c, day_density(&c, 0, 360.0f));
    printf("clear dawn visibility: %.0f\n", dawn_clear);
    assert(dawn_clear < vis[0] * 0.5f);

    /* A mod maker cranking a weather's fog gets real pea soup, floored at fog.min_visibility. */
    c.weather[1].fog = 0.05f;
    float soup = skw_fog_visibility(&c, day_density(&c, 1, 720.0f));
    printf("overcast.fog 0.05 visibility: %.0f\n", soup);
    assert(soup < 150.0f && soup >= c.fog_min_visibility);
    assert(skw_fog_wash(soup) == 1.0f);

    /* Config keys round-trip through the parser and get sanitized. */
    char err[128];
    int n = sky_weather_cfg_parse(&c, "fog.visibility_scale 3\nfog.min_visibility 1\nfog.dawn 9\n", err, sizeof(err));
    assert(n == 3);
    assert(c.fog_visibility_scale == 3.0f);
    sky_weather_cfg_sanitize(&c, 320, 44);
    assert(c.fog_min_visibility == 5.0f && c.fog_dawn == 0.05f);

    printf("sky_weather_fog_test: all passed\n");
    return 0;
}
