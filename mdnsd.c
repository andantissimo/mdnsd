#include <ifaddrs.h>
#include <limits.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

#ifndef INADDR_ALLMDNS_GROUP
#define INADDR_ALLMDNS_GROUP ((in_addr_t)0xE00000FB) /* 224.0.0.251 */
#endif

static in_addr_t get_local_addr_for(const struct ifaddrs *ifaddrs, in_addr_t remote)
{
    const struct ifaddrs *ifa;
    for (ifa = ifaddrs; ifa; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        in_addr_t addr = ((const struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
        in_addr_t mask = ((const struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
        if ((addr & mask) == (remote & mask))
            return addr;
    }
    return 0;
}

int main(void)
{
    const uint32_t ttl = 3600;

    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, sizeof hostname))
        return 1;
    char *dot = strchr(hostname, '.');
    if (dot && dot - hostname + 6 < sizeof hostname)
        strcpy(dot, ".local");
    else if (strlen(hostname) + 6 < sizeof hostname)
        strcat(hostname, ".local");

    struct ifaddrs *ifaddrs;
    if (getifaddrs(&ifaddrs))
        return 1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return 1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5353);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (const struct sockaddr *)&addr, sizeof addr))
        return 1;
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof mreq);
    mreq.imr_multiaddr.s_addr = htonl(INADDR_ALLMDNS_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof mreq))
        return 1;

    uint8_t qbuf[512 - 16];
    for (;;)
    accept:
    {
        struct sockaddr_in from;
        socklen_t slen = sizeof from;
        ssize_t qlen = recvfrom(sock, qbuf, sizeof qbuf, 0, (struct sockaddr *)&from, &slen);
        if (qlen < 12 + 5)
            continue;
        uint16_t qid  = ntohs(*(uint16_t *)&qbuf[0]);
        uint16_t qbit = ntohs(*(uint16_t *)&qbuf[2]);
        uint16_t qcnt = ntohs(*(uint16_t *)&qbuf[4]);
        uint16_t acnt = ntohs(*(uint16_t *)&qbuf[6]);
        if ((qbit & 0x8000) != 0 || qcnt != 1 || acnt != 0)
            continue;
        uint8_t *qptr = &qbuf[12];
        char qname[HOST_NAME_MAX + 1];
        memset(qname, 0, sizeof qname);
        char *p = qname;
        while (p < qname + sizeof qname - 2 && qptr + 5 <= qbuf + sizeof qbuf)
        {
            uint8_t n = *qptr++;
            if (n == 0)
                break;
            if (p + n >= qname + sizeof qname - 2 || qptr + n + 5 > qbuf + sizeof qbuf)
                goto accept;
            if (qptr - 1 != &qbuf[12])
                *p++ = '.';
            memcpy(p, qptr, n);
            p += n, qptr += n;
        }
        uint16_t qtype  = ntohs(*(uint16_t *)&qptr[0]);
        uint16_t qclass = ntohs(*(uint16_t *)&qptr[2]) & ~0x8000;
        qptr += 4;
        if (qtype != 1 || qclass != 1 || strcasecmp(qname, hostname))
            continue;
        in_addr_t local = get_local_addr_for(ifaddrs, from.sin_addr.s_addr);
        if (!local)
            continue;
        uint8_t rbuf[512];
        memset(rbuf, 0, sizeof rbuf);
        *(uint16_t *)&rbuf[0] = htons(qid);
        *(uint16_t *)&rbuf[2] = htons(0x8000);
        *(uint16_t *)&rbuf[4] = htons(1);
        *(uint16_t *)&rbuf[6] = htons(1);
        memcpy(&rbuf[12], &qbuf[12], qptr - &qbuf[12]);
        uint8_t *rptr = &rbuf[qptr - qbuf];
        *(uint16_t *)&rptr[0] = htons(0xC000 | 12);
        *(uint16_t *)&rptr[2] = htons(1);
        *(uint16_t *)&rptr[4] = htons(1);
        *(uint32_t *)&rptr[6] = htonl(ttl);
        *(uint16_t *)&rptr[10] = htons(4);
        *(uint32_t *)&rptr[12] = local;
        size_t rlen = rptr + 16 - rbuf;
        sendto(sock, rbuf, rlen, 0, (const struct sockaddr *)&from, slen);
    }
}
