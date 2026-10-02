// Standalone verification harness for EduVM array support (Phase 0 of
// docs2/EDUCATION_CURRICULUM_NORTHSTAR.md). This package has no existing
// test convention (checked before writing this -- zero *_test.* files here),
// so this is a small, self-contained main() rather than a framework-based
// suite: compile a script exercising declare/write/read/bounds-check, run
// it, assert the results.
//
// Build (from packages/education/): gcc -o /tmp/edu_test_arrays
//   edu_test_arrays.c edu_lexer.c edu_bytecode.c edu_parser.c edu_vm.c edu_bindings.c
// Run: /tmp/edu_test_arrays

#include <stdio.h>
#include <string.h>
#include "edu_parser.h"
#include "edu_vm.h"
#include "edu_bindings.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok:   %s\n", msg); \
} while (0)

// run_ok compiles+executes src, expects success, returns the VM (for
// inspecting vars/arr_mem afterward).
static int run_ok(const char *src, EduVm *vm, EduWorldState *world) {
    static unsigned char bytecode[EDU_MAX_BYTECODE];
    EduCompileOutput out;
    memset(&out, 0, sizeof(out));
    out.bytecode = bytecode;
    out.bytecode_cap = sizeof(bytecode);
    if (!edu_compile_source(src, &out)) {
        printf("FAIL: compile error: %s (line %d col %d)\n", out.error, out.error_line, out.error_col);
        return 0;
    }
    EduVmProgramMeta meta = { out.strings, out.string_count };
    EduVmLimits limits;
    memset(&limits, 0, sizeof(limits));
    limits.max_instructions = 10000;
    limits.max_stack = EDU_VM_STACK_MAX;
    limits.max_vars = EDU_VM_VAR_MAX;
    char msg[256];
    memset(world, 0, sizeof(*world));
    int ok = edu_vm_exec(vm, out.bytecode, out.bytecode_len, &meta, world, &limits, msg, sizeof(msg));
    if (!ok) printf("FAIL: vm error: %s\n", msg);
    return ok;
}

// run_expect_fail compiles+executes src and expects a *runtime* VM failure
// (e.g. an out-of-bounds array access) -- compile success but exec failure.
static int run_expect_runtime_fail(const char *src) {
    static unsigned char bytecode[EDU_MAX_BYTECODE];
    EduCompileOutput out;
    memset(&out, 0, sizeof(out));
    out.bytecode = bytecode;
    out.bytecode_cap = sizeof(bytecode);
    if (!edu_compile_source(src, &out)) {
        printf("FAIL: expected compile success, got: %s\n", out.error);
        return 0;
    }
    EduVmProgramMeta meta = { out.strings, out.string_count };
    EduVm vm;
    EduWorldState world;
    memset(&world, 0, sizeof(world));
    EduVmLimits limits;
    memset(&limits, 0, sizeof(limits));
    limits.max_instructions = 10000;
    limits.max_stack = EDU_VM_STACK_MAX;
    limits.max_vars = EDU_VM_VAR_MAX;
    char msg[256];
    int ok = edu_vm_exec(&vm, out.bytecode, out.bytecode_len, &meta, &world, &limits, msg, sizeof(msg));
    return !ok; // success means it correctly FAILED at runtime
}

