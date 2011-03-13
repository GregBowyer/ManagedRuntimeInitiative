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


#include "gcLocker.hpp"
#include "interp_masm_pd.hpp"
#include "interpreterRuntime.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "nativeInst_pd.hpp"
#include "oopTable.hpp"
#include "remoteJNI.hpp"
#include "sharedRuntime.hpp"
#include "signature.hpp"
#include "stubRoutines.hpp"
#include "tickProfiler.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "frame_pd.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "register_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#define __ _masm->


// For i2c and c2i adapters, how can we flatten the Java signature to better
// share adapters?  T_BOOLEAN, T_CHAR, T_BYTE, T_SHORT, T_INT are all treated
// as T_INT.  T_OBJECT & T_ARRAY also look alike (but different from ints and
// longs because they are handlized).  Floats & Doubles go in SSE registers.
const char *InterpreterRuntime::_native_flat_sig = "XXXXIIFDIIIJLLXX";

// ---------------------------------------------------------------
int InterpreterRuntime::generate_i2c2i_adapters( InterpreterMacroAssembler *_masm, int total_args_passed, VReg::VR max_arg, const BasicType *sig_bt, const VReg::VR *regs ) {
  // Generate an I2C adapter: adjust the I-frame to make space for the C-frame
  // layout.  Hoist the first 6 int & 8 fp args into registers.  The remaining
  // stack args will generally need to be repacked (unless we run out of int &
  // fp args at exactly the same time).  Finally, end in a jump to the
  // compiled code.  The entry point address is the start of the buffer.

  // Inputs:
  // RCX      - Method REF
  // RDX_LOC  - Start of arg list on the JEX stack
  // RSP      - aligned, with ret addr on stack
  // Arg N    - is at [RDX+8*n]

  // Outputs:
  // Stacked interpreter args loaded into compiled registers.  Args beyond 6
  // ints & 8 floats are passed on the stack.  The RSP stack is not adjusted.

  // Make sure _from_compiled_entry is read after _from_interpreted_entry.
  // On the X86 loads are not allowed to be reordered.
  // __ fence (LoadLoad);

  // FAT INTERPRETER FRAME
  // Do we need to pass args on the stack?  If so we need to grow the
  // interpreter's control frame which is otherwise fixed at 32 bytes.
  if( VReg::is_stack(VReg::VR(max_arg-1)) ) {
    uint maxstk_off = VReg::reg2stk(max_arg); // largest stack offset seen
    uint extra_framesize = 16*8;
    if( maxstk_off >= extra_framesize ) {
      extra_framesize = 256*8;
    }
    if ( maxstk_off >= extra_framesize ) {
Unimplemented();//very large frames
    }
    // Now copy the interpreter's original frame contents low in the frame.
    // All the frame accessor functions look in the 1st 3 words.
    __ ld8  (R11,RSP, 0);                 // Copy original return address
    __ ld8  (RAX,RSP, offset_of(IFrame,_cpc) +8/*ret adr*/);
    __ ld8  (R10,RSP, offset_of(IFrame,_mref)+8/*ret adr*/);
    __ ld8  (RSI,RSP, offset_of(IFrame,_loc) +8/*ret adr*/);
    __ ld8  (RDI,RSP, offset_of(IFrame,_bci) +8/*ret adr*/);
    __ sub8i(RSP,extra_framesize); // Make space for outgoing args
    __ st8  (    RSP, offset_of(IFrame,_cpc) +8/*ret adr*/, RAX);
    __ st8  (    RSP, offset_of(IFrame,_mref)+8/*ret adr*/, R10);
    __ st8  (    RSP, offset_of(IFrame,_loc) +8/*ret adr*/, RSI);
    __ st8  (    RSP, offset_of(IFrame,_bci) +8/*ret adr*/, RDI);
    __ st8  (    RSP, offset_of(IFrame,_retadr)+8/*ret adr*/, R11);

    // Set a return address that pops off the fat-ness of the frame
    address ret_adr = (extra_framesize == 16*8)
      ? Interpreter:: iframe_16_stk_args_entry()
      : Interpreter::iframe_256_stk_args_entry();
    __ st8i (RSP, 0,(intptr_t)ret_adr);
  }
  
  // Will jump to the compiled code just as if compiled code was doing it.
  // Pre-load the register-jump target early, to schedule it better.  Also
  // free up RDX for holding compiled args.
  __ cvta2va(RCX);
  __ ldz4(R10, RCX, in_bytes(methodOopDesc::from_compiled_offset()));
  __ move8(RAX,RDX);            // Arg base to RAX

  // Pick up the args into registers.
  for (int i=0; i<total_args_passed; i++) {
    VReg::VR vreg = regs[i];
    if( vreg == VReg::Bad ) // T_VOID; empty half of long or double
      continue;
    // Pick up 0, 1 or 2 words from RSP+offset.
    int ld_off = i*8;           // Load in argument order
    // Longs are given 2 64-bit slots in the interpreter, but the
    // data is passed in only 1 slot.
    if( sig_bt[i]==T_LONG || sig_bt[i]==T_DOUBLE ) ld_off += 8;
    if( VReg::is_stack(vreg) ) { // Stack args?
      __ ld8(R11,RAX,ld_off);   // Load arg into temp R11
      __ st8(RSP,8/*skip ret adr*/+VReg::reg2stk(vreg),R11);
    } else if( is_gpr(vreg) ) {
Register r=reg2gpr(vreg);
      // Since the interpreter does not distinguish 'push_int' from 'push_ptr'
      // all values are loaded & stored as 8-byte quantities.
      // I can always do a ld8 here.
      __ ld8(r,RAX,ld_off);
}else if(sig_bt[i]==T_FLOAT){
      FRegister fpr = reg2fpr(vreg);
      __ ld4(fpr,RAX,ld_off);
    } else {
      FRegister fpr = reg2fpr(vreg);
      __ ld8(fpr,RAX,ld_off);
    }
  }

  // Jump to the compiled code just as if compiled code was doing it.
  __ jmp8(R10);

  // -------------------------------------------------------------------------
  // Generate a C2I adapter.  On entry we know R10 holds a pointer to the
  // methodOop.  The methodOop will need to be loaded & read-barriered and
  // moved into RBX for the interpreter.  Args start out packed in the
  // compiled layout.  They need to be unpacked into the interpreter layout.
  // This will almost always require some stack space.  We grow the current
  // (compiled) stack, then repack the args.  We finally end in a jump to the
  // generic interpreter entry point.
  __ align(8);                  // Slight alignment for slightly faster execution.
  int c2i_offset = __ rel_pc();
  __ ldref_lvb(RInOuts::a,R11,RAX,RKeepIns::a,R10,0,false);  // Load methodRef into R11

  // Before we get into the guts of the C2I adapter, see if we should be here
  // at all.  We've come from compiled code and are attempting to jump to the
  // interpreter, which means the caller made a static call to get here
  // (vcalls always get a compiled target if there is one).  Check for a
  // compiled target.  If there is one, we need to patch the caller's call.
  __ move8(RAX,R11);
  __ cvta2va(RAX);              // Load methodOop into RAX
  __ cmp8i(RAX, in_bytes(methodOopDesc::codeRef_offset()),0);
  __ jne  (StubRoutines::resolve_and_patch_call_entry());// Have code, go patch

  // Now write the args into the outgoing interpreter space
  __ getthr(RAX);
  __ ld8  (R10,RAX,in_bytes(JavaThread::jexstk_top_offset())); // Rebuild locals pointer into R10
  for( int i=0; i<total_args_passed; i++ ) {
    VReg::VR vreg = regs[i];
    if( vreg == VReg::Bad )     // unused half of longs or doubles
      continue;
    if( VReg::is_stack(vreg) ) { // Stack args?
      switch( sig_bt[i] ) {
      case T_LONG:
      case T_DOUBLE:
      case T_OBJECT:
      case T_ARRAY:  __ ld8 (RAX,RSP,VReg::reg2stk(vreg)+8/*skip ret adr*/); break; 
      case T_CHAR:
      case T_SHORT:
      case T_BYTE:
      case T_BOOLEAN:
      case T_INT:    __ lds4(RAX,RSP,VReg::reg2stk(vreg)+8/*skip ret adr*/); break;
      case T_FLOAT:  __ ldz4(RAX,RSP,VReg::reg2stk(vreg)+8/*skip ret adr*/); break;
      default: ShouldNotReachHere();
      }
      vreg = gpr2reg(RAX);      // as part of the load/store shuffle
    }

int st_off=i*8;
    // Longs are given 2 64-bit slots in the interpreter, but the
    // data is passed in only 1 slot.
    if( sig_bt[i]==T_LONG || sig_bt[i]==T_DOUBLE ) {
      // Inlined copy of store_sentinel with non-zero offset.
      // Insert the sentinel value for the tagged interpreter
      __ store_sentinel(RKeepIns::a, R10, st_off);
      // Actual data goes in the other slot
      st_off += 8;
    }
    // Since the interpreter does not distinguish 'push_int' from 'push_ptr'
    // all values are stored as 8-byte quantities.
    if( is_gpr(vreg) ) {
      __ st8(R10,st_off,reg2gpr(vreg));
    } else {
      assert0( is_fpr(vreg) );
      if( sig_bt[i] == T_FLOAT ) {
        __ st4 (R10,st_off,reg2fpr(vreg));
        __ st4i(R10,st_off+4,0);
      } else {
        __ st8(R10,st_off,reg2fpr(vreg));
      }
    }
  }

  // Schedule the branch target address.  I'm using "interpreter_entry"
  // instead of "from_interpreter".  This is a correctness issue:
  // interpreter_entry always goes to the interpreter, but from_interpreter is
  // either the same as interpreter_entry or an i2c adapter.  If we get an i2c
  // adapter, then we'll have a compiled frame (the caller) going through this
  // c2i adapter, then an i2c adapter and back to a compiled frame.  This
  // double-stretches the caller's frame but the callee is compiled and won't
  // correct the caller's stack.
  __ move8(RCX,R11);            // methodRef to RCX
  __ cvta2va(R11);              // methodOop to R11
  __ move8(RDX_LOC,R10);        // Locals in RDX
  __ jmp8 (R11, in_bytes(methodOopDesc::interpreter_entry_offset()));

  return c2i_offset;
}


