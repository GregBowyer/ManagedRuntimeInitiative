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

//
#include "auditTrail.hpp"
#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_threadCleaner.hpp"
#include "handles.hpp"
#include "lvb.hpp"
#include "resourceArea.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "handles.inline.hpp"
#include "lvb_pd.inline.hpp"
#include "os_os.inline.hpp"

void GPGC_ThreadCleaner::enable_thread_self_cleaning(TimeDetailTracer* tdt, JavaThread::SuspendFlags flag)
{
  DetailTracer dt(tdt, false, "%s: Enable self cleaning", (flag==JavaThread::gpgc_clean_new?"N":"O"));

  assert0(flag==JavaThread::gpgc_clean_new || flag==JavaThread::gpgc_clean_old);
  assert0(GPGC_Safepoint::is_at_safepoint());

  for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
    // TODO: in the second relocation safepoint of the OldGC, we expect to find threads which still have
    // their clean flag set.  This assert should only happen for other thread cleaning starts.
    //
    //assert0( ! (jt->please_self_suspend() & flag) );
    jt->set_suspend_request_no_polling( flag );
jt->reset_unshattered_page_trap_count();
  }
}


class GPGC_LVB_CleanStackRootsClosure:public OopClosure{
  public:
    // void do_codeblob(CodeBlob *cb) { cb->oops_do(this); }
    void do_oop(objectRef* p) {
      assert0(!Thread::current()->is_gc_mode());
#ifdef ASSERT
      if ( RefPoisoning ) {
        LVB::permissive_poison_lvb(p);
      } else
#endif // ASSERT
lvb_ref(p);
    }
    void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
GCModeSetter gcmode(Thread::current());

      objectRef old_base = *base_ptr;
objectRef new_base;

assert(old_base.not_null(),"Found NULL base ref for derived pointer");
      assert((old_base.raw_value()&0x7)==0, "invalid ref (poisoned?) found");

      if ( old_base.is_stack() ) {
        return;
      }

      if ( GPGC_ReadTrapArray::is_remap_trapped(old_base) ) {
        new_base = GPGC_Collector::mutator_relocate_object(old_base);
      }
      else {
        return;
      }

      GPGC_Collector::fixup_derived_oop(derived_ptr, old_base, new_base);
    }
};


// This method is called by JavaThreads who are attempting to self clean their stacks.
void GPGC_ThreadCleaner::LVB_thread_stack(JavaThread* dirty_thread)
{
  ResourceMark rm;
  HandleMark   hm;

DEBUG_ONLY(No_Safepoint_Verifier nsv;)

Thread*cleaning_thread=Thread::current();

  assert0(cleaning_thread->is_Java_thread() || cleaning_thread->is_VM_thread());

  assert0(!cleaning_thread->is_gc_mode());
  assert0(!cleaning_thread->is_stack_cleaning());

  GPGC_LVB_CleanStackRootsClosure csrc;

  // Needed to prevent infinite recursion and stack overflow:
  cleaning_thread->set_stack_cleaning(true);

  AuditTrail::log_time(dirty_thread, AuditTrail::GPGC_START_LVB_CLEAN);

  // TODO: maw: Could we have a NewGen-only version of this function that would never
  // move a methodOop, and thus be allowed to skip the gc_prologue/gc_epilogue wrapper
  // around the thread->oops_do()?

  // The GPGC_gc_prologue/gc_epilogue calls are required, because this call might come
  // when methodOops are being moved.  So we better fix up the BCPs on the stack. 
  dirty_thread->GPGC_mutator_gc_prologue(&csrc);
  dirty_thread->oops_do(&csrc);
dirty_thread->gc_epilogue();

  AuditTrail::log_time(dirty_thread, AuditTrail::GPGC_END_LVB_CLEAN);

  cleaning_thread->set_stack_cleaning(false);

  // Since we may have modified the thread stack, we *MUST* flush the thread stack,
  // as the JavaThread might have cached some of the stack, and not see the new data.
  if ( cleaning_thread == dirty_thread ) {
os::make_self_walkable();
    AuditTrail::log_time(dirty_thread, AuditTrail::MAKE_SELF_WALKABLE, 7);
  } else {
    os::make_remote_walkable(dirty_thread);
    AuditTrail::log_time(dirty_thread, AuditTrail::MAKE_REMOTE_WALKABLE, intptr_t(cleaning_thread), 2);
  }
}


void GPGC_ThreadCleaner::self_clean_vm_thread()
{
assert0(Thread::current()->is_VM_thread());

  GPGC_LVB_CleanStackRootsClosure csrc;
  VMThread::vm_thread()->oops_do(&csrc);
}


