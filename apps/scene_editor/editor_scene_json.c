#include "editor_scene_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Tiny local JSON parser (jsmn-style) to keep the editor standalone and
 * avoid touching runtime serialization paths during the low-risk init pass.
 */
typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT = 1,
    JSMN_ARRAY = 2,
    JSMN_STRING = 3,
    JSMN_PRIMITIVE = 4
} jsmntype_t;

typedef struct {
    jsmntype_t type;
    int start;
    int end;
    int size;
    int parent;
} jsmntok_t;

typedef struct {
    unsigned int pos;
    unsigned int toknext;
    int toksuper;
} jsmn_parser;

static void jsmn_init(jsmn_parser *parser) {
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens, size_t num_tokens) {
    if (parser->toknext >= num_tokens) return NULL;
    jsmntok_t *tok = &tokens[parser->toknext++];
    tok->start = tok->end = -1;
    tok->size = 0;
    tok->parent = -1;
    tok->type = JSMN_UNDEFINED;
    return tok;
}

static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end) {
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)parser->pos;
    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];
        if (c == ',' || c == ']' || c == '}' || isspace((unsigned char)c)) {
            break;
        }
        if (c < 32 || c >= 127) {
            parser->pos = (unsigned int)start;
            return -1;
        }
    }
    jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (!token) {
        parser->pos = (unsigned int)start;
        return -1;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, (int)parser->pos);
    token->parent = parser->toksuper;
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
    int start = (int)parser->pos;
    parser->pos++;
    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];
        if (c == '"') {
            jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (!token) {
                parser->pos = (unsigned int)start;
                return -1;
            }
            jsmn_fill_token(token, JSMN_STRING, start + 1, (int)parser->pos);
            token->parent = parser->toksuper;
            return 0;
        }
        if (c == '\\') parser->pos++;
    }
    parser->pos = (unsigned int)start;
    return -1;
}

static int jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens) {
    for (; parser->pos < len; parser->pos++) {
        char c = js[parser->pos];
        jsmntok_t *token;
        int i;
        switch (c) {
            case '{':
            case '[':
                token = jsmn_alloc_token(parser, tokens, num_tokens);
                if (!token) return -1;
                if (parser->toksuper != -1) tokens[parser->toksuper].size++;
                token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
                token->start = (int)parser->pos;
                token->parent = parser->toksuper;
                parser->toksuper = (int)(parser->toknext - 1);
                break;
            case '}':
            case ']': {
                jsmntype_t type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
                for (i = (int)parser->toknext - 1; i >= 0; i--) {
                    token = &tokens[i];
                    if (token->start != -1 && token->end == -1) {
                        if (token->type != type) return -1;
                        token->end = (int)parser->pos + 1;
                        parser->toksuper = token->parent;
                        break;
                    }
                }
                if (i == -1) return -1;
                break;
            }
            case '"':
                if (jsmn_parse_string(parser, js, len, tokens, num_tokens) < 0) return -1;
                if (parser->toksuper != -1) tokens[parser->toksuper].size++;
                break;
            case '\t': case '\r': case '\n': case ' ': case ':': case ',':
                break;
            default:
                if (jsmn_parse_primitive(parser, js, len, tokens, num_tokens) < 0) return -1;
                if (parser->toksuper != -1) tokens[parser->toksuper].size++;
                break;
        }
    }
    for (unsigned int i = parser->toknext; i > 0; i--) {
        if (tokens[i - 1].start != -1 && tokens[i - 1].end == -1) return -1;
    }
    return (int)parser->toknext;
}

static int token_str_eq(const char *json, const jsmntok_t *tok, const char *s) {
    int len = tok->end - tok->start;
    return (int)strlen(s) == len && strncmp(json + tok->start, s, (size_t)len) == 0;
}

static int token_to_int(const char *json, const jsmntok_t *tok, int *out) {
    char buf[64];
    int len = tok->end - tok->start;
    if (len <= 0 || len >= (int)sizeof(buf)) return 0;
    memcpy(buf, json + tok->start, (size_t)len);
    buf[len] = '\0';
    *out = atoi(buf);
    return 1;
}

static int token_to_float(const char *json, const jsmntok_t *tok, float *out) {
    char buf[64];
    int len = tok->end - tok->start;
    if (len <= 0 || len >= (int)sizeof(buf)) return 0;
    memcpy(buf, json + tok->start, (size_t)len);
    buf[len] = '\0';
    *out = (float)atof(buf);
    return 1;
}

static int token_to_string(const char *json, const jsmntok_t *tok, char *dst, size_t dst_size) {
    int len = tok->end - tok->start;
    if (len < 0 || (size_t)len >= dst_size) return 0;
    memcpy(dst, json + tok->start, (size_t)len);
    dst[len] = '\0';
    return 1;
}

