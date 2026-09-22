// lab_sim.c -- see lab_sim.h.
#include "lab_sim.h"
#include <math.h>
#include <stdlib.h>

static float ls_rand01(void) {
    return (float)(rand() % 10000) / 10000.0f;
}

// Box-Muller Gaussian, mean 0 stddev 1 -- same real shape as zombie_values.c's own private
// zv_rand_gaussian/humanness.c's own humanness_rand_gaussian, deliberately re-implemented rather
// than shared, matching this repo's own established "each domain module owns its RNG" convention.
static float ls_rand_gaussian(void) {
    float u1 = ls_rand01();
    if (u1 < 1e-6f) u1 = 1e-6f;
    float u2 = ls_rand01();
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * 3.14159265f * u2);
}

#define LS_CENTRIFUGE_K 6000.0f
#define LS_CENTRIFUGE_OVERSPIN_THRESHOLD 8000.0f
#define LS_CENTRIFUGE_DAMAGE_PER_UNIT 0.01f

#define LS_PCR_SAFE_CYCLES 35
#define LS_PCR_DAMAGE_PER_EXTRA_CYCLE 1.5f
#define LS_PCR_CONTAM_CREEP_START_CYCLE 25
#define LS_PCR_CONTAM_CREEP_PER_CYCLE 0.05f

void lab_sample_init_wild_harvest(LabSample *s, float contamination_pct) {
    s->contamination_pct = contamination_pct;
    s->purity_pct = 0.0f;
    s->integrity_pct = 100.0f;
    s->read_depth = 1.0f;
    s->generation = 0;
    s->genetic_drift = 0.0f;
}

void centrifuge_spin(LabSample *s, float minutes, float rpm) {
    float spin_work = minutes * rpm;
    float gap = 100.0f - s->purity_pct;
    s->purity_pct += gap * (1.0f - expf(-spin_work / LS_CENTRIFUGE_K));
    if (s->purity_pct > 100.0f) s->purity_pct = 100.0f;
    if (s->purity_pct < 0.0f) s->purity_pct = 0.0f;

    if (spin_work > LS_CENTRIFUGE_OVERSPIN_THRESHOLD) {
        s->integrity_pct -= (spin_work - LS_CENTRIFUGE_OVERSPIN_THRESHOLD) * LS_CENTRIFUGE_DAMAGE_PER_UNIT;
        if (s->integrity_pct < 0.0f) s->integrity_pct = 0.0f;
    }
}

void pcr_amplify(LabSample *s, int cycles) {
    if (cycles <= 0) return;
    s->read_depth *= powf(1.9f, (float)cycles);

    if (cycles > LS_PCR_SAFE_CYCLES) {
        s->integrity_pct -= (float)(cycles - LS_PCR_SAFE_CYCLES) * LS_PCR_DAMAGE_PER_EXTRA_CYCLE;
        if (s->integrity_pct < 0.0f) s->integrity_pct = 0.0f;
    }
    if (cycles > LS_PCR_CONTAM_CREEP_START_CYCLE) {
        s->contamination_pct += (float)(cycles - LS_PCR_CONTAM_CREEP_START_CYCLE) * LS_PCR_CONTAM_CREEP_PER_CYCLE;
        if (s->contamination_pct > 100.0f) s->contamination_pct = 100.0f;
    }
}

SequencerReadout sequencer_run(const LabSample *s) {
    SequencerReadout r;

    /* Instrument imprecision -- the terminal UI never gets ground truth, only this noisy read. */
    float noise = ls_rand_gaussian() * 5.0f;
    r.measured_contamination_pct = s->contamination_pct + noise;
    if (r.measured_contamination_pct < 0.0f) r.measured_contamination_pct = 0.0f;
    if (r.measured_contamination_pct > 100.0f) r.measured_contamination_pct = 100.0f;

    r.alignment_pct = 100.0f - s->contamination_pct * 0.7f;
    if (s->read_depth < 5.0f) {
        r.alignment_pct -= (5.0f - s->read_depth) * 8.0f;
    }
    if (r.alignment_pct < 0.0f) r.alignment_pct = 0.0f;
    if (r.alignment_pct > 100.0f) r.alignment_pct = 100.0f;
    return r;
}

static float ls_splice_precision(const LabSample *s, float guide_rna_specificity, float operator_skill_0_to_1) {
    float precision = (s->purity_pct / 100.0f) * 0.3f
                     + (s->integrity_pct / 100.0f) * 0.2f
                     + guide_rna_specificity * 0.3f
                     + operator_skill_0_to_1 * 0.2f;
    /* A drifted line is less predictable to edit, regardless of how clean today's sample is. */
    precision *= (1.0f - s->genetic_drift * 0.4f);
    if (precision < 0.0f) precision = 0.0f;
    if (precision > 1.0f) precision = 1.0f;
    return precision;
}

