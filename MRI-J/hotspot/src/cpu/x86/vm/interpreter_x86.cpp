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


#include "bytecodeHistogram.hpp"
#include "c1_globals.hpp"
#include "c2_globals.hpp"
#include "interp_masm_pd.hpp"
#include "interpreter.hpp"
#include "interpreter_pd.hpp"
#include "interpreterRuntime.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "methodOop.hpp"
#include "tickProfiler.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "markSweep.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// Generation of Interpreter
//
// The InterpreterGenerator generates the interpreter into Interpreter::_code.


#define __ _masm->


address TemplateInterpreterGenerator::generate_exception_handler_common(const char* name, const char* message, bool pass_oop) {
  ScopedAsm entry(_masm);
  __ empty_java_exec_stack(RInOuts::a,RAX);
  __ mov8i(R11, name);          // name in arg1, arg0 will hold thread
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RAX, RCX_TOS, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11);
  if (pass_oop) {               // Passing in oop for detail message
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::create_klass_exception), RAX,
	       RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R11, RCX_TOS, noreg);
  } else {
    __ mov8i(RCX, message);     // Passing in char* for detail message
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::create_exception      ), RAX,
	       RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R11, RCX_TOS, noreg);
  }
  // now throw the exception
  __ verify_oop(RAX, MacroAssembler::OopVerify_OutgoingArgument);
  __ mov8(RCX_TOS,RAX);
  __ jmp (Interpreter::throw_exception_entry());
  return entry.abs_pc();
}


address TemplateInterpreterGenerator::generate_ArrayIndexOutOfBounds_handler(const char* name) {
  ScopedAsm entry(_masm);
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11);
  __ empty_java_exec_stack(RInOuts::a,Rtmp0);
  __ mov8i(R11, name);
  __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_ArrayIndexOutOfBoundsException), noreg,
             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R11, RCX_TOS, noreg);
  return entry.abs_pc();
}

address TemplateInterpreterGenerator::generate_ClassCastException_handler() {
  ScopedAsm entry(_masm);
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ empty_java_exec_stack(RInOuts::a,Rtmp0);
  __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_ClassCastException), noreg,
             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX_TOS, noreg, noreg);
  return entry.abs_pc();
}


address TemplateInterpreterGenerator::generate_StackOverflowError_handler() {
  ScopedAsm entry(_masm);
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ empty_java_exec_stack(RInOuts::a,Rtmp0);
  __ call_VM(CAST_FROM_FN_PTR(address, SharedRuntime::build_StackOverflowError), noreg,
             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX_TOS, noreg, noreg);
  return entry.abs_pc();
}


address TemplateInterpreterGenerator::generate_return_entry_for(TosState state, int step) {
assert(state==tos,"no TosStates supported");
  ScopedAsm entry(_masm);

  // CALLING CONVENTION: 
  // This is post-invoke-call code (as opposed to epilog code) - it happens in
  // the same frame as the invoke* bytecode (and not in the called frame).
  // Return results in RAX or (F00).

  // Need to rebuild the interpreter registers for this frame.
  __ unpack_interpreter_regs(RInOuts::a, RCX,
                             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);

  // Outgoing args from the call already popped by the return bytecode
  __ move8(RCX_TOS,RAX);        // Return result into "TOS", free RCX

  __ get_cache_index_at_bcp(RInOuts::a, RAX, RKeepIns::a, RSI_BCP, 1);
  __ ldz4 (RAX, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), RAX, 0); 
  __ shr4i(RAX, ConstantPoolCacheEntry::tosBits); // load return type
  __ ldz1 (R09,RSI_BCP,step);   // load bytecode from bytecode stream into RCX
  __ add8i(RSI_BCP,step);       // Advance past invoke* bytecode
  __ ld8  (R09, noreg,(intptr_t)Interpreter::normal_table(),R09,3); // load the next bytecode target

  Label done, two_words, zero_words, do_float, do_double, do_object, byte4;
  __ cmp1i(RAX,T_VOID);
  __ jeq  (zero_words);
  __ cmp1i(RAX,T_OBJECT);
  __ jeq  (do_object);
  __ cmp1i(RAX,T_INT);
  __ jeq  (byte4);
  __ cmp1i(RAX,T_BOOLEAN);
  __ jeq  (byte4);
  __ cmp1i(RAX,T_CHAR);
  __ jeq  (byte4);
  __ cmp1i(RAX,T_BYTE);
  __ jeq  (byte4);
  __ cmp1i(RAX,T_SHORT);
  __ jeq  (byte4);
  __ cmp1i(RAX,T_LONG);
  __ jeq  (two_words);
  __ cmp1i(RAX,T_FLOAT);
  __ jeq  (do_float);
  __ cmp1i(RAX,T_DOUBLE);
  __ jeq  (do_double);
  __ cmp1i(RAX,T_ARRAY);
  __ jeq  (do_object);
  __ os_breakpoint(); // shorter version of should-not-reach-here to keep branches short

__ bind(do_double);//value in XMM0
  __ mov8 (RCX_TOS,F00);

__ bind(two_words);//value in TOS is correct, adjust the R31_ASP
  // We need to mark the empty double/long slot with a sentinel value
  __ store_sentinel(RKeepIns::a,RDI_JEX,0);
  __ add8i(RDI_JEX, 8);
  __ jmp8 (R09);

