/*
 * vring.c
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VRING_C_
#define VRING_C_

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <stdbool.h>

#include "vring.h"
#include "shm.h"
#include "stat.h"
#include "vhost_user.h"

#define VRING_IDX_NONE          ((uint16_t)-1)
bool drop_packet;
extern bool dump_packet;

static struct vhost_vring* new_vring(void* vring_base)
{
    struct vhost_vring* vring = (struct vhost_vring*) vring_base;
    int i = 0;
    uintptr_t ptr = (uintptr_t) ((char*)vring + sizeof(struct vhost_vring));
    size_t initialized_size = 0;

    // Layout the descriptor table
    for (i = 0; i < VHOST_VRING_SIZE; i++) {
        // align the pointer
        ptr = ALIGN(ptr, BUFFER_ALIGNMENT);

        vring->desc[i].addr = ptr;
        vring->desc[i].len = BUFFER_SIZE;
        vring->desc[i].flags = VIRTIO_DESC_F_WRITE;
        vring->desc[i].next = i+1;

        ptr += vring->desc[i].len;
    }

    initialized_size = ptr - (uintptr_t)vring_base;

    vring->desc[VHOST_VRING_SIZE-1].next = VRING_IDX_NONE;

    vring->avail.idx = 0;
    vring->used.idx =  0;


    sync_shm(vring_base, initialized_size);

    return vring;
}

int vring_table_from_memory_region(struct vhost_vring* vring_table[], size_t vring_table_num,
        VhostUserMemory *memory)
{
    int i = 0;

    /* TODO: here we assume we're putting each vring in a separate
     * memory region from the memory map.
     * In reality this probably is not like that
     */
    assert(vring_table_num == memory->nregions);

    for (i = 0; i < vring_table_num; i++) {
        struct vhost_vring* vring = new_vring(
                (void*) (uintptr_t) memory->regions[i].guest_phys_addr);
        if (!vring) {
            fprintf(stderr, "Unable to create vring %d.\n", i);
            return -1;
        }
        vring_table[i] = vring;
    }

    return 0;
}

int set_host_vring(Client* client, struct vhost_vring *vring, int index)
{
    vring->kickfd = eventfd(0, EFD_NONBLOCK);
    vring->callfd = eventfd(0, EFD_NONBLOCK);
    assert(vring->kickfd >= 0);
    assert(vring->callfd >= 0);

    struct vhost_vring_state num = { .index = index, .num = VHOST_VRING_SIZE };
    struct vhost_vring_state base = { .index = index, .num = 0 };
    struct vhost_vring_file kick = { .index = index, .fd = vring->kickfd };
    struct vhost_vring_file call = { .index = index, .fd = vring->callfd };
    struct vhost_vring_addr addr = { .index = index,
            .desc_user_addr = (uintptr_t) &vring->desc,
            .avail_user_addr = (uintptr_t) &vring->avail,
            .used_user_addr = (uintptr_t) &vring->used,
            .log_guest_addr = (uintptr_t) NULL,
            .flags = 0 };

    if (vhost_ioctl(client, VHOST_USER_SET_VRING_NUM, &num) != 0)
        return -1;
    if (vhost_ioctl(client, VHOST_USER_SET_VRING_BASE, &base) != 0)
        return -1;
    if (vhost_ioctl(client, VHOST_USER_SET_VRING_KICK, &kick) != 0)
        return -1;
    if (vhost_ioctl(client, VHOST_USER_SET_VRING_CALL, &call) != 0)
        return -1;
    if (vhost_ioctl(client, VHOST_USER_SET_VRING_ADDR, &addr) != 0)
        return -1;

    return 0;
}

int set_host_vring_table(struct vhost_vring* vring_table[], size_t vring_table_num,
        Client* client)
{
    int i = 0;

    for (i = 0; i < vring_table_num; i++) {
        if (set_host_vring(client, vring_table[i], i) != 0) {
            fprintf(stderr, "Unable to init vring %d.\n", i);
            return -1;
        }
    }
    return 0;
}

