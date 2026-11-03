/* dw_ladder_parity_test.c -- the LADDER, pinned across two worlds.
 *
 *   ./bin/dw_ladder_parity_test            asserts PARENA/stdlib/deadweight/ladder_parity.txt
 *   ./bin/dw_ladder_parity_test --dump     REGENERATES that file from this same binary
 *
 * The fixture carries BOTH the script (a sequence of ladder operations) and the outcomes it produced.
 * Both this test and NOOBTOPIA/tests/deadweight_ladder_test.lua READ THE SCRIPT OUT OF THE FILE and
 * execute it, so the two worlds cannot drift by one of them re-typing the scenario -- only by actually
 * disagreeing, which is the point.
 *
 * The operations are deliberately in terms of what a caller does (challenge, accept the one I just
 * opened, tick the clock), never in terms of internal slots: an `accept` names "the challenge opened
 * most recently", so both sides resolve the slot the same way.
 *
 * make test-dw-ladder-parity   /   make regen-dw-ladder-parity
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dw_engine.h"
#include "dw_ladder.h"
#include "dw_match.h"

#define MAX_LIDS 4
#define MAX_OPS 64
#define MAX_LINES 128

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } \
} while (0)

typedef struct { char verb[16]; int a, b, c; } Op;

/* ---- executing a script ------------------------------------------------------------------------ */

/* Returns the "last opened" slot so `accept`/`decline` can address it without naming a slot. */
typedef struct { DwLadder l; int last_slot; } Run;

static int run_ops(Run *run, int lid, const Op *ops, int n_ops) {
    dw_ladder_reset(&run->l, 1u);
    run->last_slot = -1;
    for (int i = 0; i < n_ops; i++) {
        const Op *o = &ops[i];
        if (strcmp(o->verb, "reset") == 0) {
            dw_ladder_reset(&run->l, (uint32_t)o->a);
            run->last_slot = -1;
        } else if (strcmp(o->verb, "hero") == 0) {
            dw_ladder_set_hero(&run->l, o->a);
        } else if (strcmp(o->verb, "add") == 0) {
            dw_ladder_add_citizen(&run->l, o->a, o->b, o->c, run->l.now_ms);
        } else if (strcmp(o->verb, "challenge") == 0) {
            run->last_slot = dw_ladder_challenge(&run->l, o->a, o->b, (unsigned int)o->c);
        } else if (strcmp(o->verb, "accept") == 0) {
            dw_ladder_accept(&run->l, run->last_slot, (unsigned int)o->a);
        } else if (strcmp(o->verb, "decline") == 0) {
            dw_ladder_decline(&run->l, run->last_slot);
        } else if (strcmp(o->verb, "tick") == 0) {
            dw_ladder_tick(&run->l, (unsigned int)o->a);
        } else if (strcmp(o->verb, "remove") == 0) {
            dw_ladder_remove_pid(&run->l, o->a);
        } else {
            CHECK(0, "unknown ladder op '%s'", o->verb);
            return -1;
        }
    }
    (void)lid;
    return 0;
}

/* ---- the canonical text ------------------------------------------------------------------------ */

static void emit(FILE *f, const DwLadder *l, int lid) {
    fprintf(f, "ladder %d meta %d %d %ld results %d %d %d %d count %d hero %d"
               " last %d %d %d %d %d %d %d %d %u\n",
            lid, dw_ladder_meta_kind(l), dw_ladder_meta_share(l), l->meta_plays,
            l->results[1], l->results[2], l->results[3], l->results[4],
            l->count, l->hero_pid,
            l->last.valid, l->last.kind, l->last.pid[0], l->last.pid[1],
            l->last.winner, l->last.rounds, l->last.hull[0], l->last.hull[1], l->last.at_ms);
    for (int i = 0; i < DW_LADDER_MAX_CITIZENS; i++) {
        const DwLadderMember *m = &l->members[i];
        if (!m->active) continue;
        fprintf(f, "member %d pid %d player %d path %d aggr %d wins %d games %d rating %d deck",
                lid, m->pid, m->is_player, m->path, m->aggression, m->wins, m->games, m->rating);
        for (int d = 0; d < DW_DECK; d++) fprintf(f, " %d", m->deck[d]);
        fprintf(f, "\n");
    }
    for (int i = 0; i < DW_LADDER_MAX_CHALLENGES; i++) {
        const DwChallenge *c = &l->challenges[i];
        if (!c->active) continue;
        fprintf(f, "chal %d slot %d id %u from %d to %d kind %d opened %u expires %u\n",
                lid, i, c->id, c->from_pid, c->to_pid, c->kind, c->opened_ms, c->expires_ms);
    }
}

