#include <stddef.h>
#include "editor_app.h"

int main(int argc, char **argv) {
    const char *scene_path = NULL;
    if (argc > 1) {
        scene_path = argv[1];
    }
    return editor_app_run(scene_path);
}
