/* stream_out.h -- SHANKPIT-native streaming: the broadcast program feed leaves the game as raw video
 * (card #458: "start to replace OBS with shankpit native streaming"). The program camera's clean world view
 * (no HUD, no multiview tiles -- what a viewer should see) is read back each frame and piped, as RGB24, into
 * an encoder process. The encoder is ffmpeg: `rtmp://...` targets (Twitch, YouTube, a local nginx-rtmp) go out as
 * FLV, anything else is a file (.mp4 / .mkv / .flv by extension).
 *
 * STOPGAP, by the monorepo's PARENA-first rule ("third-party tools are stopgaps and must say so"): the encode and
 * the RTMP mux are ffmpeg's, not ours. Replacing them with a PARENA-native encoder + muxer is tracked in
 * EMILY/BACKLOG.md. What IS ours, and what OBS was doing, is the part this file and camera_rig.h own: the scene
 * (the camera rig + director), the switching, and the capture. `SHANKPIT_STREAM_SINK_CMD` replaces the encoder
 * command entirely (any program that reads raw rgb24 WxH on stdin) -- used by the tests, and a hook for other
 * encoders.
 *
 * The target ends up in a shell command, so it is whitelisted hard: [A-Za-z0-9 : / . _ - ? = & % @ + , ~] only,
 * quoted, and never empty. Anything else is refused, not escaped. */
#ifndef STREAM_OUT_H
#define STREAM_OUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#endif

typedef struct {
    FILE *pipe;
    int w, h, fps;
    unsigned char *flip;      /* row-flipped copy (glReadPixels is bottom-up, video is top-down) */
    unsigned long frames;
    unsigned long last_push_ms;
    int failed;               /* the encoder went away; stop pushing */
    char target[256];
} StreamOut;

static inline int stream_out_target_ok(const char *t) {
    if (!t || !t[0]) return 0;
    size_t n = strlen(t);
    if (n >= 256) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == ':' || c == '/' || c == '.' || c == '_' || c == '-' || c == '?' || c == '=' ||
                 c == '&' || c == '%' || c == '@' || c == '+' || c == ',' || c == '~';
        if (!ok) return 0;
    }
    return 1;
}

/* A bundled ffmpeg (the Windows release zip ships ffmpeg.exe next to the game, card #535). The game calls
 * stream_out_set_ffmpeg() with its path if the file exists; otherwise "ffmpeg" is looked up on PATH. The path ends up in
 * a shell command, so it gets the same hard whitelist as the target (plus \\ and space) and is quoted. */
static char g_stream_ffmpeg[512] = "";
static inline int stream_out_set_ffmpeg(const char *path) {
    g_stream_ffmpeg[0] = '\0';
    if (!path || !path[0] || strlen(path) >= sizeof(g_stream_ffmpeg)) return 0;
    for (const char *c = path; *c; c++) {
        char ch = *c;
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                 ch == ':' || ch == '/' || ch == '\\' || ch == '.' || ch == '_' || ch == '-' || ch == ' ' || ch == '+' || ch == '~';
        if (!ok) return 0;
    }
    snprintf(g_stream_ffmpeg, sizeof(g_stream_ffmpeg), "%s", path);
    return 1;
}
static inline const char *stream_out_ffmpeg_cmd(void) { return g_stream_ffmpeg[0] ? g_stream_ffmpeg : "ffmpeg"; }

static inline int stream_out_is_rtmp(const char *t) { return strncmp(t, "rtmp://", 7) == 0 || strncmp(t, "rtmps://", 8) == 0; }

/* stream_out_build_cmd -- the encoder command line for a target (ffmpeg). Returns 0 and leaves cmd empty if the
 * target is not acceptable. */
static inline int stream_out_build_cmd(char *cmd, size_t cap, int w, int h, int fps, const char *target) {
    cmd[0] = '\0';
    if (!stream_out_target_ok(target) || w < 16 || h < 16 || fps < 1 || fps > 120) return 0;
    const char *container = "mp4";
    size_t n = strlen(target);
    if (stream_out_is_rtmp(target)) container = "flv";
    else if (n > 4 && strcmp(target + n - 4, ".mkv") == 0) container = "matroska";
    else if (n > 4 && strcmp(target + n - 4, ".flv") == 0) container = "flv";
    int len = snprintf(cmd, cap,
        "\"%s\" -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
        "-c:v libx264 -preset veryfast -tune zerolatency -pix_fmt yuv420p -g %d -b:v 4500k -an %s -f %s \"%s\"",
        stream_out_ffmpeg_cmd(), w, h, fps, fps * 2, strcmp(container, "mp4") == 0 && !stream_out_is_rtmp(target) ? "-movflags +faststart" : "", container, target);
    if (len < 0 || (size_t)len >= cap) { cmd[0] = '\0'; return 0; }
    return 1;
}

