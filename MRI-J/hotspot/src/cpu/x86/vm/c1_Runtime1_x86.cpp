/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.



#include "collectedHeap.hpp"
#include "c1_MacroAssembler.hpp"
#include "c1_Runtime1.hpp"
#include "register_pd.inline.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

// Implementation of StubAssembler

// --- call_RT
int C1_MacroAssembler::call_RT(address entry, Register arg1, Register arg2, Register arg3) {
  // These fixed register reservations are maintained in c1_LinearScan_pd.hpp
  int save_mask = -1            // all registers by default
    - (1<<RAX)                  // RAX is generally the return result.
    ;
  return call_RT(entry,arg1,arg2,arg3,save_mask);
}

// --- call_RT
int C1_MacroAssembler::call_RT(address entry, Register arg1, Register arg2, Register arg3, int save_mask) {
  int rel_pc = call_VM_compiled(entry,save_mask,arg1,arg2,arg3,noreg);

  // check for pending exceptions
  getthr(RCX);                  // Crush RCX, but can restore it shortly...
  cmp8i(RCX, in_bytes(JavaThread::pending_exception_offset()),0);
  ld8  (RCX, RSP, frame::xRCX - (frame::runtime_stub_frame_size-8) ); // Restore RCX without blowing flags and accounting for return address being in the frame
  // forward_exception_entry *will* pop a frame.  Fortunately we always get
  // here via a CodeStub 'call' so there's a return address already on the
  // stack.  Hence the frame popped is the CodeStub frame - putting us
  // back in the C1 frame where we came from.
  jne  (Runtime1::entry_for(Runtime1::forward_exception_id));
  return rel_pc;
}


// Implementation of Runtime1

#define __ sasm->

void Runtime1::initialize_pd() {
  // nothing to do
}


// target: the entry point of the method that creates and posts the exception oop
// has_argument: true if the exception needs an argument (passed on stack because registers must be preserved)

void Runtime1::generate_exception_throw(C1_MacroAssembler* sasm, address target, bool has_argument) {
  // call target passing argument from the stack, saving and restoring all registers
  __ call_VM_compiled(target, -1, noreg, noreg, noreg, noreg, has_argument ? 1 : 0);
  // forward pending exception
  __ jmp (Runtime1::entry_for(Runtime1::forward_exception_id));
__ patch_branches();
}


void Runtime1::generate_patching(C1_MacroAssembler* sasm, address target) {
  int save_mask = -1;           // all registers by default
  __ call_VM_compiled(target,save_mask,noreg,noreg);
  // All regs are still alive with debug info

  // check for pending exceptions
__ push(RAX);
  __ getthr(RAX);
  __ cmp8i(RAX, in_bytes(JavaThread::pending_exception_offset()),0);
__ pop(RAX);
  __ jne  (StubRoutines::forward_exception_entry());
__ ret();

__ patch_branches();
}

void Runtime1::generate_code_for(StubID id, C1_MacroAssembler* sasm) {
  // stub code & info for the different stubs
  switch (id) {
  case frequency_counter_overflow_wrapper_id: {
Label have_c2_code;
    // On entry we have TWO return addresses on the stack: the original one
    // which returns to some normal call-site, and the one which returns back
    // to the C1 entry code.  Normal call_RT's require only 1 return address
    // to be able to crawl the stack.  Save the other one in case no C2 code
    // is handy; save in some register which is a temp for the normal calling
    // convention and hence is not holding any arguments or callee-save values.
__ pop(R10);
    __ call_RT((address)Runtime1::frequency_counter_overflow,R10,noreg,noreg);
    __ jmp8 (RAX);              // Jump to C2 now 
__ patch_branches();
    // Rare point in the VM where the Caller, not the Callee, must GC the args.
    CodeCache::_caller_must_gc_args_1 = __ last_oopmap_adr();
    break;
  }

  case partial_subtype_check_id:  {
    // C1 on Azul calls through to StubRoutine::partial_runtime_check directly
__ should_not_reach_here("Runtime1::generate_code_for - partial subtype check");
__ patch_branches();
    break;
  }

  case register_finalizer_id:  {
    // passed RDI as the object.  Must return it in RAX. Has call runtime semantics
    // so can blow any caller save registers.
    // load the klass and check the has finalizer flag
Label no_finalizer;
    __ mov8 (RDX, RDI);  // load object we want klass ref for into RDX
    __ verify_not_null_oop(RDX, MacroAssembler::OopVerify_IncomingArgument);
    __ ref2klass(RInOuts::a, RAX /* becomes klass ref */ , RCX /*temp*/, RDX /* becomes KID */);
    __ cvta2va(RAX);
    __ test4i(RAX, Klass::access_flags_offset_in_bytes() + sizeof(oopDesc), noreg, 0, JVM_ACC_HAS_FINALIZER);
    __ jze  (no_finalizer); // Jump if zero (we expect to only come here for objects that can have finalizers)
    __ call_RT((address)SharedRuntime::register_finalizer, RDI, noreg, noreg);
    __ verify_not_null_oop(RAX, MacroAssembler::OopVerify_ReturnValue); // return value already in RAX
__ ret();
__ bind(no_finalizer);
    __ mov8 (RAX, RDI);         // place return value in RAX
__ ret();
__ patch_branches();
    break;
  }
  case throw_range_check_failed_id:
    generate_exception_throw(sasm, CAST_FROM_FN_PTR(address, throw_range_check_exception), true);
    break;
  case throw_index_exception_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_index_exception),true);
    break;
  case throw_div0_exception_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_div0_exception),false);
    break;
  case throw_null_pointer_exception_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_null_pointer_exception),false);
    break;
  case throw_array_store_exception_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_array_store_exception),false);
    break;
  case throw_class_cast_exception_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_class_cast_exception),true);
    break;
  case throw_incompatible_class_change_error_id:
