# XS Status

What works, what's partial, and what's planned. For the current
release number, check `git tag` or `xs --version`.

## Bytecode VM

The bytecode VM is the default backend. Bare `xs file.xs` runs on the
VM; pass `--interp` to force the tree-walker, `--jit` to use the
tier-2 JIT. The VM passes the full test suite and is ~4-9x faster
than the tree-walk interpreter on compute-heavy code. Programs that
register `plugin.runtime.after_eval` hooks auto-fall back to the
interpreter.

## Tree-Walk Interpreter

Reserved for debugging and for the handful of plugins that rely on
AST-level runtime hooks. Pass `--interp` to force it.

| Feature | Status |
|---------|--------|
| Variables (let, var, const) | works |
| All data types (int, float, str, bool, null, array, map, tuple, range, re) | works |
| Arithmetic, bitwise, logical operators | works |
| String interpolation, escapes, methods | works |
| Control flow (if/elif/else, for, while, loop, match, break, continue) | works |
| Pattern matching with destructuring, guards, nested patterns | works |
| Functions, closures, default params, variadic, arrow lambdas | works |
| Function overloading (dispatch by arity) | works |
| Tagged blocks (user-defined control structures) | works |
| Reactive bindings (bind) | works |
| Gradual contracts (where clauses) | works |
| Adapt functions (multi-target) | works |
| Inline C blocks (for C transpiler) | works |
| Generators (fn*/yield) | works |
| Structs, impl, traits | works |
| Enums with associated data | works |
| Classes with inheritance | works |
| Variance markers (`<+T>`, `<-T>`) on fn / struct / enum | works |
| Higher-rank `forall<T>` types | works |
| `@scoped` annotations + escape analysis | works |
| `@[macro]` procedural-macro markers | works |
| Algebraic effects (effect/perform/handle/resume) | works |
| Concurrency (spawn, async/await, channels, actors, nurseries) | works |
| Error handling (try/catch/finally, throw, defer) | works |
| Modules and imports | works |
| List/map comprehensions | works |
| Pipe operator | works |
| Gradual typing (--check, --strict) | works |
| Plugin system | works |
| Standard library (37 modules) | works |
| HTTPS client via embedded BearSSL | works |
| Generational refcount GC + concurrent cycle collector | works |
| Universal literals (duration, color, date, size, angle) | works |
| Temporal primitives (every, after, timeout, debounce) | works |
| Multi-line strings (triple-quote) | works |
| `do` expressions | works |
| `with` resource management | works |
| Named arguments | works |
| Enum methods via impl | works |

Full test run: `tests/test_*.xs` + adversarial suite + examples + CLI drivers all pass on Linux, macOS, and MinGW Windows.

## Bytecode VM (feature matrix)

Use `--vm` flag. Full feature parity with the interpreter (except reactive bindings, which evaluate once).

| Feature | Status |
|---------|--------|
| Arithmetic, variables, functions | works |
| Closures and upvalues | works |
| Control flow (if, while, for, loop, match) | works |
| Labeled break/continue | works |
| Arrays, maps, tuples, ranges | works |
| String interpolation | works |
| Pattern matching (literals, guards, tuples, enums, structs) | works |
| Functions with default params, variadic | works |
| Structs with impl methods, spread | works |
| Classes with inheritance and super | works |
| Traits | works |
| Enums with data and matching | works |
| Concurrency (spawn, channels, actors, async/await, nursery) | works |
| Algebraic effects (perform/handle/resume) | works |
| Error handling (try/catch/finally, throw, defer) | works |
| Modules and imports | works |
| List/map comprehensions | works |
| Pipe operator | works |
| Plugin system (`load`, global.set, add_method) | works |
| All string methods (80+) | works |
| All array methods (50+) | works |
| All map methods (20+) | works |
| Number methods (is_even, digits, to_hex, etc.) | works |
| Result/Option methods (unwrap, is_ok, etc.) | works |
| Optional chaining (?.) | works |
| Range indexing (arr[1..3]) | works |
| All builtins matching interpreter | works |
| Growable stack and frames (no fixed limits) | works |

