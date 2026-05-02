/* Real concurrency: pthread-backed spawn + GIL + blocking channels.
   Replaces the previous "spawn runs synchronously, channel.recv throws
   on empty" placeholder. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#include "runtime/concurrent.h"
#include "runtime/interp.h"
#include "runtime/error.h"
#include "core/value.h"
#include "core/env.h"
#include <stdlib.h>
#include <string.h>

/* The GIL itself plus a recursion counter. Recursion lets the same
   thread reacquire the lock without deadlocking on nested interp
   entries. */
static xs_mutex_t  g_gil;
static int         g_gil_init_done = 0;

void xs_gil_init(void) {
    if (g_gil_init_done) return;
#if defined(_WIN32) || defined(__MINGW32__)
    xs_mutex_init(&g_gil);
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_gil, &attr);
    pthread_mutexattr_destroy(&attr);
#endif
    g_gil_init_done = 1;
    /* main thread enters holding the lock */
    xs_mutex_lock(&g_gil);
}

void xs_gil_acquire(void) {
    if (!g_gil_init_done) xs_gil_init();
    xs_mutex_lock(&g_gil);
}

void xs_gil_release(void) {
    if (!g_gil_init_done) return;
    xs_mutex_unlock(&g_gil);
}

/* Per-task record. Stays alive after the thread exits so callers can
   await the result. The owning future map holds a refcount via _task_id. */
typedef struct ThreadTask {
    int                 id;
    int                 done;
    int                 errored;
    int                 cancelled;     /* nursery cancellation flag */
    int                 nursery_id;    /* 0 = standalone; else nursery instance id */
    Value              *closure;       /* incref'd while running */
    Value              *result;        /* set under task_mu when done */
    Value              *error;         /* captured cf.value on errored exit */
    xs_mutex_t          mu;
    xs_cond_t           cv;
    struct ThreadTask  *next;
} ThreadTask;

static xs_mutex_t   g_tasks_mu;
static int          g_tasks_mu_init = 0;
static ThreadTask  *g_tasks_head    = NULL;
static int          g_next_task_id  = 1;

static void tasks_mu_init_once(void) {
    if (g_tasks_mu_init) return;
    xs_mutex_init(&g_tasks_mu);
    g_tasks_mu_init = 1;
}

static _Thread_local int  tls_nursery_id = 0;
static _Thread_local ThreadTask *tls_current_task = NULL;
static _Thread_local int *tls_self_cancel_ptr = NULL;
static int g_next_nursery_id = 1;

void xs_task_set_self_cancel_ptr(int *flag) { tls_self_cancel_ptr = flag; }

int xs_nursery_alloc_id(void) {
    tasks_mu_init_once();
    xs_mutex_lock(&g_tasks_mu);
    int id = g_next_nursery_id++;
    xs_mutex_unlock(&g_tasks_mu);
    return id;
}

int  xs_nursery_current_id(void)        { return tls_nursery_id; }
void xs_nursery_set_current_id(int id)  { tls_nursery_id = id; }

int xs_task_is_cancelled(void) {
    if (tls_self_cancel_ptr) return *tls_self_cancel_ptr;
    return tls_current_task ? tls_current_task->cancelled : 0;
}

/* Mark every still-running task that shares this nursery_id (other
   than the throwing task itself) as cancelled, and wake any pending
   condvars so they re-check the flag. Caller must hold g_tasks_mu. */
static void mark_siblings_cancelled_locked(int nursery_id, int self_id) {
    if (nursery_id == 0) return;
    for (ThreadTask *t = g_tasks_head; t; t = t->next) {
        if (t->id == self_id) continue;
        if (t->nursery_id != nursery_id) continue;
        xs_mutex_lock(&t->mu);
        if (!t->done) {
            t->cancelled = 1;
            xs_cond_broadcast(&t->cv);
        }
        xs_mutex_unlock(&t->mu);
    }
}

