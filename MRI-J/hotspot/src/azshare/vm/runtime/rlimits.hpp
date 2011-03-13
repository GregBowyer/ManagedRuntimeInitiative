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
#ifndef RLIMITS_HPP
#define RLIMITS_HPP

/*
 * rlimits.hpp - handles interaction with the proxy's resource limits.
 */

#include <os/lock.h>
#include <proxy/proxy_error.h>
#include <proxy/proxy_be.hpp>

/*
 * Implements ResourceStatsHandler to handle applications that leak FDs.
 */
class AvmResourceStatsHandler : public ResourceStatsHandler {

    private:
        uint32_t _score_2; // score from 2 forced cycles ago
        uint32_t _score_1; // score before last forced cycle.

        // fds closed by finalizer threads since last forced cycle
        uint64_t _closed_from_finalizer;

        // threshold that triggers a full GC; used when we still don't
        // have enough info to use the heuristic.
        uint64_t threshold;
        uint64_t emergency_threshold;

        OS_SPIN_LOCK_T forcegc_lock;

        uint32_t get_score(int nofds);

        void reset(uint32_t _score);

static AvmResourceStatsHandler*_handler;

    public:

        AvmResourceStatsHandler();

        virtual ~AvmResourceStatsHandler();

        /** Sets the limit for the number of open fds. */
        virtual void set_rlimit_nofile(uint64_t limit);

        /** Called when an fd is opened. */
        virtual void fd_opened(int nofds);

        /** Called when an fd is closed. */
        virtual void fd_closed();

        /** Initializes the handler. */
        static void initialize();
};

#endif // RLIMITS_HPP
