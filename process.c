#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>

#include <arpa/inet.h>
#include <string.h>
#include "packet.h"
#include "vhost.h"

struct packet{
    char packet[1500];
    char payload[14];
    struct packet_info info;
    struct sockaddr_in to;
    struct sockaddr_in from;

    void *hdr;
    int len;
};

struct packet udp_packet;
// support version 1.0
int hdr_len = sizeof(struct virtio_net_hdr_mrg_rxbuf);

int process_tx_desc(void *cur, uint32_t len, uint32_t offset)
{
    return len;
}

int process_rx_desc(void *cur, uint32_t len, uint32_t offset)
{

    uint32_t copy = udp_packet.len - offset;

    if (!copy)
        return 0;

    if (len < copy)
        copy = len;

    memcpy(cur, udp_packet.hdr + offset, copy);

    return copy;
}

void udp_packet_init()
{
    struct packet_info *info;
    struct virtio_net_hdr *hdr;

    info = &udp_packet.info;

    info->dmac = (unsigned char *)"\x52\x55\x00\xd1\x97\xb8";
    info->smac = (unsigned char *)"\xff\xff\xff\xff\xff\xff";
    info->head = udp_packet.packet + hdr_len;
    info->payload = udp_packet.payload;
    info->payload_size = sizeof(udp_packet.payload);

    info->to = &udp_packet.to;
    info->from = &udp_packet.from;

    info->to->sin_addr.s_addr = inet_addr("10.0.3.100");
    info->to->sin_port = htons(8080);

    info->from->sin_addr.s_addr = inet_addr("10.0.3.101");
    info->from->sin_port = htons(8080);

    xudp_packet_udp_payload(info);

    hdr = (void *)info->packet - hdr_len;
    hdr->flags = 0;
    hdr->gso_type = 0;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;

    udp_packet.hdr = hdr;
    udp_packet.len = info->len + hdr_len;
}

