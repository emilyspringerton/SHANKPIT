/* wave_rules.h -- the survival-wave numbers, with built-in defaults.
 *
 * The numbers live in PARENA (PARENA/stdlib/shankpit/wave_rules.prn, generated into wave_rules.c by
 * scripts/gen_wave_rules.sh / `make regen-wave-rules`). This header carries matching C defaults so
 * wave_spawner.h and story_ai.c compile and run in ANY binary -- lobby, server or test -- without linking
 * a generated file. That is deliberately the same arrangement zone_rules.h / widget_rules.h / slide_rules.h
 * use, and `make test-wave-spawner` asserts the two copies agree sample for sample.
 *
 * Why not just add wave_rules.c to LOBBY_SRC and SERVER_SRC? Because those are hand-copied lists that have
 * drifted nine times in this repo. A defaults-only header costs one parity test and cannot be forgotten
 * from a source list.
 */
#ifndef SHANKPIT_WAVE_RULES_H
#define SHANKPIT_WAVE_RULES_H

typedef struct {
    int (*first_delay_ms)(void);
    int (*interval_ms)(void);
    int (*count)(void);
    int (*size)(void);
    int (*size_for)(int wave);
    int (*live_cap)(void);
    int (*spawn_radius_milli)(void);
    int (*archetype)(void);
    int (*interval_for)(int wave);
    int (*should_spawn)(int wave, int live, int now_ms, int due_ms);
} WRWaveRules;

static inline int wr_clamp(int lo, int hi, int x) { return x < lo ? lo : (x > hi ? hi : x); }

static inline int wr_c_first_delay_ms(void) { return 20000; }
static inline int wr_c_interval_ms(void) { return 45000; }
static inline int wr_c_count(void) { return 8; }
static inline int wr_c_size(void) { return 3; }
static inline int wr_c_size_for(int wave) {
    if (wave < 0) return wr_c_size();
    int raw = wr_c_size() + wave / 3;
    return raw > 8 ? 8 : raw;
}
static inline int wr_c_live_cap(void) { return 24; }
static inline int wr_c_spawn_radius_milli(void) { return 30000; }
static inline int wr_c_archetype(void) { return 2; }
static inline int wr_c_interval_for(int wave) {
    if (wave < 0) return wr_c_interval_ms();
    int raw = wr_c_interval_ms() - wave * 3000;
    return raw < 15000 ? 15000 : raw;
}
static inline int wr_c_should_spawn(int wave, int live, int now_ms, int due_ms) {
    if (wave >= wr_c_count()) return 0;
    if (live >= wr_c_live_cap()) return 0;
    return now_ms >= due_ms ? 1 : 0;
}

static WRWaveRules g_wr_rules = {
    wr_c_first_delay_ms, wr_c_interval_ms, wr_c_count, wr_c_size, wr_c_size_for,
    wr_c_live_cap, wr_c_spawn_radius_milli, wr_c_archetype, wr_c_interval_for, wr_c_should_spawn
};

static inline void wr_rules_set(const WRWaveRules *r) { if (r) g_wr_rules = *r; }

#endif /* SHANKPIT_WAVE_RULES_H */
