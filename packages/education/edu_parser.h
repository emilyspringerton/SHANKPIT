#ifndef EDU_PARSER_H
#define EDU_PARSER_H

#include "edu_bindings.h"

#define EDU_MAX_VARS 64
#define EDU_MAX_STRINGS 64

typedef struct {
    char name[32];
    int slot;      // scalar vars[] index; unused (0) for arrays
    int is_array;  // 1 if this name was declared via 'let name = array(N);'
    int arr_base;  // base offset into the VM's shared arr_mem[] pool
    int arr_len;   // declared length; every index access is bounds-checked against this
} EduVarEntry;

typedef struct {
    unsigned char *bytecode;
    int bytecode_cap;
    int bytecode_len;
    const char *strings[EDU_MAX_STRINGS];
    char string_storage[EDU_MAX_STRINGS][64];
    int string_count;
    int var_count;
    EduVarEntry vars[EDU_MAX_VARS];
    int arr_mem_used; // running total of arr_mem[] slots claimed by array() declarations so far
    int ok;
    char error[160];
    int error_line;
    int error_col;
} EduCompileOutput;

int edu_compile_source(const char *source, EduCompileOutput *out);

#endif
