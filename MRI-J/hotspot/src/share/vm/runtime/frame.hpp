/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef FRAME_HPP
#define FRAME_HPP

#include "allocation.hpp"
#include "codeCache.hpp"
#include "constMethodOop.hpp"
#include "handles.hpp"
#include "objectRef_pd.hpp"
#include "register_pd.hpp"
#include "vreg.hpp"

class DebugScope;
class JavaCallWrapper;
class RegMap;
class constantPoolCacheRef;
class methodRef;
struct IFrame;

// A frame represents a physical stack frame (an activation).  Frames
// can be C or Java frames, and the Java frames can be interpreted or
// compiled.  In contrast, vframes represent source-level activations,
// so that one physical frame can correspond to multiple source level
// frames because of inlining.

class frame VALUE_OBJ_CLASS_SPEC {
 private:
  // Instance variables:
  intptr_t* _sp; // stack pointer (from Thread::last_Java_sp)
  address   _pc; // program counter (the next instruction after the call)

 public:
  // Every frame needs to return a unique id which distinguishes it from all
  // other frames.  For sparc and ia32 use SP.  ia64 can have memory frames
  // that are empty so multiple frames will have identical SP values.  For
  // ia64 the BSP (FP) value will serve.  No real frame should have an id() of
  // NULL so it is a distinguishing value for an unmatchable frame. 
  intptr_t* id(void) const;

  // type testers
  inline bool is_interpreted_frame() const; // returns to interpreter
  inline bool is_entry_frame()       const; // returns into VM
  inline bool is_native_frame()      const; // returns into a native wrapper
  inline bool is_runtime_frame()     const; // returns into a runtime_stub
  inline bool is_compiled_frame()    const; // returns into JITd C1 or C2 code
  inline bool is_c1_frame()          const; // returns into JITd C1       code
  inline bool is_java_frame()        const; // interpreted || compiled
  inline bool is_known_frame()       const; // native || interpreted || compiled
  inline bool is_first_frame()       const; // oldest frame? (has no sender).  Orthogonal to other testers?

  // returns the sending frame
frame sender_entry_frame()const;
frame pd_sender()const;
frame pd_sender_robust()const;
  frame sender()        const { return is_entry_frame() ? sender_entry_frame() : pd_sender(); }
  frame sender_robust() const { return is_entry_frame() ? sender_entry_frame() : pd_sender_robust(); }

  // returns the the sending Java frame, skipping any intermediate C frames 
  // NB: receiver must not be first frame
  frame java_sender() const;

  // Interpreter frames:

 private:
  inline intptr_t* interpreter_frame_locals_base() const;
  inline uint16_t* interpreter_frame_bci_addr() const;

 public:
  // Locals

  // The _at version returns a pointer because the address is used for GC.
  inline intptr_t* interpreter_frame_local_addr(int index) const { return &interpreter_frame_locals_base()[index]; }
  inline intptr_t  interpreter_frame_local_at  (int index) const { return  interpreter_frame_locals_base()[index]; }
  inline void  set_interpreter_frame_local_at(int index, intptr_t val) { interpreter_frame_locals_base()[index] = val; }

  // byte code index
  inline int  interpreter_frame_bci    (       ) const { return *interpreter_frame_bci_addr();}
  inline void interpreter_frame_set_bci(int bci) const { *interpreter_frame_bci_addr() = bci; }
  inline address interpreter_frame_bcp() const;

  // Find receiver for an invoke when arguments are just pushed on stack (i.e., callee stack-frame is 
  // not setup)
objectRef interpreter_callee_receiver_ref(symbolHandle signature){return*interpreter_callee_receiver_addr(signature);}

  objectRef *interpreter_callee_receiver_addr(symbolHandle signature);


 public:
  // Top of expression stack.  This stuff is very machine-specific.
  void interpreter_empty_expression_stack();
  // Used to grab the 'receiver' or 'this' during call resolving
  inline intptr_t* interpreter_frame_tos_at(jint offset) const;