static ThreadTask *tasks_find(int id) {
    for (ThreadTask *t = g_tasks_head; t; t = t->next)
        if (t->id == id) return t;
    return NULL;
}

typedef struct {
    ThreadTask *task;
    Interp     *interp;   /* shared with main; protected by GIL */
} ThreadArg;

static void *thread_entry(void *arg_) {
    ThreadArg *arg = (ThreadArg *)arg_;
    ThreadTask *t  = arg->task;
    Interp     *ig = arg->interp;
    free(arg);

    xs_gil_acquire();
    tls_current_task = t;
    tls_self_cancel_ptr = &t->cancelled;
    tls_nursery_id   = t->nursery_id;
    /* Save and clear control-flow signals so the spawned task starts
       clean and any throw it does is captured here, not propagated to
       the parent. */
    int  saved_signal = ig->cf.signal;
    Value *saved_val  = ig->cf.value;
    ig->cf.signal = 0;
    ig->cf.value  = NULL;

    Value *r = call_value(ig, t->closure, NULL, 0, "spawn");
    int errored = (ig->cf.signal == CF_THROW || ig->cf.signal == CF_ERROR
                   || ig->cf.signal == CF_PANIC);
    /* Hand the captured throw value off to the task record so a
       later xs_await_task_ex can re-raise it on the awaiter. */
    Value *captured_err = NULL;
    if (errored && ig->cf.value) {
        captured_err = ig->cf.value;
        ig->cf.value = NULL;
    }

    /* Restore parent signals; the spawned task must not poison them. */
    if (ig->cf.value) value_decref(ig->cf.value);
    ig->cf.signal = saved_signal;
    ig->cf.value  = saved_val;

    xs_mutex_lock(&t->mu);
    t->result  = r ? r : value_incref(XS_NULL_VAL);
    t->error   = captured_err;
    t->errored = errored;
    t->done    = 1;
    xs_cond_broadcast(&t->cv);
    xs_mutex_unlock(&t->mu);

    /* If we errored inside a nursery, propagate cancel to siblings so
       any blocked sleep / recv wakes up and bails before its body
       prints or sends. */
    if (errored && t->nursery_id != 0) {
        xs_mutex_lock(&g_tasks_mu);
        mark_siblings_cancelled_locked(t->nursery_id, t->id);
        xs_mutex_unlock(&g_tasks_mu);
    }

    tls_current_task = NULL;
    tls_self_cancel_ptr = NULL;
    xs_gil_release();
    return NULL;
}

Value *xs_spawn_thread(Interp *parent, Value *closure) {
    if (!closure || (VAL_TAG(closure) != XS_FUNC && VAL_TAG(closure) != XS_NATIVE)) {
        return value_incref(XS_NULL_VAL);
    }
    tasks_mu_init_once();

    ThreadTask *t = xs_calloc(1, sizeof(ThreadTask));
    xs_mutex_lock(&g_tasks_mu);
    t->id = g_next_task_id++;
    t->next = g_tasks_head;
    g_tasks_head = t;
    xs_mutex_unlock(&g_tasks_mu);

    /* Stamp the spawning thread's nursery id onto the new task so a
       sibling failure can find and cancel us. */
    t->nursery_id = tls_nursery_id;
    t->closure = value_incref(closure);
    xs_mutex_init(&t->mu);
    xs_cond_init(&t->cv);

    ThreadArg *arg = xs_malloc(sizeof(ThreadArg));
    arg->task   = t;
    arg->interp = parent;

    xs_thread_t th;
    if (xs_thread_create(&th, thread_entry, arg) != 0) {
        free(arg);
        return value_incref(XS_NULL_VAL);
    }
    xs_thread_detach(th);

    /* Future-shaped result map for the caller to await on. */
    Value *fut = xs_map_new();
    Value *id  = xs_int(t->id);
    map_set(fut->map, "_task_id", id);
    value_decref(id);
    Value *kind = xs_str("task");
    map_set(fut->map, "_kind", kind);
    value_decref(kind);
    return fut;
}

