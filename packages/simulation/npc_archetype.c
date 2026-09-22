#include "npc_archetype.h"

/* Prototype for the PARENA-generated formula (ai_brain_rules.c, do-not-edit-by-hand). No shared
 * .h between generated units in this codebase (cutscene_effect_mod.c/day_night_clock.c/
 * witness_sim.c all set this precedent already). */
int npc_effective_vigilance(int base, int startled, int tired, int fatigue_pct, int energy_deficit_pct);

void npc_brain_init(NpcBrain *b, NpcArchetype archetype, uint32_t now_ms) {
    b->archetype = archetype;
    humanness_state_init(&b->humanness, now_ms);
    if (archetype == NPC_ARCHETYPE_THE_MEN) {
        b->base_vigilance = 85;
        b->humanness.mood = HUMANNESS_MOOD_FOCUSED;
        b->humanness.energy = 0.9f;   /* professional, on the clock, not dragging */
        b->humanness.fatigue = 0.05f;
        b->humanness.boredom = 0.0f;  /* dispatched with a job, not idling */
        b->humanness.curiosity = 0.1f;
    } else {
        b->base_vigilance = 35;
        b->humanness.mood = HUMANNESS_MOOD_CURIOUS;
        b->humanness.curiosity = 0.5f;
        b->humanness.boredom = 0.3f;
    }
}

void npc_brain_tick(NpcBrain *b, uint32_t now_ms) {
    humanness_tick_mood(&b->humanness, now_ms);
}

void npc_brain_get_startled(NpcBrain *b, uint32_t now_ms) {
    humanness_get_startled(&b->humanness, now_ms);
}

int npc_brain_effective_vigilance(const NpcBrain *b) {
    int startled = b->humanness.mood == HUMANNESS_MOOD_STARTLED;
    int tired = b->humanness.mood == HUMANNESS_MOOD_TIRED;
    int fatigue_pct = (int)(b->humanness.fatigue * 100.0f + 0.5f);
    int energy_deficit_pct = (int)((1.0f - b->humanness.energy) * 100.0f + 0.5f);
    return npc_effective_vigilance(b->base_vigilance, startled, tired, fatigue_pct, energy_deficit_pct);
}

uint32_t npc_brain_reaction_delay_ms(const NpcBrain *b, uint32_t base_ms) {
    return humanness_reaction_delay_ms(&b->humanness, base_ms);
}
