#ifndef WIND_AUDIO_RULES_H
#define WIND_AUDIO_RULES_H

/* wind_audio_rules.h -- installs PARENA's weather/wind rules into the host that hears them.
 *
 * The numbers live in PARENA/stdlib/shankpit/weather_audio_rules.prn and are generated into
 * packages/audio/wind_audio_rules.c (do not edit that file; `make regen-wind-audio-rules`).
 * They are integer permille (1000 == 1.0), total functions of weather + time, so the client's ambience
 * and the fog it thins are one decision instead of two hosts inventing their own numbers.
 *
 * Deliberately NOT consumed by packages/audio/audio.c: the mixer takes already-resolved gains
 * (audio_set_wind) and does DSP only. That keeps the audio TU linkable without PARENA and leaves the
 * rules where a server or a second host can read them too. */

int weather_wind_gust_permille(int now_ms);
int weather_wind_speed_permille(int storm_pm, int gust_pm);
int weather_wind_gain_permille(int storm_pm, int fog_pm, int speed_pm);
int weather_wind_bright_permille(int speed_pm, int fog_pm);
int weather_wind_direction_deg(int now_ms);
int weather_wind_pan_permille(int dir_deg, int yaw_deg);
int weather_wind_fog_thin_permille(int speed_pm, int storm_pm);
/* thunder: how long the sound of a strike takes to arrive, and how loud it is when it does */
int weather_thunder_delay_ms(int dist_units);
int weather_thunder_gain_permille(int dist_units, int storm_pm);

#endif /* WIND_AUDIO_RULES_H */
