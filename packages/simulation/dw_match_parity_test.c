/* dw_match_parity_test.c -- the SEQUENCER, pinned across two worlds.
 *
 * DEADWEIGHT's rules and its citizen policy already exist once, in PARENA, and are emitted twice (to C
 * here, to Luau in NOOBTOPIA). The sequencer is the one piece that is still hand-written per world:
 * this file's sequencer is `dw_match.c`, NOOBTOPIA's is `src/deadweight_game.lua`. This test is what
 * stops the two drifting.
 *
 *   ./bin/dw_parity_test            asserts every row of PARENA/stdlib/deadweight/match_parity.txt
 *   ./bin/dw_parity_test --dump     REGENERATES that file from this same binary
 *
 * One binary, one format: the writer and the reader cannot disagree about the columns, because they
 * are the same code. NOOBTOPIA/tests/deadweight_match_test.lua reads the same file -- the convention
 * walk_parity.txt already set for the movement half of the three-way merge.
 *
 * The vectors are the C answers, because the C emitted from the .prn is authoritative and the Luau port
 * is the bug if it disagrees (MONOREPO/docs/THREE_WAY_MERGE.md §5). Regenerate with
 * `make regen-dw-parity`, then re-check a couple of rows by hand: a zeroed or garbled column would
 * otherwise be "parity" with itself.
 *
 * make test-dw-parity
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dw_engine.h"
#include "dw_match.h"

#define MAX_SPECS 12
#define MAX_ROUNDS 100
#define ROW_FIELDS 11

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } \
} while (0)

/* A scenario is everything a match needs besides the sequencer's own code: a seed, and for each seat
 * the facts that CRAFT its deck (path, win rate, the population's meta), plus its aggression. The deck
 * itself is NOT written down -- it is built by `cp_deck_card`, the same function a citizen's deck comes
 * from, so the vectors also pin deck crafting. */
typedef struct {
    int id;
    int seed;
    int path[2];
    int lean[2];
    int aggr[2];
    int meta_kind;
    int meta_share;
    /* the expected result */
    int winner;
    int rounds;
    int hull[2];
    int vault[2];
    int rows[MAX_ROUNDS][ROW_FIELDS];
    int n_rows;
} Spec;

/* The scenarios. Chosen for the decisions that actually differ between the two sequencers' authors:
 * identity decks against answer decks, a winning citizen against a losing one, a mirror match, the
 * 1000/0 extremes of aggression, a Defence identity (the kind that deals nothing unopposed), and
 * seed 0 (which is the RNG's own fallback, not zero). */
static Spec g_specs[MAX_SPECS] = {
    /* id seed   path0 lean0 path1 lean1 aggr0 aggr1 metaK share */
    { 0, 12345, { 2, 0 }, { 500, 300 }, { 700, 400 }, -1, 0, 0, 0, { 0, 0 }, { 0, 0 }, { { 0 } }, 0 },
    { 1,   777, { 0, 2 }, { 150, 900 }, { 300, 800 },  0, 900, 0, 0, { 0, 0 }, { 0, 0 }, { { 0 } }, 0 },
    { 2,  4242, { 4, 1 }, { 500, 500 }, {   0, 1000 }, 2, 600, 0, 0, { 0, 0 }, { 0, 0 }, { { 0 } }, 0 },
    { 3,  2026, { 2, 2 }, { 500, 500 }, { 600, 600 },  0, 850, 0, 0, { 0, 0 }, { 0, 0 }, { { 0 } }, 0 },
    { 4,     0, { 3, 4 }, { 900, 500 }, { 1000, 0 },   1, 700, 0, 0, { 0, 0 }, { 0, 0 }, { { 0 } }, 0 },
};
static int g_spec_count = 5;

static void build_decks(const Spec *sp, int d0[DW_DECK], int d1[DW_DECK]) {
    const DwEngine *e = dw_engine();
    for (int i = 0; i < DW_DECK; i++) {
        d0[i] = e->cp_deck_card(i, sp->path[0], sp->lean[0], sp->meta_kind, sp->meta_share);
        d1[i] = e->cp_deck_card(i, sp->path[1], sp->lean[1], sp->meta_kind, sp->meta_share);
    }
}

/* Runs one scenario and hands back its transcript. Rows are, in order:
 *   round  card0  card1  hull0  hull1  energy0  energy1  vault0  vault1  damage0  damage1
 * where the hull/energy/vault are the state AFTER that round and damage is what each seat TOOK. */
