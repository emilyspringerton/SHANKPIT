/* eduvm_snippets_test.c -- snippet JSON parse/serialize/placement (card #495). */
#include <stdio.h>
#include <string.h>
#include "eduvm_snippets.h"
static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("ok:   %s\n", m); } while (0)

int main(void) {
    EduSnippet sn[8];
    /* an IDUNA-shaped payload: extra fields, nested value, escapes, a \u escape */
    const char *remote = "[{\"id\":1,\"name\":\"open-all\",\"widget_name\":\"ORB\",\"source\":\"open_gate();\\nraise_bridge();\\n\",\"meta\":{\"a\":[1,2,{\"x\":\"}\"}]},\"created_at\":\"t\"},"
                         "{\"source\":\"print(1);\",\"name\":\"q\\u0041\"}]";
    int n = edu_snippets_parse(remote, sn, 8);
    CHECK(n == 2, "parses two snippets from an IDUNA-shaped array");
    CHECK(strcmp(sn[0].name, "open-all") == 0 && strcmp(sn[0].source, "open_gate();\nraise_bridge();\n") == 0, "decodes \\n escapes and skips unknown/nested fields");
    CHECK(strcmp(sn[1].name, "qA") == 0, "decodes \\u0041 and field order doesn't matter");
    CHECK(edu_snippets_parse("[]", sn, 8) == 0 && edu_snippets_parse("not json", sn, 8) == 0 && edu_snippets_parse("", sn, 8) == 0, "empty / garbage -> 0");
    CHECK(edu_snippets_parse("[{\"name\":\"x\"}]", sn, 8) == 0, "entry without source is skipped");
    CHECK(edu_snippets_parse("[{\"name\":\"a\",\"source\":\"1\"},{\"name\":\"b\",\"source\":\"2\"},{\"name\":\"c\",\"source\":\"3\"}]", sn, 2) == 2, "max caps the count");
    {
        static char big[EDU_MAX_SCRIPT_TEXT + 64 + 64];
        char *w = big; w += sprintf(w, "[{\"name\":\"big\",\"source\":\"");
        for (int i = 0; i < EDU_MAX_SCRIPT_TEXT + 10; i++) *w++ = 'a';
        sprintf(w, "\"}]");
        CHECK(edu_snippets_parse(big, sn, 8) == 0, "oversized source is skipped, not truncated");
    }
    /* placement */
    static EduScriptSystem sys;
    memset(&sys, 0, sizeof sys);
    snprintf(sys.slots[0].name, 64, "P0"); snprintf(sys.slots[0].source, EDU_MAX_SCRIPT_TEXT, "print(0);");
    snprintf(sys.slots[1].name, 64, "P1"); snprintf(sys.slots[1].source, EDU_MAX_SCRIPT_TEXT, "print(1);");
    CHECK(edu_snippets_fill_empty(&sys, sn, 0, 0) == 0, "fill with nothing places nothing");
    n = edu_snippets_parse(remote, sn, 8);
    CHECK(edu_snippets_fill_empty(&sys, sn, n, 0) == 2, "fill_empty places both");
    CHECK(strcmp(sys.slots[0].name, "P0") == 0 && strcmp(sys.slots[2].name, "open-all") == 0 && strcmp(sys.slots[3].name, "qA") == 0, "fill_empty never overwrites occupied slots");
    /* serialize -> parse round trip */
    static char json[16384];
    int len = edu_snippets_serialize(&sys, json, sizeof json);
    CHECK(len > 0, "serialize succeeds");
    EduSnippet back[8];
    int m = edu_snippets_parse(json, back, 8);
    CHECK(m == 4, "round trip keeps the four non-empty slots");
    CHECK(strcmp(back[2].source, "open_gate();\nraise_bridge();\n") == 0 && strcmp(back[3].name, "qA") == 0, "round trip preserves text exactly");
    char tiny[8];
    CHECK(edu_snippets_serialize(&sys, tiny, sizeof tiny) == -1, "serialize reports a too-small buffer");
    /* positional load */
    static EduScriptSystem sys2; memset(&sys2, 0, sizeof sys2);
    CHECK(edu_snippets_load_into(&sys2, back, m) == 4 && strcmp(sys2.slots[3].name, "qA") == 0, "load_into restores slots positionally");
    printf(failures ? "\n%d FAILED\n" : "\nAll EduVM snippet checks passed.\n", failures);
    return failures ? 1 : 0;
}
