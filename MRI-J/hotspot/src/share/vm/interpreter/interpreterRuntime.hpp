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
#ifndef INTERPRETERRUNTIME_HPP
#define INTERPRETERRUNTIME_HPP

#include "assembler_pd.hpp"
#include "bytecodes.hpp"
#include "bytes_pd.hpp"
#include "cpCacheOop.hpp"
#include "frame.hpp"
#include "handles.hpp"
#include "methodCodeOop.hpp"
#include "methodOop.hpp"
#include "register_pd.hpp"
#include "thread.hpp"
#include "vreg.hpp"
class InterpreterMacroAssembler;

// The InterpreterRuntime is called by the interpreter for everything
// that cannot/should not be dealt with in assembly and needs C support.

class InterpreterRuntime: AllStatic {
  friend class BytecodeClosure; // for method and bcp
  friend class PrintingClosure; // for method and bcp

 private:
  // Helper functions to access current interpreter state
  static frame     last_frame(JavaThread *thread)    { return thread->last_frame(); }  
  static methodOop method(JavaThread *thread)        { return last_frame(thread).interpreter_frame_method(); }
  static address   bcp(JavaThread *thread)           { return last_frame(thread).interpreter_frame_bcp(); }
  static int       get_bci(JavaThread *thread)       { return last_frame(thread).interpreter_frame_bci(); }
  static Bytecodes::Code code(JavaThread *thread)       { return Bytecodes::code_at(bcp(thread), method(thread)); }
  static bool      already_resolved(JavaThread *thread) { return cache_entry(thread)->is_resolved(code(thread)); }
  static int       one_byte_index(JavaThread *thread)   { return bcp(thread)[1]; }
  static int       two_byte_index(JavaThread *thread)   { return Bytes::get_Java_u2(bcp(thread) + 1); }
  static int       number_of_dimensions(JavaThread *thread)  { return bcp(thread)[3]; }
  static ConstantPoolCacheEntry* cache_entry(JavaThread *thread)  { return method(thread)->constants()->cache()->entry_at(Bytes::get_native_u2(bcp(thread) + 1)); }
static void note_trap(JavaThread*thread,int reason);

 public:
  // Constants
static objectRef ldc(JavaThread*thread,bool wide);

  // Allocation
static objectRef _new(JavaThread*thread,constantPoolRef pool,int index,jint length);
static objectRef anewarray(JavaThread*thread,constantPoolRef pool,int index,jint length);
static objectRef multianewarray(JavaThread*thread,jint*first_size_address);
static void register_finalizer(JavaThread*thread,objectRef obj);

  // Quicken instance-of and check-cast bytecodes
static objectRef quicken_io_cc(JavaThread*thread);

  // Exceptions thrown by the interpreter
  static void    throw_AbstractMethodError(JavaThread* thread);
  static void    throw_IncompatibleClassChangeError(JavaThread* thread);
  static void    throw_ArrayIndexOutOfBoundsException(JavaThread* thread, char* name, jint index);
static void throw_ClassCastException(JavaThread*thread,objectRef obj);
static objectRef create_exception(JavaThread*thread,char*name,char*message);
static objectRef create_klass_exception(JavaThread*thread,char*name,objectRef obj);
static address find_exception_handler(JavaThread*thread);

  // Statics & fields
  static void    resolve_get_put(JavaThread* thread, Bytecodes::Code bytecode);  
  
  // Synchronization
  //static void    monitorexit (JavaThread* thread, BasicObjectLock* elem);
  
static void throw_IllegalMonitorStateException(JavaThread*thread);
static void new_IllegalMonitorStateException(JavaThread*thread);

  // Calls
  static void    resolve_invoke     (JavaThread* thread, Bytecodes::Code bytecode);
  static void    create_native_wrapper(JavaThread* thread, methodRef method);
  static const char *_native_flat_sig;

  // Breakpoints
static void _breakpoint(JavaThread*thread,methodRef method,int bci);
static Bytecodes::Code get_original_bytecode_at(JavaThread*thread,methodRef method,int bci);
static void set_original_bytecode_at(JavaThread*thread,methodRef method,int bci,Bytecodes::Code new_code);
  static bool is_breakpoint(JavaThread *thread) { return Bytecodes::code_or_bp_at(bcp(thread)) == Bytecodes::_breakpoint; }

  // Safepoints
  static void    at_safepoint(JavaThread* thread);

  // Debugger support
static void post_field_access(JavaThread*thread,objectRef obj,
    ConstantPoolCacheEntry *cp_entry);
static void post_field_modification(JavaThread*thread,objectRef obj,
ConstantPoolCacheEntry*cp_entry,uint64_t*value_ptr);
  static void post_method_entry(JavaThread *thread);
  static void post_method_exit (JavaThread *thread);
  static int  interpreter_contains(address pc);

  // Native signature handlers
  static void prepare_native_call(JavaThread* thread, methodOopDesc* method);
  static address slow_signature_handler(JavaThread* thread, 
                                        methodOopDesc* method, 
                                        intptr_t* from, intptr_t* to);

  // Platform dependent stuff
#include "interpreterRT_pd.hpp"

  // Interpreter's frequency counter overflow
  static address frequency_counter_overflow(JavaThread* thread, bool is_osr);
};

// ---------------------------------------------------------------------------
// Implementation of AdapterHandlerLibrary
//
// This library manages argument marshaling adapters and native wrappers.
// There are 2 flavors of adapters: I2C and C2I.
//
// The I2C flavor takes a stock interpreted call setup, marshals the arguments
// for a Java-compiled call, and jumps to Rmethod-> code()->
// instructions_begin().  It is broken to call it without an nmethod assigned.
// The usual behavior is to lift any register arguments up out of the stack
// and possibly re-pack the extra arguments to be contigious.  I2C adapters
// will save what the interpreter's stack pointer will be after arguments are
// popped, then adjust the interpreter's frame size to force alignment and
// possibly to repack the arguments.  After re-packing, it jumps to the
// compiled code start.  There are no safepoints in this adapter code and a GC
// cannot happen while marshaling is in progress.
//
// The C2I flavor takes a stock compiled call setup plus the target method in
// Rmethod, marshals the arguments for an interpreted call and jumps to
// Rmethod->_i2i_entry.  On entry, the interpreted frame has not yet been
// setup.  Compiled frames are fixed-size and the args are likely not in the
// right place.  Hence all the args will likely be copied into the
// interpreter's frame, forcing that frame to grow.  The compiled frame's
// outgoing stack args will be dead after the copy.
//
// Native wrappers, like adapters, marshal arguments.  Unlike adapters they
// also perform an offical frame push & pop.  They have a call to the native
// routine in their middles and end in a return (instead of ending in a jump).
// The native wrappers are stored in real nmethods instead of the AdapterBlobs
// used by the adapters.  The code generation happens here because it's very
// similar to what the adapters have to do.

class AdapterHandlerLibrary:public AllStatic{
public:
  struct AdapterHandler {
    AdapterHandler *_next;	// Next adapter w/same general features
    address _i2c, _c2i;		// address of the code starts
    int _codesize;              // Size of both code chunks.
    const char *_flatsig;	// Flattened sig as a C string
  };
  enum { size = 5120 };		// the size of the temporary code buffer, to hold 256 args
private:
  static AdapterHandler *_adapters[11/*0-9 parms, and 10+ parms*/*2/*max_stack==0*/];
public:
  static AdapterHandler *get_create_adapter(methodOop method);
static methodCodeRef create_native_wrapper(methodHandle method,TRAPS);
};

#endif // INTERPRETERRUNTIME_HPP
