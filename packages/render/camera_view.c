/* camera_view.c -- see camera_view.h. Thin glue over the PARENA camera mods. */
#include "camera_view.h"
#include "../simulation/camera_unify_mod_host.h"
#include <math.h>

#define CV_PI 3.14159265358979f

void camview_init(CamViewState *cv) {
    cv->arm_milli = 0; cv->target_milli = 0; cv->last_out_milli = CAMVIEW_DEFAULT_ARM_MILLI; cv->fp = 1;
    cv->synced = 0; cv->written_flag = 0;
    cv->opt_follow = 1; cv->opt_recenter = 1; cv->opt_terrain = 1; cv->opt_smooth = 1; cv->opt_click_move = 0;
    cv->idle_ms = 60000; cv->pitch_off_mdeg = 0; cv->focus_y_milli = 0; cv->focus_valid = 0; cv->speed_milli_s = 0;
    cv->prev_x = cv->prev_z = 0; cv->prev_valid = 0; cv->arm_fraction_permille = 1000;
    cv->dest_active = 0; cv->dest_x_milli = cv->dest_z_milli = 0; cv->stuck_ms = 0;
    cv->body_yaw_mdeg = 0; cv->steering = 0;
}

void camview_sync_flag(CamViewState *cv, int third) {
    if (third) { cv->arm_milli = cv->target_milli = cv->last_out_milli; cv->fp = 0; }
    else { cv->arm_milli = cv->target_milli = 0; cv->fp = 1; }
    cv->synced = 1;
}

static void note_out(CamViewState *cv) { if (cv->target_milli >= zoom_cam_out_floor_milli()) cv->last_out_milli = cv->target_milli; }

void camview_wheel(CamViewState *cv, int clicks, int allow_fp) {
    int mn = allow_fp ? 0 : CAMVIEW_LOCKED_MIN_MILLI;
    cv->target_milli = zoom_cam_wheel_target_milli(cv->target_milli, clicks, 0, mn, CAMVIEW_MAX_ARM_MILLI);
    note_out(cv);
}

void camview_snap(CamViewState *cv, int allow_fp) {
    if (!allow_fp) return;
    if (cv->fp || cv->target_milli < zoom_cam_snap_milli()) cv->target_milli = cv->last_out_milli;
    else { note_out(cv); cv->target_milli = 0; }
}

void camview_cancel_dest(CamViewState *cv) { cv->dest_active = 0; cv->stuck_ms = 0; cv->steering = 0; }

int camview_click_dest(CamViewState *cv, int allow_fp, int hit_valid, int hx, int hz, int ny) {
    if (!click_move_accept(cv->opt_click_move && allow_fp, cv->fp, 0, hit_valid, ny)) return 0;
    cv->dest_active = 1; cv->dest_x_milli = hx; cv->dest_z_milli = hz; cv->stuck_ms = 0;
    return 1;
}

static int heading_mdeg(int dx, int dz) {   /* yaw whose forward (-sin y, -cos y) points along (dx,dz) */
    float deg = atan2f(-(float)dx, -(float)dz) * (180.0f / CV_PI);
    return cam_ease_wrap_mdeg((int)lroundf(deg * 1000.0f));
}

