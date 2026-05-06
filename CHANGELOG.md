# Changelog

## 1.2.0

Big subtraction + correctness release. Three rarely-used features
came out of the language; a pile of "documented but didn't actually
work" claims got either fixed or owned up to in the docs.

**Cuts.** Easier to learn, easier to type-check, smaller compiler.
- `adapt fn` (multi-target fn declaration form): gone. Anything it
  expressed can be a closure over a target switch.
- `signal()` / `derived()` / `subscribe()` / `effect()` reactive
  library: gone. The `bind` keyword covers the same use cases on
  every backend with less surface.
- `inline c { ... }` blocks: gone. Any program with one immediately
  failed to transpile to JS or WASM, so the portability story was a
  lie. FFI is still available via the `ffi` module.

**Real regex.** The Thompson-NFA engine in src/core/regex.c had been
shipping for releases but the user-facing `re.match` / `re.test` /
`re.find_all` / `re.split` / `re.replace` / `re.replace_all` /
`re.groups` natives were thin wrappers around POSIX ERE with a
hand-rolled `\d -> [0-9]` translator. Re-pointed all of them at the
real engine. `\d`, `\w`, `\s`, `\b`, non-greedy `*?` / `+?` / `??`,
non-capturing `(?:...)`, positive `(?=...)` and negative `(?!...)`
lookaheads all work now.

**JIT.** Stop bailing the entire proto on actor methods, module
construction, enum construction, send, kwarg calls, or import.
`OP_MAKE_ACTOR` / `OP_SEND` / `OP_MAKE_ENUM` / `OP_MAKE_MODULE` /
`OP_END_MODULE` / `OP_CALL_KW` / `OP_IMPORT` / `OP_IMPORT_ITEM`
now lower through `vm_step_jit` so calling code stays in tier-2
across `actor.method()`, `import math` etc. The proto-level
`is_actor_method` / `has_actor` bails are gone with them.
Closure-callback inlining for `arr.sort(|a,b| ...)` is still
deferred -- merge sort already kills the O(n²) cliff that was the
worst part of the perf trap.

**JS target.** Cleared the "rough" reputation:
- `==` / `!=` / `assert_eq` now use a structural `__xs_eq`. JS `===`
  is reference equality, which silently broke any conformance test
  that compared arrays / maps. (this caused 5/9 conformance test
  failures by itself)
- `let _ = expr` no longer emits `const _ = ...`, which would crash
  with `Identifier '_' has already been declared` the moment a
  function had two side-effecting wildcard lets.

**Doc sweep.** README claimed the tree-walker was the default
backend. It isn't, and STATUS.md said the opposite. Fixed the bash
example, the prose, and the backend-list ordering. book/cli.md and
book/performance.md no longer tell people to "use --vm by default";
the right rule is `--jit` for hot loops, default for everything
else, `--interp` for plugin debugging. Acknowledged in
book/effects.md that `--emit c` doesn't run effects yet (tracked for
v1.3) and `--emit wasm` is single-shot only. book/transpilers.md
gained a per-target "what works / what doesn't" section so the gaps
are visible at the surface, not just by reading source.

## 1.1.1

Stdlib modules (`math`, `os`, `fs`, `time`, `string`, `path`, `json`,
`http`, `re`, `crypto`, `db`, `random`, `process`, `net`, `async`,
...) now require an explicit `import name` instead of being
auto-bound on every interp / VM init. Constants (`PI`, `E`, `INF`,
`NAN`), Result / Option constructors (`Ok`, `Err`, `Some`, `None`),
the bare math fns (`sqrt`, `floor`, `sin`, ...), and the collection
helpers (`map`, `filter`, `sorted`, ...) stay top-level.

- `pause <duration>` and `del <name>` now run under the VM compiler
  (they used to print "unhandled node tag" to stderr and silently
  no-op).
- LSP `textDocument/documentSymbol` no longer trips the "selectionRange
  must be contained in fullRange" assertion that VS Code surfaced on
  incomplete source.
- Diagnostic colourizer learned the rest of the keyword set
  (`pause`, `del`, `every`, `after`, `timeout`, `debounce`, `do`,
  `with`, `use`, `plugin`, `actor`, `pure`, `inline`, `unsafe`,
  `tag`), highlights `@decorators` distinctly, and recolours
  `<number><unit>` sequences (`100ms`, `2m30s`, `5d`) as durations
  instead of "number followed by an identifier".
- Editor highlighters (vscode tmLanguage, tree-sitter grammar,
  helix / neovim / zed queries) get a dedicated capture for
  duration literals; the lingering `universal_literal` rule got
  trimmed down to the unit set the lexer actually accepts.
- `@on_panic fn report(err) { ... }` now parses (the parser used to
  reject the parameter even though the runtime calls the handler
  with the exception) and the unhandled-exception value is parked
  separately from `cf.value` so the handler actually sees it.
- Parser bug fix: `import math` followed by `import time` was
  being eaten as one import where the second `import` keyword
  looked like the bracketed-items continuation. Now we only
  consume the second token if a `{` actually follows.
