/* level_boxes_density_test.c -- a NOCK material's "density" (kg/m^3) reaches LevelBoxMaterial.density;
 * absent / non-positive means 0 = "use the engine default for the material kind". make test-brick-density */
#include "level_boxes.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    const char *json =
        "{\"name\":\"T\",\"width\":40,\"height\":10,\"depth\":40,\"ground_plane_enabled\":true,\"ground_plane_squares\":1,"
        "\"materials\":[{\"name\":\"glass\",\"shader_name\":\"glass\",\"specular\":0.05,\"shininess\":8,\"friction\":0.3,\"density\":2500},"
        "{\"name\":\"brick\",\"shader_name\":\"standard\",\"specular\":0.04,\"shininess\":6,\"friction\":0.3},"
        "{\"name\":\"junk\",\"shader_name\":\"standard\",\"specular\":0,\"shininess\":8,\"friction\":0.3,\"density\":-5}],"
        "\"walls\":[]}";
    CustomLevelData lvl;
    assert(level_boxes_parse_json(json, &lvl));
    assert(lvl.material_count == 3);
    assert(fabsf(lvl.materials[0].density - 2500.0f) < 1e-3f);   /* configured on the NOCK material */
    assert(lvl.materials[1].density == 0.0f);                    /* a pre-density export: engine default */
    assert(lvl.materials[2].density == 0.0f);                    /* nonsense is ignored, not trusted */
    printf("level_boxes_density_test OK\n");
    return 0;
}
