/* ambient_life_test.c -- the SHANKPIT half of the NOOBTOPIA three-way merge, headless.
 *
 * What this pins:
 *   * every category the rules name has at least one real line in NOOBTOPIA's data (a category with no lines would
 *     silently mute an NPC, so it fails here instead);
 *   * the PARENA decisions are total and bounded -- pick in range for every seed, cooldowns inside the documented
 *     window, tired NPCs quieter than idle ones on the same seed, a negative clock never speaking;
 *   * the host picks a line from the RIGHT category for the mood (that is the whole point of categorising at all);
 *   * same seed -> same line, and 200 draws hit several distinct lines (a crowd that repeats itself is worse than
 *     silence);
 *   * below the cooldown it returns NULL rather than speaking early.
 *
 * make test-ambient-life
 */
#include <stdio.h>
#include <string.h>

#include "ambient_life.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void) {
    /* 0. the generated data is real and covers every category the rules can return */
    CHECK(g_ambient_line_count >= 20);
    printf("ambient: %d lines from NOOBTOPIA\n", g_ambient_line_count);
    for (int cat = 0; cat < ambient_category_count(); cat++) {
        int n = ambient_life_count_in(cat);
        CHECK(n > 0);
    }
    for (int i = 0; i < g_ambient_line_count; i++)
        CHECK(g_ambient_lines[i].text && g_ambient_lines[i].text[0] != '\0');

    /* 1. picks are always in range (n = 0 has no valid index, so it is checked separately below) */
    for (int n = 1; n <= 40; n++)
        for (int seed = 0; seed < 300; seed++) {
            int got = ambient_pick(n, seed);
            CHECK(got >= 0 && got < n);
        }
    CHECK(ambient_pick(0, 99) == 0);
    CHECK(ambient_pick(5, -1) == 0);

    /* 2. mood -> category is total, and unknown moods still produce something sayable */
    CHECK(ambient_category_for_mood(3) == 3);
    CHECK(ambient_category_for_mood(4) == 4);
    CHECK(ambient_category_for_mood(1) == 1);
    CHECK(ambient_category_for_mood(99) == 0);

    /* 3. cooldowns live in the documented window and respect mood */
    for (int mood = 0; mood < 8; mood++)
        for (int seed = 0; seed < 200; seed++) {
            int ms = ambient_cooldown_ms(mood, seed);
            CHECK(ms >= 3000 && ms <= 22000);
        }
    CHECK(ambient_cooldown_ms(3, 1234) > ambient_cooldown_ms(0, 1234));

    /* 4. should-speak is exactly the comparison, and a negative clock never speaks */
    CHECK(ambient_should_speak(0, 0, 5) == 0);
    CHECK(ambient_should_speak(0, ambient_cooldown_ms(0, 5), 5) == 1);
    CHECK(ambient_should_speak(0, 999999, 5) == 1);
    CHECK(ambient_should_speak(0, -5, 5) == 0);

    /* 5. below the cooldown nothing is said at all */
    unsigned int seed = 42u;
    CHECK(ambient_life_speak(0, 0, &seed) == 0);

    /* 6. a chosen line always belongs to the mood's own category */
    for (int mood = 0; mood < 8; mood++) {
        unsigned int s = 7u;
        const char *line = ambient_life_speak(mood, 999999, &s);
        CHECK(line != 0);
        int cat = ambient_category_for_mood(mood);
        int found = 0;
        for (int i = 0; i < g_ambient_line_count; i++)
            if (g_ambient_lines[i].category == cat && strcmp(g_ambient_lines[i].text, line) == 0) found = 1;
        CHECK(found);
    }

    /* 7. determinism, and enough variety that a crowd does not chant in unison */
    unsigned int a = 42u, b = 42u;
    CHECK(ambient_life_speak(0, 999999, &a) == ambient_life_speak(0, 999999, &b));
    const char *seen[64];
    int nseen = 0;
    unsigned int s = 7u;
    for (int i = 0; i < 200; i++) {
        const char *line = ambient_life_speak(0, 999999, &s);
        if (!line) continue;
        int dup = 0;
        for (int k = 0; k < nseen; k++) if (strcmp(seen[k], line) == 0) dup = 1;
        if (!dup && nseen < 64) seen[nseen++] = line;
    }
    CHECK(nseen >= 3);
    printf("ambient: 200 greetings -> %d distinct lines\n", nseen);

    /* 8. seed advances only when something was said (a caller's stream stays stable on silent ticks) */
    unsigned int q = 99u, before = q;
    (void)ambient_life_speak(0, 0, &q);
    CHECK(q == before);

    /* 9. the seed mixer, pinned to the exact numbers NOOBTOPIA's tests/ambient_test.lua pins. MINSTD: the near-2^31
     * row is the seed that silently diverged in a double, which is why this table is here rather than trusted. */
    CHECK(ambient_life_next_seed(0u) == 0u);
    CHECK(ambient_life_next_seed(42u) == 2027382u);
    CHECK(ambient_life_next_seed(999999u) == 1026311495u);
    CHECK(ambient_life_next_seed(2147483646u) == 2147435376u);

    if (g_fail) { printf("ambient_life_test: %d FAILED\n", g_fail); return 1; }
    printf("ambient_life_test OK\n");
    return 0;
}
