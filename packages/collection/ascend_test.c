/* packages/collection/ascend_test.c -- /ascend, headless: GuildHerald's feed -> REFLUX -> guilds and invites -> the
 * lines and the links it opens. make test-ascend */
#include "ascend.h"
#include "../reflux/reflux_runtime.h"

#include <stdio.h>
#include <string.h>

static int passed, failed;
#define EQI(got, want, what) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ == w_) passed++; else { failed++; printf("FAIL: %s (got %ld, want %ld)\n", what, g_, w_); } } while (0)
#define HAS(s, sub, what) do { if (strstr((s), (sub))) passed++; else { failed++; printf("FAIL: %s\n  wanted: %s\n  in: %s\n", what, (sub), (s)); } } while (0)

static char opened[32][96];
static int nopened;
static void open_url(const char *url) { if (nopened < 32) snprintf(opened[nopened++], 96, "%s", url); }

static char out[32][ASCEND_LINE];
static int tones[32];
static char all[8192];
static int run(Ascend *a, const char *args) {
    int n = ascend_command(a, args, out, tones, 32, open_url);
    all[0] = '\0';
    for (int i = 0; i < n; i++) { strncat(all, out[i], sizeof(all) - strlen(all) - 2); strcat(all, "\n"); }
    return n;
}

/* the feed PARENA's guildherald test writes, plus one guild with many invites */
static const char *FEED =
    "collection-feed 1\n"
    "name 1 Kaldorei Vanguard\nname 2 Forever\nname 3 AbC-123\nname 4 RaidNight\nname 5 Ash Ravens\nname 6 Forever\n"
    "name 7 ravens\nname 8 Quiet Guild\nname 9 Forever\nname 10 Big Guild\nname 11 Forever\n"
    "name 12 a1\nname 13 a2\nname 14 a3\nname 15 a4\nname 16 a5\nname 17 bad!code\n"
    "row 3082 1 2 30\nrow 3083 1 3 0\nrow 3083 1 4 0\nrow 3082 5 6 1\nrow 3083 5 7 0\nrow 3082 8 9 2\n"
    "row 3082 10 11 4\nrow 3083 10 12 0\nrow 3083 10 13 0\nrow 3083 10 14 0\nrow 3083 10 15 0\nrow 3083 10 16 0\nrow 3083 10 17 0\n"
    "row 3084 4 8 0\n";

int main(void) {
    static Ascend a;
    reflux_host_reset();

    ascend_init(&a, NULL);
    a.path = NULL;
    EQI(run(&a, ""), 1, "no feed set: one line");
    HAS(all, "set COLLECTION_GUILD_FEED", "says how to turn it on");

    ascend_init(&a, "no_such_guild_feed.txt");
    run(&a, "");
    HAS(all, "no guild feed yet", "a missing file says what to run");

    EQI(ascend_load_text(&a, "nope\n"), -1, "not a v1 feed");
    EQI(ascend_load_text(&a, FEED), 1, "the feed loads through REFLUX");
    EQI(a.ng, 4, "four guilds");
    EQI(a.guilds_total, 4, "the summary's guild count");
    EQI(a.g[0].ncodes, 2, "Kaldorei Vanguard has two invites");
    EQI(a.g[3].ncodes, 5, "a bad code is dropped (checked again on this side)");

    const char *path = "ascend_test_feed.txt";
    FILE *f = fopen(path, "wb"); fputs(FEED, f); fclose(f);
    ascend_init(&a, path);
    run(&a, "");
    HAS(all, "4 guilds, 8 Discord invites", "the header counts them");
    HAS(all, "<Kaldorei Vanguard> (Forever): discord.gg/AbC-123 discord.gg/RaidNight", "each guild with its invites, case kept");
    HAS(all, "<Quiet Guild> (Forever): no Discord link", "a guild without one says so");
    HAS(all, "/ascend join", "and how to open them");
    EQI(tones[0], 5, "notice-coloured");
    EQI(nopened, 0, "listing opens nothing");

    run(&a, "join");
    EQI(nopened, 5, "join opens asc-open-max invites");
    EQI(strcmp(opened[0], "https://discord.gg/AbC-123"), 0, "as https://discord.gg/<code>");
    HAS(all, "Opening discord.gg/AbC-123 for <Kaldorei Vanguard>", "and says which");
    HAS(all, "3 more: /ascend join again.", "the rest wait (8 valid invites: 2 + 1 + 5)");
    run(&a, "join");
    EQI(nopened, 8, "the next join opens the rest, after a reload of the feed");
    run(&a, "join");
    HAS(all, "every invite has been opened", "then nothing is left");
    EQI(nopened, 8, "and nothing is opened twice");
    for (int i = 0; i < nopened; i++) EQI(strstr(opened[i], "bad") == NULL, 1, "no bad code ever reaches a URL");
    remove(path);

    int rows = 0;
    for (int i = 0; i < reflux_host_log_size(); i++) if (reflux_host_action_type_at(i) == 3085) rows++;
    EQI(rows, 2, "each join that opened something dispatches 3085");

    printf("ascend: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