/* stream_out_open -- start the encoder. `sink_cmd` (may be NULL) overrides the ffmpeg command. Returns 1 on success. */
static inline int stream_out_open(StreamOut *so, int w, int h, int fps, const char *target, const char *sink_cmd) {
    memset(so, 0, sizeof(*so));
    char cmd[1024];
    if (sink_cmd && sink_cmd[0]) snprintf(cmd, sizeof(cmd), "%s", sink_cmd);
    else if (!stream_out_build_cmd(cmd, sizeof(cmd), w, h, fps, target)) return 0;
    if (w < 16 || h < 16) return 0;
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);                 /* a dead encoder is an error return, not a process kill */
#endif
    so->flip = (unsigned char *)malloc((size_t)w * (size_t)h * 3u);
    if (!so->flip) return 0;
#ifdef _WIN32
    {   /* cmd /c strips the outer quotes of a command that starts with a quote -- wrap it once more so a quoted ffmpeg path survives */
        char wrapped[1100];
        snprintf(wrapped, sizeof(wrapped), "\"%s\"", cmd);
        so->pipe = _popen(wrapped, "wb");
    }
#else
    so->pipe = popen(cmd, "w");
#endif
    if (!so->pipe) { free(so->flip); so->flip = NULL; return 0; }
    so->w = w; so->h = h; so->fps = fps;
    snprintf(so->target, sizeof(so->target), "%s", target ? target : "");
    return 1;
}

/* stream_out_due -- true when the next frame should be pushed (paces a 60 fps render down to the stream fps). */
static inline int stream_out_due(const StreamOut *so, unsigned long now_ms) {
    return so->pipe && !so->failed && (so->frames == 0 || now_ms - so->last_push_ms >= (unsigned long)(1000 / so->fps));
}

/* stream_out_push -- one frame of bottom-up RGB24 (exactly what glReadPixels(GL_RGB, GL_UNSIGNED_BYTE) returns). */
static inline int stream_out_push(StreamOut *so, const unsigned char *rgb_bottom_up, unsigned long now_ms) {
    if (!so->pipe || so->failed) return 0;
    size_t row = (size_t)so->w * 3u;
    for (int y = 0; y < so->h; y++) memcpy(so->flip + (size_t)y * row, rgb_bottom_up + (size_t)(so->h - 1 - y) * row, row);
    size_t want = row * (size_t)so->h;
    if (fwrite(so->flip, 1, want, so->pipe) != want || fflush(so->pipe) != 0) { so->failed = 1; return 0; }
    so->frames++;
    so->last_push_ms = now_ms;
    return 1;
}

static inline int stream_out_close(StreamOut *so) {
    int rc = 0;
    if (so->pipe) {
#ifdef _WIN32
        rc = _pclose(so->pipe);
#else
        rc = pclose(so->pipe);
#endif
        so->pipe = NULL;
    }
    free(so->flip); so->flip = NULL;
    return rc;
}

/* stream_out_ffmpeg_available -- is there an ffmpeg to encode with? (Checked when the user turns streaming on, so a
 * missing encoder is a clear message instead of a silently empty stream.) */
static inline int stream_out_ffmpeg_available(void) {
    if (g_stream_ffmpeg[0]) return 1;   /* the bundled copy, already checked to exist by the caller */
#ifdef _WIN32
    FILE *p = _popen("ffmpeg -version >NUL 2>&1 && echo ok", "r");
#else
    FILE *p = popen("command -v ffmpeg >/dev/null 2>&1 && echo ok", "r");
#endif
    if (!p) return 0;
    char b[8] = {0};
    size_t n = fread(b, 1, sizeof(b) - 1, p);
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return n >= 2 && b[0] == 'o' && b[1] == 'k';
}

#endif