void camview_update(CamViewState *cv, const CamViewIn *in, CamViewOut *out) {
    int dt = in->dt_ms;
    out->first_person = cv->fp; out->yaw_mdeg = in->yaw_mdeg; out->pitch_off_mdeg = 0; out->focus_dy_milli = 0;
    out->steering = 0; out->fwd_permille = out->str_permille = 0; out->aim_yaw_mdeg = in->yaw_mdeg;
    cv->steering = 0;
    /* facing == camera unless a click-move destination owns the body heading (it keeps last frame's heading) */
    if (!cv->dest_active) cv->body_yaw_mdeg = cam_ease_wrap_mdeg(in->yaw_mdeg);
    /* zoom: ease the displayed arm toward the wheel target, then the first-person hysteresis */
    cv->arm_milli = zoom_cam_ease_arm_milli(cv->arm_milli, cv->target_milli, dt, zoom_cam_tau_ms());
    cv->fp = in->allow_fp ? zoom_cam_is_first_person(cv->arm_milli, cv->fp) : 0;
    out->first_person = cv->fp;

    /* speed from the position delta, lightly eased so one-frame jitter does not flip "moving" */
    if (cv->prev_valid && dt > 0) {
        float dx = (float)(in->px_milli - cv->prev_x), dz = (float)(in->pz_milli - cv->prev_z);
        int moved = (int)sqrtf(dx * dx + dz * dz);
        int raw = (int)((long long)moved * 1000 / dt);
        cv->speed_milli_s = cam_ease_step(cv->speed_milli_s, raw, dt, 100);
        if (cv->opt_click_move && cv->dest_active) cv->stuck_ms = click_move_stuck_update(cv->stuck_ms, dt, moved);
    }
    cv->prev_x = in->px_milli; cv->prev_z = in->pz_milli; cv->prev_valid = 1;

    if (!in->third || cv->fp) {          /* first person ignores every follow option; click-move is third-person only */
        cv->idle_ms = cam_follow_manual_idle_ms(cv->idle_ms, dt, in->manual);
        cv->pitch_off_mdeg = cam_ease_step(cv->pitch_off_mdeg, 0, dt, 120);
        cv->focus_y_milli = in->py_milli; cv->focus_valid = 1;
        if (cv->fp) camview_cancel_dest(cv);
        return;
    }
    int cm_on = cv->opt_click_move && in->allow_fp;
    cv->idle_ms = cam_follow_manual_idle_ms(cv->idle_ms, dt, in->manual);

    /* click-to-move: decide cancel/arrive, otherwise steer in the body-heading frame */
    if (cv->dest_active) {
        if (click_move_should_cancel(cm_on, cv->fp, in->wasd, cv->stuck_ms)) camview_cancel_dest(cv);
    }
    if (cv->dest_active) {
        int dx = cv->dest_x_milli - in->px_milli, dz = cv->dest_z_milli - in->pz_milli;
        if (click_move_arrived(dx, dz, click_move_arrive_radius_milli())) {
            camview_cancel_dest(cv);
        } else {
            int target_heading = heading_mdeg(dx, dz);
            /* the body turns toward the goal at the FAST recenter rate; the steering is expressed in its frame */
            cv->body_yaw_mdeg = cam_follow_yaw_step_mdeg(cv->body_yaw_mdeg, target_heading, dt, 2, 1);
            float yr = (float)cv->body_yaw_mdeg * (CV_PI / 180000.0f);
            float ff = -sinf(yr), fz = -cosf(yr), rx = cosf(yr), rz = -sinf(yr);
            int lf = (int)((float)dx * ff + (float)dz * fz), lr = (int)((float)dx * rx + (float)dz * rz);
            out->steering = cv->steering = 1;
            out->fwd_permille = click_move_steer_fwd_permille(lf, lr);
            out->str_permille = click_move_steer_right_permille(lf, lr);
            out->aim_yaw_mdeg = cv->body_yaw_mdeg;
        }
    }

    /* follow: with a click-move body heading the camera can recenter behind it (WASD mouselook keeps facing ==
       camera, so there is nothing to recenter toward) */
    {
        int active = cam_follow_recenter_active(cv->opt_follow, cv->speed_milli_s, cv->idle_ms, cv->fp);
        int behind = cv->dest_active ? cv->body_yaw_mdeg : cam_ease_wrap_mdeg(in->yaw_mdeg);
        out->yaw_mdeg = cam_follow_yaw_step_mdeg(in->yaw_mdeg, behind, dt, cv->opt_recenter, active);
    }

    /* terrain: pitch offset eased, then damped by however much the wall pull-in shortened the arm; focus height eased */
    {
        int target = cam_follow_slope_pitch_target_mdeg(cv->opt_terrain, in->slope_permille);
        cv->pitch_off_mdeg = cam_follow_pitch_ease_mdeg(cv->pitch_off_mdeg, target, dt);
        out->pitch_off_mdeg = cam_follow_wall_damp_mdeg(cv->pitch_off_mdeg, cv->arm_fraction_permille);
        if (!cv->focus_valid) { cv->focus_y_milli = in->py_milli; cv->focus_valid = 1; }
        cv->focus_y_milli = cam_follow_focus_y_step_milli(cv->opt_terrain, cv->focus_y_milli, in->py_milli, dt);
        out->focus_dy_milli = cv->focus_y_milli - in->py_milli;
    }
}

void camview_smooth_pos(const CamViewState *cv, int dt_ms, const int cur[3], const int tgt[3], int out[3]) {
    for (int i = 0; i < 3; i++) out[i] = cam_follow_smooth_step_milli(cv->opt_smooth, cur[i], tgt[i], dt_ms);
}