The VM test (`test_vm.xs`) runs through `--vm` automatically from `tests/run.sh`. Every other test also runs under both backends and the two outputs are diffed; a divergence fails the test even if each backend passes on its own. Use `xs build file.xs` to compile, `xs run file.xsc` to execute.

## JIT Compiler

Opt-in via `--jit`. Single register-allocating tier, x86-64 and
aarch64. Bytecode is lowered to a small linear IR (`src/jit/ra_ir.h`),
basic blocks are split, per-block liveness is computed
(`src/jit/ra_live.c`), a linear-scan allocator (`src/jit/ra_alloc.c`)
maps virtual registers onto three callee-saved regs, and a per-arch
code generator emits native code with SMI fast paths for arithmetic
and compares, an XMM fast path for boxed `XS_FLOAT` binops, an inlined
monomorphic IC for `LOAD_GLOBAL`, inline closure-upvalue access, a
fused compare-and-branch peephole, and a refcount-pair elimination
pass that drops redundant incref/decref around dead produced values.
Recursive calls stay in native code through a small dispatcher
(`tier2_run_until`) that re-enters compiled protos via the
`XSProto.jit_entry` cache.

Supported opcodes (from `op_supported` in `src/jit/ra_lower.c`): the
full bytecode set except generators and `OP_STORE_GLOBAL`-writes of
locals captured by inner closures (shadow-model guard). Anything that
falls outside the subset drops the whole proto back to the bytecode
VM; no template-JIT middle tier.

| Feature | Status |
|---------|--------|
| Register-allocating JIT (x86-64) | works |
| Register-allocating JIT (aarch64) | works |
| SMI fast paths for arithmetic and compares | works |
| XMM fast path for boxed floats | works |
| Inlined monomorphic IC for LOAD_GLOBAL | works |
| Closure upvalue ops in native | works |
| Recursive call re-entry via `jit_entry` | works |
| Refcount-pair peephole | works |
| Control-flow ops (THROW, TAIL_CALL, AWAIT, YIELD, SPAWN, EFFECT_*, DEFER_*) | deopt trampoline |

Observed numbers on a Linux x86-64 box:

| Workload              | `--vm`  | `--jit` | gcc -O2 | node  |
|-----------------------|---------|---------|---------|-------|
| fib(30)               |  210 ms |   20 ms |   <1 ms | 110 ms |
| fib(35)               | 2320 ms |  520 ms |   80 ms | 210 ms |
| 10M-iter `while` sum  |  640 ms |  110 ms |   20 ms | 110 ms |
| 1M-iter `while` sum   |   60 ms |   10 ms |   <1 ms | 110 ms |

The JIT is 5-8x faster than `--vm`, beats Node on every loop, and
matches or beats Node on short recursion; the V8 gap only opens up on
heavy recursion where cross-call inlining pays off. `bash tests/run.sh`
runs every test through `--jit` alongside `--interp` and `--vm` and
diffs the three outputs.

## C Transpiler

`xs --emit c file.xs` generates standalone C that compiles with gcc/clang.

| Feature | Status |
|---------|--------|
| Variables, arithmetic, control flow | works |
| Functions, default params, expression bodies | works |
| Strings, interpolation, string methods | works |
| Arrays, maps, array methods (map/filter/reduce) | works |
| Structs with impl methods | works |
| Enums with constructors and matching | works |
| Pattern matching with guards | works |
| Channels, actors, spawn, nursery | works |
| Async/await (sequential) | works |
| Closures capturing mutable state | works |
| Generators | not yet |
| Algebraic effects | not yet |
| Plugins | not supported (requires runtime) |

## JavaScript Transpiler

`xs --emit js file.xs` generates Node.js-compatible JavaScript.

| Feature | Status |
|---------|--------|
| Variables, functions, control flow | works |
| Closures, arrow lambdas | works |
| Arrays, maps | works |
| Concurrency | partial |
| Algebraic effects (perform/handle) | works: handle body lowers to `function*()` and `yield*` delegates through nested performs |
| Everything else | rough |

## WebAssembly

The path to running xs in a browser is the runtime build, not the
AOT transpiler.

