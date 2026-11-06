/*
 ============================================================================
 Name        : hev-socks5-session-dns.c
 Copyright   : Copyright (c) Wanwire LLC
 Description : Socks5 Session DNS
 ============================================================================

 The device sends every DNS query from a new UDP port, so every query is a new
 UDP flow. Without this session, each flow gets its own Socks5 UDP association:
 a task, a TCP connection and a UDP socket here, and a session with goroutines
 in the Socks5 server, all held until the flow has been idle for the UDP
 timeout. This session sends the queries of all flows to the DNS address over
 one association instead.

 Two flows can use the same DNS query ID, so the session gives each query an ID
 of its own: the index of a slot that remembers the flow and the original ID.
 The answer comes back with that ID, so the session finds the slot, puts the
 original ID back and sends the answer through the flow. Then the flow is
 removed, unless another query waits on it.
 */

#include <errno.h>
#include <string.h>
#include <time.h>

#include <lwip/udp.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-udp.h>
#include <hev-socks5-misc.h>

#include "hev-utils.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-compiler.h"
#include "hev-config-const.h"
#include "hev-socks5-tunnel.h"

#include "hev-socks5-session-dns.h"

/* The low bits of a query ID are the slot index, the high bits a generation
 * that changes each time the slot is used, so that a late answer for an
 * earlier query in the same slot is dropped */
#define SLOT_BITS 10
#define SLOT_MASK (HEV_SOCKS5_SESSION_DNS_SLOTS - 1)
#define GENERATION_MASK ((1 << (16 - SLOT_BITS)) - 1)

_Static_assert (HEV_SOCKS5_SESSION_DNS_SLOTS == (1 << SLOT_BITS),
                "slots must match SLOT_BITS");

/* A query without an answer in this time is forgotten. The device has asked
 * again by then */
#define QUERY_TIMEOUT_MS 10000
#define SWEEP_INTERVAL_MS 1000
#define DNS_HEADER_SIZE 12

typedef struct _HevSocks5DNSFrame HevSocks5DNSFrame;

struct _HevSocks5DNSFrame
{
    HevListNode node;
    HevSocks5Addr addr;
    struct pbuf *data;
};

static HevSocks5SessionDNS *shared;

static int64_t
now_ms (void)
{
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Removes the flow when no query waits on it. Call with the lwIP mutex held */
static void
release_pcb (HevSocks5SessionDNS *self, struct udp_pcb *pcb)
{
    int i;

    for (i = 0; i < HEV_SOCKS5_SESSION_DNS_SLOTS; i++)
        if (self->slots[i].pcb == pcb)
            return;

    udp_recv (pcb, NULL, NULL);
    udp_remove (pcb);
}

/* Forgets all queries of the flow and removes it. Call with the lwIP mutex
 * held */
static void
forget_pcb (HevSocks5SessionDNS *self, struct udp_pcb *pcb)
{
    int i;

    for (i = 0; i < HEV_SOCKS5_SESSION_DNS_SLOTS; i++)
        if (self->slots[i].pcb == pcb)
            self->slots[i].pcb = NULL;

    udp_recv (pcb, NULL, NULL);
    udp_remove (pcb);
}

/* Returns the slot index for a new query, or -1 when all slots wait. Call with
 * the lwIP mutex held: a slot whose query timed out may release its flow */
static int
alloc_slot (HevSocks5SessionDNS *self, struct udp_pcb *pcb, uint16_t id)
{
    int64_t now = now_ms ();
    int i;

    for (i = 0; i < HEV_SOCKS5_SESSION_DNS_SLOTS; i++) {
        unsigned int index = (self->cursor + i) & SLOT_MASK;
        HevSocks5SessionDNSSlot *slot = &self->slots[index];
        struct udp_pcb *old = slot->pcb;

        if (old && slot->expire > now)
            continue;

        slot->pcb = pcb;
        slot->id = id;
        slot->expire = now + QUERY_TIMEOUT_MS;
        slot->generation = (slot->generation + 1) & GENERATION_MASK;
        self->cursor = index + 1;

        if (old && old != pcb)
            release_pcb (self, old);

        return index;
    }

    return -1;
}

/* Forgets the queries that timed out. Runs in the session task */
static void
sweep (HevSocks5SessionDNS *self)
{
    int64_t now = now_ms ();
    int i;

    if (now < self->next_sweep)
        return;
    self->next_sweep = now + SWEEP_INTERVAL_MS;

    hev_task_mutex_lock (self->mutex);
    for (i = 0; i < HEV_SOCKS5_SESSION_DNS_SLOTS; i++) {
        HevSocks5SessionDNSSlot *slot = &self->slots[i];
        struct udp_pcb *pcb = slot->pcb;

        if (!pcb || slot->expire > now)
            continue;

        slot->pcb = NULL;
        release_pcb (self, pcb);
    }
    hev_task_mutex_unlock (self->mutex);
}

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5 *self = data;
    HevListNode *node;
    int res;

    if (self->type == HEV_SOCKS5_TYPE_UDP_IN_UDP) {
        ssize_t res;
        char buf;

        res = recv (self->fd, &buf, sizeof (buf), 0);
        if ((res == 0) || ((res < 0) && (errno != EAGAIN))) {
            hev_socks5_set_timeout (self, 0);
            return -1;
        }
    }

    res = hev_socks5_task_io_yielder (type, data);
    node = hev_socks5_session_get_node (HEV_SOCKS5_SESSION (self));
    hev_socks5_tunnel_update_session (node);

    return res;
}

