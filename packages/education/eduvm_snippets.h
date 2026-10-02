/* eduvm_snippets.h -- saveable EduScript snippets (card #495). A snippet is {name, source}. The same JSON array
 * shape is used for (a) the NOCK/IDUNA public list GET /api/v1/nock-edu-snippets?widget=ORB (extra fields such as id,
 * widget_name, timestamps are ignored) and (b) the local save file the Orb writes, so one parser serves both. */
#ifndef EDUVM_SNIPPETS_H
#define EDUVM_SNIPPETS_H
#include "edu_script.h"

typedef struct { char name[64]; char source[EDU_MAX_SCRIPT_TEXT]; } EduSnippet;

/* Parses a JSON array of objects with string fields "name" and "source" (standard escapes incl. \\uXXXX for ASCII).
 * Returns the number of snippets parsed (<= max); entries missing either field, or with an oversized source, are skipped. */
int edu_snippets_parse(const char *json, EduSnippet *out, int max);
/* Serialises the non-empty slots of sys to JSON. Returns bytes written (excluding NUL), or -1 if cap is too small. */
int edu_snippets_serialize(const EduScriptSystem *sys, char *out, int cap);
/* Copies snippets into slots [first, EDU_MAX_SCRIPTS) that are currently empty; returns how many were placed. */
int edu_snippets_fill_empty(EduScriptSystem *sys, const EduSnippet *sn, int n, int first);
/* Replaces slot contents positionally from snippets (slot i <- sn[i]); returns how many were placed. */
int edu_snippets_load_into(EduScriptSystem *sys, const EduSnippet *sn, int n);
#endif
