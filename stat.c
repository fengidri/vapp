/*
 * stat.c
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <inttypes.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include "vring.h"
#include "stat.h"
#include "vhost_server.h"

Stat stat;
Stat rx_stat;

#define STAT_PRINT_INTERVAL (3) // in ms
int init_stat(Stat* stat)
{
    clock_gettime(CLOCK_MONOTONIC, &stat->start);
    clock_gettime(CLOCK_MONOTONIC, &stat->stop);
    stat->count = 0;
    stat->diff = 0;
    return 0;
}

static void stat_show(bool tx, Vring *ring)
{
    static uint64_t max_cycle = 1;
    struct vring_avail* avail;
    struct vring_used* used;
    int cpu;
    Stat *src;
    Stat t;

    avail = ring->avail;
    used = ring->used;

    if (!used)
        return;

    if (tx)
        src = &stat;
    else
        src = &rx_stat;

#define s_diff(n) \
    t.n = src->n - src->l##n; \
    src->l##n = src->n;

    s_diff(count);
    s_diff(kick_num);
    s_diff(call_num);
    s_diff(call_skip_num);
    s_diff(idle);

    if (t.idle > max_cycle)
        max_cycle = t.idle;


    cpu = 100 - t.idle * 100 / max_cycle;

    printf("%s CPU: %d%% : %ld kick: %ld call: %ld skip call: %ld [avail: %u/%u used: %u/%u]\n",
           tx ? "TX" : "RX",
           cpu,
           t.count,
           t.kick_num,
           t.call_num,
           t.call_skip_num,
           ring->last_avail_idx,
           avail->idx,
           ring->last_used_idx,
           used->idx
          );

}

static void * stat_thread(void *p)
{
    VhostServer *vhost_server = p;
    VringTable *ring_table = &vhost_server->vring_table;
    Vring *tx_ring = &ring_table->vring[VHOST_CLIENT_VRING_IDX_TX];
    Vring *rx_ring = &ring_table->vring[VHOST_CLIENT_VRING_IDX_RX];

    while (true) {
        sleep(1);

        stat_show(true, tx_ring);
        stat_show(false, rx_ring);
        printf("\n");
    }

    return NULL;
}

int start_stat(VhostServer *vhost_server)
{
    pthread_t th;
    pthread_create(&th, 0, stat_thread, vhost_server);
    return 0;
}

int stop_stat(Stat* stat)
{
    clock_gettime(CLOCK_MONOTONIC, &stat->stop);
    return 0;
}

int update_stat(Stat* stat, uint32_t count)
{
    stat->count += count;
    return 0;
}

int print_stat(Stat* stat)
{
    struct timespec now;
    uint64_t diff;

    clock_gettime(CLOCK_MONOTONIC, &now);

    diff = (now.tv_sec - stat->last.tv_sec)
            + (now.tv_nsec - stat->last.tv_nsec) / 1000000000;

    if (diff > 1) {
        fprintf(stdout,"xmit: %"PRId64"pkt/s\r",
                (stat->count - stat->lcount) / diff);
        fflush(stdout);
        stat->lcount = stat->count;
        stat->last = now;
    }

    return 0;
}
