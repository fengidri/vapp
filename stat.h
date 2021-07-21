/*
 * stat.h
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef STAT_H_
#define STAT_H_

#include <stdint.h>
#include <time.h>

typedef struct {
    struct timespec start, stop;
    struct timespec last;
    uint64_t diff;
    uint64_t count;
    uint64_t lcount;

    uint64_t kick_num;
    uint64_t lkick_num;

    uint64_t call_num;
    uint64_t call_skip_num;

    uint64_t lcall_num;
    uint64_t lcall_skip_num;

    uint64_t idle;
    uint64_t lidle;

} Stat;

extern Stat stat;

int init_stat(Stat* stat);
int update_stat(Stat* stat, uint32_t count);
int stop_stat(Stat* stat);
int print_stat(Stat* stat);

struct VhostServer;

#include "vhost_server.h"
int start_stat(struct VhostServer *vhost_server);

#endif /* STAT_H_ */
