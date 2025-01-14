/*
 * vhost_server.c
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>

#include "fd_list.h"
#include "shm.h"
#include "vhost_server.h"
#include "vring.h"
#include "process.h"

bool dump_packet;
bool busy_mode;
bool rx_busy_mode;

typedef int (*MsgHandler)(VhostServer* vhost_server, ServerMsg* msg);

static int avail_handler_server(void* context, void* buf, size_t size);
static uintptr_t map_handler(void* context, uint64_t addr);

extern int app_running;

VhostServer* new_vhost_server(const char* path, int is_listen)
{
    VhostServer* vhost_server = (VhostServer*) calloc(1, sizeof(VhostServer));
    int idx;

    //TODO: handle errors here

    vhost_server->server = new_server(path);
    init_server(vhost_server->server, is_listen);

    vhost_server->memory.nregions = 0;

    // VringTable initalization
    vhost_server->vring_table.handler.context = (void*) vhost_server;
    vhost_server->vring_table.handler.avail_handler = avail_handler_server;
    vhost_server->vring_table.handler.map_handler = map_handler;

    for (idx = 0; idx < VHOST_CLIENT_VRING_NUM; idx++) {
        vhost_server->vring_table.vring[idx].kickfd = -1;
        vhost_server->vring_table.vring[idx].callfd = -1;
        vhost_server->vring_table.vring[idx].desc = 0;
        vhost_server->vring_table.vring[idx].avail = 0;
        vhost_server->vring_table.vring[idx].used = 0;
        vhost_server->vring_table.vring[idx].num = 0;
        vhost_server->vring_table.vring[idx].last_avail_idx = 0;
        vhost_server->vring_table.vring[idx].last_used_idx = 0;
    }

    vhost_server->buffer_size = 0;
    vhost_server->is_polling = 0;
    //init_stat(&vhost_server->stat);

    return vhost_server;
}

int end_vhost_server(VhostServer* vhost_server)
{
    int idx;

    // End server
    end_server(vhost_server->server);
    free(vhost_server->server);
    vhost_server->server = 0;

    for (idx = 0; idx < vhost_server->memory.nregions; idx++) {
        VhostServerMemoryRegion *region = &vhost_server->memory.regions[idx];
        end_shm(vhost_server->server->path,
                (void*) (uintptr_t) region->userspace_addr,
                region->memory_size, idx);
    }

    return 0;
}

static uintptr_t _map_guest_addr(VhostServer* vhost_server, uint64_t addr)
{
    uintptr_t result = 0;
    int idx;

    for (idx = 0; idx < vhost_server->memory.nregions; idx++) {
        VhostServerMemoryRegion *region = &vhost_server->memory.regions[idx];

        if (region->guest_phys_addr <= addr
                && addr < (region->guest_phys_addr + region->memory_size)) {
            result = region->mmap_addr + addr - region->guest_phys_addr;
            break;
        }
    }

    return result;
}

static uintptr_t _map_user_addr(VhostServer* vhost_server, uint64_t addr)
{
    uintptr_t result = 0;
    int idx;

    for (idx = 0; idx < vhost_server->memory.nregions; idx++) {
        VhostServerMemoryRegion *region = &vhost_server->memory.regions[idx];

        if (region->userspace_addr <= addr && addr < (region->userspace_addr + region->memory_size)) {
            result = region->mmap_addr + addr - region->userspace_addr;
            break;
        }
    }

    return result;
}

static int _set_prot_features(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    printf("set features: %lu\n", msg->msg.u64);
    vhost_server->prot_features = msg->msg.u64;

    return 0;
}

static int _get_prot_features(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    // dpdk not use this. this will call VHOST_USER_ADD_MEM_REG...
    //msg->msg.u64 = 1UL << VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS;
    msg->msg.u64 = 0;
    msg->msg.u64 |= 1UL << VHOST_USER_PROTOCOL_F_REPLY_ACK;
    msg->msg.u64 |= 1UL << VHOST_USER_PROTOCOL_F_SLAVE_REQ;
    //msg->msg.u64 |= 1UL << VHOST_USER_PROTOCOL_F_PAGEFAULT;

    printf("get flags: %u\n", msg->msg.flags);

    printf("get feature: %ld\n", msg->msg.u64);
    msg->msg.size = MEMB_SIZE(VhostUserMsg,u64);

    return 1; // should reply back
}

static int _set_slave_req_fd(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    printf("get flags: %u\n", msg->msg.flags);

    msg->msg.u64 = 0;
    msg->msg.size = MEMB_SIZE(VhostUserMsg,u64);

    return 1; // should reply back
}

static int _get_mem_slots(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    msg->msg.u64 = VHOST_MEMORY_MAX_NREGIONS;
    msg->msg.size = MEMB_SIZE(VhostUserMsg,u64);

    return 1; // should reply back
}

static int _get_features(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    uint64_t features = 0;

    features |= 1UL << VIRTIO_F_VERSION_1;
    features |= 1UL << VHOST_USER_F_PROTOCOL_FEATURES;
    features |= 1UL << VIRTIO_RING_F_EVENT_IDX;
    features |= 1UL << VIRTIO_F_IOMMU_PLATFORM;
    features |= 1UL << VIRTIO_F_RING_RESET;

    msg->msg.u64 = features;
    msg->msg.size = MEMB_SIZE(VhostUserMsg,u64);

    return 1; // should reply back
}


static int _set_features(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    /*
     * For vhost, VIRTIO_F_IOMMU_PLATFORM means the backend support
     * incremental memory mapping API via IOTLB API. For platform that
     * does not have IOMMU, there's no need to enable this feature
     * which may cause unnecessary IOTLB miss/update transactions.
     *
     * So, we support VIRTIO_F_IOMMU_PLATFORM, then the guest will has it. But
     * the device may is without iommu device, so the vhost the VIRTIO_F_IOMMU_PLATFORM
     * will be closed.
     */
    printf("set feature: iommu: %lu\n", msg->msg.u64 & (1UL << VIRTIO_F_IOMMU_PLATFORM));
    printf("set feature: event: %lu\n", msg->msg.u64 & (1UL << VIRTIO_RING_F_EVENT_IDX));
    vhost_server->vring_table.features = msg->msg.u64;
    return 0;
}