Value *xs_await_task(int task_id) {
    return xs_await_task_ex(task_id, NULL, NULL);
}

Value *xs_await_task_ex(int task_id, int *errored_out, Value **err_out) {
    if (errored_out) *errored_out = 0;
    if (err_out)     *err_out = NULL;
    if (task_id <= 0) return value_incref(XS_NULL_VAL);
    tasks_mu_init_once();

    xs_mutex_lock(&g_tasks_mu);
    ThreadTask *t = tasks_find(task_id);
    xs_mutex_unlock(&g_tasks_mu);
    if (!t) return value_incref(XS_NULL_VAL);

    /* Release the GIL so the spawned thread can actually run. */
    xs_gil_release();
    xs_mutex_lock(&t->mu);
    while (!t->done) xs_cond_wait(&t->cv, &t->mu);
    Value *r = t->result ? value_incref(t->result) : value_incref(XS_NULL_VAL);
    if (errored_out) *errored_out = t->errored;
    if (err_out && t->error) {
        *err_out  = t->error;
        t->error  = NULL;
        t->errored = 0;
    }
    xs_mutex_unlock(&t->mu);
    xs_gil_acquire();
    return r;
}

/* Drain any unjoined interp-spawned tasks, mirroring vm_drain_tasks.
   Called from main's atexit so a fire-and-forget spawn { sleep_ms(N) }
   actually completes before the process exits, even on the
   interpreter backend (which previously detached its workers and
   then walked away while they were still mid-sleep). */
void xs_drain_interp_tasks(void) {
    if (!g_tasks_mu_init) return;
    for (;;) {
        xs_mutex_lock(&g_tasks_mu);
        ThreadTask *pending = NULL;
        for (ThreadTask *t = g_tasks_head; t; t = t->next) {
            xs_mutex_lock(&t->mu);
            int done = t->done;
            xs_mutex_unlock(&t->mu);
            if (!done) { pending = t; break; }
        }
        xs_mutex_unlock(&g_tasks_mu);
        if (!pending) break;
        xs_gil_release();
        xs_mutex_lock(&pending->mu);
        while (!pending->done) xs_cond_wait(&pending->cv, &pending->mu);
        xs_mutex_unlock(&pending->mu);
        xs_gil_acquire();
    }
}

/* --- channels backed by mutex+condvar -------------------------------- */
/* The channel is a regular XS_MAP that stores an integer "_chan_id" key.
   The actual mutex+condvar+buffer live in a process-global table indexed
   by that id. This avoids extending the Value type tag with a raw-ptr
   variant. */

typedef struct ChanState {
    xs_mutex_t mu;
    xs_cond_t  cv;
    int        cap;     /* 0 = unbounded */
    int        closed;
    /* The buffer is owned by the XS_MAP slot "_buf"; the ChanState only
       provides synchronization. We still cache an XSArray* here for
       speed but it shares ownership with the map slot. */
} ChanState;

#define MAX_CHANS 4096
static ChanState g_chans[MAX_CHANS];
static int       g_n_chans = 0;
static xs_mutex_t g_chans_mu;
static int       g_chans_mu_init = 0;

static void chans_mu_init_once(void) {
    if (g_chans_mu_init) return;
    xs_mutex_init(&g_chans_mu);
    g_chans_mu_init = 1;
}

int xs_chan_alloc(int cap) {
    chans_mu_init_once();
    xs_mutex_lock(&g_chans_mu);
    if (g_n_chans >= MAX_CHANS) {
        xs_mutex_unlock(&g_chans_mu);
        return -1;
    }
    int id = g_n_chans++;
    xs_mutex_init(&g_chans[id].mu);
    xs_cond_init(&g_chans[id].cv);
    g_chans[id].cap    = cap > 0 ? cap : 0;
    g_chans[id].closed = 0;
    xs_mutex_unlock(&g_chans_mu);
    return id;
}