int put_vring(VringTable* vring_table, uint32_t v_idx, void* buf, size_t size)
{
    struct vring_desc* desc = vring_table->vring[v_idx].desc;
    struct vring_avail* avail = vring_table->vring[v_idx].avail;
    unsigned int num = vring_table->vring[v_idx].num;
    ProcessHandler* handler = &vring_table->handler;

    uint16_t a_idx = vring_table->vring[v_idx].last_avail_idx;
    void* dest_buf = 0;
    struct virtio_net_hdr *hdr = 0;
    size_t hdr_len = sizeof(struct virtio_net_hdr);

    if (size > desc[a_idx].len) {
        return -1;
    }

    // move avail head
    vring_table->vring[v_idx].last_avail_idx = desc[a_idx].next;

    // map the address
    if (handler && handler->map_handler) {
        dest_buf = (void*)handler->map_handler(handler->context, desc[a_idx].addr);
    } else {
        dest_buf = (void*) (uintptr_t) desc[a_idx].addr;
    }

    // set the header to all 0
    hdr = dest_buf;
    hdr->flags = 0;
    hdr->gso_type = 0;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;

    // We support only single buffer per packet
    memcpy(dest_buf + hdr_len, buf, size);
    desc[a_idx].len = hdr_len + size;
    desc[a_idx].flags = 0;
    desc[a_idx].next = VRING_IDX_NONE;

    // add to avail
    avail->ring[avail->idx % num] = a_idx;
    avail->idx++;

    sync_shm(dest_buf, size);
    sync_shm((void*)&(avail), sizeof(struct vring_avail));

    return 0;
}

static int free_vring(VringTable* vring_table, uint32_t v_idx, uint32_t d_idx)
{
    struct vring_desc* desc = vring_table->vring[v_idx].desc;
    uint16_t f_idx = vring_table->vring[v_idx].last_avail_idx;

    assert(d_idx>=0 && d_idx<VHOST_VRING_SIZE);

    // return the descriptor back to the free list
    desc[d_idx].len = BUFFER_SIZE;
    desc[d_idx].flags |= VIRTIO_DESC_F_WRITE;
    desc[d_idx].next = f_idx;
    vring_table->vring[v_idx].last_avail_idx = d_idx;

    return 0;
}

int process_used_vring(VringTable* vring_table, uint32_t v_idx)
{
    struct vring_used* used = vring_table->vring[v_idx].used;
    unsigned int num = vring_table->vring[v_idx].num;
    uint16_t u_idx = vring_table->vring[v_idx].last_used_idx;

    for (; u_idx != used->idx; u_idx = (u_idx + 1) % num) {
        free_vring(vring_table, v_idx, used->ring[u_idx].id);
    }

    vring_table->vring[v_idx].last_used_idx = u_idx;

    return 0;
}

static int process_desc(VringTable* vring_table, uint32_t v_idx, uint32_t a_idx)
{
    struct vring_desc*  desc  = vring_table->vring[v_idx].desc;
    struct vring_avail* avail = vring_table->vring[v_idx].avail;
    struct vring_used*  used  = vring_table->vring[v_idx].used;
    unsigned int        num   = vring_table->vring[v_idx].num;

    ProcessHandler* handler = &vring_table->handler;
    uint16_t u_idx = vring_table->vring[v_idx].last_used_idx % num;
    uint16_t d_idx = avail->ring[a_idx];
    uint32_t i, len = 0;
    size_t buf_size = ETH_PACKET_SIZE;
    uint8_t buf[buf_size];
//    struct virtio_net_hdr *hdr = 0;
//    size_t hdr_len = sizeof(struct virtio_net_hdr);

    i=d_idx;
    for (;;) {
        void* cur = 0;
        uint32_t cur_len = desc[i].len;

        if (!drop_packet) {
            // map the address
            if (handler && handler->map_handler) {
                cur = (void*)handler->map_handler(handler->context, desc[i].addr);
            } else {
                cur = (void*) (uintptr_t) desc[i].addr;
            }

            if (len + cur_len < buf_size) {
                memcpy(buf + len, cur, cur_len);
            } else {
                break;
            }
        }

        len += cur_len;

        if (desc[i].flags & VIRTIO_DESC_F_NEXT) {
            i = desc[i].next;
        } else {
            break;
        }
    }

    if (!len){
        return -1;
    }

    // add it to the used ring
    used->ring[u_idx].id = d_idx;
    used->ring[u_idx].len = len;
    return 0;
}