SpliceResult crispr_splice(const LabSample *s, float guide_rna_specificity, float operator_skill_0_to_1) {
    SpliceResult res;
    res.off_target_severity = 0.0f;
    res.retrotransposon_jump = 0;

    float precision = ls_splice_precision(s, guide_rna_specificity, operator_skill_0_to_1);
    float fail = 1.0f - precision;

    float p_nmd = fail * 0.30f;
    float p_cryptic = fail * 0.25f;
    float p_offtarget = fail * 0.30f + (s->contamination_pct / 100.0f) * 0.15f;
    /* remaining probability mass falls to the success branch below */

    float roll = ls_rand01();
    if (roll < p_nmd) {
        res.outcome = SPLICE_NONSENSE_MEDIATED_DECAY;
    } else if (roll < p_nmd + p_cryptic) {
        res.outcome = SPLICE_CRYPTIC_SPLICE_FAILURE;
    } else if (roll < p_nmd + p_cryptic + p_offtarget) {
        res.outcome = SPLICE_OFF_TARGET_MUTATION;
        res.off_target_severity = (1.0f - precision) + ls_rand01() * 0.2f;
        if (res.off_target_severity > 1.0f) res.off_target_severity = 1.0f;
    } else {
        /* Success branch -- but a drifted line can still downgrade a clean edit to unstable. */
        float p_unstable_given_success = s->genetic_drift * 0.5f;
        if (ls_rand01() < p_unstable_given_success) {
            res.outcome = SPLICE_UNSTABLE_LINE;
        } else {
            res.outcome = SPLICE_SUCCESS;
        }
    }

    /* Retrotransposon-style pathogen copying: a rare, independent side-effect, more likely on
       contaminated samples, that can fire alongside ANY outcome above -- it is a property of the
       sample's own contamination, not a splice failure mode in itself. */
    float jump_chance = 0.01f + (s->contamination_pct / 100.0f) * 0.05f;
    if (ls_rand01() < jump_chance) {
        res.retrotransposon_jump = 1;
    }

    return res;
}

RepressorInstallResult crispr_install_repressor(const LabSample *s, float guide_rna_specificity, float operator_skill_0_to_1) {
    RepressorInstallResult r;
    SpliceResult splice = crispr_splice(s, guide_rna_specificity, operator_skill_0_to_1);

    if (splice.outcome == SPLICE_NONSENSE_MEDIATED_DECAY || splice.outcome == SPLICE_CRYPTIC_SPLICE_FAILURE) {
        r.installed = 0;
        r.reliability_0_to_1 = 0.0f;
        return r;
    }

    r.installed = 1;
    float precision = ls_splice_precision(s, guide_rna_specificity, operator_skill_0_to_1);
    if (splice.outcome == SPLICE_OFF_TARGET_MUTATION) {
        r.reliability_0_to_1 = precision * (1.0f - splice.off_target_severity) * 0.5f;
    } else if (splice.outcome == SPLICE_UNSTABLE_LINE) {
        r.reliability_0_to_1 = precision * 0.6f;
    } else {
        r.reliability_0_to_1 = precision;
    }
    if (r.reliability_0_to_1 < 0.0f) r.reliability_0_to_1 = 0.0f;
    if (r.reliability_0_to_1 > 1.0f) r.reliability_0_to_1 = 1.0f;
    return r;
}

LabSample clone_breed(const LabSample *parent_a, const LabSample *parent_b) {
    LabSample child;
    child.contamination_pct = (parent_a->contamination_pct + parent_b->contamination_pct) * 0.5f;
    child.purity_pct = (parent_a->purity_pct + parent_b->purity_pct) * 0.5f;
    child.integrity_pct = (parent_a->integrity_pct + parent_b->integrity_pct) * 0.5f;
    child.read_depth = 1.0f; /* a fresh line starts back at baseline sequencing depth */

    int gen_a = parent_a->generation, gen_b = parent_b->generation;
    child.generation = (gen_a > gen_b ? gen_a : gen_b) + 1;

    /* Real, one-way per-generation drift accrual -- the digest's own named realism hook. */
    float drift_gain = 0.03f + ls_rand01() * 0.02f;
    child.genetic_drift = (parent_a->genetic_drift + parent_b->genetic_drift) * 0.5f + drift_gain;
    if (child.genetic_drift > 1.0f) child.genetic_drift = 1.0f;

    return child;
}

IncubationResult incubate_embryo(const LabSample *s, SpliceOutcome last_splice_outcome) {
    IncubationResult r;
    float viability = (s->integrity_pct * 0.5f)
                     + ((100.0f - s->contamination_pct) * 0.3f)
                     + ((1.0f - s->genetic_drift) * 100.0f * 0.2f);

    switch (last_splice_outcome) {
        case SPLICE_SUCCESS:
            break;
        case SPLICE_UNSTABLE_LINE:
            viability *= 0.7f;
            break;
        case SPLICE_OFF_TARGET_MUTATION:
            viability *= 0.5f;
            break;
        case SPLICE_NONSENSE_MEDIATED_DECAY:
        case SPLICE_CRYPTIC_SPLICE_FAILURE:
        default:
            viability = 0.0f;
            break;
    }

    if (viability < 0.0f) viability = 0.0f;
    if (viability > 100.0f) viability = 100.0f;
    r.viability_pct = viability;
    r.viable = (viability > 20.0f) ? 1 : 0; /* real floor -- below this, the embryo just doesn't take */
    return r;
}
