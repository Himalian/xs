/* event_loop.h - async event loop with epoll/kqueue/poll backends */
#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdint.h>

typedef enum {
    EV_READ   = 1,
    EV_WRITE  = 2,
    EV_TIMER  = 4,
    EV_SIGNAL = 8,
} EventType;

typedef void (*EventCallback)(int fd, EventType ev, void *ctx);
typedef void (*TimerCallback)(void *ctx);

typedef struct {
    int fd;
    EventType events;
    EventCallback callback;
    void *ctx;
    int active;
} EventSource;

typedef struct {
    int64_t when_ms;
    TimerCallback fn;
    void *ctx;
    int repeat_ms;
    int active;
    int id;
} TimerEntry;

typedef struct EventLoop {
    int backend_fd;       /* epoll fd or kqueue fd, -1 for poll */
    EventSource *sources;
    int nsources, cap;
    int running;

    TimerEntry *timers;
    int ntimers, timer_cap;
    int next_timer_id;

    /* signal handling */
    int signal_pipe[2];
    struct {
        int signum;
        EventCallback callback;
        void *ctx;
    } sig_handlers[32];
    int nsig_handlers;

    /* stats */
    int64_t total_events;
    int64_t total_timers_fired;
} EventLoop;

EventLoop *evloop_new(void);
void evloop_free(EventLoop *ev);

int  evloop_add_fd(EventLoop *ev, int fd, EventType events,
                   EventCallback cb, void *ctx);
int  evloop_mod_fd(EventLoop *ev, int fd, EventType events);
int  evloop_remove_fd(EventLoop *ev, int fd);

int  evloop_add_timer(EventLoop *ev, int ms, int repeat,
                      TimerCallback cb, void *ctx);
int  evloop_cancel_timer(EventLoop *ev, int timer_id);

int  evloop_add_signal(EventLoop *ev, int signum,
                       EventCallback cb, void *ctx);

void evloop_run(EventLoop *ev);
void evloop_run_once(EventLoop *ev, int timeout_ms);
void evloop_stop(EventLoop *ev);

int64_t evloop_now_ms(void);

#endif /* EVENT_LOOP_H */