static int skip_token_tree(const jsmntok_t *tokens, int index, int count) {
    int start = index;
    int end = index + 1;
    while (end < count && tokens[end].start < tokens[start].end) {
        end++;
    }
    return end;
}

static int parse_float_array(const char *json, const jsmntok_t *tokens, int tok_count, int arr_index, float *out, int expected) {
    if (arr_index < 0 || arr_index >= tok_count || tokens[arr_index].type != JSMN_ARRAY) return 0;
    int idx = arr_index + 1;
    for (int i = 0; i < expected; i++) {
        if (idx >= tok_count || !token_to_float(json, &tokens[idx], &out[i])) return 0;
        idx = skip_token_tree(tokens, idx, tok_count);
    }
    return 1;
}

int editor_scene_json_load(const char *path, EditorSceneAsset *out_scene) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    char *json = (char*)malloc((size_t)len + 1);
    if (!json) {
        fclose(f);
        return 0;
    }
    if (fread(json, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(json);
        return 0;
    }
    fclose(f);
    json[len] = '\0';

    enum { MAX_TOKENS = 8192 };
    jsmntok_t *tokens = (jsmntok_t*)calloc(MAX_TOKENS, sizeof(jsmntok_t));
    if (!tokens) {
        free(json);
        return 0;
    }

    jsmn_parser parser;
    jsmn_init(&parser);
    int tok_count = jsmn_parse(&parser, json, (size_t)len, tokens, MAX_TOKENS);
    if (tok_count <= 0 || tokens[0].type != JSMN_OBJECT) {
        free(tokens);
        free(json);
        return 0;
    }

    editor_scene_asset_init(out_scene);
    int i = 1;
    while (i < tok_count && tokens[i].start < tokens[0].end) {
        if (tokens[i].type != JSMN_STRING) {
            i++;
            continue;
        }
        int key = i;
        int val = i + 1;
        if (val >= tok_count) break;

        if (token_str_eq(json, &tokens[key], "version")) {
            token_to_int(json, &tokens[val], &out_scene->version);
        } else if (token_str_eq(json, &tokens[key], "scene_name")) {
            token_to_string(json, &tokens[val], out_scene->scene_name, sizeof(out_scene->scene_name));
        } else if (token_str_eq(json, &tokens[key], "boxes") && tokens[val].type == JSMN_ARRAY) {
            int bi = val + 1;
            while (bi < tok_count && tokens[bi].start < tokens[val].end && out_scene->box_count < EDITOR_MAX_BOXES) {
                if (tokens[bi].type != JSMN_OBJECT) {
                    bi = skip_token_tree(tokens, bi, tok_count);
                    continue;
                }
                EditorSceneBox *box = &out_scene->boxes[out_scene->box_count++];
                memset(box, 0, sizeof(*box));
                box->flags.solid = 1;
                box->flags.visible = 1;
                strcpy(box->name, "box");
                box->size[0] = box->size[1] = box->size[2] = 1.0f;
                box->color[0] = box->color[1] = box->color[2] = box->color[3] = 1.0f;

                int fi = bi + 1;
                while (fi < tok_count && tokens[fi].start < tokens[bi].end) {
                    int fk = fi;
                    int fv = fi + 1;
                    if (fv >= tok_count) break;
                    if (token_str_eq(json, &tokens[fk], "id")) token_to_int(json, &tokens[fv], &box->id);
                    else if (token_str_eq(json, &tokens[fk], "name")) token_to_string(json, &tokens[fv], box->name, sizeof(box->name));
                    else if (token_str_eq(json, &tokens[fk], "position")) parse_float_array(json, tokens, tok_count, fv, box->position, 3);
                    else if (token_str_eq(json, &tokens[fk], "size")) parse_float_array(json, tokens, tok_count, fv, box->size, 3);
                    else if (token_str_eq(json, &tokens[fk], "rotation_y_degrees")) token_to_float(json, &tokens[fv], &box->rotation_y_degrees);
                    else if (token_str_eq(json, &tokens[fk], "color")) parse_float_array(json, tokens, tok_count, fv, box->color, 4);
                    else if (token_str_eq(json, &tokens[fk], "flags") && tokens[fv].type == JSMN_OBJECT) {
                        int gi = fv + 1;
                        while (gi < tok_count && tokens[gi].start < tokens[fv].end) {
                            int gk = gi;
                            int gv = gi + 1;
                            if (gv >= tok_count) break;
                            if (token_str_eq(json, &tokens[gk], "solid")) token_to_int(json, &tokens[gv], &box->flags.solid);
                            else if (token_str_eq(json, &tokens[gk], "visible")) token_to_int(json, &tokens[gv], &box->flags.visible);
                            gi = skip_token_tree(tokens, gv, tok_count);
                        }
                    }
                    fi = skip_token_tree(tokens, fv, tok_count);
                }
                bi = skip_token_tree(tokens, bi, tok_count);
            }
        } else if (token_str_eq(json, &tokens[key], "stage_cameras") && tokens[val].type == JSMN_ARRAY) {
            int ci = val + 1;
            while (ci < tok_count && tokens[ci].start < tokens[val].end && out_scene->stage_camera_count < EDITOR_MAX_STAGE_CAMERAS) {
                if (tokens[ci].type != JSMN_OBJECT) {
                    ci = skip_token_tree(tokens, ci, tok_count);
                    continue;
                }
                EditorStageCamera *cam = &out_scene->stage_cameras[out_scene->stage_camera_count++];
                memset(cam, 0, sizeof(*cam));
                strcpy(cam->name, "camera");
                cam->distance = 25.0f;
                cam->pitch_degrees = 20.0f;
                cam->yaw_degrees = 0.0f;
                cam->fov_degrees = 60.0f;

                int fi = ci + 1;
                while (fi < tok_count && tokens[fi].start < tokens[ci].end) {
                    int fk = fi;
                    int fv = fi + 1;
                    if (fv >= tok_count) break;
                    if (token_str_eq(json, &tokens[fk], "id")) token_to_int(json, &tokens[fv], &cam->id);
                    else if (token_str_eq(json, &tokens[fk], "name")) token_to_string(json, &tokens[fv], cam->name, sizeof(cam->name));
                    else if (token_str_eq(json, &tokens[fk], "target")) parse_float_array(json, tokens, tok_count, fv, cam->target, 3);
                    else if (token_str_eq(json, &tokens[fk], "distance")) token_to_float(json, &tokens[fv], &cam->distance);
                    else if (token_str_eq(json, &tokens[fk], "yaw_degrees")) token_to_float(json, &tokens[fv], &cam->yaw_degrees);
                    else if (token_str_eq(json, &tokens[fk], "pitch_degrees")) token_to_float(json, &tokens[fv], &cam->pitch_degrees);
                    else if (token_str_eq(json, &tokens[fk], "fov_degrees")) token_to_float(json, &tokens[fv], &cam->fov_degrees);
                    fi = skip_token_tree(tokens, fv, tok_count);
                }
                ci = skip_token_tree(tokens, ci, tok_count);
            }
        }
        i = skip_token_tree(tokens, val, tok_count);
    }

    free(tokens);
    free(json);
    return 1;
}

