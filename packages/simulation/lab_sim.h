#ifndef LAB_SIM_H
#define LAB_SIM_H

/* lab_sim.h -- BIG_O engine merge phase 4 (EMILY/BACKLOG.md SECTION 536). Ported in verbatim from
 * BIG_O's own core/lab_sim.h -- no renaming needed, this module has zero BIG_O-specific type
 * names. Deliberately kept plain C, not moved to PARENA: heavy float math (expf/powf) and RNG
 * throughout (centrifuge diminishing returns, PCR exponential amplification, Box-Muller sequencer
 * noise, splice-outcome probability rolls) -- none of it fits PARENA's scalar I32/Bool, no-RNG
 * VS0 model, matching this module's own documented "not currently expected to need mod-author
 * tuning" reasoning (unlike witness_rules.c/ai_brain_rules.prn's pure integer decision formulas).
 *
 * Original founder real-time quote (BIG_O, 2026-09-20): "build out all of the cloning facility
 * simulation tech we want to simulate real lab equipment as much as possible." Real answer to
 * that: a headless equipment
 * pipeline covering every real piece of gear docs/DESIGN_DIGEST.md §5/§6 names -- centrifuge,
 * PCR thermocycler, sequencer/bioinformatics terminal, CRISPR splice bench, repressor/kill-switch
 * install, breeding (genetic drift), embryo incubation -- with the digest's own named realism
 * hooks as real, live mechanics rather than flavor text: raw-sample contamination and read
 * alignment (sequencer_run), guide-RNA off-target effects (crispr_splice), genetic drift of
 * repeatedly bred lines (clone_breed), nonsense-mediated decay and cryptic splice sites as real
 * failure states (SpliceOutcome), retrotransposon-style pathogen copying (retrotransposon_jump).
 *
 * Same "game vocabulary, not protocols" discipline docs/DESIGN_DIGEST.md §5 itself insists on --
 * every formula here is a game-balance curve, not a real molecular biology model.
 *
 * Same "primitives proven in isolation first" discipline as core/npc_archetype.c/core/
 * zombie_values.c: this module does not wire into the day/ server's BP_APP_LAB UI or IDUNA
 * persistence yet -- that is real, separate, deliberately deferred integration work (see
 * NORTHSTAR.md's own lab-sim section for what's named and not yet built).
 */

#include <stdint.h>

/* A raw sample -- from a wild day-phase harvest (generation 0) or bred from an existing clone
 * line. All fields are game-abstraction floats, not real assay units. */
typedef struct {
    float contamination_pct;  /* 0..100 -- environmental/host contamination in the raw sample */
    float purity_pct;         /* 0..100 -- rises via centrifuge; 0 at first harvest */
    float integrity_pct;      /* 0..100 -- structural intactness; degrades from over-processing */
    float read_depth;         /* arbitrary coverage units; rises via PCR amplification */
    int generation;           /* 0 = wild harvest; N = bred N generations deep from a wild sample */
    float genetic_drift;      /* 0..1, ONE-WAY -- accumulates per generation bred (digest's own
                                  "genetic drift of repeatedly bred lines" realism hook), never
                                  reduced by any station in this module */
} LabSample;

/* lab_sample_init_wild_harvest -- a fresh day-phase harvest: zero purity, full integrity, a
 * single read's worth of depth, generation 0, no drift. */
void lab_sample_init_wild_harvest(LabSample *s, float contamination_pct);

/* ---- Centrifuge (digest §5's own named spin-time trade-off) ----
 * Purity rises with diminishing returns on time*rpm ("spin work"); integrity degrades once spin
 * work crosses a real over-spin threshold -- pelleting/shearing damage from spinning too hard or
 * too long, the real trade-off the digest calls for. */
void centrifuge_spin(LabSample *s, float minutes, float rpm);