  int interpreter_frame_monitor_count() const; // count of monitors in this frame

  // Return/result value from this interpreter frame
  // If the method return type is T_OBJECT or T_ARRAY populates oop_result
  // For other (non-T_VOID) the appropriate field in the jvalue is populated
  // with the result value.
  // Should only be called when at method exit when the method is not 
  // exiting due to an exception.
BasicType interpreter_frame_result(oop*oop_result,jvalue*value_result,uint64_t return_value);

 public:
  // Method & constant pool cache
  methodOop interpreter_frame_method() const;
inline methodRef interpreter_frame_methodRef()const{return*interpreter_frame_method_addr();}
inline methodRef*interpreter_frame_method_addr()const;
inline constantPoolCacheRef*interpreter_frame_cache_addr()const;

  // Entry frames
  JavaCallWrapper* entry_frame_call_wrapper() const;
  intptr_t* entry_frame_argument_at(int offset) const;

  // tells whether there is another chunk of Delta stack above
  bool entry_frame_is_first() const;

  // For debugging
  void print_value() const { print_value_on(tty,NULL); }
  void print_value_on(outputStream* st, JavaThread *thread) const;
  void print_on(outputStream* st) const;
  void print_on_error(outputStream* st, char* buf, int buflen, bool verbose = false) const;  
  void ps(int depth=1) const; // Print stack with intelligence
  void ps(JavaThread *thread, int depth=1) const; // Print stack with intelligence
  void pd_ps(JavaThread *thread, const DebugScope *scope, methodOop moop) const;
  const char* debug_discovery(CodeBlob **cb, const DebugScope **scope, methodOop *moop) const;

  // If 'reg' refers to a machine register, look it up using the pd-specific
  // (hardware-window-aware) register finder.  Otherwise 'reg' refers to a
  // stack-slot.  Add in any frame-adjust (e.g. caused by C2I adapters mucking
  // with a compiled-frame size) and return the address of the stack-slot.
  inline intptr_t*  reg_to_addr    (VReg::VR reg) const;
  inline objectRef* reg_to_addr_oop(VOopReg::VR reg) const;
private:
  friend class RegMap;
  // Used to find oop registers in the youngest stub frame only
  intptr_t *pd_reg_to_addr(VReg::VR reg) const;

  // Iteration of methodCodes
  void methodCodes_code_blob_do();    
 public:
  // Memory management
void oops_do(JavaThread*thread,OopClosure*f);
void oops_arguments_do(OopClosure*f);//Include argument oops
  void oops_interpreted_do(JavaThread* thread, OopClosure* f);  
  void methodCodes_do();

  void gc_prologue(JavaThread *thr);
  void GPGC_gc_prologue(JavaThread *thr, OopClosure* f);
  void GPGC_mutator_gc_prologue(JavaThread *thr, OopClosure* f);
  void gc_epilogue(JavaThread *thr);

  // biased-lock support: count times this object is locked in this frame.
  int locked( oop o ) const;

  // Verification
void verify(JavaThread*thread);
  static bool verify_return_pc(address x);
  // Usage:
  // assert(frame::verify_return_pc(return_address), "must be a return pc");

#include "frame_pd.hpp"
};


//
// StackFrameStream iterates through the frames of a thread starting from
// top most frame. It automatically takes care of updating the location of
// all (callee-saved) registers. Notice: If a thread is stopped at
// a safepoint, all registers are saved, not only the callee-saved ones.
//
// Use:
//   
//   for(StackFrameStream fst(thread); !fst.is_done(); fst.next()) {
//     ...
//   }
//
class StackFrameStream : public StackObj {
private:
  frame       _fr;
  bool        _is_done;  
public:
StackFrameStream(JavaThread*thread);
  // Iteration
  bool is_done()                  { return (_is_done) ? true : (_is_done = _fr.is_first_frame(), false); }
void next(){if(!_is_done)_fr=_fr.sender();}
  // Query
  frame *current()                { return &_fr; }  
};

#endif // FRAME_HPP
