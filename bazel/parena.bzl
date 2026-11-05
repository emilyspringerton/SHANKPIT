"""PARENA-in-Bazel for the clean-room fork.

The rule this fork exists to prove: a module's source is a `.prn` that sits in the
package next to the code it describes, and the C it compiles is emitted by the real
compiler on every build. Nothing is checked in as generated C -- the fork carries no
`.c` at all -- so a `.prn` and its emitted C cannot drift apart: there is only ever
one copy, produced by the build.

`outs` lands in bazel-bin (Bazel never writes into the source tree), which is what
`bazel build //packages/...:<name>` and the `*_from_prn.c` output are for.

    load("//bazel:parena.bzl", "parena_module")
    parena_module(name = "slide_rules", src = "slide_rules.prn")

Scalar-only modules (I32/F64 in and out, no Arena/Vec/Bytes) pass
`strip_runtime = True` (the default): `parena build` always emits
`#include "parena_runtime.h"`, which drags SDL2/POSIX headers into packages that
have no business needing them.
"""

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

def parena_module(
        name,
        src,
        deps = [],
        copts = [],
        visibility = None,
        strip_runtime = True,
        runtime_deps = []):
    if not src.endswith(".prn"):
        fail("parena_module(%s): src must be a .prn, got %s" % (name, src))

    out = name + "_gen.c"
    steps = ["$(location @parena//src:parena) build $(location %s) -o $(location :%s)" % (src, out)]
    if strip_runtime:
        steps.append(
            "sed -i 's|^#include \"parena_runtime.h\"$$|/* %s: scalar-only, runtime not needed */|' $(location :%s)" % (src, out),
        )

    native.genrule(
        name = name + "_from_prn",
        srcs = [src],
        outs = [out],
        tools = ["@parena//src:parena"],
        cmd = " && ".join(steps),
        visibility = ["//visibility:private"],
    )

    cc_library(
        name = name,
        srcs = [":" + out],
        hdrs = native.glob(["*.h"], allow_empty = True),
        copts = copts,
        deps = deps + runtime_deps,
        visibility = visibility or ["//visibility:public"],
    )

def parena_stdlib_module(
        name,
        module,
        deps = [],
        copts = [],
        visibility = None,
        strip_runtime = True,
        subdir = "shankpit"):
    """Compile a PARENA stdlib module (`@parena//stdlib:<subdir>/<module>.prn`) into this package.

    Same shape as parena_module, but the source of truth is PARENA's own stdlib rather than a copy:
    the header comments in these packages name that stdlib file as where the numbers live, so a
    fork-local duplicate would be a second place to drift. The emitted C is a per-package build
    output, exactly as if the .prn sat here.
    """
    src = "@parena//stdlib:%s/%s.prn" % (subdir, module)
    out = name + "_gen.c"
    steps = ["$(location @parena//src:parena) build $(location %s) -o $(location :%s)" % (src, out)]
    if strip_runtime:
        steps.append(
            "sed -i 's|^#include \"parena_runtime.h\"$$|/* %s: scalar-only, runtime not needed */|' $(location :%s)" % (module, out),
        )
    native.genrule(
        name = name + "_from_prn",
        srcs = [src],
        outs = [out],
        tools = ["@parena//src:parena"],
        cmd = " && ".join(steps),
        visibility = ["//visibility:private"],
    )
    cc_library(
        name = name,
        srcs = [":" + out],
        hdrs = native.glob(["*.h"], allow_empty = True),
        copts = copts,
        deps = deps,
        visibility = visibility or ["//visibility:public"],
    )

def parena_binary(
        name,
        src,
        entry,
        deps = [],
        copts = [],
        visibility = None,
        strip_runtime = False):
    """A program whose body is PARENA: the compiler emits the C, and `entry` becomes main.

    The emitter has no notion of an entry point, so the genrule appends a one-line `main` that calls
    the exported function by its emitted (underscore) name. Nothing is checked in: the whole
    translation unit, wrapper included, is a build output.
    """
    out = name + "_main.c"
    steps = [
        "$(location @parena//src:parena) build $(location %s) -o $(location :%s)" % (src, out),
    ]
    if strip_runtime:
        steps.append(
            "sed -i 's|^#include \"parena_runtime.h\"$$|/* %s: scalar-only, runtime not needed */|' $(location :%s)" % (src, out),
        )
    steps.append(
        "printf '\\nint main(void) { return %s(); }\\n' >> $(location :%s)" % (entry, out),
    )
    native.genrule(
        name = name + "_gen",
        srcs = [src],
        outs = [out],
        tools = ["@parena//src:parena"],
        cmd = " && ".join(steps),
        visibility = ["//visibility:private"],
    )
    cc_binary(
        name = name,
        srcs = [":" + out],
        copts = copts,
        deps = deps,
        visibility = visibility or ["//visibility:public"],
    )
