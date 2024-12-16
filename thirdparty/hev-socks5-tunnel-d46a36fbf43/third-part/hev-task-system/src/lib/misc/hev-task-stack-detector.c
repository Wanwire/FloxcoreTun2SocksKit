/*
 ============================================================================
 Name        : hev-task-stack-detector.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2024 hev.
 Description : Task Stack Overflow Detector
 ============================================================================
 */

#include <stdio.h>
#include <signal.h>

#if defined(__APPLE__)
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include "kern/core/hev-task-system-private.h"
#include "kern/task/hev-task-private.h"
#include "kern/task/hev-task-stack.h"
#include "lib/misc/hev-compiler.h"

#include "hev-task-stack-detector.h"

static struct sigaction bus_sa;
static struct sigaction segv_sa;
static char stack[SIGSTKSZ];
static stack_t oss;

static int
setaltstack (const stack_t *ss, stack_t *oss)
{
#if defined(TARGET_OS_TV) && TARGET_OS_TV
    return 0;
#else
    return sigaltstack (ss, oss);
#endif
}

static void
signal_handler (int signo, siginfo_t *si, void *unused)
{
    HevTaskSystemContext *context = hev_task_system_get_context ();
    HevListNode *node = hev_list_first (&context->all_tasks);

    for (; node; node = hev_list_node_next (node)) {
        HevTask *task = container_of (node, HevTask, list_node);
        const void *stack_base, *stack_bottom;

        stack_base = hev_task_stack_get_base (task->stack);
        stack_bottom = hev_task_stack_get_bottom (task->stack);

        if (stack_base <= si->si_addr && si->si_addr <= stack_bottom) {
            fprintf (stderr, "========== Oops! Stack overflow! ==========\n");
            fprintf (stderr, "Task: %p\n", task);
            fprintf (stderr, "  Stack   : %p - %p\n", stack_base, stack_bottom);
            fprintf (stderr, "  Bad addr: %p\n", si->si_addr);
            fprintf (stderr, "===========================================\n");
        }
    }

    switch (signo) {
    case SIGBUS:
        bus_sa.sa_sigaction (signo, si, unused);
        break;
    case SIGSEGV:
        segv_sa.sa_sigaction (signo, si, unused);
        break;
    default:
        return;
    }
}

int
hev_task_stack_detector_init (void)
{
    struct sigaction sa;
    int res = 0;
    stack_t ss;

    ss.ss_size = SIGSTKSZ;
    ss.ss_sp = stack;
    ss.ss_flags = 0;

    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = signal_handler;
    sigemptyset (&sa.sa_mask);

    res |= setaltstack (&ss, &oss);
    res |= sigaction (SIGBUS, &sa, &bus_sa);
    res |= sigaction (SIGSEGV, &sa, &segv_sa);

    return res;
}

void
hev_task_stack_detector_fini (void)
{
    sigaction (SIGBUS, &bus_sa, NULL);
    sigaction (SIGSEGV, &segv_sa, NULL);
    setaltstack (&oss, NULL);
}
