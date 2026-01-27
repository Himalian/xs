/* Feature macros: we need clock_gettime(CLOCK_MONOTONIC) AND
   ru_maxrss. On Linux both live under the POSIX 2008 / X/Open set;
   on macOS/FreeBSD ru_maxrss is a BSD extension that disappears when
   _POSIX_C_SOURCE is defined alone, so pull in the BSD set as well.
   On WASI we skip rusage entirely further down. */
#if defined(__linux__)
#  define _POSIX_C_SOURCE 200809L
#  define _GNU_SOURCE
#elif defined(__APPLE__)
#  define _DARWIN_C_SOURCE
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  define _BSD_SOURCE
#endif

#include "core/limits.h"
#include "core/ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#  define LIMITS_HAVE_RUSAGE 0
#elif defined(__wasi__) || defined(__EMSCRIPTEN__)
/* WASI rusage emulation needs -D_WASI_EMULATED_PROCESS_CLOCKS and the
   matching lib; we don't link it, so skip the RSS probe entirely. */
#  include <unistd.h>
#  define LIMITS_HAVE_RUSAGE 0
#else
#  include <sys/resource.h>
#  include <unistd.h>
#  define LIMITS_HAVE_RUSAGE 1
#endif

/* One counter set per thread. The VM / interpreter are single-threaded
   per context; threads spawned by XS code have their own budgets. */
static __thread uint64_t   g_instr_budget   = 0;
static __thread uint64_t   g_instr_used     = 0;
static __thread uint64_t   g_wall_budget_ms = 0;
static __thread uint64_t   g_wall_start_ns  = 0;
static __thread size_t     g_mem_budget     = 0;
static __thread int        g_exceeded       = 0;
static __thread uint64_t   g_tick_gate      = 0;

/* How often the tick path re-checks wall-time and memory. Instruction
   budget is exact; the other two are sampled. 4096 opcodes at a few
   ns each is ~10us between checks, which is fine-grained enough for
   a human-meaningful deadline and cheap enough to not dominate hot
   loops. */
#define TICK_GATE_MASK 0xFFFu

static uint64_t monotonic_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static size_t process_rss_bytes(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)pmc.WorkingSetSize;
    }
    return 0;
#elif LIMITS_HAVE_RUSAGE
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
#  if defined(__APPLE__)
    /* Darwin reports ru_maxrss in bytes. */
    return (size_t)ru.ru_maxrss;
#  else
    /* Linux/BSD report in kilobytes. */
    return (size_t)ru.ru_maxrss * 1024u;
#  endif
#else
    /* No process RSS probe available (WASI). Memory cap is a no-op. */
    return 0;
#endif
}

void xs_limits_reset(void) {
    g_instr_used    = 0;
    g_wall_start_ns = (g_wall_budget_ms > 0) ? monotonic_ns() : 0;
    g_exceeded      = 0;
    g_tick_gate     = 0;
}

void xs_limits_clear(void) {
    g_instr_budget   = 0;
    g_wall_budget_ms = 0;
    g_mem_budget     = 0;
    xs_limits_reset();
}

void xs_limits_set_instructions(uint64_t budget) {
    g_instr_budget = budget;
}

void xs_limits_set_wall_time_ms(uint64_t ms) {
    g_wall_budget_ms = ms;
    g_wall_start_ns  = (ms > 0) ? monotonic_ns() : 0;
}

void xs_limits_set_memory_bytes(size_t bytes) {
    g_mem_budget = bytes;
}

uint64_t xs_limits_get_instructions_budget(void) { return g_instr_budget; }
uint64_t xs_limits_get_instructions_used(void)   { return g_instr_used; }
uint64_t xs_limits_get_wall_time_budget_ms(void) { return g_wall_budget_ms; }
size_t   xs_limits_get_memory_budget(void)       { return g_mem_budget; }
size_t   xs_limits_get_memory_rss(void)          { return process_rss_bytes(); }

int xs_limits_exceeded(void) { return g_exceeded; }

const char *xs_limits_exceeded_name(void) {
    switch (g_exceeded) {
        case XS_LIMIT_INSTRUCTIONS: return "instruction budget";
        case XS_LIMIT_WALL_TIME:    return "wall-time budget";
        case XS_LIMIT_MEMORY:       return "memory budget";
        default:                    return "none";
    }
}

int xs_limits_check(void) {
    if (g_exceeded) return g_exceeded;
    if (g_instr_budget && g_instr_used >= g_instr_budget) {
        g_exceeded = XS_LIMIT_INSTRUCTIONS;
        return g_exceeded;
    }
    if (g_wall_budget_ms) {
        uint64_t now = monotonic_ns();
        uint64_t elapsed_ms = (now - g_wall_start_ns) / 1000000ULL;
        if (elapsed_ms >= g_wall_budget_ms) {
            g_exceeded = XS_LIMIT_WALL_TIME;
            return g_exceeded;
        }
    }
    if (g_mem_budget) {
        size_t rss = process_rss_bytes();
        if (rss > g_mem_budget) {
            g_exceeded = XS_LIMIT_MEMORY;
            return g_exceeded;
        }
    }
    return 0;
}

int xs_limits_tick(void) {
    if (g_exceeded) return g_exceeded;
    g_instr_used++;
    if (g_instr_budget && g_instr_used >= g_instr_budget) {
        g_exceeded = XS_LIMIT_INSTRUCTIONS;
        return g_exceeded;
    }
    if ((++g_tick_gate & TICK_GATE_MASK) == 0) {
        if (g_wall_budget_ms) {
            uint64_t now = monotonic_ns();
            uint64_t elapsed_ms = (now - g_wall_start_ns) / 1000000ULL;
            if (elapsed_ms >= g_wall_budget_ms) {
                g_exceeded = XS_LIMIT_WALL_TIME;
                return g_exceeded;
            }
        }
        if (g_mem_budget) {
            size_t rss = process_rss_bytes();
            if (rss > g_mem_budget) {
                g_exceeded = XS_LIMIT_MEMORY;
                return g_exceeded;
            }
        }
    }
    return 0;
}

/* xs_limits_throw_if_exceeded used to live here but pulled
   runtime/error.c into every target that linked limits.c (unit tests,
   parser fuzzer, WASM). Callers now inline the xs_runtime_error call
   themselves; interp/vm already depend on it. */