- `examples/` removed; will be rebuilt separately.

## 1.1.0

Decorators land. A function declaration can now carry one trigger, an
optional @once, and any number of metadata markers, and the runtime
walks the resulting registry from a single event loop.

- `@on_start` / `@on_exit` / `@on_signal(name)` / `@on_panic` for
  lifecycle. on_start fires once after the top-level settles;
  on_exit and on_panic fire on the way out; on_signal supports
  `"INT"` and `"TERM"` portably plus `"HUP"` / `"USR1"` / `"USR2"` on
  unix.
- `@every(d)` / `@cron(spec)` / `@delayed(d)` for scheduling.
  Drift-free CLOCK_MONOTONIC ticks. The cron spec is the standard
  five-field POSIX form with `*` / `*/N` / `a-b` / comma lists.
- `@watch(path)` for filesystem changes. Linux uses inotify; other
  unixes fall back to a stat-poll against a cached snapshot.
- `@bench` / `@example` for discovery. They appear in the runtime
  registry so `xs bench` / `xs doc` can find them.
- `@export("name")` aliases the fn under a public name; sema sees
  the alias too so callers don't trip the undefined-name check.
- `@once` collapses with any repeating trigger and quiesces it after
  the first fire.

Time literals (`5s`, `100ms`, `1ns`, `2us`, `1m`, `1h`, `1d`) are
now native: no `use literals duration` pragma. They construct a
first-class `Duration` type backed by an int64 nanosecond count, so
arithmetic and comparisons work and `println` prints `2.5s` rather
than `2500`. The other domain literal families (color, date, size,
angle) were dropped to keep the Duration design clean.

The bytecode VM compiler emits a `__register_decorator` call after
each closure binds, so --vm and --jit see the same trigger registry
the interpreter does. The semantic resolver picks up `@export`
aliases as top-level symbols. Editor extensions (vscode, neovim,
helix, zed, jetbrains via tmlanguage, plus the tree-sitter grammar)
all carry decorator highlighting; the LSP surfaces the decorators
in completion. ~10 new regression tests under `tests/regression/`
cover the surface across interp / vm / jit.

## 1.0.0

First production release. The Tier 1 surface from `POLICY.md` is now
locked: language syntax, stdlib modules without `@unstable`, CLI flags,
and diagnostic shape do not break inside the 1.x line.

Highlights since 0.9:
- Multi-shot algebraic effects: `resume` may fire more than once and
  the second resume sees the captured stack, not the mutations the
  first resume left behind. Nested `perform`/`handle` pairs each push
  their own continuation onto a LIFO stack so an inner handler's
  resume lands on the inner body and the outer handler still has its
  own state to rewind to.
- Multi-arm `handle` finally dispatches by effect name. Previously
  every arm fell through to arm 0; now each arm gets a `DUP`/`EQ`
  check against the stacked effect name and the last arm acts as a
  catch-all so single-arm handlers keep working.
- JIT bails on actor methods (and any proto whose descendants
  contain one). The actor dispatcher passes an implicit `self` and
  state-field locals that the JIT prologue doesn't model, so the
  template VM-step path takes over and bug026 (actor + closure +
  outer var) now passes on both backends.
- `xs login` / `xs logout` / `xs whoami` for the registry. Token is
  stored at `~/.xs/credentials` (chmod 600). `xs publish` reads it
  as a fallback to `XS_REGISTRY_TOKEN`.
- `http.serve(port, router)` accepts a router map with `routes`,
  `middleware`, and `not_found`. Patterns support `:name` captures
  (populating `req.params`) and trailing `*` wildcards. Existing
  single-handler form keeps working.
- Windows registry CLI: `xs install` / `xs publish` / `xs search` /
  `xs whoami` now talk to the registry on Windows via a small raw
  socket + BearSSL HTTP client. Linking `-lws2_32` is already in
  the mingw branch of the Makefile.
- C transpiler: `println(a, b, c)` and `print(a, b, c)` now emit a
  proper sequence with single-space separators instead of dropping
  every argument after the first.
- pkg JSON field grabber rejects matches that occur inside string
  values (the previous parser would fish out the wrong substring
  when a description field happened to contain `tarball_url`), and
  caps decoded values at 8 MB so a malicious registry can't drive
  the CLI to OOM.

## 0.9.0

Multi-shot continuations groundwork; SSE/WS now route through TLS;
JIT lowers generators and shadowed locals; profiler caveats and
documented gaps in STATUS.md.

## 0.8.0

Tier-2 register-allocating JIT, inline caches for `LOAD_GLOBAL`,
soft-limit pointer in the VM struct, regex POSIX wrapper, Unicode
bytes spec, browser SDK at `static.xslang.org/xs.js`.

## 0.7.x

Pre-public cleanup: Python remnants removed, tests consolidated,
gradual typing enforced at runtime, plugin pipeline (load → eval →
parse override), HTTPS via embedded BearSSL, stdlib coverage filled
in (math, string, time, fs, os, io, fmt, json, csv, toml, http,
crypto, collections, re, net, db, ffi, reflect, gc, reactive).