static int run_spec(const Spec *sp, int rows[MAX_ROUNDS][ROW_FIELDS],
                    int *winner, int *rounds, int hull[2], int vault[2]) {
    int d0[DW_DECK], d1[DW_DECK];
    build_decks(sp, d0, d1);
    DwMatch m;
    dw_match_init(&m, (uint32_t)sp->seed, d0, d1);

    int n = 0;
    while (!m.over && n < MAX_ROUNDS) {
        dw_match_step_policy(&m, sp->aggr[0], sp->aggr[1]);
        int *r = rows[n];
        r[0] = m.round;
        r[1] = m.last_card[0];
        r[2] = m.last_card[1];
        r[3] = m.seat[0].hull;
        r[4] = m.seat[1].hull;
        r[5] = m.seat[0].energy;
        r[6] = m.seat[1].energy;
        r[7] = m.seat[0].vault;
        r[8] = m.seat[1].vault;
        r[9] = m.last_damage[0];
        r[10] = m.last_damage[1];
        n++;
    }
    *winner = m.winner;
    *rounds = m.rounds_played;
    hull[0] = m.seat[0].hull;
    hull[1] = m.seat[1].hull;
    vault[0] = m.seat[0].vault;
    vault[1] = m.seat[1].vault;
    return n;
}

/* ---- --dump: write the fixture ---------------------------------------------------------------- */

static void dump(void) {
    printf("# match_parity.txt -- the shared MATCH fixture. Two suites assert this same file.\n");
    printf("#\n");
    printf("#   SHANKPIT/packages/simulation/dw_match_parity_test.c   (make test-dw-parity)\n");
    printf("#   NOOBTOPIA/tests/deadweight_match_test.lua             (lua tests/deadweight_match_test.lua)\n");
    printf("#\n");
    printf("# The numbers are the C answers, because the C emitted from the .prn is authoritative and the\n");
    printf("# Luau port is the bug if it disagrees (MONOREPO/docs/THREE_WAY_MERGE.md §5).\n");
    printf("#\n");
    printf("# A match is identified by what a match actually needs: a seed and, per seat, the facts its deck\n");
    printf("# is CRAFTED from (path, win rate, the population's meta) plus its aggression. The deck is not\n");
    printf("# written down -- it is built by cp_deck_card, so these rows pin deck crafting too.\n");
    printf("#\n");
    printf("# match id seed path0 lean0 path1 lean1 aggr0 aggr1 metaKind metaShare winner rounds h0 h1 v0 v1\n");
    printf("# round id rnd card0 card1 h0 h1 e0 e1 v0 v1 dmg0 dmg1\n");
    printf("#\n");
    printf("# winner: 0/1 seat, 2 draw. card: a card id, or -1 for a pass. Hull/energy/credits are AFTER the\n");
    printf("# round; dmg is what each seat TOOK that round. Regenerate with `make regen-dw-parity`.\n");
    printf("#\n");

    for (int i = 0; i < g_spec_count; i++) {
        const Spec *sp = &g_specs[i];
        int rows[MAX_ROUNDS][ROW_FIELDS];
        int winner, rounds, hull[2], vault[2];
        int n = run_spec(sp, rows, &winner, &rounds, hull, vault);
        printf("match %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
               sp->id, sp->seed, sp->path[0], sp->lean[0], sp->path[1], sp->lean[1],
               sp->aggr[0], sp->aggr[1], sp->meta_kind, sp->meta_share,
               winner, rounds, hull[0], hull[1], vault[0], vault[1]);
        for (int r = 0; r < n; r++) {
            printf("round %d %d %d %d %d %d %d %d %d %d %d %d\n",
                   sp->id, rows[r][0], rows[r][1], rows[r][2], rows[r][3], rows[r][4],
                   rows[r][5], rows[r][6], rows[r][7], rows[r][8], rows[r][9], rows[r][10]);
        }
    }
}

/* ---- the reader ------------------------------------------------------------------------------- */

static const char *parity_path(void) {
    const char *dir = getenv("PARENA_DIR");
    static char buf[1024];
    if (dir && *dir) snprintf(buf, sizeof buf, "%s/stdlib/deadweight/match_parity.txt", dir);
    else snprintf(buf, sizeof buf, "%s", "../PARENA/stdlib/deadweight/match_parity.txt");
    return buf;
}

static Spec g_file_specs[MAX_SPECS];

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

