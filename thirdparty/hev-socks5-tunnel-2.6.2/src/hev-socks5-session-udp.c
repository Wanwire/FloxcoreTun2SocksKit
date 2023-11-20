/*
 ============================================================================
 Name        : hev-socks5-session-udp.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session UDP
 ============================================================================
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <lwip/udp.h>
#include <netinet/in.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-udp.h>
#include <hev-socks5-misc.h>

#include "hev-config.h"
#include "hev-logger.h"
#include "hev-compiler.h"
#include "hev-config-const.h"

#include "hev-socks5-session-udp.h"

#define task_io_yielder hev_socks5_task_io_yielder

typedef enum _HevSocks5UDPAlive HevSocks5UDPAlive;
typedef struct _HevSocks5UDPFrame HevSocks5UDPFrame;
typedef struct _HevSocks5UDPSplice HevSocks5UDPSplice;

enum _HevSocks5UDPAlive
{
    HEV_SOCKS5_UDP_ALIVE_F = (1 << 0),
    HEV_SOCKS5_UDP_ALIVE_B = (1 << 1),
};

struct _HevSocks5UDPFrame
{
    HevListNode node;
    struct sockaddr_in6 addr;
    struct pbuf *data;
};

struct _HevSocks5UDPSplice
{
    HevSocks5UDP *udp;
    HevTask *task;
    HevSocks5UDPAlive alive;
};

static int
hev_socks5_session_udp_fwd_f (HevSocks5SessionUDP *self,
                              HevSocks5UDPSplice *splice)
{
    HevSocks5UDPFrame *frame;
    struct sockaddr *addr;
    HevListNode *node;
    HevSocks5UDP *udp;
    struct pbuf *buf;
    int res;

    for (;;) {
        node = hev_list_first (&self->frame_list);
        if (node)
            break;

        res = task_io_yielder (HEV_TASK_WAITIO, self);
        if (res < 0) {
            splice->alive &= ~HEV_SOCKS5_UDP_ALIVE_F;
            if (splice->alive && hev_socks5_get_timeout (HEV_SOCKS5 (self)))
                return 0;
            return -1;
        }
    }

    frame = container_of (node, HevSocks5UDPFrame, node);
    addr = (struct sockaddr *)&frame->addr;
    buf = frame->data;

    udp = HEV_SOCKS5_UDP (self);
    res = hev_socks5_udp_sendto (udp, buf->payload, buf->len, addr);
    if (res <= 0) {
        if (res < -1) {
            splice->alive &= ~HEV_SOCKS5_UDP_ALIVE_F;
            if (splice->alive && hev_socks5_get_timeout (HEV_SOCKS5 (self)))
                return 0;
        }
        LOG_D ("%p socks5 session udp fwd f send", self);
        return -1;
    }

    splice->alive |= HEV_SOCKS5_UDP_ALIVE_F;

    hev_list_del (&self->frame_list, node);
    hev_free (frame);
    pbuf_free (buf);
    self->frames--;

    return res;
}

static int
hev_socks5_session_udp_fwd_b (HevSocks5SessionUDP *self,
                              HevSocks5UDPSplice *splice)
{
    HevSocks5UDP *udp = HEV_SOCKS5_UDP (self);
    struct sockaddr_in6 ads = { 0 };
    struct sockaddr *saddr;
    err_t err = ERR_OK;
    struct pbuf *buf;
    ip_addr_t addr;
    uint16_t port;
    int res;

    saddr = (struct sockaddr *)&ads;
    switch (self->pcb->remote_ip.type) {
    case IPADDR_TYPE_V4:
        saddr->sa_family = AF_INET;
        break;
    case IPADDR_TYPE_V6:
        saddr->sa_family = AF_INET6;
        break;
    }

    hev_task_mutex_lock (self->mutex);
    buf = pbuf_alloc (PBUF_TRANSPORT, UDP_BUF_SIZE, PBUF_RAM);
    hev_task_mutex_unlock (self->mutex);
    if (!buf) {
        LOG_D ("%p socks5 session udp fwd b buf", self);
        return -1;
    }

    res = hev_socks5_udp_recvfrom (udp, buf->payload, buf->len, saddr);
    if (res <= 0) {
        pbuf_free (buf);
        if (res < -1) {
            splice->alive &= ~HEV_SOCKS5_UDP_ALIVE_B;
            if (splice->alive)
                return 0;
        }
        LOG_D ("%p socks5 session udp fwd b recv", self);
        return -1;
    }

    if (saddr->sa_family == AF_INET) {
        struct sockaddr_in *adp;

        adp = (struct sockaddr_in *)saddr;
        addr.type = IPADDR_TYPE_V4;
        port = ntohs (adp->sin_port);
        memcpy (&addr, &adp->sin_addr, 4);
    } else if (saddr->sa_family == AF_INET6) {
        struct sockaddr_in6 *adp;

        adp = (struct sockaddr_in6 *)saddr;
        addr.type = IPADDR_TYPE_V6;
        port = ntohs (adp->sin6_port);
        memcpy (&addr, &adp->sin6_addr, 16);
    } else {
        LOG_D ("%p socks5 session udp fwd b addr", self);
        pbuf_free (buf);
        return -1;
    }

    buf->len = res;
    buf->tot_len = res;

    hev_task_mutex_lock (self->mutex);
    err = udp_sendfrom (self->pcb, buf, &addr, port);
    hev_task_mutex_unlock (self->mutex);
    pbuf_free (buf);

    if (err != ERR_OK) {
        LOG_D ("%p socks5 session udp fwd b send", self);
        return -1;
    }

    splice->alive |= HEV_SOCKS5_UDP_ALIVE_B;

    return 0;
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5SessionUDP *self = arg;
    HevSocks5UDPFrame *frame;

    if (!p) {
        hev_socks5_session_terminate (HEV_SOCKS5_SESSION (self));
        return;
    }

    if (self->frames > UDP_POOL_SIZE) {
        pbuf_free (p);
        return;
    }

    frame = hev_malloc (sizeof (HevSocks5UDPFrame));
    if (!frame) {
        pbuf_free (p);
        return;
    }

    frame->data = p;
    memset (&frame->node, 0, sizeof (frame->node));

    addr = &pcb->local_ip;
    port = pcb->local_port;

    if (addr->type == IPADDR_TYPE_V4) {
        struct sockaddr_in *adp;

        adp = (struct sockaddr_in *)&frame->addr;
        adp->sin_family = AF_INET;
        adp->sin_port = htons (port);
        memcpy (&adp->sin_addr, addr, 4);
    } else if (addr->type == IPADDR_TYPE_V6) {
        struct sockaddr_in6 *adp;

        adp = (struct sockaddr_in6 *)&frame->addr;
        adp->sin6_family = AF_INET6;
        adp->sin6_port = htons (port);
        memcpy (&adp->sin6_addr, addr, 16);
    }

    self->frames++;
    hev_list_add_tail (&self->frame_list, &frame->node);
    hev_task_wakeup (self->data.task);
}

HevSocks5SessionUDP *
hev_socks5_session_udp_new (struct udp_pcb *pcb, HevTaskMutex *mutex)
{
    HevSocks5SessionUDP *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5SessionUDP));
    if (!self)
        return NULL;

    res = hev_socks5_session_udp_construct (self, pcb, mutex);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    LOG_D ("%p socks5 session udp new", self);

    return self;
}

static void
splice_task_entry (void *data)
{
    HevSocks5UDPSplice *splice = data;
    HevSocks5UDP *self = splice->udp;
    HevTask *task = hev_task_self ();
    int fd;

    fd = hev_task_io_dup (hev_socks5_udp_get_fd (self));
    if (fd < 0)
        goto exit;

    if (hev_task_add_fd (task, fd, POLLIN) < 0)
        hev_task_mod_fd (task, fd, POLLIN);

    for (;;) {
        if (hev_socks5_session_udp_fwd_b (self, splice) < 0)
            break;
    }

    hev_task_del_fd (task, fd);
    close (fd);

exit:
    hev_task_wakeup (splice->task);
}

static int
hev_socks5_session_udp_bind (HevSocks5 *self, int fd,
                             const struct sockaddr *dest)
{
    HevConfigServer *srv;
    unsigned int mark;

    LOG_D ("%p socks5 session udp bind", self);

    srv = hev_config_get_socks5_server ();
    mark = srv->mark;

    if (mark) {
        int res = 0;

#if defined(__linux__)
        res = setsockopt (fd, SOL_SOCKET, SO_MARK, &mark, sizeof (mark));
#elif defined(__FreeBSD__)
        res = setsockopt (fd, SOL_SOCKET, SO_USER_COOKIE, &mark, sizeof (mark));
#endif
        if (res < 0)
            return -1;
    }

    return 0;
}

static void
hev_socks5_session_udp_splice (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);
    HevTask *task = hev_task_self ();
    HevSocks5UDPSplice splice;
    int stack_size;
    int fd;

    LOG_D ("%p socks5 session udp splice", self);

    splice.task = task;
    splice.udp = self;
    splice.alive = HEV_SOCKS5_UDP_ALIVE_F | HEV_SOCKS5_UDP_ALIVE_B;

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    hev_task_run (task, splice_task_entry, &splice);
    task = hev_task_ref (task);

    fd = hev_socks5_udp_get_fd (HEV_SOCKS5_UDP (self));
    if (hev_task_mod_fd (splice.task, fd, POLLOUT) < 0)
        hev_task_add_fd (splice.task, fd, POLLOUT);

    for (;;) {
        if (hev_socks5_session_udp_fwd_f (self, &splice) < 0)
            break;
    }

    for (;;) {
        if (hev_task_get_state (task) == HEV_TASK_STOPPED)
            break;

        hev_task_wakeup (task);
        hev_task_yield (HEV_TASK_WAITIO);
    }

    hev_task_unref (task);
}

static HevTask *
hev_socks5_session_udp_get_task (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    return self->data.task;
}

static void
hev_socks5_session_udp_set_task (HevSocks5Session *base, HevTask *task)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    self->data.task = task;
}

static HevListNode *
hev_socks5_session_udp_get_node (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    return &self->data.node;
}

int
hev_socks5_session_udp_construct (HevSocks5SessionUDP *self,
                                  struct udp_pcb *pcb, HevTaskMutex *mutex)
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

    LOG_D ("%p socks5 session udp construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_UDP_TYPE;

    udp_recv (pcb, udp_recv_handler, self);

    self->pcb = pcb;
    self->mutex = mutex;
    self->data.self = self;

    return 0;
}

void
hev_socks5_session_udp_destruct (HevObject *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);
    HevListNode *node;

    LOG_D ("%p socks5 session udp destruct", self);

    node = hev_list_first (&self->frame_list);
    while (node) {
        HevSocks5UDPFrame *frame;

        frame = container_of (node, HevSocks5UDPFrame, node);
        node = hev_list_node_next (node);
        pbuf_free (frame->data);
        hev_free (frame);
    }

    hev_task_mutex_lock (self->mutex);
    if (self->pcb) {
        udp_recv (self->pcb, NULL, NULL);
        udp_remove (self->pcb);
    }
    hev_task_mutex_unlock (self->mutex);

    HEV_SOCKS5_CLIENT_UDP_TYPE->finalizer (base);
}

static void *
hev_socks5_session_udp_iface (HevObject *base, void *type)
{
    if (type == HEV_SOCKS5_SESSION_TYPE) {
        HevSocks5SessionUDPClass *klass = HEV_OBJECT_GET_CLASS (base);
        return &klass->session;
    }

    return HEV_SOCKS5_CLIENT_UDP_TYPE->iface (base, type);
}

HevObjectClass *
hev_socks5_session_udp_class (void)
{
    static HevSocks5SessionUDPClass klass;
    HevSocks5SessionUDPClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5Class *skptr;
        HevSocks5SessionIface *siptr;
        void *ptr;

        ptr = HEV_SOCKS5_CLIENT_UDP_TYPE;
        memcpy (kptr, ptr, sizeof (HevSocks5ClientUDPClass));

        okptr->name = "HevSocks5SessionUDP";
        okptr->finalizer = hev_socks5_session_udp_destruct;
        okptr->iface = hev_socks5_session_udp_iface;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_udp_bind;

        siptr = &kptr->session;
        siptr->splicer = hev_socks5_session_udp_splice;
        siptr->get_task = hev_socks5_session_udp_get_task;
        siptr->set_task = hev_socks5_session_udp_set_task;
        siptr->get_node = hev_socks5_session_udp_get_node;
    }

    return okptr;
}