__ bind(do_float);//value in XMM0
  __ mov4 (RCX_TOS,F00);
__ bind(byte4);
  __ movsx84(RCX,RCX);          // Force proper sign-extension
  __ jmp8 (R09);                // Dispatch!

__ bind(zero_words);//
  __ pop_tos();			// load the correct TOS value

  if( VerifyOopLevel <= 0 ) __ bind (do_object);
__ bind(done);//one word is already in the TOS
  __ jmp8 (R09);                // Dispatch!

  if( VerifyOopLevel > 0 ) {
__ bind(do_object);
    __ verify_oop (RCX_TOS, MacroAssembler::OopVerify_ReturnValue);
    __ jmp8 (R09);              // Dispatch!
  }
  return entry.abs_pc();
}

  
address TemplateInterpreterGenerator::generate_safept_entry_for(TosState state, address runtime_entry) {
assert(state==tos,"no TosStates supported");
  ScopedAsm entry(_masm);
__ push_tos();
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ call_VM(runtime_entry, noreg,
             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
             Rtmp0, noreg, noreg, noreg);
__ pop_tos();
__ dispatch_next(0);
  return entry.abs_pc();
}


address TemplateInterpreterGenerator::generate_unpack_and_go(){
  ScopedAsm entry(_masm);
  // If TraceDeoptimization then call the trace logic for every deopt.  Can set a
  // BP in SharedRuntime::trace_bytecode and thread->ps(99) there to confirm
  // the deopt repacked the stack well.
  Label ok;
  __ getthr(RAX);
  __ cmp8i(RAX,in_bytes(Thread::pending_exception_offset()),0);
__ jeq(ok);
  __ call (StubRoutines::forward_exception_entry());
  __ bind (ok);

  if( TraceDeoptimization )
    __ call_VM_plain((address)SharedRuntime::trace_bytecode,RAX,noreg,noreg,noreg);

  // Need to rebuild the interpreter registers for this frame.
  __ unpack_interpreter_regs(RInOuts::a,RCX, RDX_LOC,RSI_BCP,RDI_JEX,R08_CPC);
__ pop_tos();
__ dispatch_next(0);
  return entry.abs_pc();
}

//----------------------------------------------------------------------------------------------------
// Entry points & stack frame layout

address TemplateInterpreterGenerator::generate_method_entry(TemplateInterpreter::MethodKind kind){
  // determine code generation flags
  bool synchronized = false;
  bool zerolocals   = false;
  bool native_call  = false;
  switch (kind) {
    case Interpreter::zerolocals             : zerolocals  = true;                      break;
    case Interpreter::zerolocals_synchronized: zerolocals  = true; synchronized = true; break;
    case Interpreter::native                 : native_call = true;                      break;
    case Interpreter::native_synchronized    : native_call = true; synchronized = true; break;
case Interpreter::empty:break;
case Interpreter::accessor:break;
case Interpreter::abstract:break;
    case Interpreter::java_lang_math_abs     : zerolocals = true;                       break;
    case Interpreter::java_lang_math_cos     : zerolocals = true;                       break;
    case Interpreter::java_lang_math_log     : zerolocals = true;                       break;
    case Interpreter::java_lang_math_log10   : zerolocals = true;                       break;
    case Interpreter::java_lang_math_sin     : zerolocals = true;                       break;
    case Interpreter::java_lang_math_sqrt    : zerolocals = true;                       break;
    case Interpreter::java_lang_math_sqrtf   : zerolocals = true;                       break;
    case Interpreter::java_lang_math_tan     : zerolocals = true;                       break;
    default                                  : ShouldNotReachHere();                    break;
  }                                    

  ScopedAsm entry(_masm);
  Register RCX_MTH = RCX;

  if (kind == Interpreter::abstract) {
__ unimplemented("abstract method");
    //__ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_AbstractMethodError), IA0);
__ should_not_reach_here("interpreter abstract method");
    return entry.abs_pc();
  }

  if (kind == Interpreter::empty && UseFastEmptyMethods) {
    __ ret   ();
    return entry.abs_pc();
  }

  if (kind == Interpreter::accessor && UseFastAccessorMethods 
      // FastAccessorMethods do not bump the compiler counters.  Hence
      // these methods are always 'cold', never compiled.  We force
      // them to be inlined via CHA.  If CHA is off, then we want
      // compilation counters.
      && (UseCHA || (!UseC1 && !UseC2))
      ) {
    // The method is known to consist of a 'aload0; (i|a)getfield X; (i|a)return'.
    // If we're not resolve or volatile or null-receiver then
    // take the normal slow path.
    // Inputs: RDX: locals/args
    //         RCX: methodRef
    Register R11_tmp = R11;
Label go_slow;
    __ bind (go_slow,Interpreter::entry_for_kind(Interpreter::zerolocals));
    __ ld8  (R09, RDX_LOC, 0);  // Get receiver
    __ test8(R09,R09);
    __ jeq  (go_slow);          // Might throw NPE
    // Load the index @2.
    __ move8(R10,RCX_MTH);
    __ cvta2va(R10);
    __ ldref_lvb(RInOuts::a, RSI_BCP, R11_tmp, RKeepIns::a, R10, in_bytes(methodOopDesc::const_offset()), false);
    __ cvta2va(RSI_BCP);        // convert BCP into a real pointer
    __ ldz2 (RAX, RSI_BCP, in_bytes(constMethodOopDesc::codes_offset())+2);
    __ shl8i(RAX, 2 + log_ptr_size); // scale by ConstantPoolCacheEntry size
    // Get constant-pool cache base
    __ ldref_lvb(RInOuts::a, RDI, R11_tmp, RKeepIns::a, R10, in_bytes(methodOopDesc::constants_offset()), false);
    __ cvta2va  (RDI);
    __ ldref_lvb(RInOuts::a, R08_CPC, R11_tmp, RKeepIns::a, RDI, constantPoolOopDesc::cache_offset_in_bytes(), false);
    __ cvta2va  (R08_CPC);

    // Get constant pool cache entry, check for being resolved
    assert0(sizeof(ConstantPoolCacheEntry) == 4 * ptr_size);
    // RAX    : scaled cache offset
    // RDX_LOC: locals/args
    // R09    : receiver
    // R08_CPC: CP cache pointer
    __ cmp1i(R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset()) + 2/*getfield*/, RAX ,0, 0);
    __ jeq  (go_slow);          // Not resolved

    // Must fence between the load of the "is_resolved" flag (really: the
    // expected-bytecode field moves from 0 to the bytecode) - and any other
    // loads of interesting data.  Not required on X86.

    // Get flags in RAX and field offset in RDI
    __ ldz4(RDI, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset   ()), RAX,0);
    __ ldz4(RAX, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), RAX,0);
    
    // On the fabulous X86 strong memory model, we do not need to fence
    // after reads from Java volatiles
    // R09: receiver
    // RAX: flags
    // RDI: offset
