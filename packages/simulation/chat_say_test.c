/* chat_say_test.c -- real, direct coverage for chat_say.h's own range/length decisions, against
 * the real, live PARENA-compiled chat_rules.c (not a mock) -- same real cross-package shape
 * party_test.c already establishes. Faithfully checks GFD's own real server/chat/chat.go
 * "say" behavior, reverse-ported.
 */
#include "./chat_say.h"
#include "../common/phone.h"

#include <assert.h>
#include <stdio.h>

/* phone.h's GFD terminal input cap must leave room for a NUL inside the say wire buffer. */
_Static_assert(BP_TERM_INPUT_MAX == CHAT_SAY_MAX_TEXT - 1, "phone terminal input cap must be CHAT_SAY_MAX_TEXT-1");

int main(void) {
    assert(chat_say_radius_cm() == 4000);
    assert(chat_max_len() == 200);
    printf("PASS: real constants match GFD's own chat.go (200-byte cap) and this repo's own chosen 40m say radius\n");

    /* Length gate: chat.go's own real Deliver() guard -- non-empty, <= 200 bytes -- AND this
       repo's own smaller fixed wire buffer. */
    assert(!chat_say_len_ok(0));
    assert(!chat_say_len_ok(-1));
    assert(chat_say_len_ok(1));
    assert(chat_say_len_ok(CHAT_SAY_MAX_TEXT));
    assert(!chat_say_len_ok(CHAT_SAY_MAX_TEXT + 1));
    assert(!chat_say_len_ok(200)); /* chat.go's own cap would allow it, but it doesn't fit the wire buffer */
    printf("PASS: empty/negative/over-wire-buffer messages rejected; in-range messages accepted\n");

    /* Range: at or under the say radius hears it; strictly beyond does not. Sender always hears
       their own say (distance 0 by construction). */
    assert(chat_say_hears(0));
    assert(chat_say_hears(3999));
    assert(chat_say_hears(4000));
    assert(!chat_say_hears(4001));
    printf("PASS: say is heard at or under the real radius, not beyond it; distance 0 (self) always hears\n");

    printf("\nALL PASS\n");
    return 0;
}
