#include <assert.h>
#include <stdio.h>
#include "edit_sync.h"
int main(void) {
    const char *j = "{\"events\":[{\"seq\":2,\"source\":\"nock\",\"kind\":\"spawner\",\"data\":{\"x\":1.5,\"y\":2,\"z\":-3,\"yaw\":90},\"at\":\"t\"},"
        "{\"seq\":3,\"source\":\"nock\",\"kind\":\"spawn_mode\",\"data\":{\"mode\":\"crosshair\"},\"at\":\"t\"},"
        "{\"seq\":4,\"source\":\"shankpit\",\"kind\":\"avatar\",\"data\":{\"x\":9,\"y\":1,\"z\":2,\"yaw\":0,\"pitch\":5},\"at\":\"t\"}],"
        "\"id\":\"ab\",\"level_id\":7,\"level_name\":\"x\",\"seq\":4,\"state\":{\"spawn_mode\":\"crosshair\"}}";
    EditSyncEvent ev[8]; int seq = 0;
    int n = edit_sync_parse(j, ev, 8, &seq);
    assert(n == 3 && seq == 4);
    assert(ev[0].seq == 2 && ev[0].from_nock && !strcmp(ev[0].kind, "spawner") && ev[0].x == 1.5f && ev[0].z == -3 && ev[0].yaw == 90);
    assert(!strcmp(ev[1].kind, "spawn_mode") && ev[1].mode_crosshair && !ev[1].has_pos);
    assert(!ev[2].from_nock && ev[2].pitch == 5);
    assert(edit_sync_parse("{\"events\":[],\"seq\":0}", ev, 8, &seq) == 0 && seq == 0);
    puts("edit_sync ok"); return 0;
}
