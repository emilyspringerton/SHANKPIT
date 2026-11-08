#!/usr/bin/env python3
# scripts/gen_wasm_test_fixtures.py -- the hand-assembled wasm modules the T0 host tests load, as C arrays.
#
#   python3 -I scripts/gen_wasm_test_fixtures.py          rewrite both headers
#   python3 -I scripts/gen_wasm_test_fixtures.py --check  fail if either committed header differs
#
# Writes packages/wasm_host/level_abi_test_fixtures.h (level_abi_test.c) and
# packages/wasm_host/level_host_test_fixtures.h (level_host_test.c). Each module is the smallest one that
# exercises one refusal or one behaviour, assembled byte by byte here so the tests need no toolchain; the
# real module the tests also run is the committed assets/levels/brawlpit/brawlpit_level.wasm.
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I32 = 0x7F


def uleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def sleb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        done = (n == 0 and not (b & 0x40)) or (n == -1 and (b & 0x40))
        out.append(b if done else b | 0x80)
        if done:
            return bytes(out)


def vec(items):
    return uleb(len(items)) + b"".join(items)


def name(s):
    return uleb(len(s)) + s.encode()


def sec(sid, payload):
    return bytes([sid]) + uleb(len(payload)) + payload


def functype(params, results):
    return b"\x60" + vec([bytes([p]) for p in params]) + vec([bytes([r]) for r in results])


def imp(mod, field, typeidx):
    return name(mod) + name(field) + b"\x00" + uleb(typeidx)


def exp(n, idx):
    return name(n) + b"\x00" + uleb(idx)


def body(code):
    return b"\x00" + code + b"\x0b"


def const(v):
    return b"\x41" + sleb(v)


def dispatch(t, a, b, c):
    return const(t) + const(a) + const(b) + const(c) + b"\x10\x00"


def module(types, imports, funcs, exports, bodies):
    m = b"\x00asm\x01\x00\x00\x00" + sec(1, vec(types))
    if imports:
        m += sec(2, vec(imports))
    m += sec(3, vec([uleb(t) for t in funcs])) + sec(7, vec(exports))
    return m + sec(10, vec([uleb(len(b)) + b for b in bodies]))


def carr(nm, b):
    rows = ["    " + ", ".join("0x%02x" % x for x in b[i:i + 16]) + "," for i in range(0, len(b), 16)]
    return "static const unsigned char %s[] = {\n%s\n};\n" % (nm, "\n".join(rows))


# ---- level_abi_test_fixtures.h ------------------------------------------------------------------------
def abi_header():
    rows = [(1003, 1, 0, 0), (1100, 7, 8, 9), (5, 0, 0, 0), (1199, 1, 2, 3), (1200, 0, 0, 0), (-1, 0, 0, 0)]
    fx_dispatch = module([functype([I32] * 4, []), functype([], [])], [imp("env", "reflux_host_dispatch", 0)], [1],
                         [exp("go", 1)], [body(b"".join(dispatch(*r) for r in rows))])
    fx_logsize = module([functype([], [I32])], [imp("env", "reflux_host_log_size", 0)], [0], [exp("go", 1)],
                        [body(b"\x10\x00")])
    fx_badsig = module([functype([I32] * 2, []), functype([], [])], [imp("env", "reflux_host_dispatch", 0)], [1],
                       [exp("go", 1)], [body(b"\x41\x01\x41\x02\x10\x00")])
    out = ("/* packages/wasm_host/level_abi_test_fixtures.h -- hand-assembled modules for level_abi_test.c (python, once).\n"
           " * fx_dispatch: imports env.reflux_host_dispatch v(iiii); exports go v(), which dispatches six rows across the\n"
           " *              family boundaries: 1003 (lifecycle), 1100, 5 (a world type), 1199, 1200, -1.\n"
           " * fx_logsize:  imports env.reflux_host_log_size i(), which no level is offered (it saturates at 256).\n"
           " * fx_badsig:   imports env.reflux_host_dispatch as v(ii). */\n")
    return out + carr("fx_dispatch", fx_dispatch) + carr("fx_logsize", fx_logsize) + carr("fx_badsig", fx_badsig)


