/* stats_bridge.h -- GFD's 23-job table, mapped into this engine's gameplay units, with C defaults.
 *
 * The rules live in PARENA (PARENA/stdlib/shankpit/stats_bridge.prn, generated into stats_bridge.c by
 * scripts/gen_stats_bridge.sh / `make regen-stats-bridge`). This header carries numerically identical C
 * defaults so any binary or test can use the bridge without linking the generated file -- the same
 * arrangement zone_rules.h / widget_rules.h / slide_rules.h already keep, and which
 * `make test-stats-bridge` enforces by comparing the two copies sample for sample.
 *
 * Phase 4 of docs/GFD_UNIFICATION.md, and the boundary it draws matters more than the numbers:
 *
 *   - The TABLE is GFD's, copied from GoblinFoxDragon/server/job/job.go (23 jobs, AllJobs order,
 *     including GFD's own 23rd job ASN). If GFD retunes a job, this table is what gets updated.
 *   - The MAPPING (haste/move/crit/evade/damage) is THIS engine's, because GFD resolves damage against
 *     a mob registry and a dice model that do not exist here. Everything here is I32 permille so the
 *     arithmetic is identical on every host and no float ever enters a replicated number.
 *
 * Deliberately strict about what it is NOT: no spells, no abilities, no loot, no party mechanics, no
 * mobs. Those remain GFD's, and this header has no opinion about them. It also does not touch
 * PlayerState -- the caller gets plain ints and decides what to do with them, so wiring a job into a
 * live match is a decision at the call site and not a hidden side effect of including a header.
 */
#ifndef SHANKPIT_STATS_BRIDGE_H
#define SHANKPIT_STATS_BRIDGE_H

#define SB_JOB_COUNT 23
#define SB_LEVEL_MAX 75

/* AllJobs order, verbatim from job.go -- the index is the wire/level-file id, so this list is a contract. */
static const char *const SB_JOB_NAMES[SB_JOB_COUNT] = {
    "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM",
    "NIN", "DRG", "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN", "ASN"
};

typedef struct {
    /* table lookups (GFD's numbers) */
    int (*hp_base)(int job);
    int (*hp_per)(int job);
    int (*mp_base)(int job);
    int (*mp_per)(int job);
    int (*str)(int job);
    int (*dex)(int job);
    int (*vit)(int job);
    int (*agi)(int job);
    int (*hp)(int job, int level);
    int (*mp)(int job, int level);
    /* the mapping into this engine */
    int (*haste_permille)(int job, int level);
    int (*cooldown)(int rof, int haste_permille);
    int (*move_permille)(int job);
    int (*crit_permille)(int job);
    int (*evade_permille)(int job, int level);
    int (*damage_permille)(int job, int level);
    int (*health)(int job, int level);
} StatsBridge;

static inline int sb_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int sb_c_level(int level) { return sb_clamp(1, SB_LEVEL_MAX, level); }
static inline int sb_c_job(int job) { return sb_clamp(0, SB_JOB_COUNT - 1, job); }

/* job.go's jobStats, in AllJobs order: {BaseHP, HPPerLevel, BaseMP, MPPerLevel, STR, DEX, VIT, AGI} */
static const int SB_C_TABLE[SB_JOB_COUNT][8] = {
    { 90, 15,   0,  0, 8, 7, 8, 6 }, /* WAR */
    {100, 16,   0,  0, 8, 8, 8, 7 }, /* MNK */
    { 65, 10,  85, 14, 5, 5, 5, 5 }, /* WHM */
    { 55,  8, 100, 16, 4, 5, 4, 5 }, /* BLM */
    { 70, 11,  75, 12, 6, 6, 5, 6 }, /* RDM */
    { 75, 12,   0,  0, 6, 9, 6, 9 }, /* THF */
    { 95, 15,  60,  9, 8, 6, 9, 5 }, /* PLD */
    { 90, 14,  45,  8, 9, 6, 7, 5 }, /* DRK */
    { 80, 13,   0,  0, 7, 7, 7, 7 }, /* BST */
    { 65, 10,  50,  9, 5, 7, 5, 7 }, /* BRD */
    { 75, 12,   0,  0, 7, 8, 6, 8 }, /* RNG */
    { 85, 14,   0,  0, 8, 7, 7, 7 }, /* SAM */
    { 75, 12,  20,  6, 6, 9, 6, 9 }, /* NIN */
    { 85, 14,   0,  0, 8, 6, 8, 6 }, /* DRG */
    { 60,  9,  90, 15, 4, 4, 4, 5 }, /* SMN */
    { 80, 13,  55,  9, 7, 7, 6, 6 }, /* BLU */
    { 70, 11,  45,  8, 6, 8, 5, 8 }, /* COR */
    { 75, 12,  30,  6, 7, 8, 6, 7 }, /* PUP */
    { 80, 13,   0,  0, 7, 8, 7, 8 }, /* DNC */
    { 55,  8,  95, 15, 4, 5, 4, 5 }, /* SCH */
    { 60,  9,  85, 14, 5, 5, 5, 5 }, /* GEO */
    { 80, 13,  40,  7, 7, 7, 7, 7 }, /* RUN */
    { 78, 12,  45,  8, 7, 9, 6, 8 }  /* ASN */
};