static int load_vectors(void) {
    FILE *f = fopen(parity_path(), "r");
    if (!f) {
        printf("SKIP: no parity vectors at %s (a standalone SHANKPIT checkout; set PARENA_DIR)\n", parity_path());
        return -1;
    }
    char line[512];
    int n = -1;
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strncmp(line, "match ", 6) == 0) {
            if (n + 1 >= MAX_SPECS) break;
            n++;
            Spec *sp = &g_file_specs[n];
            memset(sp, 0, sizeof *sp);
            int got = sscanf(line,
                "match %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                &sp->id, &sp->seed, &sp->path[0], &sp->lean[0], &sp->path[1], &sp->lean[1],
                &sp->aggr[0], &sp->aggr[1], &sp->meta_kind, &sp->meta_share,
                &sp->winner, &sp->rounds, &sp->hull[0], &sp->hull[1], &sp->vault[0], &sp->vault[1]);
            CHECK(got == 16, "match line has %d/16 fields: %s", got, line);
            if (got != 16) { n--; continue; }
        } else if (strncmp(line, "round ", 6) == 0) {
            CHECK(n >= 0, "a round line before any match line");
            if (n < 0) continue;
            Spec *sp = &g_file_specs[n];
            CHECK(sp->n_rows < MAX_ROUNDS, "match %d has more than %d rounds", sp->id, MAX_ROUNDS);
            if (sp->n_rows >= MAX_ROUNDS) continue;
            int *r = sp->rows[sp->n_rows];
            int id = -1;
            int got = sscanf(line, "round %d %d %d %d %d %d %d %d %d %d %d %d",
                             &id, &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7], &r[8], &r[9], &r[10]);
            CHECK(got == 12, "round line has %d/12 fields: %s", got, line);
            if (got != 12) continue;
            CHECK(id == sp->id, "a round line names match %d inside match %d", id, sp->id);
            sp->n_rows++;
        } else {
            CHECK(0, "unrecognised line: %s", line);
        }
    }
    fclose(f);
    return n + 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
        dump();
        return 0;
    }

    int count = load_vectors();
    if (count < 0) {
        printf("match_parity_test: SKIPPED (no vectors)\n");
        return 0;
    }
    CHECK(count > 0, "the fixture parsed zero matches");

    for (int i = 0; i < count; i++) {
        const Spec *sp = &g_file_specs[i];
        int rows[MAX_ROUNDS][ROW_FIELDS];
        int winner, rounds, hull[2], vault[2];
        int n = run_spec(sp, rows, &winner, &rounds, hull, vault);

        CHECK(n == sp->n_rows, "match %d played %d rounds, the fixture records %d", sp->id, n, sp->n_rows);
        CHECK(rounds == sp->rounds, "match %d report %d rounds, the fixture %d", sp->id, rounds, sp->rounds);
        CHECK(winner == sp->winner, "match %d winner %d, the fixture %d", sp->id, winner, sp->winner);
        CHECK(hull[0] == sp->hull[0] && hull[1] == sp->hull[1],
              "match %d hulls %d/%d, the fixture %d/%d", sp->id, hull[0], hull[1], sp->hull[0], sp->hull[1]);
        CHECK(vault[0] == sp->vault[0] && vault[1] == sp->vault[1],
              "match %d credits %d/%d, the fixture %d/%d", sp->id, vault[0], vault[1], sp->vault[0], sp->vault[1]);

        int compare = n < sp->n_rows ? n : sp->n_rows;
        for (int r = 0; r < compare; r++) {
            for (int f = 0; f < ROW_FIELDS; f++) {
                if (rows[r][f] != sp->rows[r][f]) {
                    printf("FAIL match %d round %d field %d: played %d, the fixture says %d"
                           " (round=%d card=%d/%d hull=%d/%d energy=%d/%d credits=%d/%d dmg=%d/%d)\n",
                           sp->id, r + 1, f, rows[r][f], sp->rows[r][f],
                           rows[r][0], rows[r][1], rows[r][2], rows[r][3], rows[r][4],
                           rows[r][5], rows[r][6], rows[r][7], rows[r][8], rows[r][9], rows[r][10]);
                    g_fail++;
                    break;
                }
            }
        }
    }

    if (g_fail) { printf("dw_match_parity_test: %d FAILED\n", g_fail); return 1; }
    int total_rounds = 0;
    for (int i = 0; i < count; i++) total_rounds += g_file_specs[i].n_rows;
    printf("dw_match_parity_test: %d matches, %d rounds pinned\n", count, total_rounds);
    printf("dw_match_parity_test OK\n");
    return 0;
}