/* Sends the waiting queries to the Socks5 server */
static int
hev_socks5_session_dns_fwd_f (HevSocks5SessionDNS *self, unsigned int num)
{
    HevSocks5UDPMsg msgv[num];
    HevSocks5DNSFrame *frame;
    HevListNode *node;
    struct pbuf *buf;
    int i, res;

    res = self->frames;
    if (res <= 0)
        return 0;

    res = (res > num) ? num : res;
    node = hev_list_first (&self->frame_list);
    for (i = 0; i < res; i++) {
        frame = container_of (node, HevSocks5DNSFrame, node);
        node = hev_list_node_next (node);
        buf = frame->data;

        msgv[i].buf = buf->payload;
        msgv[i].len = buf->len;
        msgv[i].addr = &frame->addr;
    }

    res = hev_socks5_udp_sendmmsg (HEV_SOCKS5_UDP (self), msgv, res);
    if (res <= 0) {
        LOG_D ("%p socks5 session dns fwd f send", self);
        return -1;
    }

    for (i = 0; i < res; i++) {
        node = hev_list_first (&self->frame_list);
        frame = container_of (node, HevSocks5DNSFrame, node);
        buf = frame->data;

        hev_list_del (&self->frame_list, node);
        hev_free (frame);
        pbuf_free (buf);
        self->frames--;
    }

    return 1;
}

/* Sends the answers from the Socks5 server back through their flows */
static int
hev_socks5_session_dns_fwd_b (HevSocks5SessionDNS *self, unsigned int num)
{
    char buf[UDP_BUF_SIZE * num];
    HevSocks5UDPMsg msgv[num];
    int i, res;

    for (i = 0; i < num; i++) {
        msgv[i].buf = buf + UDP_BUF_SIZE * i;
        msgv[i].len = UDP_BUF_SIZE;
    }

    res = hev_socks5_udp_recvmmsg (HEV_SOCKS5_UDP (self), msgv, num, 1);
    if (res <= 0) {
        if (res == -1 && errno == EAGAIN)
            return 0;
        LOG_D ("%p socks5 session dns fwd b recv", self);
        return -1;
    }

    for (i = 0; i < res; i++) {
        HevSocks5SessionDNSSlot *slot;
        struct udp_pcb *pcb;
        struct pbuf *b;
        uint16_t id;

        if (!msgv[i].addr || msgv[i].len < DNS_HEADER_SIZE)
            continue;

        memcpy (&id, msgv[i].buf, sizeof (id));
        slot = &self->slots[id & SLOT_MASK];
        pcb = slot->pcb;
        if (!pcb || slot->generation != (id >> SLOT_BITS)) {
            LOG_D ("%p socks5 session dns late answer", self);
            continue;
        }

        memcpy (msgv[i].buf, &slot->id, sizeof (slot->id));
        slot->pcb = NULL;

        b = pbuf_alloc_reference (msgv[i].buf, msgv[i].len, PBUF_REF);

        hev_task_mutex_lock (self->mutex);
        if (b) {
            /* From the address that the device asked, like a direct answer */
            if (udp_sendfrom (pcb, b, &pcb->local_ip, pcb->local_port) != ERR_OK)
                LOG_D ("%p socks5 session dns fwd b send", self);
        }
        release_pcb (self, pcb);
        hev_task_mutex_unlock (self->mutex);

        if (b)
            pbuf_free (b);
    }

    return 1;
}

