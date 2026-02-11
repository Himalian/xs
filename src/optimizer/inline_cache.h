/* inline_cache.h -- polymorphic inline caching for method dispatch. */
#ifndef INLINE_CACHE_H
#define INLINE_CACHE_H

#include "core/xs.h"

typedef struct {
    int64_t     type_tag;       /* receiver identity (map pointer) */
    const char *method_name;    /* const pool pointer, compared by identity */
    Value      *cached_method;  /* the resolved method (not owned) */
    /* Flags captured alongside the method lookup so the hot path can
       dispatch without re-querying the receiver map. Set during
       ic_update from the resolution site. */
    uint8_t     is_module;      /* receiver behaves like a module (no implicit self) */
    uint8_t     needs_self;     /* closure expects self as first param */
    int         hits;           /* cache hit counter */
} InlineCacheEntry;

/* 16k sites: keeps aliasing rare even in programs with thousands of
   call sites, while staying under a megabyte of total table memory
   (16k * 4 slots * sizeof(entry) ~= 2 MB). */
#define IC_MAX_ENTRIES    16384
#define IC_POLY_SLOTS     4     /* polymorphic: up to 4 types per call site */

typedef struct {
    InlineCacheEntry entries[IC_MAX_ENTRIES][IC_POLY_SLOTS];
    int              slot_count[IC_MAX_ENTRIES]; /* how many slots used per site */
    int              total_hits;
    int              total_misses;
} InlineCacheTable;

/* global inline cache */
extern InlineCacheTable g_ic_table;

/* init/reset the cache */
void ic_init(void);
void ic_reset(void);

/* compute a call site ID from file/line or node_id. Fibonacci hash on
   the 32-bit mixed token, then mask to the table size; this scatters
   adjacent IP offsets (common for dense call sites in one proto) into
   distinct slots rather than clustering them. */
static inline int ic_site_id(int node_id) {
    uint32_t u = (uint32_t)node_id;
    u = (u ^ (u >> 16)) * 0x9E3779B1u;
    return (int)(u & (IC_MAX_ENTRIES - 1));
}

/* lookup a cached method for the given receiver and method-name const.
   name_ptr is compared by identity (the const pool gives each site a
   stable pointer, so strcmp is unnecessary on the hot path). Returns
   the cached Value* or NULL on miss. */
Value *ic_lookup(int site_id, int64_t type_tag, const char *name_ptr);

/* variant that also yields the module/self flags captured at update time */
Value *ic_lookup_ex(int site_id, int64_t type_tag, const char *name_ptr,
                    uint8_t *is_module_out, uint8_t *needs_self_out);

/* insert/update a cache entry after a successful method resolution. */
void ic_update(int site_id, int64_t type_tag, const char *name_ptr, Value *method);

/* richer update that also records dispatch flags so the next hit can
   skip the map_get probes that derive them. */
void ic_update_ex(int site_id, int64_t type_tag, const char *name_ptr,
                  Value *method, uint8_t is_module, uint8_t needs_self);

/* get cache stats */
void ic_stats(int *hits, int *misses);

#endif /* INLINE_CACHE_H */