| Build | What it gives you | Where |
|-------|-------------------|-------|
| `make wasm` | full runtime as `xs.wasm` (~1.4 MB), wasi target. Same feature set as the native binary. | release artefact |
| `make wasm-browser` | stripped runtime as `xs-browser.wasm` (~600 kB after wasm-opt). TLS / x509 / RSA / EC / http_server / pkg / lint / doc / coverage are dropped, hashes / mac / kdf / symcipher kept. | release artefact, hosted at `static.xslang.org/xs.wasm` |

The browser SDK at `static.xslang.org/xs.js` wraps `xs-browser.wasm`
with a virtual filesystem, captured stdout/stderr, and a `loadXS()` /
`xs.run()` / `xs.exec()` API. Releases publish both artefacts, and the
static repo's daily sync workflow picks up the browser build.

`xs --emit wasm` (the AOT transpiler) is staying parked. The runtime
path covers the browser story; the C transpiler covers AOT. The
existing emitter handles arithmetic and direct calls, but bringing it
to full parity competes with the runtime path on every backend choice
without giving users something they can't already get cheaper.

## Tooling

| Tool | Status |
|------|--------|
| REPL with syntax highlighting | works |
| LSP server (hover, completion, diagnostics, definition, references, rename, formatting, signature help) | works |
| DAP debugger (breakpoints, stepping, variable inspection, evaluate) | works |
| VSCode extension | works: available on marketplace |
| Formatter (`xs fmt`) | works |
| Linter (`xs lint`) | works |
| Test runner (`xs test`) | works |
| Benchmarks (`xs bench`) | works |
| Execution tracer (`xs --record`, `xs replay`) | works |
| Profiler (`xs profile`) | works |
| Coverage (`xs coverage`) | works |
| Doc generator (`xs doc`) | works |
| Package manager (`xs install/remove/update`) | works: git + local + reg.xslang.org via `xs install <name>`, `xs search`, `xs publish` (with `XS_REGISTRY_TOKEN`) |

## Platform Support

CI runs the full 7-layer suite on every commit across each of these.
libFuzzer runs the parser fuzz harness on a short budget, and the
WASM job cross-compiles with wasi-sdk and runs the conformance and
regression layers under wasmtime.

| Platform | Status |
|----------|--------|
| Linux (x86-64) | builds and tests pass; ASan + UBSan also clean |
| macOS (aarch64) | builds and tests pass |
| Windows (MinGW, x86-64) | builds and tests pass, statically linked (`-static` in Makefile) |
| WASM (wasi-sdk 25) | conformance + regression layers pass under wasmtime 25 |
| iOS (arm64 device + x86_64 sim) | `make ios` produces `xs-ios.a` static archive (no JIT, App Store policy) |
| Android (arm64-v8a, armeabi-v7a, x86_64) | `make android` via NDK r25+ produces `libxs.so` per ABI |
| ESP32 (xtensa) | `make esp32` produces `libxs.a` for an ESP-IDF component (VM-only build) |
| Raspberry Pi (aarch64 Linux) | `make release CC=aarch64-linux-gnu-gcc` full feature set including JIT |

## Standard Library

37 modules are registered at interpreter startup (`stdlib_register` in `src/runtime/builtins.c`):
`math`, `time`, `io`, `string`, `path`, `base64`, `hash`, `uuid`, `collections`, `process`,
`random`, `os`, `json`, `log`, `fmt`, `test`, `csv`, `url`, `re`, `msgpack`, `Promise`,
`async`, `net`, `crypto`, `thread`, `buf`, `encode`, `db`, `cli`, `ffi`, `reflect`, `gc`,
`reactive`, `toml`, `http`, `fs`, `tracing`.

## Known Footguns

These are the sharp edges you are most likely to hit. They are here on
purpose: the more users trip over silently, the more trust the project
burns. Fix one, and this list gets shorter.

- **Recursion depth limits.** The tree-walker is more conservative
  about call depth (500 frames, tunable via `XS_MAX_DEPTH`). The VM
  (default) and JIT (`--jit`) handle deeper stacks fine and are
  faster on most workloads. If you hit a stack-overflow on the
  interpreter side, switching backend is usually enough.