static ChanState *chan_state(Value *ch) {
    if (!ch || (VAL_TAG(ch) != XS_MAP && VAL_TAG(ch) != XS_MODULE) || !ch->map) return NULL;
    Value *p = map_get(ch->map, "_chan_id");
    if (!p || VAL_TAG(p) != XS_INT) return NULL;
    int id = (int)VAL_INT(p);
    if (id < 0 || id >= g_n_chans) return NULL;
    return &g_chans[id];
}

static XSArray *chan_buf(Value *ch) {
    if (!ch || (VAL_TAG(ch) != XS_MAP && VAL_TAG(ch) != XS_MODULE) || !ch->map) return NULL;
    Value *b = map_get(ch->map, "_buf");
    if (!b || VAL_TAG(b) != XS_ARRAY) return NULL;
    return b->arr;
}

int xs_chan_send(Value *ch, Value *v) {
    ChanState *cs = chan_state(ch);
    XSArray   *bf = chan_buf(ch);
    if (!cs || !bf) return 1;

    /* If bounded, wait for a free slot. Drop the GIL so a peer recv
       can actually run and make space. */
    if (cs->cap > 0) {
        xs_gil_release();
        xs_mutex_lock(&cs->mu);
        while (!cs->closed && bf->len >= cs->cap) {
            xs_cond_wait(&cs->cv, &cs->mu);
        }
        if (cs->closed) {
            xs_mutex_unlock(&cs->mu);
            xs_gil_acquire();
            return 0;
        }
        array_push(bf, v);
        xs_cond_broadcast(&cs->cv);
        xs_mutex_unlock(&cs->mu);
        xs_gil_acquire();
        return 1;
    }

    xs_mutex_lock(&cs->mu);
    if (cs->closed) {
        xs_mutex_unlock(&cs->mu);
        return 0;
    }
    array_push(bf, v);
    xs_cond_broadcast(&cs->cv);
    xs_mutex_unlock(&cs->mu);
    return 1;
}

Value *xs_chan_recv(Value *ch, Interp *interp) {
    (void)interp;
    ChanState *cs = chan_state(ch);
    XSArray   *bf = chan_buf(ch);
    if (!cs || !bf) return value_incref(XS_NULL_VAL);

    /* Drop the GIL while we sit on the channel condvar so other threads
       (and the sender we are waiting on) can actually run. */
    xs_gil_release();
    xs_mutex_lock(&cs->mu);
    int cancelled = 0;
    while (bf->len == 0 && !cs->closed) {
        /* Time-bounded wait so a sibling cancellation flipped on this
           task is observed even when no sender ever broadcasts. */
        xs_cond_timedwait_ms(&cs->cv, &cs->mu, 25);
        if (tls_current_task && tls_current_task->cancelled) {
            cancelled = 1;
            break;
        }
    }
    if (cancelled) {
        xs_mutex_unlock(&cs->mu);
        xs_gil_acquire();
        return value_incref(XS_NULL_VAL);
    }
    if (bf->len == 0) {
        /* closed and drained: stop blocking, return null */
        xs_mutex_unlock(&cs->mu);
        xs_gil_acquire();
        return value_incref(XS_NULL_VAL);
    }
    /* Take ownership of the existing slot's refcount instead of
       incref-ing under the channel mutex (refcount mutation needs the
       GIL). The sender already incref'd when it pushed; we just steal
       that reference and shift the buffer. */
    Value *val = bf->items[0];
    for (int i = 0; i < bf->len - 1; i++) bf->items[i] = bf->items[i + 1];
    bf->len--;
    /* A bounded sender may be waiting for buffer space; wake them. */
    xs_cond_broadcast(&cs->cv);
    xs_mutex_unlock(&cs->mu);
    xs_gil_acquire();
    return val;
}

