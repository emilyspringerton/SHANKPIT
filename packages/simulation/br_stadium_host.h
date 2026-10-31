/* br_stadium_host.h -- the Bedrock Racers stadium shape, from PARENA.
 *
 * The shape lives in PARENA/stdlib/bedrockracers/web/br_stadium.prn (deduped out of the web game's
 * br_arena.prn, which already said the level WAS SHANKPIT's stadium), generated into br_stadium.c by
 * scripts/gen_br_stadium.sh / `make regen-br-stadium`. Both the wasm game and this client link the
 * same generated numbers, so "pixel for pixel" starts with "same walls". */
#ifndef SHANKPIT_BR_STADIUM_HOST_H
#define SHANKPIT_BR_STADIUM_HOST_H

double stadium_half_x(void);
double stadium_half_z(void);
double stadium_height(void);
double stadium_radius(void);
double stadium_center_y(void);
double stadium_tower_offset(void);
double stadium_tower_radius(void);
double stadium_tower_top(void);
double stadium_goal_y(void);
double stadium_track_scale(void);
double stadium_track_half_width(void);
double stadium_track_shoulder(void);
double stadium_free(double x, double y, double z);
int stadium_open(double x, double y, double z);
int stadium_tower(double x, double z);
int stadium_grip_permille(int speed_milli);
int stadium_facet_degrees(void);

#endif /* SHANKPIT_BR_STADIUM_HOST_H */
