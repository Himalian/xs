# Performance

XS ships three execution backends, switched at the command line:

| flag        | backend            | startup | hot loop | notes                   |
|-------------|--------------------|---------|----------|-------------------------|
| `--interp`  | tree-walking       | ~3 ms   | medium   | needed for some plugins |
| `--vm`      | bytecode VM        | ~5 ms   | medium   | default; better at allocation-heavy programs |
| `--jit`     | tier-2 native JIT  | ~5 ms   | fast     | x86-64 + arm64; pick this when you have hot loops |

The CLI default is `--vm`. **The two slow-path backends are closer
than you'd expect**: on tight non-allocating numeric loops the
tree-walker can actually beat the bytecode VM because it skips the
push/pop dispatch overhead. The VM wins on allocation-heavy code,
deeply nested calls, and anything that exercises the inline cache.
For programs with real hot loops, prefer `--jit`. The threshold
scales with bytecode length (25 calls for tiny functions, 200 for
big ones), so cold scripts won't pay the JIT compile cost.

Quick-and-dirty rule:
- One-shot script, mostly stdlib calls -> default `--vm` is fine.
- Tight inner loop with arithmetic -> `--jit`.
- Pre-1.0 plugin code with `before_eval` hooks -> `--interp`.

## Where XS is fast today

From the cross-runtime benchmark suite (`make bench`):

- **Tight integer / numeric loops under `--jit`**: competitive with
  CPython, often beats it.
- **String munging, hashing, sort on built-in types**: faster than
  CPython, often by 2-5×.
- **Startup**: under 5 ms. CPython is ~15 ms, Node ~50 ms.

## Where XS is slower

- **Allocation-heavy real-world programs** (JSON-heavy ETL, deep
  map / object construction): currently ~6-7× slower than CPython.
  The tagged-Value representation forces a heap allocation per
  field per object; we have a few obvious wins queued (small-map
  inline storage, JSON parser with reusable buffers) but they
  haven't landed yet. If you're processing big JSON, prefer
  streaming over in-memory.
- **Sort with a user comparator**: ~50× slower than the default
  comparator because every compare goes through the interpreter
  and the JIT can't yet inline closure callbacks. Use
  `arr.sort_by(|x| x.field)` instead of `arr.sort(|a, b| ...)`
  when you can -- one key-fn call per element is much cheaper than
  one comparator call per pair.

## Where XS is slow today

- **Recursive integer math** (fib, ackermann). The function-call
  overhead and the absence of speculative inlining show.
- **Tight float loops** (mandelbrot, raytrace inner loops). The JIT
  doesn't yet unbox doubles; every operation goes through the
  tagged-Value path.
- **Hot allocation paths**. Each new `[..]` or `#{...}` walks the
  generational allocator. Reusing arrays helps.

## Tuning knobs

Environment:

| variable                    | default | purpose                       |
|-----------------------------|---------|-------------------------------|
| `XS_JIT_CODE_SIZE_MB`       | 4       | JIT code buffer size          |
| `XS_LIMITS_INSTRUCTIONS`    | unset   | hard cap on bytecode op count |
| `XS_LIMITS_MEMORY_MB`       | unset   | RSS cap                       |
| `XS_LIMITS_WALL_SECONDS`    | unset   | wall-clock cap                |

CLI:

```sh
xs --vm   file.xs       # explicit VM, no JIT
xs --jit  file.xs       # force JIT consideration on every proto
xs --strict file.xs     # type checking on, may error
xs --check  file.xs     # type-check only, don't run
```

## Profiling

```sh
xs --profile file.xs > profile.json
xs --profile-flamegraph file.xs > flame.svg
```

The profiler samples at 1 ms via `SIGPROF` (or `CreateTimerQueueTimer`
on Windows). Output is folded-stack format, compatible with FlameGraph
tools.

## Benchmark suite

```sh
make bench                         # one run, prints a markdown table
make bench-compare                 # diff against committed baseline
TOLERANCE=0.10 make bench-compare  # tighter regression gate
```

Adding a bench: drop a `bench_xx.xs` plus matching `bench_xx.{py,js,go}`
into `benchmarks/`, register the entry in `run.sh`. The CI gate uses
`benchmarks/baseline.json` as the reference.

## Optimisation tips

- **Avoid allocating in hot loops**. Pre-create the array, reuse it.
- **Type the hot path**. `xs --check` is honest about where it fell
  back to dynamic dispatch; annotations let the JIT specialise.
- **Use the VM, not the interpreter**. The interpreter exists for
  plugin debugging; the VM is meant for production.
- **Watch the GC**. `gc.stats()` shows allocation pressure;
  `gc.set_threshold(0, 5000)` raises the gen-0 trigger if you want
  fewer collections at the cost of more peak RAM.
