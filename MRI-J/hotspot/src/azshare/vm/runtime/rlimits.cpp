// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

/*
 * rlimits.cpp - handles interaction with the proxy's resource limits.
 */

#ifdef AZ_PROXIED

#include "atomic.hpp"
#include "globals.hpp"
#include "rlimits.hpp"
#include "ostream.hpp"
#include "thread.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"

#include <os/thread.h>
#include <proxy/proxy.h>

AvmResourceStatsHandler* AvmResourceStatsHandler::_handler = NULL;

void AvmResourceStatsHandler::initialize()
{
    AvmResourceStatsHandler::_handler = new AvmResourceStatsHandler();
    proxy_register_resource_stats_handler(AvmResourceStatsHandler::_handler);
}

AvmResourceStatsHandler::AvmResourceStatsHandler() : ResourceStatsHandler()
{
    OS_SPIN_LOCK_INIT(&forcegc_lock);
    _score_1 = 0;
    _score_2 = 0;
    reset(0);
}

AvmResourceStatsHandler::~AvmResourceStatsHandler()
{

}

void AvmResourceStatsHandler::reset(uint32_t _score)
{
    Atomic::store_ptr(0, (intptr_t*)&_closed_from_finalizer);
    _score_2 = _score_1;
    _score_1 = _score;
}

/** Sets the limit for the number of open fds. */
void AvmResourceStatsHandler::set_rlimit_nofile(uint64_t limit)
{
    threshold =  (limit * GConFDLimitThreshold / 100);
    emergency_threshold = (limit * GConFDLimitEmergency / 100);
    ResourceStatsHandler::set_rlimit_nofile(limit);
}

/**
 * The score is a number between 0 and 100, calculated in the following
 * way:
 *
 * ((_score_2 * 10) + (_score_1 * 25) + (65 * _score)) / 100
 *
 * and _score is calculated based on the information gathered about where the fds
 * were closed from.
 */
uint32_t AvmResourceStatsHandler::get_score(int nofds)
{
uint32_t score=0;
    uint32_t credit = (uint32_t) (_rlimit_nofile - nofds);
    if (credit) {
        score  = (_score_2 * 10) + (_score_1 * 25);
        score += 65 * (100 * _closed_from_finalizer / credit);
        score /= 100;
    } else {
        // we'll probably crash if this is reached. but at least the crash
        // won't be because of a division by zero.
        score = 100;
    }
    return score;
}

/** Called when an fd is opened. */
void AvmResourceStatsHandler::fd_opened(int nofds)
{
assert(_rlimit_nofile,"file description limit should be > 0");
    // don't do anything for non-java threads.
    if (!Thread::current()->is_Complete_Java_thread() || nofds < 0) {
        return;
    }

    if (nofds > (int64_t)emergency_threshold && _closed_from_finalizer > 0) {
        // if we're getting too close to the limit for comfort, and we detected
        // that running finalizers has done at least some good, just force a gc.
        // might make us survive a little bit more.
        if (GConFDLimitVerbose) {
tty->print_cr("GConFDLimit: emergency GC cycle, nofds = %d",nofds);
        }
        JVM_SystemResourceLimit_GC();
        reset(100);
    } else if (nofds > (int64_t)threshold) {
        if (_score_1 == 0  || _score_2 == 0) {
            // we're still in the learning phase. so we figure out whether to
            // force a GC based on a dumb heursitic that will let us collect
            // some data for future use.
            uint32_t _score = (uint32_t) (nofds * 100 / _rlimit_nofile);
            if (_score_1 == 0 || ((abs(_score_1 - _score) > 10) && _closed_from_finalizer > 0)) {
                if (GConFDLimitVerbose) {
tty->print_cr("GConFDLimit: forcing GC cycle for training, score = %d",_score);
                }
                JVM_SystemResourceLimit_GC();
reset(_score);
            }
        } else {
            // we have data from at least two previous forced cycles, so we're
            // good to calculate a more informed score to decide on whether to
            // force a GC.
            uint32_t _score = get_score(nofds);
            if (_score >= GConFDLimitScore) {
                bool gc = false;
                OS_SPIN_LOCK(&forcegc_lock);
                _score = get_score(nofds);
                if (_score >= GConFDLimitScore) {
                    gc = true;
reset(_score);
                }
                OS_SPIN_UNLOCK(&forcegc_lock);
                if (gc) {
                    if (GConFDLimitVerbose) {
tty->print_cr("GConFDLimit: forcing GC cycle, score is %d",_score);
                    }
                    JVM_SystemResourceLimit_GC();
                }
            }
        }
    }
}

/** Called when an fd is opened. */
void AvmResourceStatsHandler::fd_closed()
{
Thread*current=Thread::current();
if(current->is_Complete_Java_thread()){
JavaThread*jthread=(JavaThread*)current;
if(jthread->is_finalizer_thread()){
            Atomic::inc_ptr((intptr_t*)&_closed_from_finalizer);
        }
    }
}

void proxy_rlimits_initialize()
{
    if (GConFDLimit) {
        if (GConFDLimitVerbose) {
tty->print_cr("GConFDLimit: registering proxy callback");
        }
AvmResourceStatsHandler::initialize();
    }
}

#else  // !AZ_PROXIED:

void proxy_rlimits_initialize()
{
  // No proxy initialization if AZ_PROXIED not set.
}

#endif // !AZ_PROXIED
