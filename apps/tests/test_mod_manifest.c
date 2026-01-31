#include <stdio.h>
#include <string.h>
#include "../../packages/mods/mod_manifest.h"

int main(void) {
    const char *fixture = "{ \"id\": \"test_mod\", \"name\": \"Test\", \"version\": \"0.1\", \"api_version\": \"1.0\", \"type\": \"native\", \"entry\": \"test.so\", \"priority\": 5, \"capabilities\": [\"hooks\", \"console\"] }";
    ModManifest manifest = {0};
    char tmp_path[] = "/tmp/mod_manifest_test.json";
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        printf("❌ FAIL: open temp file\n");
        return 1;
    }
    fputs(fixture, fp);
    fclose(fp);
    if (!mod_manifest_load(tmp_path, &manifest)) {
        printf("❌ FAIL: load manifest\n");
        return 1;
    }
    if (strcmp(manifest.id, "test_mod") != 0 || manifest.priority != 5) {
        printf("❌ FAIL: manifest fields\n");
        return 1;
    }
    printf("✅ PASS: mod manifest\n");
    return 0;
}
