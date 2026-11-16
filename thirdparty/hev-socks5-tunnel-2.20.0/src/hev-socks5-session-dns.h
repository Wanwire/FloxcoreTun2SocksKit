/*
 ============================================================================
 Name        : hev-socks5-session-dns.h
 Copyright   : Copyright (c) Wanwire LLC
 Description : Socks5 Session DNS: one Socks5 UDP association for all DNS
               queries (UDP port 53)
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_DNS_H__
#define __HEV_SOCKS5_SESSION_DNS_H__

#include <stdint.h>

#include <hev-socks5-client-udp.h>

#include "hev-socks5-session.h"

#define HEV_SOCKS5_SESSION_DNS(p) ((HevSocks5SessionDNS *)p)
#define HEV_SOCKS5_SESSION_DNS_CLASS(p) ((HevSocks5SessionDNSClass *)p)
#define HEV_SOCKS5_SESSION_DNS_TYPE (hev_socks5_session_dns_class ())

/* Queries that wait for an answer at the same time. A power of two: the slot
 * index is the low bits of the query ID that the session sends */
#define HEV_SOCKS5_SESSION_DNS_SLOTS 1024

typedef struct _HevSocks5SessionDNS HevSocks5SessionDNS;
typedef struct _HevSocks5SessionDNSClass HevSocks5SessionDNSClass;
typedef struct _HevSocks5SessionDNSSlot HevSocks5SessionDNSSlot;

/* A query that waits for its answer. pcb is the lwIP flow of the query (NULL
 * for a free slot); the answer goes back through it */
struct _HevSocks5SessionDNSSlot
{
    struct udp_pcb *pcb;
    int64_t expire;
    uint16_t id;
    uint16_t generation;
};

struct _HevSocks5SessionDNS
{
    HevSocks5ClientUDP base;

    HevSocks5SessionData data;

    HevList frame_list;
    HevTaskMutex *mutex;
    int frames;
    unsigned int cursor;
    int64_t next_sweep;

    HevSocks5SessionDNSSlot slots[HEV_SOCKS5_SESSION_DNS_SLOTS];
};

struct _HevSocks5SessionDNSClass
{
    HevSocks5ClientUDPClass base;

    HevSocks5SessionIface session;
};

HevObjectClass *hev_socks5_session_dns_class (void);

HevSocks5SessionDNS *hev_socks5_session_dns_new (HevTaskMutex *mutex);

/* The session that runs now, or NULL */
HevSocks5SessionDNS *hev_socks5_session_dns_get_shared (void);

/* Sends the queries of a new lwIP flow through the session */
void hev_socks5_session_dns_add_pcb (HevSocks5SessionDNS *self,
                                     struct udp_pcb *pcb);

#endif /* __HEV_SOCKS5_SESSION_DNS_H__ */