static int __process_avail_vring_busy(VringTable* vring_table, uint32_t v_idx)
{
    Vring *vq;
    struct vring_avail* avail;
    struct vring_used* used;
    unsigned int num;
    uint32_t count = 0;
    uint16_t a_idx;

    vq = &vring_table->vring[v_idx];

    avail = vq->avail;
    used  = vq->used;
    num   = vq->num;

    a_idx = vq->last_avail_idx % num;

    for (count = 0; count < MAX_PKT_BURST; ) {
        if (vq->last_avail_idx == avail->idx)
            break;

        process_desc(vring_table, v_idx, a_idx);

        a_idx = (a_idx + 1) % num;

        vq->last_avail_idx++;
        vq->last_used_idx++;
        count++;
    }

    if (count) {
        used->idx = vq->last_used_idx;
        mb();
        call(vring_table, v_idx);

        stat.count += count;
    } else {
        stat.idle += 1;
    }

    return count;
}

int process_avail_vring_busy(VringTable* vring_table, uint32_t v_idx)
{
    Vring *vq;

    vq = &vring_table->vring[v_idx];

    if (!vq->desc)
        return 0;

    vq->used->flags = VRING_USED_F_NO_NOTIFY;

    vq->polling = true;
    vq->tx_stopped = false;

    printf("=== start tx polling\n");

    while (vq->polling)
        __process_avail_vring_busy(vring_table, v_idx);

    mb();
    vq->tx_stopped = true;
    printf("=== exit tx polling\n");

    return 0;
}

int process_avail_vring(VringTable* vring_table, uint32_t v_idx)
{
    Vring *vq;
    struct vring_avail* avail;
    struct vring_used* used;
    unsigned int num;
    uint32_t count = 0;
    uint16_t a_idx;

    vq = &vring_table->vring[v_idx];

    avail = vq->avail;
    used  = vq->used;
    num   = vq->num;

    a_idx = vq->last_avail_idx % num;

    // Loop all avail descriptors
    for (;;) {
        /* we reached the end of avail */
        if (vq->last_avail_idx == avail->idx) {
            break;
        }

        process_desc(vring_table, v_idx, a_idx);

        a_idx = (a_idx + 1) % num;

        vq->last_avail_idx++;
        vq->last_used_idx++;
        count++;

        if (count % MAX_PKT_BURST == 0) {
            mb();

            vhost_avail_event(vq) = vq->last_avail_idx;

            used->idx = vq->last_used_idx;
            mb();
            call(vring_table, v_idx);
        }
    }

    if (count % MAX_PKT_BURST) {
        mb();

        vhost_avail_event(vq) = vq->last_avail_idx;

        used->idx = vq->last_used_idx;
        mb();
        call(vring_table, v_idx);
    }

    return count;
}

/*
 * The following is used with VIRTIO_RING_F_EVENT_IDX.
 * Assuming a given event_idx value from the other size, if we have
 * just incremented index from old to new_idx, should we trigger an
 * event?
 */
static int
vhost_need_event(uint16_t event_idx, uint16_t new_idx, uint16_t old)
{
	return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old);
}

static void
vhost_vring_call_split(Vring *vq, uint64_t features)
{
    uint64_t call_it = 1;

    mb();
	/* Flush used->idx update before we read avail->flags. */
	//rte_smp_mb();

	/* Don't kick guest if we don't reach index specified by guest. */
	if (features & (1ULL << VIRTIO_RING_F_EVENT_IDX)) {
		uint16_t old = vq->signalled_used;
		uint16_t new = vq->last_used_idx;
		uint16_t eid;
		bool signalled_used_valid = vq->signalled_used_valid;

		vq->signalled_used = new;
		vq->signalled_used_valid = true;

        eid = vhost_used_event(vq);
		if (vhost_need_event(eid, new, old) || !signalled_used_valid) {
            goto call;
        }

	} else {
		/* Kick the guest if necessary. */
		if (!(vq->avail->flags & VRING_AVAIL_F_NO_INTERRUPT))
            goto call;
	}

    stat.call_skip_num += 1;
    return;
call:
    stat.call_num += 1;
    write(vq->callfd, &call_it, sizeof(call_it));
    fsync(vq->callfd);
}

int call(VringTable* vring_table, uint32_t v_idx)
{

    vhost_vring_call_split(&vring_table->vring[v_idx], vring_table->features);

    return 0;
}

int kick(VringTable* vring_table, uint32_t v_idx)
{
    uint64_t kick_it = 1;
    int kickfd = vring_table->vring[v_idx].kickfd;

    write(kickfd, &kick_it, sizeof(kick_it));
    fsync(kickfd);

    return 0;
}

#endif /* VRING_C_ */
