# XS public API surface

What 1.0 freezes, what it doesn't, and how to read the rest of the
docs as a contract instead of a snapshot. Pair this with `POLICY.md`
(tiers, deprecation cycle, LTS cadence) and `LANGUAGE.md` (the
language proper).

## Frozen at 1.0

Everything in this section is Tier 1. Behaviour cannot change inside
the 1.x line; bug fixes that alter observable output ship under an
opt-in flag first.

### Language

- Lexical structure, keyword set, and the operator table from
  `LANGUAGE.md`.
- Gradual typing: bare bindings stay dynamic, annotations are checked
  at call boundaries, `--strict` requires annotations on every
  exported declaration.
- `fn`, `fn*` (generator), `class`, `struct`, `enum`, `trait`, `impl`,
  `actor`, `module`, plus `let`, `var`, `const`, `match`, `for`,
  `while`, `loop`.
- Both `try`/`catch`/`finally` and algebraic effects (`effect`,
  `perform`, `handle`/`resume`). Nested handlers compose.
- Concurrency primitives: `spawn`, `await`, `async`, channels, actors,
  nurseries.
- Imports: `import` for stdlib, `use` for files and packages, `use plugin`
  for compiler plugins.

### Stdlib (Tier 1)

Modules importable without `@unstable`:

- `math`, `string`, `time`, `random`, `fmt`
- `io`, `fs`, `path`, `os`, `process`, `cli`
- `json`, `csv`, `toml`, `url`, `base64`, `hash`, `uuid`, `encode`,
  `msgpack`
- `collections`, `re`, `buf`
- `crypto` (SHA, HMAC, AES, ChaCha20),
  `http` (client + server with router; HTTPS via embedded BearSSL)
- `net` (TCP), `db` (SQLite-compatible)
- `ffi`, `reflect`, `gc`, `log`, `tracing`
- `Promise`, `async`, `thread`

`@unstable` modules at 1.0 (Tier 2; opt-in only):

- `experimental.*` namespace (Tier 3, requires `XS_EXPERIMENTAL=1`).

### CLI (`xs`)

Frozen subcommands:

- Run: `xs <file>`, `xs run`, `xs check`, `xs build`, `xs -e <code>`.
  Bare `xs` enters the REPL on a TTY; with stdin redirected it prints
  the help banner.
- Tooling: `xs fmt`, `xs lint`, `xs doc`, `xs test`, `xs bench`,
  `xs profile`, `xs coverage`, `xs explain`.
- Debug: `xs --record <trace> <file>`, `xs replay`, `xs lsp`, `xs dap`.
- Pkg: `xs new`, `xs install`, `xs remove`, `xs update`, `xs publish`,
  `xs search`, `xs login`, `xs logout`, `xs whoami`,
  `xs pkg <subcmd>` (subcmds: `install`, `remove`, `update`, `list`,
  `add`, `search`, `publish`).
- Transpile: `xs transpile`, `xs --emit {ast,bytecode,ir,js,c,wasm}`.

Frozen flags: `--strict`, `--check`, `--lenient`, `--vm`, `--interp`,
`--jit`, `--backend {interp,vm,jit}`, `--no-color`, `--watch`,
`--gc-debug`, `--optimize`, `--plugin`, `--record`.

### Diagnostic shape

The `error[CODE]: ...` / `--> file:line:col` layout, the `hint:` and
`for more info: xs --explain` lines, and the error code prefixes
(`L0xxx` lexer, `P0xxx` parser, `T0xxx` types, `R0xxx` runtime,
`S0xxx` semantic) are part of the contract. Editors and CI that
parse the output stay valid.

### File formats

- `xs.toml` schema (`[package]`, `[dependencies]`, `[unstable]`,
  `[plugins]`, `[bin]`, `[lib]`).
- `.xsc` bytecode header magic and version field. New fields can be
  appended; an older xs binary refuses an unknown version cleanly.
- `~/.xs/credentials` is a single-line token file (chmod 600).

### Plugin API surface

Surfaces documented in `PLUGINS.md`:
- The `load "..."` form for loading a plugin file.
- Runtime hooks: `plugin.runtime.before_eval(tag, fn)`,
  `plugin.runtime.after_eval(tag, fn)`,
  `plugin.runtime.global.set(name, value)`,
  `plugin.runtime.add_method(type, name, fn)`,
  `plugin.runtime.resolve_import(fn)`,
  `plugin.runtime.on_error(fn)`.
- Parser hooks: `plugin.parser.override(keyword, fn)`,
  `plugin.parser.on_unknown(fn)`,
  `plugin.parser.on_unknown_expr(fn)`,
  `plugin.parser.on_postfix(fn)`.
- Tag filtering: pass the tag string as the first argument to
  `before_eval` / `after_eval` to filter by AST node type.
- The declarative `plugin "name" { meta { ... } parser { ... } lexer { ... } }`
  form for parse-time syntax extension.

Internals not in `PLUGINS.md` (AST node layout, value tag values,
GC trigger heuristics) stay un-frozen; see `POLICY.md` "What isn't
covered."

## Not frozen

These can change inside 1.x:

- GC pause budgets, collection cadence, generational thresholds.
- JIT lowering decisions (which opcodes get fast paths, when tier-2
  is triggered, code-buffer sizes).
- Internal symbols prefixed with `_xs_` or `__`.
- Plugin-injected names from third-party plugins (those are the
  plugin's contract).
- Inline cache layout, `vm->global_version` semantics, hardcoded
  VM offsets used by the JIT.
- `xs --emit` output formatting beyond "the result is valid in the
  named target language."
- Bench numbers in `STATUS.md`.

## Deprecation / removal expectations

See `POLICY.md` "Deprecation" for the rules. Summary:

1. `@deprecated("use X", since="1.y.z")` lands.
2. Next minor flips on the `D0xxx` warning at every use site.
3. Earliest removal is the next major (`2.0`).

Tier 2 (`@unstable`) and Tier 3 (`experimental.*`) opt out of this;
they can shift or vanish freely.

## How additions ship

- New stdlib modules / functions: minor bump.
- New language syntax: minor bump (parser is forward-compatible: an
  older xs running on newer code surfaces a clear `P0xxx` rather
  than silently miscompiling).
- New CLI subcommands or flags: minor bump.
- Anything that changes existing behaviour: opt-in flag, then minor
  bump that flips the default behind a deprecation warning, then
  major bump that removes the flag.

## Reporting a regression

If a 1.x release breaks a Tier 1 documented behaviour, that's a
release-blocking bug. File at
<https://github.com/xs-lang0/xs/issues> with a minimal reproducer
and the output of `xs --version`. We treat Tier 1 regressions as
P0 and ship a patch within the support window in `POLICY.md`.