__ shr4i(RAX,ConstantPoolCacheEntry::tosBits);
    __ cvta2va(R09);        // Strip metadata in a common place

    Label is_8bits, is_16bits, is_u16bits, is_32bits, is_64bits, is_object, is_fltbits, is_dblbits;
    __ cmp1i(RAX,T_INT);
    __ jeq  (is_32bits);
    __ cmp1i(RAX,T_OBJECT);
    __ jeq  (is_object);
    __ cmp1i(RAX,T_ARRAY);
    __ jeq  (is_object);
    __ cmp1i(RAX,T_LONG);
    __ jeq  (is_64bits);
    __ cmp1i(RAX,T_SHORT);
    __ jeq  (is_16bits);
    __ cmp1i(RAX,T_CHAR);
    __ jeq  (is_u16bits);
    __ cmp1i(RAX,T_DOUBLE);
    __ jeq  (is_dblbits);
    __ cmp1i(RAX,T_FLOAT);
    __ jeq  (is_fltbits);
    __ cmp1i(RAX,T_BYTE);
    __ jeq  (is_8bits);
    __ cmp1i(RAX,T_BOOLEAN);
    __ jeq  (is_8bits);
    __ os_breakpoint(); // shorter version of should-not-reach-here to keep branches short

__ bind(is_64bits);
    __ ld8 (RAX, R09, 0, RDI, 0);
__ ret();
    
__ bind(is_16bits);
    __ lds2(RAX, R09, 0, RDI, 0);
__ ret();

__ bind(is_u16bits);
    __ ldz2(RAX, R09, 0, RDI, 0);
__ ret();

__ bind(is_8bits);
    __ lds1(RAX, R09, 0, RDI, 0);
__ ret();

__ bind(is_object);
    __ ldref_lvb(RInOuts::a, RAX, RCX /*temp*/, RKeepIns::a, R09, 0, RDI, 0, true);
__ ret();

__ bind(is_32bits);
    __ lds4(RAX, R09, 0, RDI, 0);
__ ret();

__ bind(is_fltbits);
    __ ld4 (F00, R09, 0, RDI, 0);
__ ret();

__ bind(is_dblbits);
    __ ld8 (F00, R09, 0, RDI, 0);