/* A query from a flow. Runs in the lwIP task, with the lwIP mutex held */
static void
pcb_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5SessionDNS *self = arg;
    HevSocks5DNSFrame *frame;
    uint16_t id;
    int index;

    if (!p) {
        forget_pcb (self, pcb);
        return;
    }

    if (p->len != p->tot_len) {
        struct pbuf *q = pbuf_clone (PBUF_TRANSPORT, PBUF_RAM, p);

        pbuf_free (p);
        p = q;
        if (!p)
            goto drop;
    }

    if (p->len < DNS_HEADER_SIZE || self->frames > UDP_POOL_SIZE)
        goto drop;

    frame = hev_malloc (sizeof (HevSocks5DNSFrame));
    if (!frame)
        goto drop;

    memcpy (&id, p->payload, sizeof (id));
    index = alloc_slot (self, pcb, id);
    if (index < 0) {
        LOG_D ("%p socks5 session dns no free slot", self);
        hev_free (frame);
        goto drop;
    }

    id = (self->slots[index].generation << SLOT_BITS) | index;
    memcpy (p->payload, &id, sizeof (id));

    frame->data = p;
    memset (&frame->node, 0, sizeof (frame->node));
    hev_socks5_addr_from_lwip (&frame->addr, &pcb->local_ip, pcb->local_port);

    self->frames++;
    hev_list_add_tail (&self->frame_list, &frame->node);
    hev_task_wakeup (self->data.task);
    return;

drop:
    if (p)
        pbuf_free (p);
    release_pcb (self, pcb);
}

void
hev_socks5_session_dns_add_pcb (HevSocks5SessionDNS *self, struct udp_pcb *pcb)
{
    udp_recv (pcb, pcb_recv_handler, self);
}

HevSocks5SessionDNS *
hev_socks5_session_dns_get_shared (void)
{
    return shared;
}

static int
hev_socks5_session_dns_construct (HevSocks5SessionDNS *self,
                                  HevTaskMutex *mutex);

HevSocks5SessionDNS *
hev_socks5_session_dns_new (HevTaskMutex *mutex)
{
    HevSocks5SessionDNS *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5SessionDNS));
    if (!self)
        return NULL;

    res = hev_socks5_session_dns_construct (self, mutex);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    LOG_D ("%p socks5 session dns new", self);

    shared = self;
    return self;
}

static uint16_t
hev_socks5_addr_get_port (const HevSocks5Addr *addr)
{
    uint16_t port = 0;

    switch (addr->atype) {
    case HEV_SOCKS5_ADDR_TYPE_IPV4:
        port = addr->ipv4.port;
        break;
    case HEV_SOCKS5_ADDR_TYPE_IPV6:
        port = addr->ipv6.port;
        break;
    case HEV_SOCKS5_ADDR_TYPE_NAME:
        memcpy (&port, addr->domain.addr + addr->domain.len, 2);
    }

    return port;
}

static int
hev_socks5_session_dns_set_upstream_addr (HevSocks5Client *base,
                                          HevSocks5Addr *addr)
{
    HevConfigServer *srv = hev_config_get_socks5_server ();
    HevSocks5ClientClass *ckptr;

    if (srv->udp_in_udp && srv->udp_addr[0]) {
        uint16_t port = hev_socks5_addr_get_port (addr);
        hev_socks5_addr_from_name (addr, srv->udp_addr, port);
    }

    ckptr = HEV_SOCKS5_CLIENT_CLASS (HEV_SOCKS5_CLIENT_UDP_TYPE);
    return ckptr->set_upstream_addr (base, addr);
}

