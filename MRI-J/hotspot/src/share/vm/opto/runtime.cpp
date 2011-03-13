/*
 * Copyright 1998-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "arrayKlass.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "deoptimization.hpp"
#include "interfaceSupport.hpp"
#include "methodCodeOop.hpp"
#include "oop.hpp"
#include "refsHierarchy_pd.hpp"
#include "runtime.hpp"
#include "thread.hpp"
#include "type.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
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
#include "os_os.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

//=============================================================================
// Opto compiler runtime routines
//=============================================================================

static objectRef multi_allocate(JavaThread *thread, klassRef elem_type, int dim, intptr_t sba_hint, jint *dims) {
  assert(elem_type.as_oop()->is_klass(), "not a class");
  oop o = arrayKlass::cast(elem_type.as_klassOop())->multi_allocate(dim, dims, sba_hint, thread);
assert(thread->last_frame().is_compiled_frame(),"not being called from compiled-like code");
  if( !thread->has_pending_exception() ) 
return objectRef(o);
  // Throwing?  Likely it's an OutOfMemory
  // Must deoptimize with throw instead of returning
frame fr=thread->last_frame();
CodeBlob*cb=CodeCache::find_blob(fr.pc());
  cb->owner().as_methodCodeOop()->deoptimize_now(Deoptimization::Reason_install_async);
  return nullRef; // Exception throwing, so no return value
}

// multianewarray for 1 dimensions
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, OptoRuntime, multianewarray1_Java, (JavaThread *thread, klassRef elem_type, int len1))
  GET_RPC;
jint dims[1];
  dims[0] = len1;
  return multi_allocate(thread, elem_type, 1, RPC, dims);
JRT_END

// multianewarray for 2 dimensions
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, OptoRuntime, multianewarray2_Java, (JavaThread *thread, klassRef elem_type, int len1, int len2))
  GET_RPC;
  jint dims[2];
  dims[0] = len1;
  dims[1] = len2;
  return multi_allocate(thread, elem_type, 2, RPC, dims);
JRT_END

// multianewarray for 3 dimensions
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, OptoRuntime, multianewarray3_Java, (JavaThread *thread, klassRef elem_type, int len1, int len2, int len3))
  GET_RPC;
  jint dims[3];
  dims[0] = len1;
  dims[1] = len2;
  dims[2] = len3;
  return multi_allocate(thread, elem_type, 3, RPC, dims);
JRT_END

// multianewarray for 4 dimensions
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, OptoRuntime, multianewarray4_Java, (JavaThread *thread, klassRef elem_type, int len1, int len2, int len3, int len4))
  GET_RPC;
  jint dims[4];
  dims[0] = len1;
  dims[1] = len2;
  dims[2] = len3;
  dims[3] = len4;
  return multi_allocate(thread, elem_type, 4, RPC, dims);
JRT_END

// multianewarray for 5 dimensions
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, OptoRuntime, multianewarray5_Java, (JavaThread *thread, klassRef elem_type, int len1, int len2, int len3, int len4, int len5))
  GET_RPC;
  jint dims[5];
  dims[0] = len1;
  dims[1] = len2;
  dims[2] = len3;
  dims[3] = len4;
  dims[4] = len5;
  return multi_allocate(thread, elem_type, 5, RPC, dims);
JRT_END

//-----------------------------------------------------------------------------
const TypeFunc *OptoRuntime::multianewarray_Type(int ndim, const TypeAryPtr *arr) {
  // create input type (domain)
const Type**fields=TypeTuple::fields(ndim+2);
fields[TypeFunc::Parms+0]=TypeRawPtr::BOTTOM;//Thread
fields[TypeFunc::Parms+1]=TypeInstPtr::NOTNULL;//element klass
for(int i=0;i<ndim;i++)
    fields[TypeFunc::Parms + 2 + i] = TypeInt::INT; // array size
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+2+ndim, fields);

  // create result type (range)
  fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=arr;//Returned oop
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+1, fields);

  return TypeFunc::make(domain, range);
}

//-----------------------------------------------------------------------------
// Used when failing a optimized exception-throw lookup.
const TypeFunc*OptoRuntime::forward_exception2_Type(){
  // create input type (domain)
  const Type **fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeInstPtr::NOTNULL; // Klass to be allocated
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);

  // create result type (range)
  fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeRawPtr::NOTNULL; // Returned oop

  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+1, fields);

  return TypeFunc::make(domain, range);
}

//-----------------------------------------------------------------------------
// Monitor Handling
const TypeFunc*OptoRuntime::complete_monitor_exit_Type(){
  // create input type (domain)
  const Type **fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=TypeInstPtr::NOTNULL;//Object to be un-Locked
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1,fields);

  // create result type (range)
  fields = TypeTuple::fields(0);

  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0,fields);

  return TypeFunc::make(domain,range);
}

const TypeFunc *OptoRuntime::uncommon_trap_Type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(1);
  // symbolOop name of class to be loaded
  fields[TypeFunc::Parms+0] = TypeInt::INT; 
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1, fields);

  // create result type (range)
  fields = TypeTuple::fields(0);
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0, fields);

  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::l2f_Type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = TypeLong::LONG;
  fields[TypeFunc::Parms+1] = Type::HALF;
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+2, fields);

  // create result type (range)
  fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = Type::FLOAT;
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+1, fields);

  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::modf_Type() {
  const Type **fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = Type::FLOAT;
  fields[TypeFunc::Parms+1] = Type::FLOAT;
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+2, fields);

  // create result type (range)
  fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = Type::FLOAT;

  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+1, fields);

  return TypeFunc::make(domain, range);
}

const TypeFunc *OptoRuntime::Math_D_D_Type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(2);
  // symbolOop name of class to be loaded
  fields[TypeFunc::Parms+0] = Type::DOUBLE; 
  fields[TypeFunc::Parms+1] = Type::HALF; 
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+2, fields);

  // create result type (range)
  fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = Type::DOUBLE;
  fields[TypeFunc::Parms+1] = Type::HALF;
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+2, fields);

  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::Math_DD_D_Type() {
  const Type **fields = TypeTuple::fields(4);
  fields[TypeFunc::Parms+0] = Type::DOUBLE;
  fields[TypeFunc::Parms+1] = Type::HALF;
  fields[TypeFunc::Parms+2] = Type::DOUBLE;
  fields[TypeFunc::Parms+3] = Type::HALF;
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+4, fields);

  // create result type (range)
  fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = Type::DOUBLE;
  fields[TypeFunc::Parms+1] = Type::HALF;
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+2, fields);

  return TypeFunc::make(domain, range);
}

//-------------- currentTimeMillis
const TypeFunc* OptoRuntime::current_time_millis_Type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(0);
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+0, fields);

  // create result type (range)
  fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = TypeLong::LONG;
  fields[TypeFunc::Parms+1] = Type::HALF;
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+2, fields);

  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::fast_arraycopy_Type() {
  // This signature is simple:  Two base pointers and a size_t.
  // create input type (domain)
  const Type** fields = TypeTuple::fields(4);
  int argp = TypeFunc::Parms;
  fields[argp++] = TypePtr::NOTNULL;    // src
  fields[argp++] = TypePtr::NOTNULL;    // dest
fields[argp++]=TypeLong::LONG;//size in whatevers (size_t)
fields[argp++]=Type::HALF;//other half of long length
const TypeTuple*domain=TypeTuple::make(TypeFunc::Parms+4,fields);

  // create result type if needed
  fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = NULL; // void
  const TypeTuple* range = TypeTuple::make(TypeFunc::Parms+0, fields);
  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::checkcast_arraycopy_Type() {
  // An extension of fast_arraycopy_Type which adds type checking.
  // Azul arg layout differs from Sun
const Type**fields=TypeTuple::fields(6);
  int argp = TypeFunc::Parms;
fields[argp++]=TypeInt::INT;//dest_index
fields[argp++]=TypePtr::NOTNULL;//derived-ptr src
fields[argp++]=TypeInt::INT;//elements to copy
fields[argp++]=TypeInt::INT;//supercheck offset
fields[argp++]=TypeInt::INT;//super_kid
fields[argp++]=TypePtr::NOTNULL;//dest array base
const TypeTuple*domain=TypeTuple::make(argp,fields);

  // create result type if needed
  fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=TypeInt::INT;//status result
  const TypeTuple* range = TypeTuple::make(TypeFunc::Parms+1, fields);
  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::slow_arraycopy_Type() {
  // This signature matches SharedRuntime::slow_arraycopy_C
  // create input type (domain)
const Type**fields=TypeTuple::fields(6);
  int argp = TypeFunc::Parms;
fields[argp++]=TypeRawPtr::NOTNULL;//thread
  fields[argp++] = TypePtr::NOTNULL;    // src
  fields[argp++] = TypeInt::INT;        // src_pos
  fields[argp++] = TypePtr::NOTNULL;    // dest
  fields[argp++] = TypeInt::INT;        // dest_pos
  fields[argp++] = TypeInt::INT;        // length
const TypeTuple*domain=TypeTuple::make(argp,fields);

  // create result type if needed
  fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = NULL; // void
  const TypeTuple* range = TypeTuple::make(TypeFunc::Parms+0, fields);
  return TypeFunc::make(domain, range);
}

const TypeFunc* OptoRuntime::generic_arraycopy_Type() {
  // This signature is like System.arraycopy, except that it returns status.
  // create input type (domain)
const Type**fields=TypeTuple::fields(5);
  int argp = TypeFunc::Parms;
  fields[argp++] = TypePtr::NOTNULL;    // src
  fields[argp++] = TypeInt::INT;        // src_pos
  fields[argp++] = TypePtr::NOTNULL;    // dest
  fields[argp++] = TypeInt::INT;        // dest_pos
  fields[argp++] = TypeInt::INT;        // length
const TypeTuple*domain=TypeTuple::make(argp,fields);

  // create result type if needed
  fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=TypeInt::INT;//status result
const TypeTuple*range=TypeTuple::make(TypeFunc::Parms+1,fields);
  return TypeFunc::make(domain, range);
}


//------------- Interpreter state access for on stack replacement
const TypeFunc* OptoRuntime::osr_end_Type() {
  // create input type (domain)
const Type**fields=TypeTuple::fields(3);
fields[TypeFunc::Parms+0]=TypeRawPtr::BOTTOM;//OSR temp buf
fields[TypeFunc::Parms+1]=TypeInt::INT;//max locals
fields[TypeFunc::Parms+2]=TypeInt::INT;//active monitors
const TypeTuple*domain=TypeTuple::make(TypeFunc::Parms+3,fields);

  // create result type
  fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=TypeInstPtr::NOTNULL;//locked oop
  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+1, fields);
  return TypeFunc::make(domain, range);  
}


const TypeFunc *OptoRuntime::register_finalizer_Type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(1);
  fields[TypeFunc::Parms+0] = TypeInstPtr::NOTNULL;  // oop;          Receiver
  // // The JavaThread* is passed to each routine as the last argument
  // fields[TypeFunc::Parms+1] = TypeRawPtr::NOTNULL;  // JavaThread *; Executing thread
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1,fields);

  // create result type (range)
  fields = TypeTuple::fields(0);

  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0,fields);

  return TypeFunc::make(domain,range);
}
