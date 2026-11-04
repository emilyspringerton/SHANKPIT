/* aquaponics_rules.c -- the ecosystem decision layer for fish, plants, and the water-food web.
 *
 * A PARENA-native ecosys framework: the fish, the plants, and the water column form a small
 * closed loop, and every actor is a decision layer that runs on the server's own tick, exactly
 * like the empire influence tree and the resource collector. There is nothing here that touches
 * the renderer: this is the "what happens" layer only, and the rendering is the parena mod that
 * hosts it, driven by the same os_menu / game_info pattern this repo already carries.
 *
 * The two actors:
 *   - fish: an EU_ARCH_FISCH actor, rule by PARENA decide/feed rules, each fish has a
 *     stamina and a feed spread, and it moves to the nearest grow bed on its own.
 *   - plant: a PARENA mod with its own light/fertilizer dynamics, and it is a decision layer
 *     that runs the same way, so the player can put crops in the water and watch them grow.
 *
 * Pattern: mirrors empire_rules.c / zone_rules.c exactly, and every number lives in the rules
 * layer so the PARENA host can decide the same thing on server and client. */#include "aquaponics_rules.h"
#include <stdint.h>