/* ---- the two built-in scripts (used only by --dump) -------------------------------------------- */

static const Op S0[] = {
    { "reset",     777, 0, 0 },
    { "hero",        0, 0, 0 },
    { "add",         5, 0, 400 },   /* CRAFT  */
    { "add",         6, 2, 800 },   /* DUEL   */
    { "add",         7, 4, 200 },   /* IDLE   */
    { "challenge",   0, 5, 1000 },  /* player -> citizen */
    { "accept",   1200, 0, 0 },
    { "challenge",   5, 0, 1300 },  /* citizen -> player */
    { "accept",   1400, 0, 0 },
    { "challenge",   6, 7, 1500 },  /* citizen -> citizen */
    { "decline",     0, 0, 0 },
    { "challenge",   7, 6, 1700 },  /* and the other way round */
    { "accept",   1800, 0, 0 },
    { "tick",     2000, 0, 0 },     /* citizens start a game of their own */
    { "tick",     6000, 0, 0 },
    { "tick",    10000, 0, 0 },
};

static const Op S1[] = {
    { "reset",    4242, 0, 0 },
    { "hero",        0, 0, 0 },
    { "add",         5, 2, 900 },
    { "add",         6, 1, 500 },
    { "challenge",   5, 0, 1000 },
    { "tick",     1010, 0, 0 },     /* too soon after the open for a citizen challenge? */
    { "tick",    46100, 0, 0 },     /* 45000 ms later: the open challenge is dead */
    { "challenge",   6, 0, 47000 },
    { "accept",  47100, 0, 0 },
    { "remove",      5, 0, 0 },
    { "challenge",   6, 5, 47200 }, /* a pid that has left the world */
    { "tick",    50000, 0, 0 },
};

typedef struct { const Op *ops; int n; } Script;
static const Script SCRIPTS[] = {
    { S0, (int)(sizeof S0 / sizeof S0[0]) },
    { S1, (int)(sizeof S1 / sizeof S1[0]) },
};

static void dump(void) {
    printf("# ladder_parity.txt -- the shared LADDER fixture. Two suites assert this same file.\n");
    printf("#\n");
    printf("#   SHANKPIT/packages/simulation/dw_ladder_parity_test.c   (make test-dw-ladder-parity)\n");
    printf("#   NOOBTOPIA/tests/deadweight_ladder_test.lua             (lua tests/deadweight_ladder_test.lua)\n");
    printf("#\n");
    printf("# Both sides READ THE SCRIPT BELOW and execute it, then compare the outcome lines. The script\n");
    printf("# is here rather than in either implementation so the two cannot drift by one of them\n");
    printf("# re-typing the scenario. `accept`/`decline` address \"the challenge opened most recently\".\n");
    printf("#\n");
    printf("# op <lid> <verb> <a> <b> <c>   verbs: reset seed | hero pid | add pid path aggr |\n");
    printf("#                               challenge from to now | accept now | decline | tick now |\n");
    printf("#                               remove pid\n");
    printf("# ladder <lid> meta <kind> <share> <plays> results <x4> count <n> hero <pid>\n");
    printf("#        last <valid> <kind> <pid0> <pid1> <winner> <rounds> <hull0> <hull1> <at_ms>\n");
    printf("# member <lid> pid <p> player <0/1> path <p> aggr <a> wins <w> games <g> rating <r> deck <16 ids>\n");
    printf("# chal   <lid> slot <i> id <id> from <f> to <t> kind <k> opened <ms> expires <ms>\n");
    printf("#\n");
    printf("# The decks are NOT in the save format on purpose -- a deck is a function of the record and\n");
    printf("# the meta -- so they are printed here as PROOF that re-crafting reproduces them exactly.\n");
    printf("# Regenerate with `make regen-dw-ladder-parity`, and re-check a row by hand.\n");
    printf("#\n");

    for (int lid = 0; lid < (int)(sizeof SCRIPTS / sizeof SCRIPTS[0]); lid++) {
        const Op *ops = SCRIPTS[lid].ops;
        for (int i = 0; i < SCRIPTS[lid].n; i++)
            printf("op %d %s %d %d %d\n", lid, ops[i].verb, ops[i].a, ops[i].b, ops[i].c);
        Run run;
        if (run_ops(&run, lid, ops, SCRIPTS[lid].n) == 0) emit(stdout, &run.l, lid);
    }
}

