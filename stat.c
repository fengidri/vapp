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

Stat stat;

#define STAT_PRINT_INTERVAL (3) // in ms
int init_stat(Stat* stat)
{
    clock_gettime(CLOCK_MONOTONIC, &stat->start);
    clock_gettime(CLOCK_MONOTONIC, &stat->stop);
    stat->count = 0;
    stat->diff = 0;
    return 0;
}

static void * stat_thread(void *p)
{
    VhostServer *vhost_server = p;
    VringTable *ring_table = &vhost_server->vring_table;
    Vring *tx_ring = &ring_table->vring[VHOST_CLIENT_VRING_IDX_TX];
    struct vring_avail* tx_avail;
    struct vring_used* tx_used;
    uint64_t num, n_call, n_call_skip;

    while (true) {
        sleep(1);

        tx_avail = tx_ring->avail;
        tx_used = tx_ring->used;

        if (!tx_used) {
            continue;
        }

        num = stat.count - stat.lcount;
        stat.lcount = stat.count;

        n_call = stat.call_num - stat.lcall_num;
        stat.lcall_num = stat.call_num;

        n_call_skip = stat.call_skip_num - stat.lcall_skip_num;
        stat.lcall_skip_num = stat.call_skip_num;

        printf("xmit: %ldpkt/s call: %ld skip call: %ld tx[avail: %u/%u used: %u/%u]\n",
               num,
               n_call, n_call_skip,
               tx_ring->last_avail_idx,
               tx_avail->idx,
               tx_ring->last_used_idx,
               tx_used->idx
               );
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