__ ret();

    return entry.abs_pc();
  }

  if (native_call) {
    // We need to build the native wrapper and trampoline to it.  This path is
    // only taken the first time a native is called, because after this the
    // native will have an i2c adapter and natively compiled code and the
    // normal invoke logic will jump to the compiled code.

    // This VM call can cause a GC (allocating strings during native method
    // lookup or blocking on access to the code cache).  We need to make it
    // look enough like a 'real' interpreter frame to survive a GC.
    // RSP aligned with ret addr pushed.
    // RCX holds REF not OOP method to be executed
    // RDX points to incoming locals/args area
    // RSI_BCP not setup - but only needs to be a -1 BCI
    // R08_CPC not setup - and can be ignored (is never GC'd nor needed)

    // CALLING CONVENTION: 
    // This is "prolog" code (as opposed to pre-call code).
    __ pushi(0);                // Push the pad1 word
    __ pushi(0);                // Push the lck/bci word
    __ pushi(0);                // Push the loc/stk word
__ push(RCX);//Save the methodRef
    __ pushi(0);                // Save the cpCacheOop
    // Stack is now complete & aligned

    // Setup the Locals/Args offset.  The wrapper generation can GC and needs
    // to find the argument oops.
    __ move8(RSI,RCX);          // Save methodRef in RSI for outgoing call
    __ cvta2va(RCX);
    __ ldz2 (RCX, RCX, in_bytes(methodOopDesc::size_of_parameters_offset()) );
    __ getthr(RAX);
    __ lea  (RDI_JEX,RDX_LOC,0,RCX,3); // JEX stack is at end of locals
    __ st8  (RAX,in_bytes(JavaThread::jexstk_top_offset()), RDI_JEX);
    __ st4  (RSP,offset_of(IFrame,_loc),RDX_LOC);
    __ st4  (RSP,offset_of(IFrame,_stk),RDI_JEX);

    __ notify_method_entry(); // post_method_exit called in generate_native_wrapper

    // Standard VM call - with no interpreter register setup - we already did
    // that directly here..  If the native lookup fails it can set a
    // pending_exception and we'll jump to forward_exception from here.  The
    // native is not locked here; the wrapper locks as needed.  GC happens, so
    // all non-stacked oops are blown.

    __ call_VM_plain(CAST_FROM_FN_PTR(address, InterpreterRuntime::create_native_wrapper), RSI, noreg, noreg, noreg);
    // Interpreter registers are *not* rebuilt; no need since calling into compiled-code layout

__ check_and_handle_popframe();

    // Check for exceptions
    __ getthr(RDI);
{Label no_exception;
__ null_chk(RDI,in_bytes(JavaThread::pending_exception_offset()),no_exception);
      // forward_exception_entry *will* pop a frame, so push one now in case we
      // are going to catch this exception in this method.
      __ call (StubRoutines::forward_exception_entry());
__ bind(no_exception);
    }

    // CALLING CONVENTION: 
    // This is "epilog" code unwinding the above "prolog".  We are basically
    // retrying the call now that we have a native wrapper prepared.

    // Unwind our partial interpreter frame, then get the i2c adapter address
    // and jump to it.  The i2c adapter will jump to the c2n wrapper which
    // will do the native call.
    __ ld8  (RCX_MTH,RSP,offset_of(IFrame,_mref)); // methodRef was saved on stack
    __ ldz4 (RDX_LOC,RSP,offset_of(IFrame,_loc)); // local/args offset from thread base
    __ getthr(RAX);
    __ or_8 (RDX_LOC,RAX);      // Rebuild RDX_LOC
    __ move8(RAX,RCX);
    __ cvta2va(RAX);            // methodOop into RAX

    __ add8i(RSP,sizeof(IFrame)-8); // pop frame
    __ jmp8 (RAX, in_bytes(methodOopDesc::from_interpreted_offset()) );
    return entry.abs_pc();
  } // End of native code handling


assert(!native_call,"this part for non-native methods only");

  // ---
  // RSP 16b aligned; ret address on stack
  // RCX holds REF not OOP method to be executed
  // RDX points to incoming locals/args area
  // RDI_JEX not setup
  // RSI_BCP not setup
  // R08_CPC not setup

  // CALLING CONVENTION: 
  // This is "prolog" code (as opposed to pre-call code).

  // Setup ByteCode Pointer - BCP
  __ pushi(0);                // Push the pad1 word
  __ pushi(0);                // Push the lck/bci word
  __ pushi(0);                // Push the loc/stk word
__ push(RCX);//Save the methodRef;
  __ cvta2va  (RCX_MTH);
  __ ldref_lvb(RInOuts::a, RDI    , RAX_TMP, RKeepIns::a, RCX_MTH, in_bytes(methodOopDesc::constants_offset()), false);
  __ ldref_lvb(RInOuts::a, RSI_BCP, RAX_TMP, RKeepIns::a, RCX_MTH, in_bytes(methodOopDesc::const_offset()), false);
  __ cvta2va  (RDI);            // Constants - a pointer to the Constant Pool Cache
  __ cvta2va  (RSI_BCP);        // convert BCP into a real pointer
  __ add8i(RSI_BCP, in_bytes(constMethodOopDesc::codes_offset()));
  // Setup the Constant Pool Cache - CPC
  __ ldref_lvb(RInOuts::a, R08_CPC, RAX_TMP, RKeepIns::a, RDI, constantPoolOopDesc::cache_offset_in_bytes(), false);
__ push(R08_CPC);//Save CPCacheRef
  __ cvta2va(R08_CPC);          // cpCacheOop into R08

  // ---
  // RSP 16b aligned
  // RAX - free, RBX - callee-save, RCX - methodOop, RDX - LOCs, RSI - BCP, RDI - free
  // R08 - CPCacheOop, R09 - free, R10 - free, R11 - free, R12-R15 - callee-save

  // Setup the Java locals area.
  __ ldz2 (RAX, RCX_MTH, in_bytes(methodOopDesc::size_of_locals_offset()));
  __ lea  (RDI_JEX, RDX, 0, RAX, 3); // Method's JEX Stack starts at end of locals

  // RAX - num_locals, RBX - callee-save, RCX - methodOop, RDX - LOCs, RSI - BCP, RDI - JEX Stk
  // R08 - CPCacheRef, R09 - free, R10 - free, R11 - free, R12-R15 - callee-save
  // Clear the new Java locals, from RDX to RDI
