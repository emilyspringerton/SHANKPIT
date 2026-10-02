#include <stdio.h>
#include <string.h>
#include "edu_vm.h"
#include "edu_bytecode.h"

enum {
    EDU_VM_OK = 0,
    EDU_VM_ERR_IP = 1,
    EDU_VM_ERR_STACK = 2,
    EDU_VM_ERR_VAR = 3,
    EDU_VM_ERR_OPCODE = 4,
    EDU_VM_ERR_LIMIT = 5,
    EDU_VM_ERR_BUILTIN = 6,
    EDU_VM_ERR_DIVZERO = 7,
    EDU_VM_ERR_ARR_BOUNDS = 8
};

static int read_i32(const unsigned char *code, int *ip, int code_len, int *out_v) {
    if (*ip + 4 > code_len) return 0;
    int b0 = code[(*ip)++];
    int b1 = code[(*ip)++];
    int b2 = code[(*ip)++];
    int b3 = code[(*ip)++];
    *out_v = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    return 1;
}

void edu_vm_init(EduVm *vm) { memset(vm, 0, sizeof(*vm)); }

static int push(EduVm *vm, int v, EduVmLimits *limits) {
    int max_stack = limits->max_stack > 0 ? limits->max_stack : EDU_VM_STACK_MAX;
    if (vm->sp >= max_stack || vm->sp >= EDU_VM_STACK_MAX) return 0;
    vm->stack[vm->sp++] = v;
    return 1;
}

static int pop(EduVm *vm, int *out) {
    if (vm->sp <= 0) return 0;
    *out = vm->stack[--vm->sp];
    return 1;
}

