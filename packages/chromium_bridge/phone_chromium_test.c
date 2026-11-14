/* phone_chromium_test.c -- the web-game STOPGAP end to end, headless: chromedriver + headless Chromium, driven by
 * PARENA's WebDriver client on the bridge's worker thread. Opens phc_test_page.html (a page whose script paints the
 * whole canvas and changes colour on a key), waits for its picture, sends 'g' and Up as the phone would, and checks
 * the picture changes. Prints SKIP and passes when no chromedriver is found (CI without a browser).
 * Build: make test-phone-chromium */
#include "phone_chromium.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL: %s\n", msg); } } while (0)

static uint8_t fb[1920 * 1200 * 4];
static int w, h, seq;

/* wait up to ms for a frame whose centre pixel is (r,g,b) */
static int wait_colour(int r, int g, int b, int ms) {
    for (int t = 0; t < ms; t += 50) {
        phc_frame(fb, (int)sizeof fb, &w, &h, &seq);
        if (w > 0) {
            const uint8_t *p = fb + ((size_t)(h / 2) * (size_t)w + (size_t)(w / 2)) * 4;
            if (abs(p[0] - r) < 16 && abs(p[1] - g) < 16 && abs(p[2] - b) < 16) return 1;
        }
        usleep(50000);
    }
    return 0;
}

int main(void) {
    if (!phc_available()) { printf("phone chromium: SKIP (no chromedriver; set SHANKPIT_CHROMEDRIVER)\n"); return 0; }
    char cwd[PATH_MAX], url[PATH_MAX + 64];
    if (!getcwd(cwd, sizeof cwd)) return 1;
    snprintf(url, sizeof url, "file://%s/packages/chromium_bridge/phc_test_page.html", cwd);
    phc_open(url);
    char st[192];
    int red = wait_colour(255, 0, 0, 30000);
    phc_status(st, sizeof st);
    if (!red) printf("      status: %s\n", st);
    CHECK(red, "the page's own picture arrives (red)");
    CHECK(w == 960 && h == 540, "at the rules' 960x540");
    printf("      (%dx%d, frame %d)\n", w, h, seq);
    CHECK(st[0] == 0, "and the status line is clear while it plays");
    phc_key('g', 1); phc_key('g', 0);
    CHECK(wait_colour(0, 255, 0, 10000), "a key the phone sends reaches the page ('g' -> green)");
    phc_key(1073741906, 1); phc_key(1073741906, 0);
    CHECK(wait_colour(0, 0, 255, 10000), "a named key too (Up -> blue)");
    phc_close();
    phc_status(st, sizeof st);
    CHECK(phc_frame(fb, (int)sizeof fb, &w, &h, &seq) == 0, "closed: no more pictures");
    printf("phone chromium: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
