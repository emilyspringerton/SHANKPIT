/* packages/collection/collection_overlay_test.c -- the Collection overlay mod, headless: feed -> REFLUX -> model ->
 * the stream overlay, terminal and APPS page. make test-collection-overlay */
#include "collection_overlay.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed, failed;
#define EQI(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)
#define HAS(s, sub, what) do { if (strstr((s), (sub))) passed++; else { failed++; printf("FAIL: %s [%s]\n", what, (s)); } } while (0)

/* The feed PARENA's tests/reflux_wow/sf_feed_test.lua writes (two accounts, three characters, one vendor). */
static const char *FEED1 =
    "collection-feed 1\n"
    "name 1 Bjorn-Forever\nname 2 Ingrid-Forever\nname 3 Grimtak\nname 4 Cora-Forever\n"
    "row 3072 1 0 1\nrow 3073 1 8 1763400100\nrow 3072 2 2 1\nrow 3074 3 3881 2\nrow 3072 4 1 2\nrow 3075 3 2 1\n";
/* Later: Ingrid broke (an AH buy, kind 6). */
static const char *FEED2 =
    "collection-feed 1\n"
    "name 1 Bjorn-Forever\nname 2 Ingrid-Forever\nname 3 Grimtak\nname 4 <Cora&Co>\n"
    "row 3072 1 0 1\nrow 3073 1 8 1763400100\nrow 3072 2 0 3\nrow 3073 2 6 1763401000\nrow 3074 3 3881 2\n"
    "row 3072 4 1 2\nrow 3075 3 1 1\n";

static char out[64][COLLECTION_LINE_LEN];
static int tones[64];

static int find_line(int n, const char *sub) {
    for (int i = 0; i < n; i++) if (strstr(out[i], sub)) return i;
    return -1;
}

