/* lab_station_test.c -- host glue over the PARENA-generated rules + the level_boxes lab_stations parser (SECTION 592).
 * make test-lab-station */
#include "lab_station_host.h"
#include "../world/level_boxes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Phone p; phone_init(&p);
    LabStationResult r;
    /* nothing to work with */
    lab_station_use(&p, LABST_SPLICE, &r); assert(!r.used && p.clone_count == 0);
    /* PCR needs a sample too */
    p.samples[1] = 1;
    lab_station_use(&p, LABST_PCR, &r); assert(r.used && p.samples[1] == 2 && r.base == 1);
    /* centrifuge: 2 of base 1 -> 1 of base 2 */
    lab_station_use(&p, LABST_CENTRIFUGE, &r); assert(r.used && p.samples[1] == 0 && p.samples[2] == 1);
    /* splice base 2 -> a clone of base 2 */
    p.lab_trait = 1;
    lab_station_use(&p, LABST_SPLICE, &r); assert(r.used && p.samples[2] == 0 && p.clone_count == 1 && p.clones[0] == 2 && p.clone_traits[0] == 1);
    /* vat decants the clone and heals */
    lab_station_use(&p, LABST_VAT, &r); assert(r.used && r.heal == 25 && p.clone_count == 0);
    lab_station_use(&p, LABST_VAT, &r); assert(!r.used && r.heal == 0);
    /* console opens the phone; fridge reads out */
    lab_station_use(&p, LABST_CONSOLE, &r); assert(r.used && r.open_phone);
    lab_station_use(&p, LABST_FRIDGE, &r); assert(r.used && strstr(r.msg, "FRIDGE"));
    lab_station_use(&p, 9, &r); assert(!r.used);
    /* clone slots full -> splice refuses */
    p.clone_count = BP_CLONES; p.samples[0] = 5;
    lab_station_use(&p, LABST_SPLICE, &r); assert(!r.used && p.samples[0] == 5);

    /* the level_boxes parser, on a literal and on the real exported LAB level */
    CustomLevelData *lvl = (CustomLevelData *)malloc(sizeof *lvl);
    assert(lvl);
    const char *json = "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,\"walls\":[],"
        "\"lab_stations\":[{\"kind\":\"vat\",\"x\":1,\"y\":2,\"z\":3},{\"kind\":\"bogus\",\"x\":0,\"y\":0,\"z\":0},{\"kind\":\"console\",\"x\":-4,\"y\":5,\"z\":6}]}";
    assert(level_boxes_parse_json(json, lvl));
    assert(lvl->lab_station_count == 2 && lvl->lab_stations[0].kind == LABST_VAT && lvl->lab_stations[1].kind == LABST_CONSOLE && lvl->lab_stations[1].x == -4.0f);
    if (level_boxes_load_from_file("var/lab/lab.json", lvl)) {
        assert(lvl->lab_station_count == 8);
        int seen[6] = {0};
        for (int i = 0; i < lvl->lab_station_count; i++) seen[lvl->lab_stations[i].kind]++;
        for (int k = 0; k < 6; k++) assert(seen[k] >= 1);
        printf("real var/lab/lab.json: %d stations, all 6 kinds present\n", lvl->lab_station_count);
    } else printf("var/lab/lab.json not present: skipped real-level check\n");
    free(lvl);
    printf("lab_station_test OK\n");
    return 0;
}