Value *xs_chan_try_recv(Value *ch) {
    ChanState *cs = chan_state(ch);
    XSArray   *bf = chan_buf(ch);
    if (!cs || !bf) return value_incref(XS_NULL_VAL);

    xs_mutex_lock(&cs->mu);
    Value *val = NULL;
    if (bf->len > 0) {
        val = value_incref(bf->items[0]);
        for (int i = 0; i < bf->len - 1; i++) bf->items[i] = bf->items[i + 1];
        bf->len--;
        xs_cond_broadcast(&cs->cv);
    }
    xs_mutex_unlock(&cs->mu);
    return val ? val : value_incref(XS_NULL_VAL);
}

int xs_chan_len(Value *ch) {
    ChanState *cs = chan_state(ch);
    XSArray   *bf = chan_buf(ch);
    if (!cs || !bf) return 0;
    xs_mutex_lock(&cs->mu);
    int n = bf->len;
    xs_mutex_unlock(&cs->mu);
    return n;
}

int xs_chan_cap(Value *ch) {
    ChanState *cs = chan_state(ch);
    if (!cs) return 0;
    return cs->cap;
}

int xs_chan_is_full(Value *ch) {
    ChanState *cs = chan_state(ch);
    XSArray   *bf = chan_buf(ch);
    if (!cs || !bf) return 0;
    if (cs->cap <= 0) return 0;
    xs_mutex_lock(&cs->mu);
    int full = bf->len >= cs->cap;
    xs_mutex_unlock(&cs->mu);
    return full;
}

int xs_chan_is_closed(Value *ch) {
    ChanState *cs = chan_state(ch);
    if (!cs) return 0;
    xs_mutex_lock(&cs->mu);
    int c = cs->closed;
    xs_mutex_unlock(&cs->mu);
    return c;
}

void xs_chan_close(Value *ch) {
    ChanState *cs = chan_state(ch);
    if (!cs) return;
    xs_mutex_lock(&cs->mu);
    cs->closed = 1;
    /* Wake every waiter: pending recvs return null, pending bounded
       sends bail out with the closed-channel error. */
    xs_cond_broadcast(&cs->cv);
    xs_mutex_unlock(&cs->mu);
}

int xs_chan_select(Value **chs, int n, Value **out) {
    /* Walk the channel array once; first one with a buffered value
       wins. Acquire each channel's mutex in turn so the read/dequeue is
       atomic with respect to that channel. We don't lock all of them
       up front: the GIL is already held, so concurrent senders only
       advance once we release it (e.g. via blocking ops). */
    if (out) *out = NULL;
    for (int i = 0; i < n; i++) {
        ChanState *cs = chan_state(chs[i]);
        XSArray   *bf = chan_buf(chs[i]);
        if (!cs || !bf) continue;
        xs_mutex_lock(&cs->mu);
        if (bf->len > 0) {
            Value *v = bf->items[0];
            for (int j = 0; j < bf->len - 1; j++) bf->items[j] = bf->items[j + 1];
            bf->len--;
            xs_cond_broadcast(&cs->cv);
            xs_mutex_unlock(&cs->mu);
            if (out) *out = v;
            else value_decref(v);
            return i;
        }
        xs_mutex_unlock(&cs->mu);
    }
    return -1;
}

void xs_sleep_seconds(double secs) {
    if (secs <= 0) return;
    /* Sleep in 25ms chunks so a cancellation flipped on this task by a
       sibling's throw is observed within bounded latency, not after
       the full requested duration. */
    const double chunk = 0.025;
    double remaining = secs;
    xs_gil_release();
    while (remaining > 0) {
        double s = remaining > chunk ? chunk : remaining;
        xs_thread_sleep_ns(s);
        remaining -= s;
        if (xs_task_is_cancelled()) break;
    }
    xs_gil_acquire();
}

/* --- lazy generator worker thread --------------------------------- */