/* ---- PCR thermocycler ----
 * Amplifies read_depth roughly exponentially per cycle; cycling past a safe count both damages
 * integrity (primer dimer/degradation) and lets nonspecific amplification creep contamination up
 * -- a real trade-off between "more material" and "cleaner material". */
void pcr_amplify(LabSample *s, int cycles);

/* ---- Sequencer / bioinformatics terminal readout ----
 * The digest's own "isolate reads, filter contamination, align" terminal UI reads THIS struct,
 * not the sample's own ground-truth fields -- sequencing is a real, imperfect instrument
 * measurement, not an oracle. */
typedef struct {
    float measured_contamination_pct; /* a noisy estimate of s->contamination_pct */
    float alignment_pct;              /* 0..100, reads that align to reference; degraded by true
                                          contamination and by low read_depth */
} SequencerReadout;

SequencerReadout sequencer_run(const LabSample *s);

/* ---- CRISPR splice bench ---- */
typedef enum {
    SPLICE_SUCCESS = 0,
    SPLICE_OFF_TARGET_MUTATION,      /* the digest's own named "guide-RNA off-target" failure */
    SPLICE_NONSENSE_MEDIATED_DECAY,  /* the digest's own named failure state -- silent dead end */
    SPLICE_CRYPTIC_SPLICE_FAILURE,   /* the digest's own named failure state -- wrong site used */
    SPLICE_UNSTABLE_LINE             /* viable now, but a real, elevated future-failure line */
} SpliceOutcome;

typedef struct {
    SpliceOutcome outcome;
    float off_target_severity;  /* 0..1, meaningful only when outcome == SPLICE_OFF_TARGET_MUTATION */
    int retrotransposon_jump;   /* 1 if a rare, independent copy-and-jump side-effect also fired
                                    this splice -- the digest's own named "retrotransposon-style
                                    pathogen copying" realism hook; can co-occur with ANY outcome,
                                    it is not itself a splice failure mode */
} SpliceResult;

/* guide_rna_specificity (0..1): how well-designed the guide is -- a future lab-bench player
 * choice. operator_skill_0_to_1: the digest's own named micromanipulation needle mini-game's real
 * integration point -- no mini-game UI is built yet, but this parameter is exactly where a future
 * mini-game's score plugs in. */
SpliceResult crispr_splice(const LabSample *s, float guide_rna_specificity, float operator_skill_0_to_1);

/* A dedicated splice type: sequence a repressor/kill-switch into a clone (digest §5). Runs the
 * same real risk model as crispr_splice underneath -- an off-target or drift-driven outcome here
 * means the switch may silently fail to fire later, a reliability value rather than a bare
 * present/absent flag. */
typedef struct {
    int installed;             /* 0 if the splice failed outright (NMD/cryptic splice failure) */
    float reliability_0_to_1;  /* how likely the switch actually fires when triggered later */
} RepressorInstallResult;

RepressorInstallResult crispr_install_repressor(const LabSample *s, float guide_rna_specificity, float operator_skill_0_to_1);

/* ---- Breeding / genetic drift across generations (digest §5) ----
 * A child sample averages its parents' physical stats but ALWAYS accrues new one-way drift on
 * top of the parents' own -- repeatedly breeding a line makes it progressively less predictable
 * to edit and less viable to incubate, exactly the digest's own named realism hook. */
LabSample clone_breed(const LabSample *parent_a, const LabSample *parent_b);

/* ---- Incubation / embryo viability (digest §6's embryo vial) ----
 * Whether an embryo actually takes, derived from the sample's real integrity/contamination/drift
 * plus the outcome of whatever splice was last run on it -- an NMD or cryptic-splice-failure
 * sample can never incubate viable, no matter how clean its physical stats are. */
typedef struct {
    int viable;
    float viability_pct; /* 0..100, informational even when viable == 1 */
} IncubationResult;

IncubationResult incubate_embryo(const LabSample *s, SpliceOutcome last_splice_outcome);

#endif /* LAB_SIM_H */