generate_exception_throw(sasm,CAST_FROM_FN_PTR(address,throw_incompatible_class_change_error),false);
    break;
 
  case forward_exception_id:
    __ jmp(StubRoutines::forward_exception_entry());
__ patch_branches();
    break;
  case handle_exception_nofpu_id:
  case handle_exception_id:
    __ jmp(StubRoutines::forward_exception_entry());
__ patch_branches();
    break;
  case unwind_exception_id:
    __ jmp(StubRoutines::forward_exception_entry());
__ patch_branches();
    break;

  case access_field_patching_id:
generate_patching(sasm,CAST_FROM_FN_PTR(address,access_field_patching));
    break;
    
  case load_klass_patching_id:
generate_patching(sasm,CAST_FROM_FN_PTR(address,move_klass_patching));
    break;
    
#if 0
    case jvmti_exception_throw_id:
      { // rax,: exception oop
        StubFrame f(sasm, "jvmti_exception_throw", dont_gc_arguments);
        // Preserve all registers across this potentially blocking call
        const int num_rt_args = 2;  // thread, exception oop
#if 0
        OopMap* map = save_live_registers(sasm, num_rt_args);
#endif
        int call_offset = __ call_RT(noreg, noreg, CAST_FROM_FN_PTR(address, Runtime1::post_jvmti_exception_throw), rax);
#if 0
        oop_maps = new OopMapSet();
        oop_maps->add_gc_map(call_offset, map);
#endif
        restore_live_registers(sasm);
      }
      break;
#endif
  case new_instance_id:
    __ mov8i (RCX,0);           // Zero for instances and fall into the next code
  case new_array_id: {
    // RDX - size in bytes, RSI - kid, RCX - length for arrays or zero for instance
    __ mov8i (RAX, UseSBA);     // sba hint
    int save_mask = -1 -        // all registers saved except these...
      (1<<RAX) -
      (1<<RCX) -
      (1<<RDX) -
      (1<<RSI) -
      0;
    __ call_VM_compiled((address)SharedRuntime::_new, save_mask, RSI/*kid*/, RDX/*size in bytes*/, RCX/*len*/, RAX/*sba hint*/ );
    // don't check for pending exception as SharedRuntime::_new will deopt the caller
    // if an exception is thrown
    __ verify_oop(RAX, MacroAssembler::OopVerify_NewOop);
    // NB it is important that RAX equals the result in case we return to the deopt sled
__ ret();
__ patch_branches();
    break;
  }

  case fpu2long_stub_id:
    // C1 on Azul does all of the [df]2[il] conversion inline
__ should_not_reach_here("Runtime1::generate_code_for - fpu2long");
__ patch_branches();
    break;
  case slow_subtype_check_id:
    // C1 on Azul does all of the type checking inline (or in the shared stub)
__ should_not_reach_here("Runtime1::generate_code_for - slow_subtype_check");
__ patch_branches();
    break;

  case new_multi_array_id:
    // C1 on Azul calls through to Runtime1::new_multi_array directly
__ should_not_reach_here("Runtime1::generate_code_for - new_multi_array");
__ patch_branches();
    break;

  case monitorenter_nofpu_id:
  case monitorenter_id:
    // C1 on Azul uses shared stub for locking
__ should_not_reach_here("Runtime1::generate_code_for - monitorenter");
__ patch_branches();
    break;

  case monitorexit_nofpu_id:
  case monitorexit_id:
    // C1 on Azul uses shared stub for unlocking
__ should_not_reach_here("Runtime1::generate_code_for - monitorexit");
__ patch_branches();
    break;

 default:  
   __ unimplemented(name_for(id));
__ patch_branches();
   break;
  }
}

#undef __
