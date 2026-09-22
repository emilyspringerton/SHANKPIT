/* lab_sim_test.c -- real tests for lab_sim.c (BIG_O engine merge, phase 4), a representative
 * subset of BIG_O's own real core/lab_sim_test.c (17 tests total) -- this is a verbatim port with
 * zero logic transformation (unlike phase 3's PARENA move), so full re-derivation of every one of
 * BIG_O's 17 statistical tests isn't needed to establish confidence; this covers the real contract
 * of each equipment function at least once. Plain assert() harness, same MISHRI-bar discipline.
 *
 * Build and run:
 *   gcc -Wall -Wextra -O2 -o /tmp/lab_sim_test packages/simulation/lab_sim_test.c \
 *       packages/simulation/lab_sim.c -lm && /tmp/lab_sim_test
 */
#include "lab_sim.h"

#include <assert.h>
#include <stdio.h>

#define TRIALS 500

int main(void) {
    /* centrifuge: purity rises with real diminishing returns, never exceeds 100. */
    {
        LabSample s;
        lab_sample_init_wild_harvest(&s, 20.0f);
        assert(s.purity_pct == 0.0f);
        centrifuge_spin(&s, 5.0f, 500.0f);
        float after_first = s.purity_pct;
        assert(after_first > 0.0f && after_first < 100.0f);
        centrifuge_spin(&s, 5.0f, 500.0f);
        float second_gain = s.purity_pct - after_first;
        assert(second_gain < after_first); /* diminishing returns */
        printf("PASS: centrifuge_spin raises purity with real diminishing returns\n");
    }

    /* centrifuge: over-spinning damages integrity, a safe spin does not. */
    {
        LabSample gentle, harsh;
        lab_sample_init_wild_harvest(&gentle, 10.0f);
        lab_sample_init_wild_harvest(&harsh, 10.0f);
        centrifuge_spin(&gentle, 5.0f, 500.0f);   /* 2500 spin work -- safe */
        centrifuge_spin(&harsh, 30.0f, 1000.0f);  /* 30000 spin work -- way over threshold */
        assert(gentle.integrity_pct == 100.0f);
        assert(harsh.integrity_pct < 100.0f);
        printf("PASS: over-spinning damages integrity, a safe spin does not\n");
    }

    /* PCR: substantial amplification, excessive cycles damage integrity + creep contamination. */
    {
        LabSample s;
        lab_sample_init_wild_harvest(&s, 5.0f);
        float before = s.read_depth;
        pcr_amplify(&s, 10);
        assert(s.read_depth > before * 5.0f);
        LabSample excessive;
        lab_sample_init_wild_harvest(&excessive, 5.0f);
        pcr_amplify(&excessive, 60);
        assert(excessive.integrity_pct < 100.0f);
        assert(excessive.contamination_pct > 5.0f);
        printf("PASS: pcr_amplify raises read_depth; excessive cycles damage integrity + creep contamination\n");
    }

    /* sequencer: measured contamination is noisy but centered on the real ground truth. */
    {
        LabSample s;
        lab_sample_init_wild_harvest(&s, 30.0f);
        s.contamination_pct = 30.0f;
        double sum = 0.0;
        for (int i = 0; i < TRIALS; i++) sum += sequencer_run(&s).measured_contamination_pct;
        double mean = sum / TRIALS;
        assert(mean > 27.0 && mean < 33.0); /* converges near ground truth over many trials */
        printf("PASS: sequencer_run's noisy readout converges to ground truth over %d trials (mean=%.2f)\n", TRIALS, mean);
    }

    /* crispr_splice: higher precision inputs (purity/integrity/guide/skill) genuinely raise the
       real success rate over many trials. */
    {
        LabSample poor, good;
        lab_sample_init_wild_harvest(&poor, 50.0f);
        lab_sample_init_wild_harvest(&good, 0.0f);
        good.purity_pct = 100.0f;
        good.integrity_pct = 100.0f;
        int poor_success = 0, good_success = 0;
        for (int i = 0; i < TRIALS; i++) {
            if (crispr_splice(&poor, 0.1f, 0.1f).outcome == SPLICE_SUCCESS) poor_success++;
            if (crispr_splice(&good, 0.95f, 0.95f).outcome == SPLICE_SUCCESS) good_success++;
        }
        assert(good_success > poor_success);
        printf("PASS: crispr_splice success rate genuinely rises with precision inputs (poor=%d good=%d /%d)\n",
               poor_success, good_success, TRIALS);
    }

    /* clone_breed: generation increments correctly, and drift never decreases across many bred
       generations -- the module's own named "one-way" realism hook. */
    {
        LabSample a, b;
        lab_sample_init_wild_harvest(&a, 10.0f);
        lab_sample_init_wild_harvest(&b, 10.0f);
        LabSample child = clone_breed(&a, &b);
        assert(child.generation == 1);
        assert(child.genetic_drift > 0.0f);
        /* Breed against a partner whose drift is set to match the line's own current drift each
           generation (the real methodology BIG_O's own test uses) -- averaging against a fixed
           low-drift constant would pull the average down and isn't what "drift never decreases"
           actually claims (a real, live-found trap in this test's own first draft, not a bug in
           the port). */
        LabSample line = child;
        float last_drift = line.genetic_drift;
        for (int gen = 0; gen < 20; gen++) {
            LabSample partner = a;
            partner.generation = line.generation;
            partner.genetic_drift = line.genetic_drift;
            line = clone_breed(&line, &partner);
            assert(line.genetic_drift >= last_drift);
            last_drift = line.genetic_drift;
        }
        printf("PASS: clone_breed increments generation and drift never decreases across 20 bred generations\n");
    }

    /* incubation: NMD/cryptic-splice-failure always yields zero viability regardless of physical
       stats; the viable flag respects the real floor. */
    {
        LabSample pristine;
        lab_sample_init_wild_harvest(&pristine, 0.0f);
        pristine.purity_pct = 100.0f;
        pristine.integrity_pct = 100.0f;
        IncubationResult r_nmd = incubate_embryo(&pristine, SPLICE_NONSENSE_MEDIATED_DECAY);
        assert(r_nmd.viability_pct == 0.0f);
        assert(r_nmd.viable == 0);
        IncubationResult r_ok = incubate_embryo(&pristine, SPLICE_SUCCESS);
        assert(r_ok.viable == 1);
        printf("PASS: incubate_embryo -- NMD/cryptic failure zeroes viability, a clean success is viable\n");
    }

    printf("ALL PASS\n");
    return 0;
}