- **Effect handlers on the JS target work via generator delegation.**
  The transpiler wraps the handled expression in a `function*()` and
  uses `yield*` to forward through any nested generators, so
  `perform` lowers to a plain `yield` and resumes correctly. Direct
  performs in the `handle` body and performs from helper functions
  both round-trip; the prior parse error is gone. Effects in code
  paths the C target reaches (the runtime preamble doesn't model
  continuations) are still VM-only.
- **`xs --emit wasm` is the AOT path, not the browser path.** Browsers
  run `xs-browser.wasm` (the runtime build) via the SDK at
  `static.xslang.org/xs.js`; that's full parity with the native VM
  because it *is* the native VM, just compiled to wasm32-wasi. The
  transpiler-emit `--emit wasm` only handles arithmetic and direct
  function calls and is intentionally not being filled in further;
  use `--emit c` for AOT.
- **Cycle collector is opt-in for concurrent mode.** The default GC
  catches reference cycles synchronously (CPython-style trial deletion)
  on a generational schedule. For workloads where the multi-ms free
  walk dominates pause time, set `XS_GC_CONCURRENT=1` to move the
  sweep onto a worker thread; mark stays inline because it's already
  fast. Pause-time SLO documented at the top of `src/core/gc_concurrent.h`.
- **`{` inside double-quoted strings is interpolation.** `"a {x} b"`
  evaluates `x`. To pass a literal `{` (e.g. a JSON blob), use a
  backtick raw string: `` `{"a": 1}` ``. The interpolation grammar
  doesn't have an escape sequence for a single `{`; `{{` collapses
  to one but the inner content is still parsed as an expression.
- **Regex is POSIX-extended, not PCRE - this is the v1.0 answer.** No
  `\d`, `\w`, lookaround, or backreferences. Use `[0-9]`, `[a-zA-Z_]`,
  etc. PCRE adds 30 kB of code for shorthand and a much larger surface
  for catastrophic backtracking; POSIX gives a safer perf envelope.
  If you need the bigger feature set, write a plugin against the regex
  module. Not changing the default pre-1.0.
- **HTTPS server uses BearSSL termination.** Pass a PEM cert + key
  to `http_server_use_tls(server, "cert.pem", "key.pem")` before
  calling `http_server_start`, and the listener attaches a per-
  connection BearSSL engine that handles the handshake + record
  framing transparently. Plain HTTP listeners pay zero cost: the
  conn_recv / conn_send bridge dispatches through the engine only
  when `tls_state` is non-null. SSE and the WebSocket helpers
  (`sse_send_event`, `ws_send_frame`) still take a raw fd and so
  bypass TLS for now; threading them through HTTPConnection is the
  remaining piece.
- **Unicode is byte-oriented and that is the v1.0 answer.** `.len()`,
  `.slice()`, indexing all work on bytes. Multi-byte UTF-8 sequences
  round-trip correctly through every operation that doesn't need
  case-mapping. `.upper()` / `.lower()` are ASCII-only; grapheme-aware
  operations are not implemented. Adding a full ICU-style codepoint /
  grapheme layer balloons the binary and the API surface, and the
  byte model lines up with how real-world text is read off disk and
  socket. If you need graphemes, a stdlib module on top is fine; the
  string primitive stays bytes.
- **Package manager: hosted + git + local.**
  `xs install <name>` hits `https://reg.xslang.org/api/pkg/<name>/latest`,
  pulls the tarball off Supabase storage, and unpacks into `.xs_lib/`.
  `xs search <query>` queries `/api/search`. `xs publish` builds the
  package tarball, base64-encodes it, and POSTs to
  `/api/pkg/<name>/publish` with `Authorization: Bearer <token>`,
  picking the token up from `$XS_REGISTRY_TOKEN` first and then
  `~/.xs/credentials` (chmod 600). Run `xs login` once to store the
  token, `xs whoami` to confirm, `xs logout` to forget it. GitHub
  shorthand (`user/repo`) and `https://...git` keep working as
  before. Override the registry with `-DPKG_REGISTRY_URL=...` at
  build time.
