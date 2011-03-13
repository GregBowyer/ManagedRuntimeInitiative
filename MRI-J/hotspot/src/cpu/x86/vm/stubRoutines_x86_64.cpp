/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_Runtime1.hpp"
#include "codeCache.hpp"
#include "gcLocker.hpp"
#include "interfaceSupport.hpp"
#include "interpreter.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "synchronizer.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "handles.inline.hpp"
#include "markWord.inline.hpp"
#include "os_os.inline.hpp"

// Implementation of the platform-specific part of StubRoutines - for
// a description of how to extend it, see the stubRoutines.hpp file.

address StubRoutines::x86::_handler_for_null_ptr_exception_entry=NULL;
address StubRoutines::x86::_handler_for_array_index_check_entry=NULL;

address StubRoutines::x86::_handler_for_nio_protection_entry=NULL;
address StubRoutines::x86::_handler_for_GCLdValueTr_entry=NULL;
address StubRoutines::x86::_handler_for_GCStValueNMTTr_entry=NULL;
address StubRoutines::x86::_handler_for_GCThread_GCLdValueTr_entry=NULL;

address StubRoutines::x86::_partial_subtype_check=NULL;
address StubRoutines::x86::_full_subtype_check=NULL;

address StubRoutines::x86::_prim_arraycopy1=NULL;
address StubRoutines::x86::_prim_arraycopy2=NULL;
address StubRoutines::x86::_prim_arraycopy4=NULL;
address StubRoutines::x86::_prim_arraycopy8=NULL;
address StubRoutines::x86::_prim_arraycopy1_no_overlap=NULL;
address StubRoutines::x86::_prim_arraycopy2_no_overlap=NULL;
address StubRoutines::x86::_prim_arraycopy4_no_overlap=NULL;
address StubRoutines::x86::_prim_arraycopy8_no_overlap=NULL;
address StubRoutines::x86::_arraycopy_a = NULL;  // oop    aligned data

address StubRoutines::x86::_c1_profile_callee = NULL;  // wrapper for unaligned recording of mem-op data

address StubRoutines::x86::_sba_escape_handler=NULL;

address StubRoutines::x86::_c2_lock_entry=NULL;
address StubRoutines::x86::_blocking_lock_entry=NULL;

//============================================================================
JRT_ENTRY_NO_GC_ON_EXIT(address, StubRoutines, find_NPE_continuation_address, (JavaThread *thread, address faulting_pc))
  // The continuation address is the entry point of the exception handler of the
  // previous frame depending on the return address.
  // Sometimes no OopMap is handy.
  SharedRuntime::_throw_null_ctr++; // implicit null throw

  // Handle the interpreter first
  if (TemplateInterpreter::contains(faulting_pc)) {
    return TemplateInterpreter::throw_NullPointerException_entry();
  }
  // Fault should now be either in an methodCode or a vtable stub
CodeBlob*cb=CodeCache::find_blob(faulting_pc);
  
if(cb->is_vtable_stub()){
    address entry = cb->code_begins() + 8; // 8 byte hole for next blob ptr
    address last_entry = entry;
    while( entry <  faulting_pc ) {
      int vstublen = ((int16_t*)entry)[0];
      assert0( vstublen > 0 );
last_entry=entry;
      entry += vstublen; // skip to next stub
    }
address vtableStubBegins=(address)round_to((intptr_t)(last_entry+4),CodeEntryAlignment);
    int offset = faulting_pc - vtableStubBegins; // offset relative to beginning of vtable stub
#ifdef ASSERT
    // We do allocation here and a GC is possible.  Force it.
    InterfaceSupport::check_gc_alot();
#endif
    // Create an exception with a real stack crawl.  Then throw it.  Since we
    // are at an inline-cache, we are at a GC point and it is OK to allocate.
    if( offset == vtable_ame_offset  ||
        offset == itable_ame_offset  ||
        offset == itable_ame_offset0 ||
        offset == itable_ame_offsetB ) {
      THROW_(vmSymbols::java_lang_AbstractMethodError(), StubRoutines::forward_exception_entry());
    } else {
      assert0( offset == vtable_npe_offset || 
	       offset == itable_npe_offset );
      THROW_(vmSymbols::java_lang_NullPointerException(),StubRoutines::forward_exception_entry());
    }
    ShouldNotReachHere();
  }
  // Else it is a plain load/store.
  int rel_pc = faulting_pc - (address)cb;

  // Now use the faulting pc to find the code blob
#ifdef ASSERT
  if (cb != NULL && !cb->is_java_method()) {
warning("Unexpected non-Java code blob found for faulting pc %p:",faulting_pc);
cb->decode();
  }
#endif
guarantee(cb!=NULL&&cb->is_java_method(),"Java exception being thrown in non Java code.");
  int handler_pc = cb->owner().as_methodCodeOop()->_NPEinfo->get(rel_pc);
#ifdef ASSERT
  if( handler_pc == NO_MAPPING ) {
    ResourceMark rm(thread);
    ResetNoHandleMark rnhm;
tty->print_cr("implicit exception happened at "PTR_FORMAT,faulting_pc);
cb->print_on(tty);
cb->decode();
    guarantee(handler_pc != NO_MAPPING, "null exception in compiled code");
  }
#endif
  return (address)cb + handler_pc;

JRT_END
