#ifndef EDU_SCRIPT_H
#define EDU_SCRIPT_H

#include "edu_vm.h"
#include "edu_parser.h"
#include "edu_bytecode.h"

#define EDU_MAX_SCRIPTS 8
#define EDU_MAX_SCRIPT_TEXT 2048

typedef struct {
    char name[64];
    char source[EDU_MAX_SCRIPT_TEXT];
    int source_len;
    unsigned char bytecode[EDU_MAX_BYTECODE];
    int bytecode_len;
    int compile_ok;
    const char *strings[EDU_MAX_STRINGS];
    char string_storage[EDU_MAX_STRINGS][64];
    int string_count;
} EduScriptSlot;

typedef struct {
    int terminal_open;
    int active_slot;
    int compile_ok;
    char compile_status[128];
    char run_status[128];
    char last_output[128];
    int last_instruction_count;
    EduScriptSlot slots[EDU_MAX_SCRIPTS];
    EduWorldState world;
} EduScriptSystem;

void edu_script_init(EduScriptSystem *sys);
void edu_script_toggle_terminal(EduScriptSystem *sys);
void edu_script_reset_active(EduScriptSystem *sys);
void edu_script_insert_text(EduScriptSystem *sys, const char *text);
void edu_script_backspace(EduScriptSystem *sys);
void edu_script_newline(EduScriptSystem *sys);
int edu_script_compile_active(EduScriptSystem *sys);
int edu_script_run_active(EduScriptSystem *sys);

#endif
