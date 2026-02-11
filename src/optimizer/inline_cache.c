/* inline_cache.c -- polymorphic inline caching for method dispatch. */

#include "optimizer/inline_cache.h"
#include <string.h>

InlineCacheTable g_ic_table;

void ic_init(void) {
    memset(&g_ic_table, 0, sizeof(g_ic_table));
}

void ic_reset(void) {
    memset(&g_ic_table, 0, sizeof(g_ic_table));
}

Value *ic_lookup_ex(int site_id, int64_t type_tag, const char *name_ptr,
                    uint8_t *is_module_out, uint8_t *needs_self_out) {
    if (site_id < 0 || site_id >= IC_MAX_ENTRIES) return NULL;

    int nslots = g_ic_table.slot_count[site_id];
    for (int i = 0; i < nslots; i++) {
        InlineCacheEntry *e = &g_ic_table.entries[site_id][i];
        if (e->type_tag == type_tag && e->method_name == name_ptr && e->cached_method) {
            e->hits++;
            g_ic_table.total_hits++;
            if (is_module_out)  *is_module_out  = e->is_module;
            if (needs_self_out) *needs_self_out = e->needs_self;
            return e->cached_method;
        }
    }
    g_ic_table.total_misses++;
    return NULL;
}

Value *ic_lookup(int site_id, int64_t type_tag, const char *name_ptr) {
    return ic_lookup_ex(site_id, type_tag, name_ptr, NULL, NULL);
}

void ic_update_ex(int site_id, int64_t type_tag, const char *name_ptr,
                  Value *method, uint8_t is_module, uint8_t needs_self) {
    if (site_id < 0 || site_id >= IC_MAX_ENTRIES) return;
    if (!method) return;

    int nslots = g_ic_table.slot_count[site_id];

    /* check if already cached */
    for (int i = 0; i < nslots; i++) {
        InlineCacheEntry *e = &g_ic_table.entries[site_id][i];
        if (e->type_tag == type_tag && e->method_name == name_ptr) {
            e->cached_method = method;
            e->is_module = is_module;
            e->needs_self = needs_self;
            return;
        }
    }

    /* add new entry if room */
    if (nslots < IC_POLY_SLOTS) {
        InlineCacheEntry *e = &g_ic_table.entries[site_id][nslots];
        e->type_tag = type_tag;
        e->method_name = name_ptr;
        e->cached_method = method;
        e->is_module = is_module;
        e->needs_self = needs_self;
        e->hits = 0;
        g_ic_table.slot_count[site_id] = nslots + 1;
    } else {
        /* cache is full for this site, evict least-used */
        int min_hits = g_ic_table.entries[site_id][0].hits;
        int min_idx = 0;
        for (int i = 1; i < IC_POLY_SLOTS; i++) {
            if (g_ic_table.entries[site_id][i].hits < min_hits) {
                min_hits = g_ic_table.entries[site_id][i].hits;
                min_idx = i;
            }
        }
        InlineCacheEntry *e = &g_ic_table.entries[site_id][min_idx];
        e->type_tag = type_tag;
        e->method_name = name_ptr;
        e->cached_method = method;
        e->is_module = is_module;
        e->needs_self = needs_self;
        e->hits = 0;
    }
}

void ic_update(int site_id, int64_t type_tag, const char *name_ptr, Value *method) {
    ic_update_ex(site_id, type_tag, name_ptr, method, 0, 0);
}

void ic_stats(int *hits, int *misses) {
    if (hits) *hits = g_ic_table.total_hits;
    if (misses) *misses = g_ic_table.total_misses;
}