Label zero_locals;
  __ move8(R11,RCX_MTH);
  __ ldz2 (RCX, RCX_MTH, in_bytes(methodOopDesc::size_of_parameters_offset()));
__ push(RDI_JEX);
  __ lea  (RDI,RDX_LOC,0,RCX,3);// start of locals needing clearing
  __ sub4 (RAX,RCX);            // number of locals needing clearing
  __ jeq  (zero_locals);
  __ move4(RCX,RAX);
  __ xor4 (RAX,RAX);            // zero RAX
  __ repeated() ->stos8();      // store *rdi++ = rax; rcx--;
__ bind(zero_locals);
__ pop(RDI_JEX);

  // RAX - free, RBX - callee-save, RCX - free, RDX - LOCs, RSI - BCP, RDI - JEX Stk
  // R08 - CPCacheRef, R09 - free, R10 - free, R11 - MTH, R12-R15 - callee-save
  // freq count goes here
  // Increment the counter if we are using the compiler or generally counting invocations.
  if (UseC1 || UseC2) {
Label dont_kick_compiler;
    __ ldz4 (RAX, R11, in_bytes(methodOopDesc::invocation_counter_offset() + InvocationCounter::counter_offset()));
    __ add4i(RAX, 1);
    __ st4  (R11, in_bytes(methodOopDesc::invocation_counter_offset() + InvocationCounter::counter_offset()), RAX);
    __ add4 (RAX, R11, in_bytes(methodOopDesc::  backedge_counter_offset() + InvocationCounter::counter_offset()));
    // Now TMP0 holds the summed counters together.  See if they overflow.
    __ cmp8i(RAX, UseC1 ? C1CompileThreshold : C2CompileThreshold);
    __ jlt  (dont_kick_compiler);
    __ mov8i(R10, 0);           // Normal, not OSR, overflow
    { Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R10);
      __ call_VM((address)InterpreterRuntime::frequency_counter_overflow, noreg,
                 RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R10, noreg, noreg);
    }
    if( synchronized) {         // Reload R11 for sync/static flag test coming up
      __ ld8(R11,RSP,offset_of(IFrame,_mref));
      __ cvta2va(R11);
    }
__ bind(dont_kick_compiler);
  }

  // Check for synchronized methods.
  // Must happen after invocation_counter check so that method is not locked on counter overflow.
  if (synchronized) {
    Label dynamic, fast_locked;
    assert0( is_power_of_2(JVM_ACC_STATIC) );
    __ btx4i(R11, in_bytes(methodOopDesc::access_flags_offset()),log2_intptr(JVM_ACC_STATIC));
    // Last use of MTH, RDI is now a temp...
    __ ld8  (RCX, RDX_LOC, 0);  // optimistically load the receiver
    __ jnb  (dynamic);    // Jump if carry clear==> not JVM_ACC_STATIC
    // Locking a static method: load the mirror to lock
    { Register Rtmp = __ get_free_reg(Bytecodes::_athrow, RAX, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11);
      __ ldref_lvb (RInOuts::a, RCX, Rtmp, RKeepIns::a, R11, in_bytes(methodOopDesc::constants_offset()), false);
      __ cvta2va   (RCX);
      __ ldref_lvb (RInOuts::a, R11, Rtmp, RKeepIns::a, RCX, constantPoolOopDesc::pool_holder_offset_in_bytes(), false);
      __ cvta2va   (R11);
      __ ldref_lvb (RInOuts::a, RCX, Rtmp, RKeepIns::a, R11, klassOopDesc::klass_part_offset_in_bytes() + Klass::java_mirror_offset_in_bytes(), false);
    }
__ bind(dynamic);

    // lock the monitor
    // RCX: objectRef to lock; not blown
    // R09, R10 - temp, blown.
    // RAX - holds loaded markWord on slow-path.
    __ fast_path_lock( RInOuts::a, R10, R09, RAX, RKeepIns::a, RCX, fast_locked );
    // RAX: markWord from lock attempt
    // R09: shifted thread ID
    // RCX: objectRef to lock
    __ call (StubRoutines::lock_entry());
    // Returns:
    // RCX: objectRef
    // Flags: Z - locked, NZ - not locked
    __ jeq  (fast_locked);

    // Slow-path locking: must be totally safe for a VM crawl.
    // GC can happen, locks inflate, biases revoked, etc.
    { Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RAX, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
      __ call_VM((address)SharedRuntime::monitorenter, RAX,
                 RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX, noreg, noreg);
      __ move8(RCX,RAX);          // Object locked was returned in RAX
    }
    
__ bind(fast_locked);
    // Push locked object onto the monitor list
Label ok,retry;
    __ bind (retry);
    __ getthr(RAX);
    __ ld8  (R09,RAX,in_bytes(JavaThread::lckstk_top_offset()));
    __ cmp8 (R09,RAX,in_bytes(JavaThread::lckstk_max_offset()));
__ jlt(ok);
__ push(RCX_TOS);
__ push(RDX_LOC);
__ push(RSI_BCP);
__ push(RDI_JEX);
__ push(R08_CPC);
    __ call_VM_leaf((address)JavaThread::extend_lckstk,RAX);
__ pop(R08_CPC);
__ pop(RDI_JEX);
__ pop(RSI_BCP);
__ pop(RDX_LOC);
__ pop(RCX_TOS);
__ jmp(retry);
__ bind(ok);
    if( RefPoisoning ) __ xor8i(RCX,-1);
    __ st8  (R09,0,RCX);        // Store lock in lockstack
    __ add8i(R09,8);            // Bump lockstack top
    __ st8  (RAX,in_bytes(JavaThread::lckstk_top_offset()),R09);
    __ add2i(RSP,offset_of(IFrame,_numlck),1);
  } // !synchronized

  // jvmti support
  __ notify_method_entry();