int edu_vm_exec(EduVm *vm, const unsigned char *code, int code_len,
                const EduVmProgramMeta *meta, EduWorldState *world,
                EduVmLimits *limits, char *out_msg, int out_msg_cap) {
    edu_vm_init(vm);
    vm->code = code;
    vm->code_len = code_len;
    vm->running = 1;
    int ret = 0;

    while (vm->running) {
        if (vm->ip < 0 || vm->ip >= vm->code_len) {
            vm->last_error = EDU_VM_ERR_IP;
            break;
        }
        vm->instruction_count++;
        if (limits->max_instructions > 0 && vm->instruction_count > limits->max_instructions) {
            limits->halted_due_to_limit = 1;
            vm->last_error = EDU_VM_ERR_LIMIT;
            break;
        }

        unsigned char op = vm->code[vm->ip++];
        int a = 0, b = 0;
        switch (op) {
            case EDU_OP_PUSH_INT:
            case EDU_OP_PUSH_BOOL:
            case EDU_OP_PUSH_STR:
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &a) || !push(vm, a, limits)) vm->last_error = EDU_VM_ERR_STACK;
                break;
            case EDU_OP_LOAD_VAR:
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &a)) vm->last_error = EDU_VM_ERR_IP;
                else if (a < 0 || a >= limits->max_vars || a >= EDU_VM_VAR_MAX) vm->last_error = EDU_VM_ERR_VAR;
                else if (!push(vm, vm->vars[a], limits)) vm->last_error = EDU_VM_ERR_STACK;
                break;
            case EDU_OP_STORE_VAR:
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &a)) vm->last_error = EDU_VM_ERR_IP;
                else if (a < 0 || a >= limits->max_vars || a >= EDU_VM_VAR_MAX) vm->last_error = EDU_VM_ERR_VAR;
                else if (!pop(vm, &b)) vm->last_error = EDU_VM_ERR_STACK;
                else vm->vars[a] = b;
                break;
            case EDU_OP_POP:
                if (!pop(vm, &a)) vm->last_error = EDU_VM_ERR_STACK;
                break;
            case EDU_OP_ADD:
            case EDU_OP_SUB:
            case EDU_OP_MUL:
            case EDU_OP_DIV:
            case EDU_OP_EQ:
            case EDU_OP_LT:
            case EDU_OP_GT:
                if (!pop(vm, &b) || !pop(vm, &a)) vm->last_error = EDU_VM_ERR_STACK;
                else {
                    int r = 0;
                    if (op == EDU_OP_ADD) r = a + b;
                    else if (op == EDU_OP_SUB) r = a - b;
                    else if (op == EDU_OP_MUL) r = a * b;
                    else if (op == EDU_OP_DIV) {
                        if (b == 0) vm->last_error = EDU_VM_ERR_DIVZERO;
                        else r = a / b;
                    } else if (op == EDU_OP_EQ) r = (a == b);
                    else if (op == EDU_OP_LT) r = (a < b);
                    else if (op == EDU_OP_GT) r = (a > b);
                    if (!vm->last_error && !push(vm, r, limits)) vm->last_error = EDU_VM_ERR_STACK;
                }
                break;
            case EDU_OP_JMP:
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &a)) vm->last_error = EDU_VM_ERR_IP;
                else vm->ip = a;
                break;
            case EDU_OP_JMP_IF_FALSE:
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &a) || !pop(vm, &b)) vm->last_error = EDU_VM_ERR_STACK;
                else if (!b) vm->ip = a;
                break;
            case EDU_OP_CALL_BUILTIN: {
                int bid = 0;
                int argc = 0;
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &bid) || !read_i32(vm->code, &vm->ip, vm->code_len, &argc)) {
                    vm->last_error = EDU_VM_ERR_IP;
                    break;
                }
                int args[8] = {0};
                if (argc < 0 || argc > 8) {
                    vm->last_error = EDU_VM_ERR_BUILTIN;
                    break;
                }
                for (int i = argc - 1; i >= 0; i--) {
                    if (!pop(vm, &args[i])) { vm->last_error = EDU_VM_ERR_STACK; break; }
                }
                if (vm->last_error) break;
                int call_ret = 0;
                if (!edu_binding_call(world, bid, args, argc, meta->strings, meta->string_count, &call_ret,
                                      vm->debug_line, (int)sizeof(vm->debug_line))) {
                    vm->last_error = EDU_VM_ERR_BUILTIN;
                    break;
                }
                if (!push(vm, call_ret, limits)) vm->last_error = EDU_VM_ERR_STACK;
                break;
            }
            case EDU_OP_LOAD_ARR: {
                int base = 0, len = 0, idx = 0;
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &base) ||
                    !read_i32(vm->code, &vm->ip, vm->code_len, &len)) {
                    vm->last_error = EDU_VM_ERR_IP;
                    break;
                }
                if (!pop(vm, &idx)) { vm->last_error = EDU_VM_ERR_STACK; break; }
                if (idx < 0 || idx >= len || base + idx >= EDU_VM_ARR_MEM_MAX) {
                    vm->last_error = EDU_VM_ERR_ARR_BOUNDS;
                    break;
                }
                if (!push(vm, vm->arr_mem[base + idx], limits)) vm->last_error = EDU_VM_ERR_STACK;
                break;
            }
            case EDU_OP_STORE_ARR: {
                int base = 0, len = 0, idx = 0, val = 0;
                if (!read_i32(vm->code, &vm->ip, vm->code_len, &base) ||
                    !read_i32(vm->code, &vm->ip, vm->code_len, &len)) {
                    vm->last_error = EDU_VM_ERR_IP;
                    break;
                }
                // Parser emits index expr then value expr, so value is on top.
                if (!pop(vm, &val) || !pop(vm, &idx)) { vm->last_error = EDU_VM_ERR_STACK; break; }
                if (idx < 0 || idx >= len || base + idx >= EDU_VM_ARR_MEM_MAX) {
                    vm->last_error = EDU_VM_ERR_ARR_BOUNDS;
                    break;
                }
                vm->arr_mem[base + idx] = val;
                break;
            }
            case EDU_OP_RETURN:
                if (!pop(vm, &ret)) ret = 0;
                vm->running = 0;
                break;
            case EDU_OP_HALT:
                vm->running = 0;
                break;
            default:
                vm->last_error = EDU_VM_ERR_OPCODE;
                break;
        }
        if (vm->last_error) break;
    }

    limits->error_code = vm->last_error;
    if (vm->last_error) {
        if (vm->last_error == EDU_VM_ERR_BUILTIN && vm->debug_line[0]) {
            snprintf(out_msg, (size_t)out_msg_cap, "vm builtin error: %s instr=%d", vm->debug_line, vm->instruction_count);
        } else {
            snprintf(out_msg, (size_t)out_msg_cap, "vm error=%d instr=%d", vm->last_error, vm->instruction_count);
        }
        return 0;
    }
    snprintf(out_msg, (size_t)out_msg_cap, "vm halt instructions=%d ret=%d", vm->instruction_count, ret);
    return 1;
}