int main(void) {
    EduVm vm;
    EduWorldState world;

    // 1. Declare, write, read a single element.
    {
        const char *src =
            "let arr = array(5);\n"
            "arr[0] = 42;\n"
            "let x = arr[0];\n";
        CHECK(run_ok(src, &vm, &world), "declare/write/read single element");
    }

    // 2. Bubble sort a 5-element array, verify sorted output via a builtin
    //    print (using 'print' if registered) is unnecessary -- inspect
    //    arr_mem directly isn't possible from outside without the base
    //    offset, so instead verify via reading each index back with 'let'.
    {
        const char *src =
            "let arr = array(5);\n"
            "arr[0] = 5;\n"
            "arr[1] = 3;\n"
            "arr[2] = 4;\n"
            "arr[3] = 1;\n"
            "arr[4] = 2;\n"
            "let n = 5;\n"
            "let i = 0;\n"
            "while (i < n) {\n"
            "  let j = 0;\n"
            "  while (j < n - i - 1) {\n"
            "    if (arr[j] > arr[j+1]) {\n"
            "      let tmp = arr[j];\n"
            "      arr[j] = arr[j+1];\n"
            "      arr[j+1] = tmp;\n"
            "    }\n"
            "    j = j + 1;\n"
            "  }\n"
            "  i = i + 1;\n"
            "}\n"
            "let r0 = arr[0];\n"
            "let r1 = arr[1];\n"
            "let r2 = arr[2];\n"
            "let r3 = arr[3];\n"
            "let r4 = arr[4];\n";
        int ok = run_ok(src, &vm, &world);
        CHECK(ok, "bubble sort compiles and runs");
        if (ok) {
            // r0..r4 are the last 5 declared scalar vars; find their slots by
            // position isn't exposed here, so re-derive: they're vars 8..12
            // in declaration order (arr is not a vars[] slot; n,i,j,tmp are
            // scalars 0..3, r0..r4 are 4..8). Simpler and robust: just check
            // arr_mem directly, since arr's base is 0 (first and only array).
            CHECK(vm.arr_mem[0] == 1, "sorted[0] == 1");
            CHECK(vm.arr_mem[1] == 2, "sorted[1] == 2");
            CHECK(vm.arr_mem[2] == 3, "sorted[2] == 3");
            CHECK(vm.arr_mem[3] == 4, "sorted[3] == 4");
            CHECK(vm.arr_mem[4] == 5, "sorted[4] == 5");
        }
    }

    // 3. Out-of-bounds read is a runtime error, not a crash or silent wrap.
    {
        const char *src =
            "let arr = array(3);\n"
            "let x = arr[10];\n";
        CHECK(run_expect_runtime_fail(src), "out-of-bounds read fails cleanly at runtime");
    }

    // 4. Out-of-bounds write is a runtime error.
    {
        const char *src =
            "let arr = array(3);\n"
            "arr[10] = 1;\n";
        CHECK(run_expect_runtime_fail(src), "out-of-bounds write fails cleanly at runtime");
    }

    // 5. Negative index is a runtime error.
    {
        const char *src =
            "let arr = array(3);\n"
            "let i = 0 - 1;\n"
            "let x = arr[i];\n";
        CHECK(run_expect_runtime_fail(src), "negative index fails cleanly at runtime");
    }

    // 6. Using an array name without indexing is a compile error, not a
    //    silent misread of arr_base as a scalar value.
    {
        static unsigned char bytecode[EDU_MAX_BYTECODE];
        EduCompileOutput out;
        memset(&out, 0, sizeof(out));
        out.bytecode = bytecode;
        out.bytecode_cap = sizeof(bytecode);
        const char *src = "let arr = array(3);\nlet x = arr;\n";
        int compiled = edu_compile_source(src, &out);
        CHECK(!compiled, "bare array name (no index) is a compile error");
    }

    // 7. Two independent arrays don't alias each other's memory.
    {
        const char *src =
            "let a = array(2);\n"
            "let b = array(2);\n"
            "a[0] = 1;\n"
            "a[1] = 2;\n"
            "b[0] = 100;\n"
            "b[1] = 200;\n";
        int ok = run_ok(src, &vm, &world);
        CHECK(ok, "two independent arrays compile and run");
        if (ok) {
            CHECK(vm.arr_mem[0] == 1 && vm.arr_mem[1] == 2, "array a untouched by b's writes");
            CHECK(vm.arr_mem[2] == 100 && vm.arr_mem[3] == 200, "array b at its own base offset");
        }
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
