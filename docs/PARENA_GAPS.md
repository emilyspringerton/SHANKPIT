# PARENA gaps found while rebuilding the engine in SPGF

Every entry below was hit for real while building this fork or while trying to reuse
`PARENA/stdlib/shankpit460/` (the shankpit-460 web engine, the code behind
<https://emilyspringerton.github.io/SHANKPIT/shankpit-460/>). Each has a minimal reproduction and the
exact output the pinned compiler produced. **None of these are fixed here** -- this repository is the
consumer, and the compiler lives in `PARENA`, which this fork pins via `local_path_override`.

Reproduce any of them with the same binary Bazel builds:

```sh
cd SPGF && bazel build @parena//src:parena
P=$PWD/bazel-bin/external/parena+/src/parena
$P build /tmp/repro.prn -o /tmp/repro.c
```

## 1. `cond` clauses are rejected

```lisp
(module t/cond)
(export c1)
(defn c1 [(i : I32)] : I32 (cond (<= i 4) 1 :else 2))
```

```
parena: cond: each clause must be (test result) at line 4
```

The form is used throughout 460 (`sp_view.prn`'s `box-color-dust`, among others), so every one of its
render modules fails at parse time.

## 2. Typed `let` bindings are rejected

```lisp
(module t/letty)
(export l2)
(defn l2 [] : I32 (let [(x : I32) 4] (+ x 1)))
```

```
parena: let: binding name must be a plain identifier at line 4
```

`sp_main.prn`'s frame loop is written this way (`(let [(last : F64) (loadf64 (clock-slot)) ...]`), so
the game loop itself does not reach the backend.

## 3. `f32` conversion emits a call nothing defines

```lisp
(defn sky-t [(y : I32)] : F32 (/ (f32 (- 180 y)) 234.0))
```

Emits `f32(...)` and fails to link:

```
undefined reference to 'f32'
```

Found in `packages/render/sky_rules.prn`; worked around there by taking the row as `F32` and not
converting at all.

## 4. `min` emits a call nothing defines

```lisp
(defn steps [(a : I32) (b : I32)] : I32 (min a b))
```

Emits `min(...)`; neither `parena_runtime.c` nor libc defines it:

```
undefined reference to 'min'
```

Found in `packages/frame/frame_rules.prn`; written as a comparison instead. (`max` was not exercised.)

## 5. The memory path is incomplete: `arena_new`, `bytes_get_u8`, `bytes_set_u8_` do not exist

These compile cleanly but cannot link:

```lisp
(defn mk [] : Bytes (arena-new 1024))               ; -> arena_new(1024)
(defn poke [(b : Bytes) (i : I32) (v : I32)] : Unit (bytes-set-u8! b i v))
```

`runtime/parena_runtime.h` defines the `Bytes`/`Arena` types and `arena_init`/`bytes_alloc_impl`, but
symbol counts for the names the emitter actually calls are: `arena_new` 0, `bytes_get_u8` 0,
`bytes_set_u8_` 0.

**Why this matters most:** a framebuffer, entity records and the 460 memory map are all Bytes/Arena
work. Until the emitter and runtime agree on these names, the fork cannot allocate a pixel buffer, so
its render progress is limited to scalar arithmetic (palette, sky curve) even though PARENA's docs
describe the memory core as present.

## What would unblock the 460 port

Fixing 1, 2 and 5 turns `PARENA/stdlib/shankpit460/` from a prebuilt `.wasm` into 19 modules this fork
can compile: simulation, bots, the 640x360 software rasteriser, HUD and net. Items 3 and 4 matter for
any program that uses conversions or `min`/`max`.
