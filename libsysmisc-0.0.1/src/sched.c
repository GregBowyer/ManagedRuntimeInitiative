// Copyright 2005 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// sched.c - Scheduler subsystem for Aztek OS Services
//

#include <stdlib.h>

#include <os/sched.h>
#include <os/memory.h>

#include <sched.h>

int az_sched_setscheduler(pid_t pid, int priority)
{
    struct sched_param sparam;

    sparam.__sched_priority = priority;
    return sched_setscheduler(pid, AZ_SCHED_AZUL_POLICY, &sparam);
}
