#ifndef Tun2SocksKitC_H
#define Tun2SocksKitC_H

#include <stdint.h>

#define CTLIOCGINFO 0xc0644e03UL

struct ctl_info
{
    uint32_t ctl_id;
    char ctl_name[96];
};
struct sockaddr_ctl
{
    uint8_t sc_len;
    uint8_t sc_family;
    uint16_t ss_sysaddr;
    uint32_t sc_id;
    uint32_t sc_unit;
    uint32_t sc_reserved[5];
};

#endif /* Tun2SocksKitC_H */
