/*
 * ble_npl_callout backend for macOS (replaces the POSIX-timer based
 * porting/npl/linux/src/os_callout.c, which needs timer_create()).
 *
 * One manager thread owns a sorted list of armed callouts; it waits on a
 * condvar until the earliest deadline and then fires expired callouts with
 * the same semantics as the Linux SIGEV_THREAD callback:
 *   - clear c_active
 *   - enqueue the callout event on its eventq (or invoke cb directly when
 *     the callout has no eventq)
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "nimble/nimble_npl.h"

struct npl_timer_node {
    struct ble_npl_callout *co;
    uint64_t deadline_ms;          /* monotonic ms */
    struct npl_timer_node *next;   /* list, earliest deadline first */
};

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cond = PTHREAD_COND_INITIALIZER;
static struct npl_timer_node *s_head;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static bool s_stopping;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* pthread_cond_timedwait wants absolute CLOCK_REALTIME; convert a delta. */
static void delta_to_abstime(uint64_t delta_ms, struct timespec *out)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    out->tv_sec = now.tv_sec + (time_t)(delta_ms / 1000U);
    out->tv_nsec = now.tv_nsec + (long)(delta_ms % 1000U) * 1000000L;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_sec += 1;
        out->tv_nsec -= 1000000000L;
    }
}

static void fire_callout(struct ble_npl_callout *c)
{
    c->c_active = false;
    if (c->c_evq) {
        ble_npl_eventq_put(c->c_evq, &c->c_ev);
    } else if (c->c_ev.ev_cb) {
        c->c_ev.ev_cb(&c->c_ev);
    }
}

static void *callout_manager(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&s_lock);
    while (!s_stopping) {
        if (!s_head) {
            pthread_cond_wait(&s_cond, &s_lock);
            continue;
        }

        uint64_t now = now_ms();
        if (s_head->deadline_ms > now) {
            struct timespec wake;
            delta_to_abstime(s_head->deadline_ms - now, &wake);
            pthread_cond_timedwait(&s_cond, &s_lock, &wake);
            continue;
        }

        /* fire everything already expired */
        while (s_head && s_head->deadline_ms <= now_ms()) {
            struct npl_timer_node *node = s_head;
            s_head = node->next;
            node->next = NULL;
            fire_callout(node->co);
        }
    }
    pthread_mutex_unlock(&s_lock);
    return NULL;
}

static void ensure_manager(void)
{
    pthread_t tid;
    if (pthread_create(&tid, NULL, callout_manager, NULL) != 0) {
        fprintf(stderr, "blectl: callout manager thread failed\n");
        abort();
    }
    pthread_detach(tid);
}

static struct npl_timer_node *find_node(struct ble_npl_callout *c)
{
    for (struct npl_timer_node *n = s_head; n; n = n->next) {
        if (n->co == c) {
            return n;
        }
    }
    return NULL;
}

static void remove_node(struct npl_timer_node *node)
{
    struct npl_timer_node **pp = &s_head;
    while (*pp && *pp != node) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        *pp = node->next;
        node->next = NULL;
    }
}

void ble_npl_callout_init(struct ble_npl_callout *c, struct ble_npl_eventq *evq,
                          ble_npl_event_fn *ev_cb, void *ev_arg)
{
    memset(c, 0, sizeof(*c));
    c->c_ev.ev_cb = ev_cb;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq = evq;
    c->c_active = false;

    /* Allocate the permanent timer node; its existence marks the callout
     * as initialized (mirrors timer_create() setting a non-NULL timer_t). */
    c->c_timer = calloc(1, sizeof(struct npl_timer_node));
    assert(c->c_timer);

    pthread_once(&s_once, ensure_manager);
}

bool ble_npl_callout_is_active(struct ble_npl_callout *c)
{
    return c->c_active;
}

int ble_npl_callout_inited(struct ble_npl_callout *c)
{
    return (c->c_timer != NULL);
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c, ble_npl_time_t ticks)
{
    struct npl_timer_node *node = c->c_timer;

    if (ticks == 0) {
        ticks = 1;
    }

    pthread_mutex_lock(&s_lock);

    c->c_ticks = ble_npl_time_get() + ticks;
    c->c_active = true;

    if (node->co == c) {
        remove_node(node);          /* re-arm: drop old position */
    } else {
        node->co = c;               /* first use */
    }
    node->deadline_ms = now_ms() + ticks;

    /* insert sorted */
    struct npl_timer_node **pp = &s_head;
    while (*pp && (*pp)->deadline_ms <= node->deadline_ms) {
        pp = &(*pp)->next;
    }
    node->next = *pp;
    *pp = node;

    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_lock);
    return BLE_NPL_OK;
}

int ble_npl_callout_queued(struct ble_npl_callout *c)
{
    return c->c_active;
}

void ble_npl_callout_stop(struct ble_npl_callout *c)
{
    struct npl_timer_node *node = c->c_timer;

    if (!ble_npl_callout_inited(c)) {
        return;
    }

    pthread_mutex_lock(&s_lock);
    remove_node(node);
    c->c_active = false;
    pthread_cond_signal(&s_cond);
    pthread_mutex_unlock(&s_lock);
}

ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co)
{
    return co->c_ticks;
}

void ble_npl_callout_set_arg(struct ble_npl_callout *co, void *arg)
{
    co->c_ev.ev_arg = arg;
}

uint32_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *co, ble_npl_time_t now)
{
    struct npl_timer_node *node = co->c_timer;
    uint32_t rt = 0;

    pthread_mutex_lock(&s_lock);
    if (find_node(co)) {
        uint64_t now_mono = now_ms();
        if (node->deadline_ms > now_mono) {
            rt = (uint32_t)(node->deadline_ms - now_mono);
        }
    }
    pthread_mutex_unlock(&s_lock);

    (void)now;
    return rt;
}
