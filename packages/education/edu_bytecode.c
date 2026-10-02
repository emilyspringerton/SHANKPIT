#include "edu_bytecode.h"

void edu_bc_init(EduBytecodeWriter *w, unsigned char *buffer, int capacity) {
    w->data = buffer;
    w->len = 0;
    w->cap = capacity;
    w->error = 0;
}

int edu_bc_emit_u8(EduBytecodeWriter *w, uint8_t v) {
    if (w->len + 1 > w->cap) {
        w->error = 1;
        return 0;
    }
    w->data[w->len++] = v;
    return 1;
}

int edu_bc_emit_i32(EduBytecodeWriter *w, int v) {
    if (w->len + 4 > w->cap) {
        w->error = 1;
        return 0;
    }
    w->data[w->len++] = (unsigned char)(v & 0xFF);
    w->data[w->len++] = (unsigned char)((v >> 8) & 0xFF);
    w->data[w->len++] = (unsigned char)((v >> 16) & 0xFF);
    w->data[w->len++] = (unsigned char)((v >> 24) & 0xFF);
    return 1;
}

int edu_bc_patch_i32(EduBytecodeWriter *w, int at, int v) {
    if (at < 0 || at + 3 >= w->len) return 0;
    w->data[at] = (unsigned char)(v & 0xFF);
    w->data[at + 1] = (unsigned char)((v >> 8) & 0xFF);
    w->data[at + 2] = (unsigned char)((v >> 16) & 0xFF);
    w->data[at + 3] = (unsigned char)((v >> 24) & 0xFF);
    return 1;
}

int edu_bc_emit_jump(EduBytecodeWriter *w, EduOpcode op) {
    if (!edu_bc_emit_u8(w, (uint8_t)op)) return -1;
    int patch_at = w->len;
    if (!edu_bc_emit_i32(w, 0)) return -1;
    return patch_at;
}
