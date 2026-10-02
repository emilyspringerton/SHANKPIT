/* stream_out_test.c -- headless checks for stream_out.h (#458a). No ffmpeg needed: the encoder command is
 * replaced by a plain file sink. make test-stream-out */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stream_out.h"

int main(void) {
    /* ---- target whitelist: anything shell-meaningful is refused ---- */
    assert(stream_out_target_ok("rtmp://live.twitch.tv/app/live_123_abcDEF"));
    assert(stream_out_target_ok("rtmp://127.0.0.1:1935/live/shankpit?key=a&b=c"));
    assert(stream_out_target_ok("out/match-01.mp4"));
    assert(!stream_out_target_ok(""));
    assert(!stream_out_target_ok(NULL));
    assert(!stream_out_target_ok("a.mp4; rm -rf /"));
    assert(!stream_out_target_ok("a.mp4 | nc evil 1"));
    assert(!stream_out_target_ok("$(whoami).mp4"));
    assert(!stream_out_target_ok("`id`.mp4"));
    assert(!stream_out_target_ok("a\"b.mp4"));
    assert(!stream_out_target_ok("a'b.mp4"));
    assert(!stream_out_target_ok("a b.mp4"));
    assert(!stream_out_target_ok("a\nb.mp4"));
    assert(!stream_out_target_ok("a>b.mp4"));

    /* ---- ffmpeg command line ---- */
    char cmd[1024];
    assert(stream_out_build_cmd(cmd, sizeof(cmd), 1280, 720, 30, "rtmp://x.example/live/key"));
    assert(strstr(cmd, "-f flv \"rtmp://x.example/live/key\"") && strstr(cmd, "-s 1280x720") && strstr(cmd, "-r 30") && strstr(cmd, "-g 60"));
    assert(!strstr(cmd, "faststart"));                              /* a live stream has no moov atom to move */
    assert(stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 25, "match.mp4"));
    assert(strstr(cmd, "-f mp4") && strstr(cmd, "faststart"));
    assert(stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 25, "match.mkv") && strstr(cmd, "-f matroska"));
    assert(!stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 25, "bad;name.mp4") && cmd[0] == '\0');
    assert(!stream_out_build_cmd(cmd, sizeof(cmd), 8, 8, 25, "a.mp4"));
    assert(!stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 0, "a.mp4"));
    assert(!stream_out_build_cmd(cmd, 20, 640, 360, 25, "a.mp4"));  /* does not fit: refused, never truncated */

    /* ---- bundled ffmpeg path (#535) ---- */
    assert(stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 25, "a.mp4") && strncmp(cmd, "\"ffmpeg\" ", 9) == 0);
    assert(stream_out_set_ffmpeg("C:\\Games\\ShankPit Client\\ffmpeg.exe"));
    assert(stream_out_build_cmd(cmd, sizeof(cmd), 640, 360, 25, "a.mp4") && strncmp(cmd, "\"C:\\Games\\ShankPit Client\\ffmpeg.exe\" ", 38) == 0);
    assert(stream_out_ffmpeg_available());
    assert(!stream_out_set_ffmpeg("x\";calc;\"") && !g_stream_ffmpeg[0]);   /* shell metacharacters: refused, falls back to PATH */

    /* ---- capture -> pipe: frames arrive intact and top-down ---- */
    const int W = 32, H = 16;
    const char *out = "/tmp/shankpit_stream_out_test.raw";
    remove(out);
    char sink[200]; snprintf(sink, sizeof(sink), "cat > %s", out);
    StreamOut so;
    assert(stream_out_open(&so, W, H, 30, "ignored.mp4", sink));
    unsigned char *frame = (unsigned char *)malloc((size_t)W * H * 3);
    for (int f = 0; f < 3; f++) {
        /* bottom-up frame: row r (from the bottom) is filled with the value r + 10*f */
        for (int r = 0; r < H; r++) memset(frame + (size_t)r * W * 3, r + 10 * f, (size_t)W * 3);
        assert(stream_out_due(&so, 1000ul + 40ul * (unsigned)f) || f > 0);
        assert(stream_out_push(&so, frame, 1000ul + 40ul * (unsigned)f));
    }
    assert(so.frames == 3);
    assert(!stream_out_due(&so, so.last_push_ms + 5));               /* paced: too soon */
    assert(stream_out_due(&so, so.last_push_ms + 40));
    assert(stream_out_close(&so) == 0);
    FILE *f = fopen(out, "rb");
    assert(f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    assert(sz == (long)W * H * 3 * 3);                               /* exactly three whole frames */
    unsigned char *got = (unsigned char *)malloc((size_t)sz);
    assert(fread(got, 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    /* frame 0 in the file is top-down: file row 0 is the TOP, i.e. the source's last row (H-1) */
    assert(got[0] == H - 1);
    assert(got[(size_t)(H - 1) * W * 3] == 0);                       /* file's last row = source's first row */
    /* frame 2 starts after two frames and carries its +20 offset */
    assert(got[(size_t)W * H * 3 * 2] == H - 1 + 20);
    free(got); free(frame); remove(out);

    /* ---- a dead encoder is an error return, not a crash ---- */
    StreamOut dead;
    assert(stream_out_open(&dead, W, H, 30, "x.mp4", "true"));       /* exits at once, closing the pipe */
    unsigned char *z = (unsigned char *)calloc((size_t)W * H * 3, 1);
    int pushed_ok = 1;
    for (int i = 0; i < 400 && pushed_ok; i++) pushed_ok = stream_out_push(&dead, z, 100ul * (unsigned)i);   /* fills the pipe buffer, then EPIPE */
    assert(!pushed_ok && dead.failed);
    assert(!stream_out_push(&dead, z, 99999));                       /* stays failed, no more writes */
    stream_out_close(&dead);
    free(z);

    /* refused targets never spawn anything */
    StreamOut bad;
    assert(!stream_out_open(&bad, W, H, 30, "a.mp4; echo pwned", NULL));
    assert(bad.pipe == NULL);

    printf("stream_out_test OK (ffmpeg %s on this machine)\n", stream_out_ffmpeg_available() ? "IS present" : "is not installed");
    return 0;
}