static inline int sb_c_hp_base(int job) { return SB_C_TABLE[sb_c_job(job)][0]; }
static inline int sb_c_hp_per(int job) { return SB_C_TABLE[sb_c_job(job)][1]; }
static inline int sb_c_mp_base(int job) { return SB_C_TABLE[sb_c_job(job)][2]; }
static inline int sb_c_mp_per(int job) { return SB_C_TABLE[sb_c_job(job)][3]; }
static inline int sb_c_str(int job) { return SB_C_TABLE[sb_c_job(job)][4]; }
static inline int sb_c_dex(int job) { return SB_C_TABLE[sb_c_job(job)][5]; }
static inline int sb_c_vit(int job) { return SB_C_TABLE[sb_c_job(job)][6]; }
static inline int sb_c_agi(int job) { return SB_C_TABLE[sb_c_job(job)][7]; }

static inline int sb_c_hp(int job, int level) {
    return sb_c_hp_base(job) + sb_c_hp_per(job) * (sb_c_level(level) - 1);
}
static inline int sb_c_mp(int job, int level) {
    return sb_c_mp_base(job) + sb_c_mp_per(job) * (sb_c_level(level) - 1);
}
static inline int sb_c_haste_permille(int job, int level) {
    return sb_clamp(700, 1300, 1000 + (sb_c_agi(job) - 5) * 20 + (sb_c_level(level) - 1) * 2);
}
static inline int sb_c_cooldown(int rof, int haste_permille) {
    return sb_clamp(1, 999, (sb_clamp(1, 999, rof) * sb_clamp(700, 1300, haste_permille)) / 1000);
}
static inline int sb_c_move_permille(int job) { return sb_clamp(850, 1150, 1000 + (sb_c_agi(job) - 5) * 10); }
static inline int sb_c_crit_permille(int job) { return sb_clamp(0, 400, 50 + (sb_c_dex(job) - 5) * 15); }
static inline int sb_c_evade_permille(int job, int level) {
    return sb_clamp(0, 350, (sb_c_agi(job) - 5) * 12 + (sb_c_level(level) - 1) * 3);
}
static inline int sb_c_damage_permille(int job, int level) {
    return sb_clamp(600, 2000, 1000 + (sb_c_str(job) - 5) * 30 + (sb_c_level(level) - 1) * 5);
}
static inline int sb_c_health(int job, int level) { return sb_clamp(10, 255, sb_c_hp(job, level)); }

static StatsBridge g_stats_bridge = {
    sb_c_hp_base, sb_c_hp_per, sb_c_mp_base, sb_c_mp_per, sb_c_str, sb_c_dex, sb_c_vit, sb_c_agi,
    sb_c_hp, sb_c_mp, sb_c_haste_permille, sb_c_cooldown, sb_c_move_permille, sb_c_crit_permille,
    sb_c_evade_permille, sb_c_damage_permille, sb_c_health
};

static inline void stats_bridge_set(const StatsBridge *b) { if (b) g_stats_bridge = *b; }

/* Job id <-> name. An id outside the table reads as "WAR": a name is for display, so an out-of-range id
 * must print something a person recognises rather than the last job in the list (which would silently
 * relabel a corrupt save as an Assassin). Note the RULES clamp differently on purpose -- a stat lookup
 * for an id past the table lands on ASN, negative lands on WAR -- because those two cases mean different
 * things: a corrupt id gets the first job, an exhausted search gets the last. `stats_bridge_job_id`
 * returns -1 for a name it does not recognise, so a caller can still tell "unknown" from "Warrior". */
static inline int stats_bridge_job_id(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SB_JOB_COUNT; i++) {
        const char *a = SB_JOB_NAMES[i];
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}
static inline const char *stats_bridge_job_name(int job) {
    if (job < 0 || job >= SB_JOB_COUNT) return SB_JOB_NAMES[0];
    return SB_JOB_NAMES[job];
}

#endif /* SHANKPIT_STATS_BRIDGE_H */
