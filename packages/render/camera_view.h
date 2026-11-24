#ifndef SHANKPIT_CAMERA_VIEW_H
#define SHANKPIT_CAMERA_VIEW_H

/* camera_view.h -- thin host glue between the lobby's camera and the PARENA camera mods
 * (stdlib/camera/{zoom_cam,click_move,camera_follow}_mod.prn, generated into
 * packages/simulation/camera_unify_mod.c). BACKLOG SECTION 636, UNIFY-2 / UNIFY-4.
 *
 * Every DECISION (zoom step/ease/first-person hysteresis, click-move accept/arrive/cancel/steer, follow
 * recenter/slope/smoothing) is a PARENA mod call. This file only owns the state ints the stateless mods
 * hand back, converts to/from the host's floats, and sequences the calls once per frame. No GL, no SDL, no
 * physics: the host feeds in the player's position, the ground slope it sampled, and this frame's inputs.
 *
 * Units are the mods': milli = world units x1000, permille, mdeg = degrees x1000, ms. */

#define CAMVIEW_DEFAULT_ARM_MILLI 6000   /* the old fixed THIRD_PERSON_CAM_DIST */
#define CAMVIEW_MAX_ARM_MILLI     20000
#define CAMVIEW_LOCKED_MIN_MILLI  4000   /* a view that may not go first person (e.g. server-forced third) */

typedef struct CamView {
    /* zoom */
    int arm_milli, target_milli, last_out_milli, fp;
    int synced, written_flag;
    /* the four third-person options (+ the click-to-move toggle) -- stored/persisted by the host */
    int opt_follow;       /* 0 Free, 1 Behind when moving, 2 Always behind */
    int opt_recenter;     /* 0 slow, 1 normal, 2 fast */
    int opt_terrain;      /* 0 Off, 1 Slope */
    int opt_smooth;       /* 0 off, 1 low, 2 high */
    int opt_click_move;   /* 0 off (default), 1 on */
    /* follow state */
    int idle_ms, pitch_off_mdeg, focus_y_milli, focus_valid, speed_milli_s;
    int prev_x, prev_z, prev_valid;
    int arm_fraction_permille;   /* host sets after clipping: actual arm / wanted arm */
    /* click-to-move */
    int dest_active, dest_x_milli, dest_z_milli, stuck_ms;
    int body_yaw_mdeg, steering;
} CamView;

typedef struct CamViewIn {
    int dt_ms;
    int third;           /* the local player's view is a third-person one this frame (zoomed out, or server-forced) */
    int allow_fp;        /* the view may zoom into first person (not a server-forced third-person mode) */
    int px_milli, py_milli, pz_milli;
    int yaw_mdeg;
    int manual;          /* the user orbited (mouse/stick) this frame */
    int wasd;            /* any movement key/stick held */
    int slope_permille;  /* ground rise/run along the view direction, positive = uphill ahead */
} CamViewIn;

typedef struct CamViewOut {
    int first_person;
    int yaw_mdeg;          /* camera yaw after auto-recenter */
    int pitch_off_mdeg;    /* terrain pitch offset, positive = look further down, already wall-damped */
    int focus_dy_milli;    /* eased focus height minus the player's height */
    int steering;          /* click-to-move is driving the movement input */
    int fwd_permille, str_permille;
    int aim_yaw_mdeg;      /* command yaw to use while steering (the body heading) */
} CamViewOut;

/* Defaults: SHANKPIT = Behind when moving + Slope + normal recenter + low smoothing, click-to-move OFF. */
void camview_init(CamView *cv);
/* Re-seed zoom from the host's third_person flag (first use, or after an external change). */
void camview_sync_flag(CamView *cv, int third);
/* SDL wheel.y (positive = scroll up = zoom in). */
void camview_wheel(CamView *cv, int clicks, int allow_fp);
/* The V key: snap between first person and the last third-person distance. */
void camview_snap(CamView *cv, int allow_fp);
/* Click-to-move: ground hit (world x/z in milli, normal.y permille) from a left click. Returns 1 if accepted. */
int camview_click_dest(CamView *cv, int allow_fp, int hit_valid, int hx_milli, int hz_milli, int normal_y_permille);
void camview_cancel_dest(CamView *cv);
/* Once per frame, after input and before the sim ticks. */
void camview_update(CamView *cv, const CamViewIn *in, CamViewOut *out);
/* Per-axis camera position lag (smoothing option), once per frame. Off returns the target exactly. */
void camview_smooth_pos(const CamView *cv, int dt_ms, const int cur_milli[3], const int target_milli[3], int out_milli[3]);

#endif
