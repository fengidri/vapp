#ifndef __PROCESS_H__
#define __PROCESS_H__

void udp_packet_init();
int process_tx_desc(void *cur, uint32_t len, uint32_t offset);
int process_rx_desc(void *cur, uint32_t len, uint32_t offset);

#endif


