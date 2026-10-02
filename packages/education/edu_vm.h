#ifndef EDU_VM_H
#define EDU_VM_H

#include "edu_bindings.h"
#include "edu_bytecode.h" // for EDU_VM_ARR_MEM_MAX

#define EDU_VM_STACK_MAX 256
#define EDU_VM_VAR_MAX 64

typedef struct {
    int max_instructions;
    int max_stack;
    int max_vars;
    int halted_due_to_limit;
    int error_code;
} EduVmLimits;

typedef struct {
    int stack[EDU_VM_STACK_MAX];
    int sp;
    int vars[EDU_VM_VAR_MAX];
    int arr_mem[EDU_VM_ARR_MEM_MAX]; // shared pool every array() declaration draws from
    const unsigned char *code;
    int code_len;
    int ip;
    int running;
    int instruction_count;
    int last_error;
    char debug_line[128];
} EduVm;

typedef struct {
    const char *const *strings;
    int string_count;
} EduVmProgramMeta;

void edu_vm_init(EduVm *vm);
int edu_vm_exec(EduVm *vm, const unsigned char *code, int code_len,
                const EduVmProgramMeta *meta, EduWorldState *world,
                EduVmLimits *limits, char *out_msg, int out_msg_cap);

#endif