static void
hev_socks5_session_dns_splice (HevSocks5Session *base)
{
    HevSocks5SessionDNS *self = HEV_SOCKS5_SESSION_DNS (base);
    HevTask *task = hev_task_self ();
    int res_f = 1, res_b = 1;
    int num;
    int fd;

    LOG_D ("%p socks5 session dns splice", self);

    num = hev_config_get_misc_udp_copy_buffer_nums ();
    fd = hev_socks5_udp_get_fd (HEV_SOCKS5_UDP (self));
    if (hev_task_mod_fd (task, fd, POLLIN | POLLOUT) < 0)
        hev_task_add_fd (task, fd, POLLIN | POLLOUT);

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = hev_socks5_session_dns_fwd_f (self, num);
        if (res_b >= 0)
            res_b = hev_socks5_session_dns_fwd_b (self, num);
        sweep (self);

        if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else if ((res_f & res_b) == 0)
            type = HEV_TASK_WAITIO;
        else
            break;

        if (task_io_yielder (type, self))
            break;
    }
}

static HevTask *
hev_socks5_session_dns_get_task (HevSocks5Session *base)
{
    HevSocks5SessionDNS *self = HEV_SOCKS5_SESSION_DNS (base);

    return self->data.task;
}

static void
hev_socks5_session_dns_set_task (HevSocks5Session *base, HevTask *task)
{
    HevSocks5SessionDNS *self = HEV_SOCKS5_SESSION_DNS (base);

    self->data.task = task;
}

static HevListNode *
hev_socks5_session_dns_get_node (HevSocks5Session *base)
{
    HevSocks5SessionDNS *self = HEV_SOCKS5_SESSION_DNS (base);

    return &self->data.node;
}

static int
hev_socks5_session_dns_construct (HevSocks5SessionDNS *self,
                                  HevTaskMutex *mutex)
{
    HevConfigServer *srv = hev_config_get_socks5_server ();
    int type;
    int res;

    if (srv->udp_in_udp)
        type = HEV_SOCKS5_TYPE_UDP_IN_UDP;
    else
        type = HEV_SOCKS5_TYPE_UDP_IN_TCP;

    res = hev_socks5_client_udp_construct (&self->base, type);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session dns construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_DNS_TYPE;

    self->mutex = mutex;
    self->data.self = self;

    return 0;
}

static void
hev_socks5_session_dns_destruct (HevObject *base)
{
    HevSocks5SessionDNS *self = HEV_SOCKS5_SESSION_DNS (base);
    HevListNode *node;
    int i;

    LOG_D ("%p socks5 session dns destruct", self);

    if (shared == self)
        shared = NULL;

    node = hev_list_first (&self->frame_list);
    while (node) {
        HevSocks5DNSFrame *frame;

        frame = container_of (node, HevSocks5DNSFrame, node);
        node = hev_list_node_next (node);
        pbuf_free (frame->data);
        hev_free (frame);
    }

    hev_task_mutex_lock (self->mutex);
    for (i = 0; i < HEV_SOCKS5_SESSION_DNS_SLOTS; i++)
        if (self->slots[i].pcb)
            forget_pcb (self, self->slots[i].pcb);
    hev_task_mutex_unlock (self->mutex);

    HEV_SOCKS5_CLIENT_UDP_TYPE->destruct (base);
}

static void *
hev_socks5_session_dns_iface (HevObject *base, void *type)
{
    if (type == HEV_SOCKS5_SESSION_TYPE) {
        HevSocks5SessionDNSClass *klass = HEV_OBJECT_GET_CLASS (base);
        return &klass->session;
    }

    return HEV_SOCKS5_CLIENT_UDP_TYPE->iface (base, type);
}

HevObjectClass *
hev_socks5_session_dns_class (void)
{
    static HevSocks5SessionDNSClass klass;
    HevSocks5SessionDNSClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5Class *skptr;
        HevSocks5ClientClass *ckptr;
        HevSocks5SessionIface *siptr;
        void *ptr;

        ptr = HEV_SOCKS5_CLIENT_UDP_TYPE;
        memcpy (kptr, ptr, sizeof (HevSocks5ClientUDPClass));

        okptr->name = "HevSocks5SessionDNS";
        okptr->destruct = hev_socks5_session_dns_destruct;
        okptr->iface = hev_socks5_session_dns_iface;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_bind;

        ckptr = HEV_SOCKS5_CLIENT_CLASS (kptr);
        ckptr->set_upstream_addr = hev_socks5_session_dns_set_upstream_addr;

        siptr = &kptr->session;
        siptr->splicer = hev_socks5_session_dns_splice;
        siptr->get_task = hev_socks5_session_dns_get_task;
        siptr->set_task = hev_socks5_session_dns_set_task;
        siptr->get_node = hev_socks5_session_dns_get_node;
    }

    return okptr;
}