/* ---- reading the fixture ----------------------------------------------------------------------- */

typedef struct {
    Op ops[MAX_OPS];
    int n_ops;
    char lines[MAX_LINES][256];
    int n_lines;
} LidScript;

static LidScript g_file[MAX_LIDS];

static const char *parity_path(void) {
    const char *dir = getenv("PARENA_DIR");
    static char buf[1024];
    if (dir && *dir) snprintf(buf, sizeof buf, "%s/stdlib/deadweight/ladder_parity.txt", dir);
    else snprintf(buf, sizeof buf, "%s", "../PARENA/stdlib/deadweight/ladder_parity.txt");
    return buf;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
        dump();
        return 0;
    }

    FILE *f = fopen(parity_path(), "r");
    if (!f) {
        printf("SKIP: no ladder vectors at %s (a standalone SHANKPIT checkout; set PARENA_DIR)\n", parity_path());
        return 0;
    }
    char line[256];
    int lids = 0;
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        if (strncmp(line, "op ", 3) == 0) {
            int lid = -1;
            char verb[16];
            int a = 0, b = 0, c = 0;
            int got = sscanf(line, "op %d %15s %d %d %d", &lid, verb, &a, &b, &c);
            CHECK(got >= 3, "unreadable op line: %s", line);
            if (got < 3 || lid < 0 || lid >= MAX_LIDS) continue;
            LidScript *ls = &g_file[lid];
            CHECK(ls->n_ops < MAX_OPS, "lid %d has more than %d ops", lid, MAX_OPS);
            if (ls->n_ops >= MAX_OPS) continue;
            snprintf(ls->ops[ls->n_ops].verb, sizeof ls->ops[0].verb, "%s", verb);
            ls->ops[ls->n_ops].a = a;
            ls->ops[ls->n_ops].b = b;
            ls->ops[ls->n_ops].c = c;
            ls->n_ops++;
            if (lid + 1 > lids) lids = lid + 1;
        } else if (strncmp(line, "ladder ", 7) == 0 || strncmp(line, "member ", 7) == 0 ||
                   strncmp(line, "chal ", 5) == 0) {
            int lid = -1;
            if (sscanf(line, "%*s %d", &lid) != 1) { CHECK(0, "no lid on: %s", line); continue; }
            CHECK(lid >= 0 && lid < MAX_LIDS, "lid %d out of range", lid);
            if (lid < 0 || lid >= MAX_LIDS) continue;
            LidScript *ls = &g_file[lid];
            CHECK(ls->n_lines < MAX_LINES, "lid %d has more than %d outcome lines", lid, MAX_LINES);
            if (ls->n_lines >= MAX_LINES) continue;
            snprintf(ls->lines[ls->n_lines], sizeof ls->lines[0], "%s", line);
            ls->n_lines++;
        } else {
            CHECK(0, "unrecognised line: %s", line);
        }
    }
    fclose(f);
    CHECK(lids > 0, "the fixture parsed no ladders");

    for (int lid = 0; lid < lids; lid++) {
        LidScript *ls = &g_file[lid];
        CHECK(ls->n_ops > 0, "lid %d has no ops", lid);
        Run run;
        if (run_ops(&run, lid, ls->ops, ls->n_ops) != 0) continue;

        FILE *buf = tmpfile();
        CHECK(buf != NULL, "could not open a scratch buffer");
        if (!buf) continue;
        emit(buf, &run.l, lid);
        fflush(buf);
        rewind(buf);

        int idx = 0;
        char got[256];
        while (fgets(got, sizeof got, buf)) {
            trim(got);
            if (got[0] == '\0') continue;
            if (idx >= ls->n_lines) {
                CHECK(0, "lid %d produced an extra line: %s", lid, got);
                break;
            }
            if (strcmp(got, ls->lines[idx]) != 0) {
                printf("FAIL lid %d line %d\n  played:   %s\n  fixture:  %s\n", lid, idx + 1, got, ls->lines[idx]);
                g_fail++;
            }
            idx++;
        }
        CHECK(idx == ls->n_lines, "lid %d produced %d lines, the fixture records %d", lid, idx, ls->n_lines);
        fclose(buf);
    }

    if (g_fail) { printf("dw_ladder_parity_test: %d FAILED\n", g_fail); return 1; }
    int total = 0;
    for (int lid = 0; lid < lids; lid++) total += g_file[lid].n_lines;
    printf("dw_ladder_parity_test: %d scripts, %d outcome lines pinned\n", lids, total);
    printf("dw_ladder_parity_test OK\n");
    return 0;
}
