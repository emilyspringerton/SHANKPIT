#ifndef DW_RATING_HOST_H
#define DW_RATING_HOST_H

/* dw_rating_host.h -- prototypes for the PARENA-generated ladder rating
 * (PARENA/stdlib/deadweight/rating.prn -> packages/simulation/dw_rating.c).
 *
 * The SAME .prn is emitted to Luau by NOOBTOPIA/scripts/gen_deadweight.sh, which is what lets a Roblox
 * server's per-server leaderboard and this C ladder print the same number for the same match. Nothing in
 * this file is hand-maintained logic; the constants (start 1000, floor 100, the 16/8..24 swing) live in
 * the .prn. */

int rating_start(void);
int rating_floor(void);
int rating_gain(int mine, int theirs);
int rating_loss(int mine, int theirs);
int rating_draw(int mine, int theirs);
int rating_apply(int rating, int delta);

#endif
