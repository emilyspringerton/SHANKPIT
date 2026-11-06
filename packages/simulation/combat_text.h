#ifndef COMBAT_TEXT_H
#define COMBAT_TEXT_H

/* combat_text.h -- floating combat text API for Shankpit.
 * Tracks damage/healing/shield numbers that float up above actors. */

#include "../common/protocol.h"
#include "combat_text_rules.h"
#include <string.h>

#define COMBAT_TEXT_DURATION_MS 1500

static inline void combat_text_note_damage(PlayerState *p, int damage, unsigned int now_ms) {
    if (!p) return;
    if (damage == 0) return;
    p->combat_text_damage = damage;
    p->combat_text_time = now_ms;
    if (damage < 0) {
        p->combat_text_type = 1; /* heal */
    } else {
        p->combat_text_type = 0; /* damage */
    }
}

static inline void combat_text_note_heal(PlayerState *p, int amount, unsigned int now_ms) {
    if (!p) return;
    if (amount == 0) return;
    p->combat_text_damage = -amount;
    p->combat_text_time = now_ms;
    p->combat_text_type = 1; /* heal */
}

static inline void combat_text_note_shield(PlayerState *p, int amount, unsigned int now_ms) {
    if (!p) return;
    if (amount == 0) return;
    p->combat_text_damage = amount;
    p->combat_text_time = now_ms;
    p->combat_text_type = 2; /* shield */
}

static inline int combat_text_get_damage(const PlayerState *p, unsigned int now_ms) {
    if (!p) return 0;
    if (p->combat_text_time == 0) return 0;
    if ((int)(now_ms - p->combat_text_time) > COMBAT_TEXT_DURATION_MS) return 0;
    return p->combat_text_damage;
}

static inline int combat_text_get_age(const PlayerState *p, unsigned int now_ms) {
    if (!p || p->combat_text_time == 0) return -1;
    return (int)(now_ms - p->combat_text_time);
}

static inline int combat_text_is_active(const PlayerState *p, unsigned int now_ms) {
    if (!p) return 0;
    if (p->combat_text_time == 0) return 0;
    if ((int)(now_ms - p->combat_text_time) > COMBAT_TEXT_DURATION_MS) return 0;
    return 1;
}

static inline unsigned char combat_text_get_type(const PlayerState *p, unsigned int now_ms) {
    if (!p || !combat_text_is_active(p, now_ms)) return 0;
    return p->combat_text_type;
}

#endif /* COMBAT_TEXT_H */