static int _set_owner(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static int _reset_owner(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static int __add_reg(VhostServer* vhost_server, VhostServerMemoryRegion *region,
                     VhostUserMemoryRegion *reg, int fd)
{
    void *addr;
    uint64_t size;

    printf("size: %ld offset: %ld\n", reg->memory_size, reg->mmap_offset);

    size = reg->memory_size + reg->mmap_offset;

    addr = init_shm_from_fd(fd, size);
    if (!addr)
        exit(-1);

    region->memory_size     = reg->memory_size;
    region->guest_phys_addr = reg->guest_phys_addr;
    region->userspace_addr  = reg->userspace_addr;

    region->mmap_addr = (uintptr_t)addr;
    region->mmap_addr += reg->mmap_offset;

    vhost_server->memory.nregions++;

    return 0;
}

static int _add_mem_reg(VhostServer* vhost_server, ServerMsg* msg)
{
    VhostServerMemoryRegion *region;

    if (msg->fd_num != 1) {
        printf(">>>> err: not fd.\n");
        exit(-1);
    }

    printf("size: %u fd num: %lu\n", msg->msg.size, msg->fd_num);

    region = &vhost_server->memory.regions[vhost_server->memory.nregions];

    __add_reg(vhost_server, region, &msg->msg.memory_single.region, msg->fds[0]);

    return 0;
}

static int _set_mem_table(VhostServer* vhost_server, ServerMsg* msg)
{
    int idx;
    fprintf(stdout, "%s\n", __FUNCTION__);

    vhost_server->memory.nregions = 0;

    for (idx = 0; idx < msg->msg.memory.nregions; idx++) {
        __add_reg(vhost_server,
                  &vhost_server->memory.regions[idx],
                  &msg->msg.memory.regions[idx],
                  msg->fds[idx]);
    }

    fprintf(stdout, "Got memory.nregions %d\n", vhost_server->memory.nregions);

    if (vhost_server->prot_features & (1UL << VHOST_USER_PROTOCOL_F_REPLY_ACK)) {
        msg->msg.u64 = 0;
        return 1; // reply
    }

    return 0;
}

static int _set_log_base(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static int _set_log_fd(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static int _set_vring_num(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->msg.state.index;

    assert(idx<VHOST_CLIENT_VRING_NUM);

    vhost_server->vring_table.vring[idx].num = msg->msg.state.num;

    return 0;
}

static int _set_vring_addr(VhostServer* vhost_server, ServerMsg* msg)
{
    int idx = msg->msg.addr.index;
    uintptr_t p;

    fprintf(stdout, "%s  idx: %d\n", __FUNCTION__, idx);

    assert(idx<VHOST_CLIENT_VRING_NUM);

    printf("addrs: %p %p %p\n", (void *)msg->msg.addr.desc_user_addr,
                                (void *)msg->msg.addr.avail_user_addr,
                                (void *)msg->msg.addr.used_user_addr);

    p = _map_user_addr(vhost_server, msg->msg.addr.desc_user_addr);
    if (!p) {
        printf("get desc addr fail.\n");
        exit(-1);
    }
    vhost_server->vring_table.vring[idx].desc = (struct vring_desc*)p;

    p = _map_user_addr(vhost_server, msg->msg.addr.avail_user_addr);
    if (!p) {
        printf("get avail addr fail.\n");
        exit(-1);
    }
    vhost_server->vring_table.vring[idx].avail = (struct vring_avail*)p;

    p = _map_user_addr(vhost_server, msg->msg.addr.used_user_addr);
    if (!p) {
        printf("get used addr fail.\n");
        exit(-1);
    }
    vhost_server->vring_table.vring[idx].used = (struct vring_used*)p;

    vhost_server->vring_table.vring[idx].last_used_idx =
            vhost_server->vring_table.vring[idx].used->idx;
    printf("last_used_idx: %u\n", vhost_server->vring_table.vring[idx].last_used_idx);

    return 0;
}

static int _set_vring_base(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->msg.state.index;

    assert(idx<VHOST_CLIENT_VRING_NUM);

    vhost_server->vring_table.vring[idx].last_avail_idx = msg->msg.state.num;
    vhost_server->vring_table.vring[idx].last_used_idx = msg->msg.state.num;

    printf("ring idx: %d idx: %d\n", idx, msg->msg.state.num);

    return 0;
}

static int _get_vring_base(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->msg.state.index;
    Vring *vq;

    assert(idx<VHOST_CLIENT_VRING_NUM);

    msg->msg.state.num = vhost_server->vring_table.vring[idx].last_avail_idx;
    msg->msg.size = MEMB_SIZE(VhostUserMsg,state);


    vq = &vhost_server->vring_table.vring[idx];

    if (vq->enabled) {
        if (idx == VHOST_CLIENT_VRING_IDX_RX && !rx_busy_mode)
            return 1;

        vq->reset = true;
        vq->polling = false;
        while (true) {
            mb();

            if (vq->stopped)
                break;
        }
    }

    return 1; // should reply back
}

static int avail_handler_server(void* context, void* buf, size_t size)
{
    VhostServer* vhost_server = (VhostServer*) context;

    // copy the packet to our private buffer
    memcpy(vhost_server->buffer, buf, size);
    vhost_server->buffer_size = size;

#ifdef DUMP_PACKETS
    if (dump_packet)
        dump_buffer(buf, size);
#endif

    return 0;
}

static uintptr_t map_handler(void* context, uint64_t addr)
{
    VhostServer* vhost_server = (VhostServer*) context;
    return _map_guest_addr(vhost_server, addr);
}

static int _poll_avail_vring(VhostServer* vhost_server, int idx)
{
    uint32_t count = 0;

    // if vring is already set, process the vring
    if (vhost_server->vring_table.vring[idx].desc) {
        count = process_avail_vring(&vhost_server->vring_table, idx);

        stat.count += count;
        stat.kick_num += 1;
    }

    return count;
}

static int _kick_server(FdNode* node)
{
    VhostServer* vhost_server = (VhostServer*) node->context;
    int kickfd = node->fd;
    ssize_t r;
    uint64_t kick_it = 0;

    r = read(kickfd, &kick_it, sizeof(kick_it));

    if (r < 0) {
        perror("recv kick");
    } else if (r == 0) {
        fprintf(stdout, "Kick fd closed\n");
        del_fd_list(&vhost_server->server->fd_list, FD_READ, kickfd);
    } else {
        _poll_avail_vring(vhost_server, VHOST_CLIENT_VRING_IDX_TX);
    }

    return 0;
}

static void *busy_mode_tx_cycle(void *p)
{
    VhostServer* vhost_server = p;
    Vring *vq;

    vq = &vhost_server->vring_table.vring[VHOST_CLIENT_VRING_IDX_TX];

    vq->stat = &stat;

    process_avail_vring_busy(&vhost_server->vring_table,
                             VHOST_CLIENT_VRING_IDX_TX);

    return NULL;
}

static void *busy_mode_rx_cycle(void *p)
{
    VhostServer* vhost_server = p;
    Vring *vq;

    vq = &vhost_server->vring_table.vring[VHOST_CLIENT_VRING_IDX_RX];

    vq->stat = &rx_stat;

    process_avail_vring_busy(&vhost_server->vring_table,
                             VHOST_CLIENT_VRING_IDX_RX);

    return NULL;
}

static int _vring_enable(VhostServer* vhost_server, ServerMsg* msg)
{
    int idx = msg->msg.u64 & VHOST_USER_VRING_IDX_MASK;
    pthread_t th;
    Vring *vq;

    vq = &vhost_server->vring_table.vring[idx];
    if (vq->desc)
        vq->enabled = true;

    if (idx == VHOST_CLIENT_VRING_IDX_TX)
        vq->process_desc = process_tx_desc;
    else
        vq->process_desc = process_rx_desc;

    if (idx == VHOST_CLIENT_VRING_IDX_TX && busy_mode) {
        pthread_create(&th, 0, busy_mode_tx_cycle, vhost_server);
    }

    if (idx == VHOST_CLIENT_VRING_IDX_RX && rx_busy_mode) {
        pthread_create(&th, 0, busy_mode_rx_cycle, vhost_server);
    }

    return 0;
}

static int _set_vring_kick(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->msg.u64 & VHOST_USER_VRING_IDX_MASK;
    int validfd = (msg->msg.u64 & VHOST_USER_VRING_NOFD_MASK) == 0;

    assert(idx<VHOST_CLIENT_VRING_NUM);
    if (validfd) {
        assert(msg->fd_num == 1);

        vhost_server->vring_table.vring[idx].kickfd = msg->fds[0];

        fprintf(stdout, "Got kickfd 0x%x\n", vhost_server->vring_table.vring[idx].kickfd);

        if (idx == VHOST_CLIENT_VRING_IDX_TX && !busy_mode) {
            add_fd_list(&vhost_server->server->fd_list, FD_READ,
                    vhost_server->vring_table.vring[idx].kickfd,
                    (void*) vhost_server, _kick_server);
            fprintf(stdout, "Listening for kicks on 0x%x\n", vhost_server->vring_table.vring[idx].kickfd);
        }
        vhost_server->is_polling = 0;
    } else {
        fprintf(stdout, "Got empty kickfd. Start polling.\n");
        vhost_server->is_polling = 1;
    }

    return 0;
}

static int _set_vring_call(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);

    int idx = msg->msg.u64 & VHOST_USER_VRING_IDX_MASK;
    int validfd = (msg->msg.u64 & VHOST_USER_VRING_NOFD_MASK) == 0;
    Vring *vq;

    assert(idx<VHOST_CLIENT_VRING_NUM);
    if (validfd) {
        assert(msg->fd_num == 1);

        vhost_server->vring_table.vring[idx].callfd = msg->fds[0];

        fprintf(stdout, "Got callfd 0x%x\n", vhost_server->vring_table.vring[idx].callfd);
    }

    vq = &vhost_server->vring_table.vring[idx];

    if (vq->reset) {
        vq->reset = false;

        if (idx == VHOST_CLIENT_VRING_IDX_TX && busy_mode) {
            pthread_t th;
            pthread_create(&th, 0, busy_mode_tx_cycle, vhost_server);
        }

        if (idx == VHOST_CLIENT_VRING_IDX_RX && rx_busy_mode) {
            pthread_t th;
            pthread_create(&th, 0, busy_mode_rx_cycle, vhost_server);
        }
    }

    return 0;
}

static int _set_vring_err(VhostServer* vhost_server, ServerMsg* msg)
{
    fprintf(stdout, "%s\n", __FUNCTION__);
    return 0;
}

static MsgHandler msg_handlers[VHOST_USER_MAX] = {
        0,                  // VHOST_USER_NONE
        _get_features,      // VHOST_USER_GET_FEATURES
        _set_features,      // VHOST_USER_SET_FEATURES
        _set_owner,         // VHOST_USER_SET_OWNER
        _reset_owner,       // VHOST_USER_RESET_OWNER
        _set_mem_table,     // VHOST_USER_SET_MEM_TABLE
        _set_log_base,      // VHOST_USER_SET_LOG_BASE
        _set_log_fd,        // VHOST_USER_SET_LOG_FD
        _set_vring_num,     // VHOST_USER_SET_VRING_NUM
        _set_vring_addr,    // VHOST_USER_SET_VRING_ADDR
        _set_vring_base,    // VHOST_USER_SET_VRING_BASE
        _get_vring_base,    // VHOST_USER_GET_VRING_BASE
        _set_vring_kick,    // VHOST_USER_SET_VRING_KICK
        _set_vring_call,    // VHOST_USER_SET_VRING_CALL
        _set_vring_err,     // VHOST_USER_SET_VRING_ERR
        _get_prot_features, // VHOST_USER_GET_PROTOCOL_FEATURES
        _set_prot_features, // VHOST_USER_GET_PROTOCOL_FEATURES
        [VHOST_USER_GET_MAX_MEM_SLOTS] = _get_mem_slots,
        [VHOST_USER_ADD_MEM_REG] = _add_mem_reg,
        [VHOST_USER_SET_VRING_ENABLE] = _vring_enable,
        [VHOST_USER_SET_SLAVE_REQ_FD] = _set_slave_req_fd,
        };

static int in_msg_server(void* context, ServerMsg* msg)
{
    VhostServer* vhost_server = (VhostServer*) context;
    int result = 0;

    fprintf(stdout, "Processing message: %s idx: %d\n",
            cmd_from_vhostmsg(&msg->msg),
            msg->msg.state.index);

    assert(msg->msg.request > VHOST_USER_NONE && msg->msg.request < VHOST_USER_MAX);

    if (msg_handlers[msg->msg.request]) {
        result = msg_handlers[msg->msg.request](vhost_server, msg);
    } else {
        printf(">>> NOT Found cmd handle.\n");
    }

    return result;
}

static int poll_server(void* context)
{
    //VhostServer* vhost_server = (VhostServer*) context;
    //int tx_idx = VHOST_CLIENT_VRING_IDX_TX;
    //int rx_idx = VHOST_CLIENT_VRING_IDX_RX;

    //if (vhost_server->vring_table.vring[rx_idx].desc) {
    //    // process TX ring
    //    //if (vhost_server->is_polling && !busy_mode) {
    //    //    _poll_avail_vring(vhost_server, tx_idx);
    //    //}

    //    // process RX ring
    //    if (vhost_server->buffer_size) {
    //        // send a packet from the buffer
    //        put_vring(&vhost_server->vring_table, rx_idx,
    //                  vhost_server->buffer, vhost_server->buffer_size);

    //        // signal the client
    //        kick(&vhost_server->vring_table, rx_idx);

    //        // mark the buffer empty
    //        vhost_server->buffer_size = 0;
    //    }
    //}

    return 0;
}

static AppHandlers vhost_server_handlers =
{
        .context = 0,
        .in_handler = in_msg_server,
        .poll_handler = poll_server
};

int start_stat(struct VhostServer *vhost_server);

int run_vhost_server(VhostServer* vhost_server)
{
    vhost_server_handlers.context = vhost_server;
    set_handler_server(vhost_server->server, &vhost_server_handlers);

    start_stat(vhost_server);

    app_running = 1; // externally modified
    while (app_running) {
        loop_server(vhost_server->server);
    }

    //stop_stat(&vhost_server->stat);

    return 0;
}
