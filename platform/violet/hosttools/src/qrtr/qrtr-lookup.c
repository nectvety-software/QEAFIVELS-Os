// SPDX-License-Identifier: MIT
/* qrtr-lookup SERVICE [VERSION] [INSTANCE] — ordinary QRTR client that asks
 * the name service for a service and prints NEW_SERVER/DEL_SERVER packets.
 * Works when the kernel (not userspace) owns the control port. */
#include <libqrtr.h>
#include <linux/qrtr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct sockaddr_qrtr sq = { .sq_family = AF_QIPCRTR };
    DEFINE_QRTR_PACKET(pkt, 4096);
    uint32_t service, version = 1, instance = 0, node = 0, port = 0;
    int fd;

    if(argc < 2) { fprintf(stderr, "usage: %s SERVICE [VER] [INST]\n", argv[0]); return 1; }
    service = strtoul(argv[1], NULL, 0);
    if(argc > 2) version = strtoul(argv[2], NULL, 0);
    if(argc > 3) instance = strtoul(argv[3], NULL, 0);

    fd = qrtr_open(0);
    if(fd < 0) { perror("qrtr_open"); return 1; }
    if(qrtr_new_lookup(fd, service, version, instance) < 0) {
        perror("qrtr_new_lookup"); return 1;
    }
    printf("lookup 0x%x/0x%x/0x%x sent\n", service, version, instance);
    fflush(stdout);

    for(int i = 0; i < 20; i++) {
        int n = qrtr_recvfrom(fd, pkt_buf, sizeof(pkt_buf), &node, &port);
        if(n <= 0) { usleep(200000); continue; }
        sq.sq_node = node;
        sq.sq_port = port;
        if(qrtr_decode(&pkt, pkt_buf, n, &sq) < 0) continue;
        if(pkt.type == QRTR_TYPE_NEW_SERVER || pkt.type == QRTR_TYPE_DEL_SERVER)
            printf("%s svc=0x%x ver=0x%x inst=0x%x node=0x%x port=0x%x\n",
                   pkt.type == QRTR_TYPE_NEW_SERVER ? "NEW_SERVER" : "DEL_SERVER",
                   pkt.service, pkt.version, pkt.instance, pkt.node, pkt.port);
        else
            printf("pkt type=%d svc=0x%x node=0x%x port=0x%x\n",
                   pkt.type, pkt.service, pkt.node, pkt.port);
        fflush(stdout);
    }
    qrtr_close(fd);
    return 0;
}
