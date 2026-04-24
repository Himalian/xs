# Changelog

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