/* Thread-local yield/resume channel slots. Every generator worker runs
   on its own OS thread, so keeping the channels here instead of on the
   Interp prevents worker-A from being handed worker-B's channels after
   a context switch (which we hit as deadlocks once more than one
   generator was alive at a time). */
static _Thread_local Value *tls_yield_chan  = NULL;
static _Thread_local Value *tls_resume_chan = NULL;

Value *xs_gen_tls_yield_chan(void)  { return tls_yield_chan;  }
Value *xs_gen_tls_resume_chan(void) { return tls_resume_chan; }

void xs_gen_tls_set(Value *yield_chan, Value *resume_chan) {
    tls_yield_chan  = yield_chan;
    tls_resume_chan = resume_chan;
}

typedef struct GenArg {
    Interp *parent;
    Value  *closure;
    Value  *yield_chan;
    Value  *resume_chan;
} GenArg;

static void *gen_worker_entry(void *arg_) {
    GenArg *ga = (GenArg *)arg_;
    Interp *ip = ga->parent;
    Value  *closure  = ga->closure;
    Value  *ych      = ga->yield_chan;
    Value  *rch      = ga->resume_chan;
    free(ga);

    xs_gil_acquire();

    /* Wait for the first .next() to issue a permit before any code in
       the body runs. This makes generator bodies behave like a paused
       coroutine that resumes on demand. */
    Value *first = xs_chan_recv(rch, ip);
    if (first) value_decref(first);

    /* Install the lazy-handoff channels so NODE_YIELD routes through
       them instead of accumulating into i->yield_collect. We use TLS
       rather than interp fields so concurrent generator workers don't
       trample each other's channel pointers. We also snapshot i->env
       because call_value is going to replace it with the generator
       body's scope, and main would otherwise pick up the stale pointer
       after the body finishes. */
    Value *saved_collect = ip->yield_collect;
    int    saved_limit   = ip->yield_limit;
    Env   *saved_env     = ip->env ? env_incref(ip->env) : NULL;
    tls_yield_chan    = ych;
    tls_resume_chan   = rch;
    ip->yield_collect = NULL;
    ip->yield_limit   = 0;

    Value *body = call_value(ip, closure, NULL, 0, "gen");
    if (body) value_decref(body);
    if (ip->cf.signal == CF_RETURN || ip->cf.signal == CF_YIELD) {
        if (ip->cf.value) value_decref(ip->cf.value);
        ip->cf.signal = 0; ip->cf.value = NULL;
    }

    tls_yield_chan    = NULL;
    tls_resume_chan   = NULL;
    ip->yield_collect = saved_collect;
    ip->yield_limit   = saved_limit;
    if (ip->env) env_decref(ip->env);
    ip->env = saved_env;

    /* End-of-stream sentinel. The for-loop / .next() consumer sees
       _gen_eos=true and stops. */
    Value *eos = xs_map_new();
    Value *t   = value_incref(XS_TRUE_VAL);
    map_set(eos->map, "_gen_eos", t);
    value_decref(t);
    xs_chan_send(ych, eos);
    value_decref(eos);

    value_decref(closure);
    value_decref(ych);
    value_decref(rch);
    xs_gil_release();
    return NULL;
}

void xs_spawn_generator(Interp *parent, Value *closure,
                        Value *yield_chan, Value *resume_chan) {
    GenArg *ga = xs_malloc(sizeof(GenArg));
    ga->parent      = parent;
    ga->closure     = value_incref(closure);
    ga->yield_chan  = value_incref(yield_chan);
    ga->resume_chan = value_incref(resume_chan);
    xs_thread_t th;
    if (xs_thread_create(&th, gen_worker_entry, ga) != 0) {
        value_decref(ga->closure);
        value_decref(ga->yield_chan);
        value_decref(ga->resume_chan);
        free(ga);
        return;
    }
    xs_thread_detach(th);
}