Label Lpoll;
  __ getthr(RAX);
  __ cmp8i(RAX, in_bytes(JavaThread::please_self_suspend_offset()), 0 );
  __ jnz  (Lpoll);
  Label Lpoll_continue(_masm);

  // start execution of bytecodes
  __ pop_tos();                 // over-pop / cache TOS - on entry RCX_TOS holds last local variable
__ dispatch_next(0);

  // out of line entry to safe point
__ bind(Lpoll);
  { Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), noreg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, noreg, noreg, noreg);
  }
__ jmp(Lpoll_continue);

  return entry.abs_pc();
}

//----------------------------------------------------------------------------------------------------
// Exceptions

void TemplateInterpreterGenerator::generate_throw_exception() {

  // entry point for exceptions thrown within interpreter code
  // TOS: exceptionOop
  { ScopedAsm entry(_masm);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ getthr(RAX);
  __ st8  (RAX,in_bytes(Thread::pending_exception_offset()), RCX_TOS);
  // empty the expression stack
  __ empty_java_exec_stack(RInOuts::a,RAX);

  // Pack BCI/LOC/JEX into stack for lookup
  __ pack_interpreter_regs(RInOuts::a, RAX, R09, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);

  // Call, leaving the current frame on the stack.  forward_exception will
  // jump to either 'remove_activation_entry' or 'continue_activation_entry'.
  // No other targets are allowed, and we do not return from here.
  __ call ( StubRoutines::forward_exception_entry());
__ os_breakpoint();
  Interpreter::_throw_exception_entry = entry.abs_pc();
  }

  //
  // JVMTI PopFrame support
  //
  { ScopedAsm entry(_masm);
__ unimplemented("remove_activation_preserving_args");
  // Set the popframe_processing bit in popframe_condition indicating that we are
  // currently handling popframe, so that call_VMs that may happen later do not
  // trigger new popframe handling cycles.

//  __ ld4     (TMP0, THR, in_bytes(JavaThread::popframe_condition_offset()));
//  __ set_int (TMP1, JavaThread::popframe_processing_bit);
//  __ or_     (TMP0, TMP0, TMP1);
//  __ st4     (THR, in_bytes(JavaThread::popframe_condition_offset()), TMP0);
//
//  // Empty the expression stack, as in normal exception handling
//  __ ld8     (R31_ASP, FP, 0);	// empty the expression stack
//  __ remove_activation(false,false);
//
//  {
//    // Check to see whether we are returning to a deoptimized frame.
//    // (The PopFrame call ensures that the caller of the popped frame is
//    // either interpreted or compiled and deoptimizes it if compiled.)
//    // In this case, we can't call dispatch_next() after the frame is
//    // popped, but instead must save the incoming arguments and restore
//    // them after deoptimization has occurred.
//    //
//    // Note that we don't compare the return PC against the
//    // deoptimization blob's unpack entry because of the presence of
//    // adapter frames in C2.
//    Label caller_not_deoptimized;
//    __ mfcr (R9, RPC);
//    __ call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::interpreter_contains), R9);
//    __ beqi (R9, 1, caller_not_deoptimized);
//
//    // Save these arguments
//    __ ldu2  (R10, MTH, in_bytes(methodOopDesc::size_of_parameters_offset()));
//    __ shl8i (R10, R10, LogBytesPerWord);
//    __ subu8i(R11, R10, 8);
//    __ subu8 (R11, LOC, R11);
//    __ move  (R9, THR);
//    __ call_VM_leaf(CAST_FROM_FN_PTR(address, Deoptimization::popframe_preserve_args), IA0);
//    // Inform deoptimization that it is responsible for restoring these arguments
//    __ st4i (THR, in_bytes(JavaThread::popframe_condition_offset()), JavaThread::popframe_force_deopt_reexecution_bit);
//
//    // Return from the current method
//    __ ret();
//
//    __ bind(caller_not_deoptimized);
//  }
//
//  // Clear the popframe condition flag
//  __ st4i (THR, in_bytes(JavaThread::popframe_condition_offset()), JavaThread::popframe_inactive);
//
//  // Get out of the current method
//  __ popframe  ();
//
//  // Resume bytecode interpretation at the current bcp
//  __ dispatch_next(0);
  // end of JVMTI PopFrame support
  Interpreter::_remove_activation_preserving_args_entry = entry.abs_pc();
  }

  // ----------------------------------------- 
  // The current interpreter frame needs to be popped.  There remains
  // a pending exception in thread->_pending_exception which needs to
  // be thrown into the caller.
  { ScopedAsm entry(_masm);

  // If we come here from inside the interpreter via throw_exception_entry
  // then the interpreter stack is already cleared.  However, if we get here
  // from stack unwinding (via some prior frame popping and using
  // forward_exception on us) then the stack will not yet have been cleared.
  // However, since we are popping the control frame, the Java Exec frame
  // will be automagically popped, so we don't need to do any explicit stack
  // clear action here.
  //__ empty_java_exec_stack(RInOuts::a,RAX);

  // Remove any locks
  { Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    // unlock if synchronized method and check that all monitors are unlocked
    __ remove_activation(RInOuts::a, RSI, RCX, Rtmp0, true,true);
  }

  // We came here from a prior forward_ex and no regs are valid.
  // Force the stack & locals to their smallest value so the runtime
  // can tell whether or not the *next* frame also needs to pop.
  __ ldz4 (RDX_LOC,RSP,offset_of(IFrame,_loc));// Get count of java locals
  __ st4  (RSP,offset_of(IFrame,_stk),RDX_LOC);// Smash JEX stack to match in the control word
  __ getthr(RAX);
  __ or_8 (RDX_LOC,RAX);        // Rebuild locals pointer
  __ st8  (RAX,in_bytes(JavaThread::jexstk_top_offset()),RDX_LOC);

  // Pop this frame (but leave ret adr) and forward exception.
  __ add8i(RSP,sizeof(IFrame)-8); // pop stack 
  __ jmp  ( StubRoutines::forward_exception_entry());

  Interpreter::_remove_activation_entry = entry.abs_pc();
  }

  // ----------------------------------------- 
  // The current interpreter frame needs to be popped because the stack
  // overflowed.  There remains a pending stack overflow exception in
  // thread->_pending_exception which needs to be thrown into the caller.  We
  // can't go to any bytecode handler to unlock synchronized blocks; we need
  // to just unlock any locks and get out.  See above code.
  { ScopedAsm entry(_masm);

  // We came here from a prior forward_ex and no regs are valid.
  // Force the stack & locals to their smallest value so the runtime
  // can tell whether or not the *next* frame also needs to pop.
  __ ldz4 (RDX_LOC,RSP,offset_of(IFrame,_loc));// Get count of java locals
  __ st4  (RSP,offset_of(IFrame,_stk),RDX_LOC);// Smash JEX stack to match in the control word
  __ getthr(RAX);
  __ or_8 (RDX_LOC,RAX);        // Rebuild locals pointer
  __ st8  (RAX,in_bytes(JavaThread::jexstk_top_offset()),RDX_LOC);

  { Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    // unlock if synchronized method and check that all monitors are unlocked
    __ remove_activation(RInOuts::a, RSI, RCX, Rtmp0, true, false);
  }
  // Pop this frame (but leave ret adr) and forward exception.
  __ add8i(RSP,sizeof(IFrame)-8); // pop stack 
  __ jmp  ( StubRoutines::forward_exception_entry());

  Interpreter::_remove_activation_force_unwind_entry = entry.abs_pc();
  }

  // ----------------------------------------- 
  // The current interpreter frame will handle the pending exception in
  // thread->_pending_exception.  Move the exception into TOS and dispatch.
  { ScopedAsm entry(_masm);

  // If we come here from inside the interpreter via throw_exception_entry
  // then the interpreter stack is already cleared.  However, if we get here
  // from stack unwinding (via some prior frame popping and using
  // forward_exception on us) then the stack will not yet have been cleared.
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ unpack_interpreter_regs(RInOuts::a, Rtmp0, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ empty_java_exec_stack(RInOuts::a, Rtmp0);

  // Load pending exception into RCX_TOS cache; clear it from Thread
  __ getthr(RAX);
  __ mov8i(RCX_TOS,0);          // load & clear pending exception
  __ xchg (RCX_TOS, RAX, in_bytes(Thread::pending_exception_offset())); // no need for atomic/lock-prefix
  // Dispatch to the 'atos' state: the exception oop is already in TOS.  Skip
  // 0 bytecodes, as we are pointing at the correct bytecode to begin execution.
__ dispatch_next(0);
  Interpreter::_continue_activation_entry = entry.abs_pc();
  }
}


// --- generate_iframe_stk_args
// FAT INTERPRETER FRAME; normal return
// When passing more args than fit in registers, we grow the interpreter's
// control-stack to pass stack arguments.  The frame is still fixed-size, so
// GDB & friends can crawl it but it's larger than the usual frame::
// iframe_size.  When we return from such a call, we need to restore the
// frame size back to normal.
address TemplateInterpreterGenerator::generate_iframe_stk_args(int extra_words, const char *name) {
  InterpreterMacroAssembler masm(CodeBlob::interpreter, name);
  int spc = masm.rel_pc();

  // Note that this code is used on the return path, so the return value
  // in RAX is alive, also RDI as a bonus return value.
  
  // Copy the interpreter's orginal frame back to it's original location.
  masm.ld8  (RCX,RSP, offset_of(IFrame,_cpc) );
  masm.ld8  (RDX,RSP, offset_of(IFrame,_mref));
  masm.ld8  (RSI,RSP, offset_of(IFrame,_loc) );
  masm.ld8  (R11,RSP, offset_of(IFrame,_bci) );
  masm.ld8  (R10,RSP, offset_of(IFrame,_retadr));
  masm.add8i(RSP,extra_words*8);
  masm.st8  (    RSP, offset_of(IFrame,_cpc) , RCX );
  masm.st8  (    RSP, offset_of(IFrame,_mref), RDX );
  masm.st8  (    RSP, offset_of(IFrame,_loc) , RSI );
  masm.st8  (    RSP, offset_of(IFrame,_bci) , R11 );
  
  masm.jmp8 (R10);                // and continue as if nothing ever happened...
    
  masm.bake_into_CodeBlob(extra_words*8);
  CodeBlob* newBlob = masm.blob();

  // GDB Support
  // Tell GDB about these rare 'phat' interpreter frames
MutexLocker ml(ThreadCritical_lock);
  address start = newBlob->code_begins();
  hotspot_to_gdb_symbol_t *hsgdb = CodeCache::get_new_hsgdb();
  hsgdb->startPC = (intptr_t)start;
  hsgdb->codeBytes = masm.rel_pc() - spc;
  hsgdb->frameSize = extra_words*8; // a new interpreter frame
  hsgdb->nameAddress = name;
  hsgdb->nameLength = strlen(hsgdb->nameAddress);
  hsgdb->savedRBP = false;
  hsgdb->pad1 = hsgdb->pad2 = hsgdb->pad3 = 0;

  return start;
}

//
// JVMTI ForceEarlyReturn support
//
address TemplateInterpreterGenerator::generate_earlyret_entry_for(TosState state) {
  ScopedAsm entry(_masm);
__ unimplemented("JVMTI early return not implemented");
  return entry.abs_pc();
} // end of ForceEarlyReturn support


// --------------------------------------------------------------------------------
InterpreterGenerator::InterpreterGenerator(InterpreterMacroAssembler*masm)
:TemplateInterpreterGenerator(masm){
   generate_all(); // down here so it can be "virtual"
   // Sentinel IC marking end of all Codelets
   InterpreterCodelet *ic = masm->make_codelet("last codelet",Bytecodes::_illegal);
   *(int*)&ic->_size = 0;       // Sentinel has zero size

  // Must be the last few bits of the interpreter, so the GDB support can lop
  // these off the normal GDB interpreter section and they stand alone (because
  // they are both standard interpreter frames AND have larger than normal
  // framesizes).
  {
    Interpreter:: _iframe_16_stk_args_entry = generate_iframe_stk_args( 16, "fat_stack_16_args");
    StubRoutines::set_forward_exception_fat16_entry((int64_t)Interpreter:: _iframe_16_stk_args_entry);
  }
  {
    Interpreter::_iframe_256_stk_args_entry = generate_iframe_stk_args(256, "fat_stack_256_args");
    StubRoutines::set_forward_exception_fat256_entry((int64_t)Interpreter:: _iframe_256_stk_args_entry);
  }
}

// Non-product code
#ifndef PRODUCT
address TemplateInterpreterGenerator::generate_trace_code(TosState state) {
assert(state==tos,"no TosStates supported");
  ScopedAsm entry(_masm);
  Label L;

  // Check if TraceBytecodes is still enabled, to avoid calling runtime unnecessarily.
__ ld8(RAX,(address)&BytecodeCounter::_counter_value);
  __ cmp8 (RAX,(address)&TraceBytecodesAt );
  __ jab  (L);
__ ret();
__ bind(L);

  // dump expression stack
__ push_tos();
#if 0
for(int i=0;i<12;i++){
    __ st8 (RDI_JEX, 8+(i<<3),(Register)i);
  }
#endif
  Register Rax_arg_and_result = RAX;
__ pop(Rax_arg_and_result);//pull return address off stack as argument
  __ move8(R10, RCX);
  __ ld8  (R11, RDI_JEX, -16);
  Register Rtmp0 = __ get_free_reg(Bytecodes::_athrow, RAX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R10, R11);
  __ call_VM(CAST_FROM_FN_PTR(address, SharedRuntime::trace_bytecode), Rax_arg_and_result,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, Rax_arg_and_result, R10, R11);
__ push(Rax_arg_and_result);//Restore return address
#if 0
for(int i=0;i<12;i++){
    __ ld8 ((Register)i,RDI_JEX, 8+(i<<3));
  }
#endif
  __ pop_tos();                 // restore expression stack
  __ ret  ();
  return entry.abs_pc();
}


// helpers for generate_and_dispatch
void TemplateInterpreterGenerator::count_bytecode() {
  __ inc8((address)&BytecodeCounter::_counter_value);
}


void TemplateInterpreterGenerator::stop_interpreter_at() {
  Label L;
__ ld8(RAX,(address)&BytecodeCounter::_counter_value);
  __ cmp8 (RAX,(address)&StopInterpreterAt );
  __ jbl  (L);
__ stop("StopInterpreterAt reached.");
  __ bind (L);
}

void TemplateInterpreterGenerator::trace_bytecode(Template* t) {
  __ call(Interpreter::trace_code(tos));
}
#endif // not PRODUCT
