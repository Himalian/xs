/* scheduler.h -- cooperative task scheduler for XS concurrency */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "core/xs.h"
#include "core/value.h"
#include "core/env.h"
#include "core/ast.h"

typedef struct Interp Interp;

/* task status */
#define TASK_PENDING  0
#define TASK_RUNNING  1
#define TASK_DONE     2
#define TASK_ERROR    3

typedef struct Task {
    int      id;
    Value   *closure;     /* XS_FUNC to call (zero-arg) */
    Value   *result;      /* result once done */
    int      status;
    int      nursery_id;  /* -1 if not in a nursery */
    struct Task *next;
} Task;

typedef struct Scheduler {
    Task *head;           /* linked list of all tasks */
    Task *tail;
    Task *current;        /* task currently running */
    int   next_id;
    int   nursery_depth;  /* current nursery nesting level */
    int   next_nursery;   /* next nursery id to assign */
} Scheduler;

Scheduler *scheduler_new(void);
void       scheduler_free(Scheduler *s);

/* create a task; returns the task id. closure must be XS_FUNC. */
int        scheduler_spawn(Scheduler *s, Value *closure, int nursery_id);

/* run a specific task by id. returns 1 if found and run, 0 otherwise */
int        scheduler_run_task(Scheduler *s, int task_id, Interp *interp);

/* run all pending tasks (optionally only those in a given nursery, -1 for all) */
void       scheduler_run_all(Scheduler *s, Interp *interp, int nursery_id);

/* run tasks until task_id completes; returns the task's result (borrowed) */
Value     *scheduler_await(Scheduler *s, int task_id, Interp *interp);

/* get a task by id */
Task      *scheduler_get_task(Scheduler *s, int task_id);

/* build a future map value from a task id */
Value     *scheduler_make_future(int task_id);

/* check if a value is a future */
int        scheduler_is_future(Value *v, int *task_id_out);

#endif /* SCHEDULER_H */