int editor_scene_json_save(const char *path, const EditorSceneAsset *scene) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", scene->version);
    fprintf(f, "  \"scene_name\": \"%s\",\n", scene->scene_name);
    fprintf(f, "  \"boxes\": [\n");
    for (size_t i = 0; i < scene->box_count; i++) {
        const EditorSceneBox *b = &scene->boxes[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %d,\n", b->id);
        fprintf(f, "      \"name\": \"%s\",\n", b->name);
        fprintf(f, "      \"position\": [%.3f, %.3f, %.3f],\n", b->position[0], b->position[1], b->position[2]);
        fprintf(f, "      \"size\": [%.3f, %.3f, %.3f],\n", b->size[0], b->size[1], b->size[2]);
        fprintf(f, "      \"rotation_y_degrees\": %.3f,\n", b->rotation_y_degrees);
        fprintf(f, "      \"color\": [%.3f, %.3f, %.3f, %.3f],\n", b->color[0], b->color[1], b->color[2], b->color[3]);
        fprintf(f, "      \"flags\": { \"solid\": %d, \"visible\": %d }\n", b->flags.solid ? 1 : 0, b->flags.visible ? 1 : 0);
        fprintf(f, "    }%s\n", (i + 1 < scene->box_count) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"stage_cameras\": [\n");
    for (size_t i = 0; i < scene->stage_camera_count; i++) {
        const EditorStageCamera *c = &scene->stage_cameras[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %d,\n", c->id);
        fprintf(f, "      \"name\": \"%s\",\n", c->name);
        fprintf(f, "      \"target\": [%.3f, %.3f, %.3f],\n", c->target[0], c->target[1], c->target[2]);
        fprintf(f, "      \"distance\": %.3f,\n", c->distance);
        fprintf(f, "      \"yaw_degrees\": %.3f,\n", c->yaw_degrees);
        fprintf(f, "      \"pitch_degrees\": %.3f,\n", c->pitch_degrees);
        fprintf(f, "      \"fov_degrees\": %.3f\n", c->fov_degrees);
        fprintf(f, "    }%s\n", (i + 1 < scene->stage_camera_count) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    return 1;
}
