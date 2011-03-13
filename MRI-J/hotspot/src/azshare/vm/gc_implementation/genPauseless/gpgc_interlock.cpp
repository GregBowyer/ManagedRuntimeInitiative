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


#include "gpgc_interlock.hpp"
#include "gpgc_thread.hpp"
#include "mutexLocker.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

elapsedTimer GPGC_Interlock::_timers[MaxTypes];
Thread*      GPGC_Interlock::_interlocks[] = {
NULL,//TLB_Resync                    = 0
NULL,//OldGC_RelocateOld             = 1
NULL,//OldGC_RelocatePerm            = 2
NULL,//NewToOld_RootsFlushed         = 3
NULL,//NewGC_Relocate                = 4
NULL,//VerifyingMarking              = 5
NULL,//OldGC_MarkClearing            = 6
NULL,//NewGC_MarkClearing            = 7
NULL,//NewGC_NMTFlipping             = 8
NULL,//GPGC_Safepoint                = 9
NULL,//SidebandAlloc                 = 10
NULL,//NewGC_CardMarkScan            = 11
NULL,//NewGC_SystemDictionaryMarking = 12
NULL,//OopTableUnlink                = 13
NULL,//NewGC_Verifying               = 14
NULL,//SymbolStringTable_verify      = 15
NULL//BatchedMemoryOps              = 16
                                             };
const char*  GPGC_Interlock::_interlock_name[] = {
"TLB_Resync",
"OldGC_RelocateOld",
"OldGC_RelocatePerm",
"NewToOld_RootsFlushed",
"NewGC_Relocate",
"VerifyingMarking",
"OldGC_MarkClearing",
"NewGC_MarkClearing",
"NewGC_NMTFlipping",
"GPGC_Safepoint",
"SidebandAlloc",
"NewGC_CardMarkScan",
"NewGC_SystemDictionaryMarking",
"OopTableUnlink",
"NewGC_Verifying",
"SymbolStringTable_verify",
"BatchedMemoryOps",
                                             };



void GPGC_Interlock::acquire_interlock(InterlockType type)
{
assert0(Thread::current()->is_GenPauselessGC_thread());
  assert(type>=0 && type<GPGC_Interlock::MaxTypes, "Undefined GPGC_Interlock type");

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

thread->set_blocked(true);

MutexLocker ml(GPGC_Interlock_lock);

  while ( _interlocks[type] != NULL ) {
    assert(_interlocks[type] != Thread::current(), "Interlock deadlocked on self");

    GPGC_Interlock_lock.wait();

    if ( GPGC_Thread::should_terminate() ) {
      MutexUnlocker mul(GPGC_Interlock_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
    }
  }

  if ( GPGC_Thread::should_terminate() ) {
    // Unlock to avoid rank-ordering assert, yes I know asserts don't matter
    // here since VM is dead but don't want to weaken asserts elsewhere.
    MutexUnlocker mul(GPGC_Interlock_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
  }

thread->set_blocked(false);

  _interlocks[type] = Thread::current();

  _timers[type].reset();
  _timers[type].start();
}


void GPGC_Interlock::acquire_interlock(TimeDetailTracer* tdt, InterlockType type, const char* gc_tag)
{
  DetailTracer dt(tdt, false, "%s Acquiring %s interlock", gc_tag, _interlock_name[type]);
acquire_interlock(type);
}


void GPGC_Interlock::release_interlock(TimeDetailTracer* tdt, InterlockType type, const char* gc_tag)
{
assert0(Thread::current()->is_GenPauselessGC_thread());
  assert(type>=0 && type<GPGC_Interlock::MaxTypes, "Undefined GPGC_Interlock type");

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

thread->set_blocked(true);

  double secs = 0;

  {
MutexLocker ml(GPGC_Interlock_lock);

    if ( GPGC_Thread::should_terminate() ) {
      MutexUnlocker mul(GPGC_Interlock_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
    }

thread->set_blocked(false);

    assert(_interlocks[type] == Thread::current(), "Can't release interlock not owned by you!");
    _interlocks[type] = NULL;

    _timers[type].stop();
    secs = _timers[type].seconds();

GPGC_Interlock_lock.notify_all();
  }

  GCLogMessage::log_a(tdt->details(), tdt->stream(), true, "%s Released %s interlock: held for %3.7f secs",
                    gc_tag, _interlock_name[type], secs);
}


bool GPGC_Interlock::interlock_held_by_self(InterlockType type)
{
  return (_interlocks[type] == Thread::current());
}


bool GPGC_Interlock::interlock_held_by_OldGC(InterlockType type)
{
  return (_interlocks[type] == GPGC_Thread::old_gc_thread());
}


GPGC_Interlock::GPGC_Interlock(TimeDetailTracer* tdt, InterlockType type, const char* gc_tag)
  : _type(type), _tdt(tdt), _gc_tag(gc_tag)
{
  acquire_interlock(_tdt, _type, _gc_tag);
}


GPGC_Interlock::~GPGC_Interlock()
{
  release_interlock(_tdt, _type, _gc_tag);
}