static const int int_reg_max = 6;
static const int flt_reg_max = 8;
static const  Register intarg[int_reg_max] = { RDI, RSI, RDX, RCX, R08, R09 };
static const FRegister fltarg[flt_reg_max] = { F00, F01, F02, F03, F04, F05, F06, F07 };

// ---------------------------------------------------------------------------
// Generate a native wrapper for a given method.  The method takes arguments
// in the Java compiled code convention, marshals them to the native
// convention (handlizes oops, etc), unlocks the jvm_lock, makes the call,
// reclaims the jvm_lock (possibly blocking), unhandlizes any result and
// returns.
#define NUM_EXTRA_REMOTEARGS 1
int InterpreterRuntime::generate_native_wrapper( InterpreterMacroAssembler *_masm, methodHandle method, int total_args_passed, VReg::VR max_arg, BasicType *sig_bt, const VReg::VR *regs, BasicType ret_type ) {
  // Inputs:
  // RDI,RSI,RDX,RCX,R08,R09 - compiled non-float args
  // XMM0-XMM7 - compiled float args
  // RBP is nothing special (i.e., not Frame Pointer)
  // RAX holds the base of extra args beyond what fits in registers.
  //
  // +--------------+ <--- Here up   Owned by Caller
  // |    parmX     | <--- Here down Owned by Callee
  // :      :       :
  // |  last parm   |
  // +--------------+
  // | return adr   | <--- RSP
  // +--------------+

  // Outputs:
  // RDI     - JNIEnv (Thread*+offset)
  // RSI     - handle for static class, or arg0 (For remote methods, RSI will contain
  //           remoteJNI_info and RDX will contain the handle for static class)
  // RDX-R09 - args1-args5, packed (extra word for longs filled with next arg),
  //           and handlized (ptrs to oops on stack)
  //
  // +--------------+ <--- Here up   Owned by Caller
  // |  last parm   | <--- Here down Owned by Callee
  // :      :       :
  // |    parmX     | 
  // +--------------+      -----frame split----
  // | return adr   | <--- Old RSP
  // |   oops       |      handlized oops
  // |              | <--- RAX incoming argument base
  // | last stk arg |
  // :      :       :      stack args, packed for C & handlized
  // |   parmY      |
  // +--------------+ <--- RSP

  bool is_remote = method->is_remote();

  // Compute framesize for the wrapper.  We need to handlize all oops in
  // registers (force them to the stack and generate OopMap entries for them),
  // and shuffle args to match the native convention
  
  // The first 6/8 args packed in registers, then a 64-bit stack slot for each
  // arg past that.
  int int_word_count = 0;
  int flt_word_count = 0;
  for( int i=0; i<total_args_passed; i++ ) {
    switch( sig_bt[i] ) {
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
    case T_INT:
    case T_SHORT:
case T_LONG://Longs & doubles fit in 1 register
    case T_ARRAY:
    case T_OBJECT:
      int_word_count++; 
      break;
    case T_FLOAT:
    case T_DOUBLE:
      flt_word_count++; 
      break;
case T_VOID://Always follows a long or double
      break;
    default:
    case T_ADDRESS: // Used, e.g., in slow-path locking for the lock's stack address
      ShouldNotReachHere();
    }
  }
  // Add one for the JNI env, and maybe one for the static klass argument
  int_word_count += 1 + (method->is_static() ? 1 : 0);
  const int max_int_word_count = int_word_count;
  const int max_flt_word_count = flt_word_count;

  if( is_remote ) 
    int_word_count += NUM_EXTRA_REMOTEARGS;   // account for (entrypoint, fingerprint, signature)
  int outgoing_c_args = int_word_count;
  int outgoing_flt_args = flt_word_count;
  // Really: stack size for outgoing C args
  int stack_size_in_words = MAX2(int_word_count-6,0) + MAX2(flt_word_count-8,0);  
  int stack_arg_idx = stack_size_in_words; // After last outgoing arg location

  // Above the memory argument words, we need space for any oops passed in
  // registers.  The register argument oops are stored here and handles placed
  // in the register.  Also, the OopMap refers to these locations.  Oops
  // already on the stack can stay there, and so don't need extra space.
  int oop_offset_in_words = stack_size_in_words;
  for( int i=0; i<total_args_passed; i++ ) {
    if( !VReg::is_stack(regs[i]) && (sig_bt[i] == T_OBJECT || sig_bt[i] == T_ARRAY) )
      stack_size_in_words++;
  }
  // Static calls will need a handlized class oop
if(method->is_static())
    stack_size_in_words++;      // Make space for 1 more oop

  // X86 - Return address is already on the stack
  stack_size_in_words++;        // X86 return address
  stack_size_in_words++;        // Saved RBX

  // Align to 16b; which is 2 words
  stack_size_in_words = (stack_size_in_words+1)&-2;

  // Generate a new frame for the wrapper, not counting the return address
  __ add8i(RSP, -(stack_size_in_words-1)*wordSize);
  __ st8  (RSP,  (stack_size_in_words-2)*wordSize, RBX);

  // -----------------
  // The Grand Shuffle

  // Natives require 1 or 2 extra arguments over the normal ones: the JNIEnv*
  // (derived from JavaThread*) and, if static, the class mirror instead of a
  // receiver.  This pretty much guarentees that register layout will not
  // match.  Also, all oops must be handlized - and that requires stack space
  // which was not allocated before the args got pushed on the stack.

  // Arguments are passed in RDI, RSI, RDX, RCX, R08, R09 & XMM registers,
  // then passed in memory.  Since we need more intptr args than we started
  // with, we need to shuffle R09 to stack, R08 into R09, RCX into R08, etc.

  // OopMap support
  GrowableArray<short> oop_offs;
  int lock_off=-1;              // Offset to the locked object

  // Now loop over the args, moving them about as needed.
  // Roll over the list backwards, to free up later registers.
for(int i=total_args_passed-1;i>=0;i--){
    if( regs[i] == VReg::Bad ) continue; // Ignore unused T_VOID
Register Rarg=noreg;//Place to put argument
    FRegister FRarg = (FRegister)noreg; // Place to put argument
BasicType arg_type=sig_bt[i];
    bool is_float_arg = arg_type==T_FLOAT || arg_type==T_DOUBLE;

    if (VReg::is_stack(regs[i])) { // Arg is on stack?  Move to register
      int ld_off = VReg::reg2stk(regs[i]) + stack_size_in_words*8;
      Rarg = RAX; // stack argument will be loaded into RAX;
      switch( arg_type) {
      case T_FLOAT:   if ( is_remote ) { // floats are widened to double to go thru varargs interface in proxy
                        FRarg = F09;  // caller-save and does not overlap outgoing FP argument regs.
                        __ cvt_f2d(FRarg, RSP, ld_off);
                      } else { 
                        __ ldz4(Rarg,RSP,ld_off); // zero extend for a slightly shorter instruction
                      }
                      break; 
      case T_LONG:
      case T_DOUBLE:
      case T_OBJECT:
      case T_ARRAY:   __ ld8 (Rarg,RSP,ld_off); break;
      default:        __ lds4(Rarg,RSP,ld_off); break;
      }
    } else if( is_gpr(regs[i]) ) {
      Rarg  = reg2gpr(regs[i]); // arg starts out in a register
    } else {
      FRarg = reg2fpr(regs[i]); // arg starts out in a register
      if ( is_remote && (arg_type == T_FLOAT) ) {
        // need to widen float args in registers prior to calling thru varargs
        __ cvt_f2d ( FRarg, FRarg);
      }
    }

    // Handlize if needed.
if(arg_type==T_OBJECT||arg_type==T_ARRAY){
      __ verify_oop(Rarg, MacroAssembler::OopVerify_OutgoingArgument);
      int off;
      if( VReg::is_stack(regs[i]) ) { // Oop starts on stack?
        off = VReg::reg2stk(regs[i])+stack_size_in_words*8;
      } else {                  // Must store oop to stack for handle
        off = (oop_offset_in_words++)<<3; // Next oop goes in next slot
        __ st8(RSP, off, Rarg);
      }
      oop_offs.push(off);       // Add stack offset of oop to list


if(i==0){//Arg zero is also an oop?
        lock_off = off;         // Record stack offset of object locked
        if( method->is_synchronized() )
          __ move8(R11,Rarg);   // Also capture ref to be locked
      }
      
      // Handle check
Label isnull;
      if( i != 0 || method->is_static() ) // Receiver is known not-null
        __ null_chk(Rarg,isnull);
      __ lea (Rarg, RSP, off);
__ bind(isnull);
    }

    // Where does the argument go?
    if(( is_float_arg && flt_word_count > 8) ||
       (!is_float_arg && int_word_count > 6)) {
      // if it's a float register and there aren't enough float registers, or
      // it's an int register without enough int registers, on the stack
      assert0(stack_arg_idx > 0);
      stack_arg_idx--;
      if (FRarg == (int)noreg) {
        // int arg from register or stack, or float arg from stack
        __ st8(RSP,stack_arg_idx<<3,Rarg);
      } else {
        // float arg from register
        __ st8(RSP,stack_arg_idx<<3,FRarg);           
      }
      if (is_float_arg) flt_word_count--;
      else              int_word_count--;
    } else {                    // Register?
      if( is_float_arg ) {
        flt_word_count--;       // Float args should be untouched
        assert0( fltarg[flt_word_count] == FRarg );
      } else {
        int_word_count--;
        Register dst = intarg[int_word_count];
        __ move8(dst,Rarg);
      }
    }
  }

  // address of the native_function in the wrapper; needed next time a register
  // native is called.
Label nativefuncaddrpc;

  // Next up: set up parameter to get at remote entry point, signature, fingerprint
Label rdatapc;
  enum { 
    entrypoint_off = 0,
    fingerprint_off= 8,
    staticFlag_off =16,
    reserved_off   =20,
    nameOff_off    =24,
    sigOff_off     =28,  
    name_off       =32
  };
  if( is_remote ) {
Label rjnidata_label;
__ jmp(rjnidata_label);
    __ align(8);                // align so data is stored fast
__ bind(rdatapc);
    // if method is remote layout information about the method
    // (entrypoint/fingerprint/signature - all known at compile time)
    // into the code for the method so that this information can be passed
    // to the remote JNI handler code
    // Name
    const char *rname = method->name_as_C_string();
    const size_t nameLen = strlen(rname);
    uint idx;
    for( idx = 0; idx < nameLen + 1; idx += 4 ) ;
    const int sig_off = name_off+idx;
__ bind(nativefuncaddrpc);
    __ emit8((uint64_t)method->native_function()); // Entrypoint
    __ emit8(Fingerprinter(method).fingerprint()); // Fingerprint
    __ emit4(method->is_static());                 // static flag
    __ emit4(0);                                   // reserved
    __ emit4(name_off - nameOff_off);              // Name pointer
    __ emit4( sig_off -  sigOff_off);              // Signature pointer
    // Name, 4 bytes at a time
    for( idx = 0; idx < nameLen + 1; idx += 4 ) {
      __ emit4(*(int32_t*)&rname[idx]);
    }
    // Signature
    symbolOop sig = method->signature();
    const char *sigstr = sig->as_C_string();
    for (int idx = 0; idx < sig->utf8_length() + 1; idx += 4)
      __ emit4(*(int32_t*)&sigstr[idx]);

__ align(8);
__ bind(rjnidata_label);
    // move the address of the data region to RSI (second argument)
    assert0( entrypoint_off == 0 );
    __ mov8i(RSI, rdatapc /*+entrypoint_off*/);  // 5 byte opcode; last 4 bytes will be patched

  }

  // Next up: handlize the static class mirror in RSI.  It's known not-null.
  if (method->is_static()) {
    objectRef o = Klass::cast(method->method_holder())->java_mirror_ref();
    int oidx = CodeCacheOopTable::putOop(o);
    __ record_constant_oop(oidx);
    // RSI is the 2nd outgoing C argument register
    __ oop_from_OopTable(RInOuts::a, R11, RAX_TMP, oidx);
    int off = (oop_offset_in_words++) * wordSize;
    __ st8(RSP, off, R11);
    oop_offs.push(off);         // Add stack offset of oop to list
    lock_off = off;             // Record stack offset of object locked
    __ lea(is_remote ? RDX : RSI, RSP, off);
  }

  // END of The Grand Shuffle
  // -----------------

  // Lock a synchronized method
  if (method->is_synchronized()) {
Label fast_locked;
    // Note that all C-arg registers are live here: RDI (JNIEnv), RSI
    // (handle of 'this' or static-mirror), RDX, RCX, R08, R09 -
    // outgoing args oop_mask is the valid OopMap here.  
    // Blows RAX, RBX, R10.  Locks R11.
    __ verify_oop(R11, MacroAssembler::OopVerify_OutgoingArgument);
    if( max_int_word_count >= 6 ) __ mov8 (R10,R09); // Save outgoing arg R09
    __ fast_path_lock( RInOuts::a, RBX, R09, RAX, RKeepIns::a, R11, fast_locked);

    // RAX: markWord from lock attempt
    // R09: shifted thread ID
    // R10: saved R09
    // R11: objectRef to lock
    __ call (StubRoutines::lock_entry());
    // Returns:
    // R11: objectRef to lock
    // Flags: Z - locked, NZ - not locked
    __ jeq  (fast_locked);

    // Slow-path locking
    // RAX - dead
    // RBX - saved already, dead here
    // RCX - outgoing live arg
    // RDX - outgoing live arg
    // RSI - outgoing live arg
    // RDI - outgoing live arg
    // RBP - callee save, so preserved
    // RSP - callee save, so preserved
    // R08 - outgoing live arg
    // R09 - dead
    // R10 - saved R09
    // R11 - Arg to THIS call, but dead past call
    // R12-R15 - callee save
    // F00-F07 - Possible float args
    int savemask = 0;           // save only the outgoing native arguments
    for( int i=0; i<max_int_word_count && i<6; i++ ) {
      savemask |= (1<<intarg[i]);
    }
    OopMap2 *_lock_oopmap = new OopMap2();
for(int i=0;i<oop_offs.length();i++){
      _lock_oopmap->add( VOopReg::stk2reg(oop_offs.at(i)) );
    }
    __ verify_oopmap (_lock_oopmap, MacroAssembler::OopVerify_OopMap_PreCall);
    savemask |= (1<<max_flt_word_count)-1;
    assert0 (-frame::runtime_stub_frame_size + frame::xPAD == -16);
    __ st8  (RSP, -16, R11);
    __ call (StubRoutines::x86::blocking_lock_entry());
    __ add_oopmap( __ rel_pc(), _lock_oopmap);

    DebugScopeBuilder *dbg = new DebugScopeBuilder( NULL, method->objectId(), 0, 0, 1, -1 );
    dbg->add_vreg(DebugInfoBuilder::JLock, 0, VReg::stk2reg(lock_off), true);
    __ add_dbg( __ rel_pc(), dbg);
    //__ untested("oop map is screwed and lots of live registers");
    // R10: saved R09
__ bind(fast_locked);
    __ verify_oopmap (_lock_oopmap, MacroAssembler::OopVerify_OopMap_PostCall);
    if( max_int_word_count >= 6 ) __ move8(R09,R10);
  }

  // Use avm Env for local calls or if not proxy build; proxy Env for remote calls
  // RDI is the 1st outgoing C argument register
  Register R10_THR=R10;
  __ getthr(R10_THR);
  __ lea   (RDI, R10_THR, in_bytes(JavaThread::jni_environment_offset()));

  // At this point, synchronized methods have been locked, arguments have been
  // copied off of stack into their JNI positions.  Oops are boxed in-place on
  // the stack, with handles copied to arguments.

  // reset handle block
  __ ld8  (RAX, R10_THR, in_bytes(JavaThread::active_handles_offset()) );
  __ mov8i(RBX,0);
  __ st8  (RAX, JNIHandleBlock::top_offset_in_bytes(), RBX);

  if( TickProfiler::is_profiling() ) { // Set the VM tag to the "current" PC so that we can identify and attribute time spent in native methods.
    __ ld8 (RBX, R10_THR, in_bytes(Thread::vm_tag_offset())); // preserve tag in callee-saved register
    Label thepc(_masm);
    __ st8i(R10_THR, in_bytes(Thread::vm_tag_offset()), thepc);// Plop down a 'contains()' PC for lookups.
  }

address adr=0;
  if( is_remote ) {
#ifdef AZ_PROXIED
    switch (ret_type) {
    case T_VOID:
      adr = (address)remoteJNI::call_void_remotejni_method;
      break;
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
    case T_SHORT:
    case T_INT:
      adr = (address)remoteJNI::call_jint_remotejni_method;
      break;
    case T_OBJECT:
    case T_ARRAY:
      adr = (address)remoteJNI::call_jobject_remotejni_method;
      break;
    case T_FLOAT:
      adr = (address)remoteJNI::call_jfloat_remotejni_method;
      break;
    case T_LONG:
      adr = (address)remoteJNI::call_jlong_remotejni_method;
      break;
    case T_DOUBLE:
      adr = (address)remoteJNI::call_jdouble_remotejni_method;
      break;
    default:
      ShouldNotReachHere();
    }
#endif // AZ_PROXIED
  }
  else {
    adr = method->native_function();
  }
assert(adr,"native lookup already happened");

  // For calls that may call functions that use varargs or stdargs (prototype-less
  // calls or calls to functions containing ellipsis (. . . ) in the declaration)
  // %al is used as hidden argument to specify the number of vector registers used.
  // The contents of %al do not need to match exactly the number of registers, but
  // must be an upper bound on the number of vector registers used and is in the
  // range 0-8 inclusive.
  //
  // RAX == 0 bombs?  Someone downstream depends upon this flag value...
  // RAX seems to require min(8, max(outgoing_c_args, outgoing_flt_args))
  if (outgoing_flt_args > outgoing_c_args) {  outgoing_c_args = outgoing_flt_args; }
  if (outgoing_c_args < 8) {
assert(outgoing_c_args>1,"invalid count for outgoing args in native wrapper");
    __ mov8i(RAX, outgoing_c_args);
  } else {
    __ mov8i(RAX, 8);
  }

  OopMap2 *_nativecall_oopmap = new OopMap2();
for(int i=0;i<oop_offs.length();i++){
    _nativecall_oopmap->add( VOopReg::stk2reg(oop_offs.at(i)) );
  }

  // Now do the call.
  __ verify_oopmap(_nativecall_oopmap, MacroAssembler::OopVerify_OopMap_PreCall);
  int thepc = __ call_native(CAST_FROM_FN_PTR(address, adr), R10_THR, in_bytes(JavaThread::jvm_lock_offset()), is_remote ? NULL : &nativefuncaddrpc);

  // Record oopmaps
  __ add_oopmap( thepc, _nativecall_oopmap);

  if( method->is_synchronized() ) {
    DebugScopeBuilder *dbg = new DebugScopeBuilder( NULL, method->objectId(), 0, 0, 1, -1 );
    dbg->add_vreg(DebugInfoBuilder::JLock, 0, VReg::stk2reg(lock_off), true);
    __ add_dbg(thepc, dbg);
  }

  Register R09_THR=R09;
  __ getthr(R09_THR);
  if( TickProfiler::is_profiling() ) {
    // Restore the original tag in the thread.
    __ st8(R09_THR, in_bytes(Thread::vm_tag_offset()), RBX);
  }

  if( ret_type != T_VOID ) {     // Any results to save across VM calls?
    __ move8(RBX,RAX);          // Move results into callee-save register
  }
  // Results are now sitting in RBX (or XMM0).  Leave 'em there during any
  // blocking or unlocking calls.  Note that since this was a native call, RBX
  // cannot be holding an oop hence is GC-safe (very likely RBX is a Handle).

  // CAS to retake the JVM lock.  If this fails, we call back into the VM to
  // poll and possibly block.
Label no_block;
#ifndef PRODUCT
  if (!WalkStackALot && !SafepointALot) { // These flags make us go slow always
#endif
Label go_slow;
    // If we find our own SP in the JVM lock, the lock is free.  Take it by
    // plunking down a 0.  
    __ ld8  (RAX, R09_THR, in_bytes(JavaThread::jvm_lock_offset()));
    __ testi(RAX,3);            // Fail if locked by VM or polling-advisory set
    __ jne  (go_slow);          // Found a reason to fail
    __ mov8i(RCX,0);            // No immediate-form for CAS so zero in a register
    __ locked()->cas8(R09_THR, in_bytes(JavaThread::jvm_lock_offset()), RCX);
    __ jeq  (no_block);
    // Come here for either contention, or a real reason to block
__ bind(go_slow);
#ifndef PRODUCT
  }
#endif
  
  // Block.  Use a leaf call to leave the last_Java_frame setup undisturbed -
  // stack crawlers will believe we're still in the native call with it's
  // OopMap.  We might think about unlocking before blocking, except we do not
  // own the JVM lock and so we cannot muck with the object header.
__ call_VM_leaf(CAST_FROM_FN_PTR(address,JavaThread::jvm_lock_self_or_suspend_static_nat),R09_THR);
  __ getthr(R09_THR);


  // Check and handle exceptions for non-sync methods in the jvm_lock
  // slow-path.  This means the fast-path doesn't have to.  Sync methods need
  // to unlock before testing & throwing the exception.
if(!method()->is_synchronized()){
Label no_exception;
__ null_chk(R09_THR,in_bytes(JavaThread::pending_exception_offset()),no_exception);
    // Since this is a native call, we *know* the proper exception handler:
    // it's the empty function.  Just pop frame & jump.
    __ ld8  (RBX, RSP, (stack_size_in_words-2)*wordSize); // restore rbx
    __ add8i(RSP,      (stack_size_in_words-1)*wordSize); // pop frame
    __ jmp  (StubRoutines::forward_exception_entry());
__ bind(no_exception);
  }

  // Come here with the JVM lock being held and polling accomplished.
  // We can now hold Oops in registers.
  __ bind(no_block);
  __ verify_oopmap(_nativecall_oopmap, MacroAssembler::OopVerify_OopMap_PostCall);
#if defined(ASSERT)
if(!method()->is_synchronized()){
Label missed_PAS_bit;
__ null_chk(R09_THR,in_bytes(JavaThread::pending_exception_offset()),missed_PAS_bit);
__ should_not_reach_here("have pending ex AND CAS succeeded, PAS not set?");
__ bind(missed_PAS_bit);
  }
#endif // defined(ASSERT)

  // Unlock a synchronized native
  // R09 - thread
  // RBX - return result
  if (method()->is_synchronized()) {
    Label slow_path;
    __ ld8  (R10,RSP,lock_off);
    __ fast_path_unlock(RInOuts::a,R10,RCX,RAX,slow_path);

    Label no_exception, check_ex;
__ bind(check_ex);
__ null_chk(R09_THR,in_bytes(JavaThread::pending_exception_offset()),no_exception);
    // Since this is a native call, we *know* the proper exception handler:

    // it's the empty function.  Just pop frame & jump.
    __ ld8  (RBX, RSP, (stack_size_in_words-2)*wordSize); // restore rbx
    __ add8i(RSP,      (stack_size_in_words-1)*wordSize); // pop frame
    __ jmp  (StubRoutines::forward_exception_entry());

    __ bind (slow_path);
    __ call (StubRoutines::unlock_entry()); // Crushes R09
    __ getthr(R09_THR);         // Reload R09
__ jmp(check_ex);
__ bind(no_exception);
  }

  // jvmti/jvmpi support
  if (JvmtiExport::can_post_interpreter_events()) {
    Label L;
    __ null_chk(R09_THR, in_bytes(JavaThread::interp_only_mode_offset()),L);
__ unimplemented("jvmti following native");
//    __ move (R12, R10);
//    address thepc = __ call_VM_ex( CAST_FROM_FN_PTR(address, SharedRuntime::post_method_exit), R11 );
//    // empty oopmap -- nothing live past this point, except the possible oop result,
//    // which is in a handle
//    OopMap* themap = new OopMap(0, 0);
//    oop_maps->add_gc_map( thepc - start, themap);  // OopMap setup    
    __ bind(L);
  }

  // Unpack native results.
  switch( ret_type ) {
  case T_BYTE:  __ movsx81(RAX,RBX); break; // Sign-extend 1->4 (NB use movsx8 as per Intel compiler coding rule 68)
  case T_CHAR:  __ movzx42(RAX,RBX); break; // Zero-extend 2->4
  case T_INT:   __ move4  (RAX,RBX); break; // Nothing to do!  Zero extension is the ABI
  case T_LONG:  __ move8  (RAX,RBX); break; // Nothing to do!
  case T_SHORT: __ movsx82(RAX,RBX); break; // Sign-extend 2->4 (NB use movsx8 as per Intel compiler coding rule 68)
  case T_VOID:                       break; // Nothing to do!
  case T_DOUBLE:                     break; // It's in XMM0, not RAX
case T_FLOAT:break;//It's in XMM0, not RAX
  case T_BOOLEAN: {
    __ mov8i (RAX,0);
    __ test4i(RBX,0xff);        // Test only the low byte: x86 ABI leaves other bits as crap
    __ setnz (RAX);
    break;
  }
  case T_OBJECT:                // Really a handle
  case T_ARRAY: {
    Label on_stack, done, weak_handle;
    __ mov8i(RAX,0);            // Set up for null result
    __ test8(RBX,RBX);          // Skip de-ref of NULL handles
    __ jeq  (done);

    assert0(is_int8(JNIHandles::JNIWeakHandleTag));
    __ test1i(RBX,JNIHandles::JNIWeakHandleTag);
    __ jnz   (weak_handle);

    // Fast path resolve for strong JNIHandles
    // NB oops in handle blocks are poisoned, oops on the stack are not..
    assert0(__THREAD_STACK_REGION_START_ADDR__ == (1L<<AZUL_THREAD_STACKS_BASE_SHIFT));
    Register RDI_TMP = RDI;
    __ move8(RDI_TMP, RBX);
    __ shr8i(RDI_TMP, AZUL_THREAD_STACKS_BASE_SHIFT);
    __ jnz  (on_stack);
    __ ldref_lvb(RInOuts::a,RAX,RDI_TMP,RKeepIns::a,RBX,0,true); // oop in handle block, do full lvb
    __ jmp  (done);
__ bind(on_stack);
    __ ld8  (RAX, RBX, 0); // grab value out of stack location
    __ jmp  (done);

__ bind(weak_handle);//Slow path call to resolve weak JNIHandles
__ call_VM_leaf(CAST_FROM_FN_PTR(address,JNIHandles::resolve_non_null_weak),RBX);

    __ bind (done);
    __ verify_oop(RAX, MacroAssembler::OopVerify_ReturnValue);
    break;
  }
  default:
    ShouldNotReachHere();
  }

  // If we passed an OOP in RDI, then return it.
  if( total_args_passed > 0 && (sig_bt[0] == T_OBJECT || sig_bt[0] == T_ARRAY) ) {
    __ ld8  (RDI, RSP, lock_off); // Squirreled away on the stack here
    __ verify_oop(RDI, MacroAssembler::OopVerify_OutgoingArgument);
  }

  // SBA: reset handle block
  __ ld8  (RCX, R09_THR, in_bytes(JavaThread::active_handles_offset()) );
  __ mov8i(RDX,0);
  __ st8  (RCX, JNIHandleBlock::top_offset_in_bytes(), RDX);

  // Restore registers & exit
  __ ld8  (RBX, RSP, (stack_size_in_words-2)*wordSize);
  __ add8i(RSP,      (stack_size_in_words-1)*wordSize);
__ ret();

  // Force branches to resolve before we know where the oopmap is
__ patch_branches();

  // Record the location of the patchable native_function address in the wrapper.
  if ( is_remote ) { // address is encoded as an immediate in the instruction stream
    method->store_native_function_address_address_in_wrapper((address)nativefuncaddrpc.abs_pc(_masm));
  }
  else { // address is that of the immediate in the "mov imm, R11"
    method->store_native_function_address_address_in_wrapper((address)nativefuncaddrpc.abs_pc(_masm)+2);
  }

  // Return framesize_bytes.  Include the X86 return address.
  return stack_size_in_words*wordSize;
}


