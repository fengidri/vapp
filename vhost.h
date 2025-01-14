/*
 * vhost.h
 *
 * Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_H_
#define VHOST_H_

enum {
    VHOST_MEMORY_MAX_NREGIONS = 128
};

#define MAX_PKT_BURST 64


//#define mb()		__asm__ __volatile__ ("": : :"memory")

#define mb()	asm volatile("mfence":::"memory")
#define rmb()		mb()
//#define wmb()		__asm__ __volatile__ ("": : :"memory")
#define wmb()	asm volatile("mfence":::"memory")

// Structures imported from the Linux headers.
struct vhost_vring_state { unsigned int index, num; };
struct vhost_vring_file { unsigned int index; int fd; };
struct vhost_vring_addr {
  unsigned int index;
  unsigned int flags;
  uint64_t desc_user_addr;
  uint64_t used_user_addr;
  uint64_t avail_user_addr;
  uint64_t log_guest_addr;


};

#define VRING_USED_F_NO_NOTIFY	1
#define VRING_AVAIL_F_NO_INTERRUPT  1

#define VIRTIO_RING_F_EVENT_IDX		29

/* v1.0 compliant. */
#define VIRTIO_F_VERSION_1		32

#define VIRTIO_F_ACCESS_PLATFORM	33
/* Legacy name for VIRTIO_F_ACCESS_PLATFORM (for compatibility with old userspace) */
#define VIRTIO_F_IOMMU_PLATFORM		VIRTIO_F_ACCESS_PLATFORM

/*
 * This feature indicates that the driver can reset a queue individually.
 */
#define VIRTIO_F_RING_RESET		40

struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM     1       // Use csum_start, csum_offset
#define VIRTIO_NET_HDR_F_DATA_VALID    2       // Csum is valid
    uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE         0       // Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4        1       // GSO frame, IPv4 TCP (TSO)
#define VIRTIO_NET_HDR_GSO_UDP          3       // GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6        4       // GSO frame, IPv6 TCP
#define VIRTIO_NET_HDR_GSO_ECN          0x80    // TCP has ECN set
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};


struct virtio_net_hdr_mrg_rxbuf {
	struct virtio_net_hdr hdr;
	uint16_t num_buffers;	/* Number of merged rx buffers */
};

#endif /* VHOST_H_ */
