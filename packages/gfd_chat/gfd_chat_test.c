#include <assert.h>
#include "gfd_chat.h"
int main(void) {
    const char *j = "[{\"id\":7,\"channel\":\"shankpit\",\"sender_name\":\"Ty{ler\",\"sender_source\":\"mud\",\"body\":\"hi \\\"you\\\" }\",\"created_at\":\"x\"},{\"id\":8,\"channel\":\"say\",\"sender_name\":\"B\",\"sender_source\":\"gfd_server\",\"body\":\"z\"}]";
    size_t len = strlen(j), pos = 0, s, e; char v[128];
    assert(gfd_chat_next_object(j, len, &pos, &s, &e));
    assert(gfd_chat_int_field(j + s, e - s, "id") == 7);
    assert(gfd_chat_str_field(j + s, e - s, "sender_name", v, sizeof v) && !strcmp(v, "Ty{ler"));
    assert(gfd_chat_str_field(j + s, e - s, "body", v, sizeof v) && !strcmp(v, "hi \"you\" }"));
    assert(gfd_chat_next_object(j, len, &pos, &s, &e));
    assert(gfd_chat_int_field(j + s, e - s, "id") == 8);
    assert(!gfd_chat_next_object(j, len, &pos, &s, &e));
    char o[64]; gfd_chat_json_escape("a\"b\\c", o, sizeof o); assert(!strcmp(o, "a\\\"b\\\\c"));
    /* echo suppression */
    g_gfd_chat.echo_n = 0; snprintf(g_gfd_chat.echo[g_gfd_chat.echo_n++], sizeof g_gfd_chat.echo[0], "N\nB");
    assert(gfd_chat_is_echo("N", "B") && !gfd_chat_is_echo("N", "B"));
    puts("gfd_chat_test: ok"); return 0;
}