# ---- level_host_test_fixtures.h: minimal levels, all 16 exports + memory ------------------------------
LEVEL_EXPORTS = [("level_api", "i()"), ("level_enter", "i()"), ("level_poll", "i(i)"), ("level_tick", "v()"),
                 ("level_render", "v()"), ("level_exit", "v()"), ("level_seed", "v(i)"), ("level_key", "v(ii)"),
                 ("level_pointer", "v(iii)"), ("level_blur", "v()"), ("level_fb_addr", "i()"), ("level_fb_w", "i()"),
                 ("level_fb_h", "i()"), ("level_asset_addr", "i(i)"), ("level_asset_set", "v(ii)"),
                 ("level_lost_rows", "i()")]
TYPE_OF = {"i()": 0, "i(i)": 1, "v()": 2, "v(i)": 3, "v(ii)": 4, "v(iii)": 5}
LEVEL_TYPES = [functype([], [I32]), functype([I32], [I32]), functype([], []), functype([I32], []),
               functype([I32, I32], []), functype([I32] * 3, []), functype([I32] * 4, [])]
UNREACHABLE = b"\x00"


def level(api=1, enter=const(0), tick=b"", fbw=960, fbh=540, pages=32, memory=True):
    code = {"level_api": const(api), "level_enter": enter, "level_poll": b"\x20\x00", "level_tick": tick,
            "level_fb_addr": const(0), "level_fb_w": const(fbw), "level_fb_h": const(fbh),
            "level_asset_addr": const(0), "level_lost_rows": const(0)}
    m = b"\x00asm\x01\x00\x00\x00" + sec(1, vec(LEVEL_TYPES)) + sec(2, vec([imp("env", "reflux_host_dispatch", 6)]))
    m += sec(3, vec([uleb(TYPE_OF[sig]) for _, sig in LEVEL_EXPORTS]))
    if memory:
        m += sec(5, vec([b"\x01" + uleb(pages) + uleb(pages)]))
    exps = [name(n) + b"\x00" + uleb(i + 1) for i, (n, _) in enumerate(LEVEL_EXPORTS)]
    if memory:
        exps.append(name("memory") + b"\x02" + uleb(0))
    m += sec(7, vec(exps))
    bodies = [body(code.get(n, b"")) for n, _ in LEVEL_EXPORTS]
    return m + sec(10, uleb(len(bodies)) + b"".join(uleb(len(b)) + b for b in bodies))


def host_header():
    variants = [
        ("fx_lv_ok", level(), "a well-formed minimal level (enter 0, tick does nothing)"),
        ("fx_lv_api2", level(api=2), "level_api() returns 2"),
        ("fx_lv_enter7", level(enter=const(7)), "level_enter() returns 7"),
        ("fx_lv_enter_trap", level(enter=UNREACHABLE), "level_enter() traps"),
        ("fx_lv_tick_trap", level(tick=UNREACHABLE), "level_tick() traps"),
        ("fx_lv_tick_lifecycle", level(tick=dispatch(1003, 1, 0, 0) + dispatch(1100, 5, 0, 0)),
         "level_tick() dispatches 1003 (a lifecycle type) then 1100"),
        ("fx_lv_nomem", level(memory=False), "exports no memory"),
        ("fx_lv_bigfb", level(pages=1), "claims a 960x540 framebuffer in a 64 KiB memory"),
    ]
    out = ("/* packages/wasm_host/level_host_test_fixtures.h -- hand-assembled minimal levels for level_host_test.c\n"
           " * (python, once): all 16 level exports + a 32-page memory + env.reflux_host_dispatch, one behaviour varied:\n")
    for nm, _, desc in variants:
        out += " *   %-21s %s\n" % (nm + ":", desc)
    out += " */\n"
    return out + "".join(carr(nm, b) for nm, b, _ in variants)


def main():
    targets = [("packages/wasm_host/level_abi_test_fixtures.h", abi_header()),
               ("packages/wasm_host/level_host_test_fixtures.h", host_header())]
    check = "--check" in sys.argv[1:]
    stale = 0
    for rel, text in targets:
        path = os.path.join(ROOT, rel)
        if check:
            with open(path) as f:
                if f.read() != text:
                    print("STALE: %s differs from scripts/gen_wasm_test_fixtures.py" % rel)
                    stale = 1
        else:
            with open(path, "w") as f:
                f.write(text)
            print("wrote %s" % rel)
    if check and not stale:
        print("wasm test fixtures are current (%d headers)" % len(targets))
    return stale


if __name__ == "__main__":
    sys.exit(main())