int main(void) {
    static CollectionOverlay o;
    static CollectionBridge b;
    reflux_host_reset();
    reflux_host_dispatch(101, 1, 2, 3); /* someone else's row before the mod starts: not ours */

    /* off: no COLLECTION_FEED */
    collection_bridge_init(&b, NULL);
    b.path = NULL;
    collection_overlay_init(&o);
    int n = collection_stream_lines(&o, &b, 0, out, tones, 8);
    EQI(n, 1, "off: one line");
    HAS(out[0], "Collection feed off (set COLLECTION_FEED)", "off says how to turn it on");
    static char html[65536];
    EQI(collection_apps_html(&o, &b, html, (int)sizeof(html)) > 0, 1, "off: the APPS page still renders");
    HAS(html, "set COLLECTION_FEED", "and says how to turn it on");

    /* load 1 */
    EQI(collection_bridge_load_text(&b, &o, "nope\n", 0), -1, "a file that is not a v1 feed is refused");
    EQI(collection_bridge_load_text(&b, &o, FEED1, 1000), 6, "six rows dispatched into REFLUX");
    EQI(o.loads, 1, "the summary row completes a load");
    EQI(o.shown.total, 3, "three characters");
    EQI(o.shown.self_found, 2, "two self-found");
    EQI(o.shown.vendors_found, 1, "one vendor");
    EQI(o.shown.nchars, 3, "three character rows");
    EQI(strcmp(o.shown.chars[0].name, "Ingrid-Forever"), 0, "verified self-found sorts first");
    EQI(strcmp(o.shown.chars[1].name, "Cora-Forever"), 0, "then since-tracking");
    EQI(strcmp(o.shown.chars[2].name, "Bjorn-Forever"), 0, "then broken");
    EQI(o.shown.chars[0].tone, 1, "green for since level 1");
    EQI(o.shown.chars[1].tone, 2, "amber for since tracking");
    EQI(o.shown.chars[2].tone, 3, "red for broken");
    EQI(o.shown.chars[2].broken_kind, 8, "Bjorn broke by a trade");

    n = collection_stream_lines(&o, &b, 1000, out, tones, 8);
    EQI(n, 4, "stream: header + three characters");
    HAS(out[0], "SELF FOUND 2/3  vendors 1", "stream header");
    EQI(tones[1], 1, "stream line tone comes from the rules");
    EQI(find_line(n, "IS NO LONGER"), -1, "no banner on the first load (nothing to compare)");

    n = collection_terminal(&o, &b, "sf", out, tones, 32);
    HAS(out[0], "3 characters, 2 self-found, 1 vendors found", "terminal summary");
    EQI(find_line(n, "Bjorn-Forever: broken by trade, 1 transactions") >= 0, 1, "terminal says why a character broke");
    EQI(find_line(n, "Cora-Forever: self-found since tracking, 2 transactions") >= 0, 1, "and the honest status");
    n = collection_terminal(&o, &b, "sf vendors", out, tones, 32);
    EQI(find_line(n, "Grimtak (npc 3881): 2 items") >= 0, 1, "terminal lists vendors");
    n = collection_terminal(&o, &b, "sf help", out, tones, 32);
    EQI(n, 2, "terminal help");

    /* load 2: Ingrid breaks -> a banner on the stream for co_banner_ms */
    EQI(collection_bridge_load_text(&b, &o, FEED2, 2000), 7, "second load");
    EQI(o.loads, 2, "two loads");
    n = collection_stream_lines(&o, &b, 2000, out, tones, 8);
    HAS(out[0], "Ingrid-Forever IS NO LONGER SELF-FOUND (AH buy)", "a newly broken character gets a banner");
    EQI(tones[0], 3, "in red");
    n = collection_stream_lines(&o, &b, 2000 + 8000, out, tones, 8);
    EQI(find_line(n, "IS NO LONGER"), -1, "the banner is gone after co_banner_ms");
    EQI(collection_apps_html(&o, &b, html, (int)sizeof(html)) > 0, 1, "APPS page");
    HAS(html, "&lt;Cora&amp;Co&gt;", "names are escaped in the page");
    HAS(html, "<td>Grimtak</td><td>3881</td><td>2</td>", "vendors in the page");
    EQI(collection_apps_html(&o, &b, html, 64), -1, "a short buffer is refused, not overrun");

    /* more rows than the REFLUX ring holds: batching lets the mod read them all */
    {
        static char big[200000];
        int len = snprintf(big, sizeof(big), "collection-feed 1\n");
        for (int i = 1; i <= 300; i++) len += snprintf(big + len, sizeof(big) - (size_t)len, "name %d Alt%03d-Forever\n", i, i);
        for (int i = 1; i <= 300; i++) len += snprintf(big + len, sizeof(big) - (size_t)len, "row 3072 %d %d %d\n", i, i % 3, i);
        len += snprintf(big + len, sizeof(big) - (size_t)len, "row 3075 300 200 0\n");
        int lost_before = o.lost;
        EQI(collection_bridge_load_text(&b, &o, big, 3000), 301, "301 rows dispatched");
        EQI(o.lost, lost_before, "none lost to the 256-row ring");
        EQI(o.shown.nchars, COLLECTION_MAX_CHARS, "the model keeps at most COLLECTION_MAX_CHARS characters");
        EQI(o.shown.total, 300, "the summary still counts all 300");
        n = collection_terminal(&o, &b, "sf", out, tones, 64);
        EQI(find_line(n, "... and ") >= 0, 1, "the terminal says how many it did not show");
        n = collection_stream_lines(&o, &b, 3000, out, tones, 64);
        EQI(n, 1 + 1 + 5, "while Ingrid's banner is up: banner + header + co_surface_lines(stream) characters");
        n = collection_stream_lines(&o, &b, 20000, out, tones, 64);
        EQI(n, 1 + 5, "after it: header + co_surface_lines(stream) characters");
    }

    /* the file path: reload when the file changes */
    {
        const char *path = "collection_feed_test.txt";
        FILE *f = fopen(path, "wb"); fputs(FEED1, f); fclose(f);
        static CollectionOverlay o2;
        static CollectionBridge b2;
        collection_overlay_init(&o2);
        collection_bridge_init(&b2, path);
        EQI(collection_bridge_tick(&b2, &o2, 10), 1, "the bridge reads the feed file");
        EQI(o2.shown.total, 3, "into the model");
        EQI(collection_bridge_tick(&b2, &o2, 20), 0, "not again before co_reload_ms");
        EQI(collection_bridge_tick(&b2, &o2, 10 + 5000), 0, "an unchanged file is not reloaded");
        f = fopen(path, "wb"); fputs(FEED2, f); fclose(f);
        EQI(collection_bridge_tick(&b2, &o2, 10 + 10000), 1, "a changed file is reloaded");
        EQI(o2.shown.self_found, 1, "with the new numbers");
        remove(path);
        static CollectionBridge b3;
        collection_bridge_init(&b3, "no_such_feed_file.txt");
        EQI(collection_bridge_tick(&b3, &o2, 0), 0, "a missing file is not an error");
        HAS(b3.error, "no feed file yet", "it says so");
    }

    printf("collection_overlay: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