- **JIT covers most of the bytecode.** The register-allocating
  pipeline lowers generators (`fn*`/`yield`) and shadowed-local
  cases through `IR_VM_STEP_CF`. Actor methods - whose dispatcher
  passes an implicit `self` and state-field locals the JIT prologue
  doesn't model - now bail at lower time, including any outer proto
  that builds an actor downstream, so an actor-with-closure works
  uniformly on both backends. Tight arithmetic/branch loops get 5-8x
  over VM; call-heavy workloads sit closer to VM parity until
  call-site fast paths land. Both x86-64 and aarch64 are supported.
- **JIT method-call dispatch on stdlib-module receivers can confuse
  the operand-stack flush** when the result of `fs.read(x)` (or any
  other module method call) is consumed inline as an argument to a
  surrounding call (`println(fs.read(x))`). Storing the intermediate
  to a `let` first works (`let r = fs.read(x); println(r)`). The
  VM and interpreter handle both forms identically; pass `--vm` if
  you hit it. test_fs / test_gc / test_record_prov / test_stdlib_*
  still trip over this on `--jit` and are documented as a v1.0
  carryover for the call-site fast-path rewrite.

## Known Limitations

- JIT lowers a fixed opcode subset; generators + shadowed locals pass
  through. Actor methods (and any proto whose descendants build one)
  bail to the bytecode VM at lower time, so an `actor { ... }` that
  also captures an outer `let` works on both backends without the
  caller doing anything.
- `xs --emit wasm` is parked: arithmetic + direct calls work, the
  rest doesn't. Use `--emit c` for AOT and `xs-browser.wasm` (the
  runtime build) for browsers.
- VM effects: nested perform/handle pairs each push their own
  continuation onto a LIFO stack and `resume` may now fire more than
  once. Each perform captures the live stack `[0..sp_off)` so a
  second resume sees the original locals rather than the mutations
  the first resume left behind. Mutable heap state (arrays, maps,
  closures) is shallow-snapshotted: the array reference replays, but
  pushes the resumed body did still appear on the second resume.
  That matches the documented "values capture, references share"
  rule in `LANGUAGE.md`; deep-cloning heap state on every perform is
  out of scope for 1.0.
- Regex uses POSIX extended syntax only (no `\d`, `\w` shorthand,
  use `[0-9]`, `[a-zA-Z_]`).
- Interpreter call-depth cap is 500 frames (raise with
  `XS_MAX_DEPTH=N`). Hitting it throws a catchable `StackOverflow`
  rather than segfaulting; the VM has its own growable stack.
- JS transpiler effect handlers wrap the handled expression in a
  generator and use `yield*` delegation, so a `perform` lowers to
  `yield` cleanly when the handle body itself yields. Direct
  top-level `perform` outside a `handle` still has no surrounding
  generator and will be a parse error under Node, mirroring the
  language rule that `perform` only makes sense in a handled
  context.
- `http.serve(port, router)` accepts either a single handler
  function (the original loop) or a router map with `routes`,
  `middleware`, and `not_found`. Patterns support `:name` captures
  (populating `req.params`) and a trailing `*` wildcard. The richer
  async router in `src/net/http_server.c` (idle / slow-request
  culling, graceful shutdown, per-server limits) remains C-only
  for cases where you embed xs into a larger server.
- TLS server termination uses BearSSL via
  `http_server_use_tls(server, cert_pem, key_pem)`, and SSE +
  WebSocket helpers (`sse_send_event`, `ws_send_frame`,
  `ws_send_text`, `ws_send_close`, `ws_send_ping`, `ws_send_pong`)
  go through the per-connection bridge so streaming endpoints
  encrypt over HTTPS without each call site doing TLS plumbing. The
  fd-based variants (`sse_send_event_fd`, `sse_send_retry_fd`)
  remain for callers that genuinely run on plain sockets.
- Registry CLI (`xs install`, `xs publish`, `xs search`,
  `xs whoami`, `xs login`, `xs logout`) talks to
  `https://reg.xslang.org` on Linux, macOS, and Windows. The
  Windows path goes through a small raw-socket + BearSSL client
  in `src/pkg/pkg_http.c` so MinGW builds don't need to pull in
  the full async client. wasi targets keep the registry stub.
