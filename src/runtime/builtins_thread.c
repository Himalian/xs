#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "runtime/builtins.h"
#include "runtime/concurrent.h"
#include "core/value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* threads */
#include "core/xs_thread.h"

typedef struct {
    Interp *interp;  /* parent interp (used read-only for call_value) */
    Value  *fn;      /* function to call: incref'd before thread start */
    Value  *result;  /* output: set by the thread */
} ThreadArg;

static void *thread_entry(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    /* Call the XS function in isolation.
       Since XS values are not thread-safe we keep it simple:
       the function receives no arguments and its return value is
       stored for later retrieval via thread.join(). */
    ta->result = call_value(ta->interp, ta->fn, NULL, 0, "thread.spawn");
    return NULL;
}

/* forward declaration so the non-POSIX stub in spawn can reference join */
static Value *native_thread_join(Interp *ig, Value **a, int n);

static Value *native_thread_spawn(Interp *ig, Value **a, int n) {
    if (n < 1 || (VAL_TAG(a[0]) != XS_FUNC && VAL_TAG(a[0]) != XS_NATIVE))
        return xs_str("error: thread.spawn requires a callable");

    ThreadArg *ta = xs_malloc(sizeof(ThreadArg));
    ta->interp = ig;
    ta->fn     = value_incref(a[0]);
    ta->result = NULL;

    xs_thread_t tid;
    if (xs_thread_create(&tid, thread_entry, ta) != 0) {
        value_decref(ta->fn);
        free(ta);
        return xs_str("error: thread creation failed");
    }

    XSMap *handle = map_new();
    map_take(handle, "_tid", xs_int((int64_t)(uintptr_t)tid));
    map_take(handle, "_targ", xs_int((int64_t)(uintptr_t)ta));
    map_set(handle, "status", xs_str("running"));
    return xs_module(handle);
}

static Value *native_thread_join(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return xs_str("error: thread.join requires a thread handle");
    Value *tid_v  = map_get(a[0]->map, "_tid");
    Value *targ_v = map_get(a[0]->map, "_targ");
    if (!tid_v || VAL_TAG(tid_v) != XS_INT || !targ_v || VAL_TAG(targ_v) != XS_INT)
        return xs_str("error: invalid thread handle");

    xs_thread_t tid = (xs_thread_t)(uintptr_t)VAL_INT(tid_v);
    ThreadArg *ta = (ThreadArg *)(uintptr_t)VAL_INT(targ_v);

    int err = xs_thread_join(tid, NULL);
    if (err != 0)
        return xs_str("error: thread join failed");

    Value *result = ta->result ? ta->result : value_incref(XS_NULL_VAL);
    value_decref(ta->fn);
    free(ta);

    /* Update handle status */
    map_set(a[0]->map, "status", xs_str("joined"));

    return result;
}

static Value *native_thread_id(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    return xs_int((int64_t)xs_thread_self_id());
}

static Value *native_thread_cpu_count(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
#if defined(_SC_NPROCESSORS_ONLN)
    return xs_int(sysconf(_SC_NPROCESSORS_ONLN));
#else
    return xs_int(1);
#endif
}

static Value *native_thread_sleep(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return value_incref(XS_NULL_VAL);
    double secs = 0.0;
    if (VAL_TAG(a[0]) == XS_FLOAT) secs = a[0]->f;
    else if (VAL_TAG(a[0]) == XS_INT) secs = (double)VAL_INT(a[0]);
    xs_thread_sleep_ns(secs);
    return value_incref(XS_NULL_VAL);
}

/* cross-platform mutex implementation */

static xs_mutex_t *mutex_from_map(XSMap *m) {
    Value *pv = map_get(m, "_ptr");
    if (!pv || VAL_TAG(pv) != XS_INT) return NULL;
    return (xs_mutex_t *)(uintptr_t)VAL_INT(pv);
}

static Value *native_mutex_lock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    /* 'self' is passed as the first argument by the method-call dispatch */
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_lock(mtx);
    if (err == 0) map_set(a[0]->map, "locked", value_incref(XS_TRUE_VAL));
    return err == 0 ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_unlock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_unlock(mtx);
    if (err == 0) map_set(a[0]->map, "locked", value_incref(XS_FALSE_VAL));
    return err == 0 ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_try_lock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_trylock(mtx);
    if (err == 0) {
        map_set(a[0]->map, "locked", value_incref(XS_TRUE_VAL));
        return value_incref(XS_TRUE_VAL);
    }
    return value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_destroy_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE))
        return value_incref(XS_NULL_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (mtx) {
        xs_mutex_destroy(mtx);
        free(mtx);
        /* Clear the pointer so double-destroy is harmless */
        map_take(a[0]->map, "_ptr", xs_int(0));
    }
    return value_incref(XS_NULL_VAL);
}

static Value *native_thread_mutex(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    xs_mutex_t *mtx = xs_malloc(sizeof(xs_mutex_t));
    if (xs_mutex_init(mtx) != 0) {
        free(mtx);
        return value_incref(XS_NULL_VAL);
    }
    XSMap *m = map_new();
    /* Store the mutex pointer as an opaque int (same pattern as XSBuf) */
    map_take(m, "_ptr", xs_int((int64_t)(uintptr_t)mtx));
    map_set(m, "locked", value_incref(XS_FALSE_VAL));
    map_take(m, "lock",    xs_native(native_mutex_lock_fn));
    map_take(m, "unlock",  xs_native(native_mutex_unlock_fn));
    map_take(m, "try_lock", xs_native(native_mutex_try_lock_fn));
    map_take(m, "destroy", xs_native(native_mutex_destroy_fn));
    return xs_module(m);
}

Value *make_thread_module(void) {
    XSMap *m = map_new();
    map_take(m, "spawn",     xs_native(native_thread_spawn));
    map_take(m, "join",      xs_native(native_thread_join));
    map_take(m, "id",        xs_native(native_thread_id));
    map_take(m, "cpu_count", xs_native(native_thread_cpu_count));
    map_take(m, "sleep",     xs_native(native_thread_sleep));
    map_take(m, "mutex",     xs_native(native_thread_mutex));
    return xs_module(m);
}