class GPGC_Base_CleanDirtyThreadsClosure: public JavaThreadClosure {
  private:
    intptr_t _dirty_threads;
    long     _stack_flag;
  public:
    GPGC_Base_CleanDirtyThreadsClosure(long stack_flag) : _dirty_threads(0), _stack_flag(stack_flag) {}

    void increment_dirty_threads() { Atomic::inc_ptr(&_dirty_threads); }
    long dirty_threads()           { return _dirty_threads; }

    // Implementors of this better call os::make_remote_walkable(jt) after cleaning the stack.
    virtual void clean_by_gc_thread(JavaThread* jt) = 0;

    // Generic thread cleaning of JavaThreads by themselves.  Cleaning of JavaThreads by
    // GC threads is done by the subclass's clean_by_gc_thread() method.
    void do_java_thread(JavaThread* jt) {
      if ( jt->please_self_suspend() & _stack_flag ) {
        increment_dirty_threads();
        Thread* current_thread = Thread::current();
        if (current_thread == jt) {
          // This case is for a JavaThread cleaning it's own stack.
          GPGC_ThreadCleaner::LVB_thread_stack(jt);
          // LVB_thread_stack() does os::make_self_walkable(), so I don't have to do it here.
          // We reset both the new_gen and old_gen stack cleaning flags when cleaning with LVB.
          jt->clr_suspend_request( JavaThread::SuspendFlags(JavaThread::gpgc_clean_old | JavaThread::gpgc_clean_new) );
        } else {
          // If the thread isn't cleaning itself, then the cleaning thread needs to be a GC thread.
          assert0((!current_thread->is_Java_thread()) && (!current_thread->is_VM_thread()));
          clean_by_gc_thread(jt);
        }
      }
    }
};


// This thread stack cleaning closure only cleans refs that NewGC needs to know about.
// If there are OldGen dirty refs left uncleaned, the JavaThread's stack cleaning flag will
// be left set, so that either the JavaThread or the OldGC will clean the stack again later.
class GPGC_NewGC_CleanDirtyThreadsClosure: public GPGC_Base_CleanDirtyThreadsClosure {
  private:
    GPGC_GCManagerNewStrong* _gcm;
  public:
    GPGC_NewGC_CleanDirtyThreadsClosure(GPGC_GCManagerNewStrong* gcm)
      : GPGC_Base_CleanDirtyThreadsClosure(JavaThread::gpgc_clean_new), _gcm(gcm) {}
    void clean_by_gc_thread(JavaThread* jt)
    {
      GPGC_NewGC_MarkPushClosure* closure;
      if ( GPGC_NewCollector::mark_old_space_roots() ) {
      	closure = _gcm->nto_root_mark_push_closure();
      } else {
      	closure = _gcm->new_root_mark_push_closure();
      }

      assert0(!closure->check_derived_oops());
      closure->activate_derived_oops();

      AuditTrail::log_time(jt, AuditTrail::GPGC_START_NEW_GC_CLEAN);

      jt->GPGC_gc_prologue(closure);
      jt->oops_do(closure);
jt->gc_epilogue();

      AuditTrail::log_time(jt, AuditTrail::GPGC_END_NEW_GC_CLEAN);

      closure->deactivate_derived_oops();

      // Since we may have modified the thread stack, we *MUST* flush the thread stack,
      // as the JavaThread might have cached some of the stack, and not see the new data.
      os::make_remote_walkable(jt);
      AuditTrail::log_time(jt, AuditTrail::MAKE_REMOTE_WALKABLE, intptr_t(Thread::current()), 6);

      if ( GPGC_NewCollector::mark_old_space_roots() ) {
        jt->clr_suspend_request(JavaThread::gpgc_clean_old);
      }
      jt->clr_suspend_request(JavaThread::gpgc_clean_new);
    }
};


void GPGC_ThreadCleaner::checkpoint_and_clean_threads(JavaThreadClosure* jtc, SafepointTimes* times)
{
  GPGC_Safepoint::do_checkpoint(jtc, times);
}


void GPGC_ThreadCleaner::new_gen_checkpoint_and_clean_threads(TimeDetailTracer* tdt, GPGC_GCManagerNewStrong* gcm, const char* tag)
{
SafepointTimes times;

  {
    DetailTracer dt(tdt, false, "%s Clean all threads: ", tag);

    AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_START_NEW_CKP_CLEAN);


    GPGC_NewGC_CleanDirtyThreadsClosure cdtc(gcm);
    checkpoint_and_clean_threads(&cdtc, &times);

    AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_END_NEW_CKP_CLEAN);

    dt.print("%d found dirty", cdtc.dirty_threads());
  }

  GPGC_Collector::log_checkpoint_times(tdt, &times, tag, "clean_threads");
}
