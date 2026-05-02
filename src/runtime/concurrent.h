#ifndef XS_CONCURRENT_H
#define XS_CONCURRENT_H

#include "core/xs_thread.h"
#include "core/value.h"

/* Global interpreter lock. Every thread that touches XS state must hold
   this. Acquired by the main thread at startup. spawned threads acquire
   it before running their closure and release it before exiting.
   Blocking ops (channel.recv on empty, sleep) release the GIL while
   waiting and reacquire when ready. */
void xs_gil_init(void);
void xs_gil_acquire(void);
void xs_gil_release(void);

/* Spawn a new thread that will run `closure` to completion under the
   GIL. Returns a future-like map { _task_id, _status, _result }. */
struct Interp;
Value *xs_spawn_thread(struct Interp *parent, Value *closure);

/* Block until the given task finishes and return its result. Releases
   the GIL while waiting. The _ex variant additionally reports whether
   the task errored and (optionally) hands back the captured error
   value with ownership transferred to the caller. */
Value *xs_await_task(int task_id);
Value *xs_await_task_ex(int task_id, int *errored_out, Value **err_out);

/* Wait for every interp-spawned task that hasn't been awaited yet.
   atexit calls this so fire-and-forget spawns finish before the
   process exits. Mirrors vm_drain_tasks for the interp backend. */
void xs_drain_interp_tasks(void);

/* Channel primitives backed by mutex + condvar. Each channel value is
   a regular XS_MAP with `_buf` (FIFO array) and `_chan_id` (int index
   into a global mutex/condvar table; allocate with xs_chan_alloc).
   `cap` is the bounded capacity (0 = unbounded). */
int    xs_chan_alloc(int cap);
/* Returns 1 on success, 0 if the channel was closed. The closed-send
   case is reported back to the caller so it can raise a runtime error
   on the right backend. */
int    xs_chan_send(Value *ch, Value *v);
/* Recv: if the channel is closed and the buffer is empty, returns null
   without blocking. Otherwise blocks until a value is available. */
Value *xs_chan_recv(Value *ch, struct Interp *interp);
Value *xs_chan_try_recv(Value *ch);
int    xs_chan_len(Value *ch);
int    xs_chan_cap(Value *ch);
int    xs_chan_is_full(Value *ch);
int    xs_chan_is_closed(Value *ch);
void   xs_chan_close(Value *ch);
/* Try to receive across multiple channels. Returns the index in `chs`
   of the first channel with a buffered value, with `*out` set to the
   value (refcount transferred). Returns -1 if nothing is ready. */
int    xs_chan_select(Value **chs, int n, Value **out);

/* Sleep that releases the GIL for the duration. */
void xs_sleep_seconds(double secs);

/* Cooperative cancellation for nursery siblings. Each task carries the
   id of the nursery it was spawned in; when one task in a nursery
   errors out, every still-running sibling gets its cancelled flag
   flipped. Blocking primitives (sleep, recv) check the current
   thread's task and bail with a CF_THROW carrying a Cancelled error
   when cancelled. */
int  xs_nursery_alloc_id(void);
int  xs_nursery_current_id(void);
void xs_nursery_set_current_id(int id);
int  xs_task_is_cancelled(void);
/* Hand the runtime a pointer to the current task's cancelled flag so
   xs_task_is_cancelled() and the chunked sleep / channel recv loops
   can poll it. Pass NULL to clear (e.g. when a worker thread exits or
   when running on the main thread outside of any task). */
void xs_task_set_self_cancel_ptr(int *flag);

/* Spawn a lazy-generator worker thread. The worker waits for the
   first .next() (which sends a token on resume_chan), then runs the
   closure with the thread-local yield/resume channel slots installed.
   When the closure returns, the worker sends an EOS sentinel map
   {_gen_eos: true} on yield_chan and exits. */
void xs_spawn_generator(struct Interp *parent, Value *closure,
                        Value *yield_chan, Value *resume_chan);

/* Per-thread generator handoff channels. NODE_YIELD reads these; the
   worker thread sets them on entry and restores them on exit. They
   have to be thread-local because all generator workers share the
   single Interp pointer under the GIL. */
Value *xs_gen_tls_yield_chan(void);
Value *xs_gen_tls_resume_chan(void);
void   xs_gen_tls_set(Value *yield_chan, Value *resume_chan);

#endif
