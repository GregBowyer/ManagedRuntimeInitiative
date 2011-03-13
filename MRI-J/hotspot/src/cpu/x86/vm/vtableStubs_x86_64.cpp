/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "assembler_pd.hpp"
#include "codeBlob.hpp"
#include "instanceKlass.hpp"
#include "methodOop.hpp"
#include "c1_Runtime1.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

#define __ masm->

int vtable_npe_offset = 0;      // filled in lazily
int vtable_ame_offset = 0;      // filled in lazily

// --- pd_create_vtable_stub -------------------------------------------------
// Create a vtable trampoline.  Called with possibly all argument registers
// and all callee-save registers busy, so can use only caller-save registers.
// On X86 this is RAX, R10 & R11.  Also on X86 the KID is supposed to be in
// RAX already (as part of the down-grade path from a caching Inline Cache to
// a vtable Inline Cache).
//
// We do not share vtable stubs.  Note that the tiny bit of code is identical
// between vtable stubs that share the same v-index so in theory sharing is
// possible (e.g. lazily make each stub as a particular v-index goes mega-
// morphic at some inline-cache site).  But the hardware predictions then get
// shared - splitting the vtable stubs per-inline-cache-site will cause some
// modest code growth in exchange for giving a private hardware prediction per
// call site.  Since the vtable stubs are not shared, their lifetime is tied
// to the inline-cache lifetime.
void pd_create_vtable_stub(MacroAssembler *masm, int vtable_index) {
  // RDI: receiver
  // RAX: KID
  // RSI, RDX, RCX, R08, R09 - arguments
  // RBX, RBP, RSP, R12-R15 - callee save
  // R10, R11 - temp
  // [RSP+0] - return address
  int start_off = __ rel_pc();
  __ verify_oop(RDI, MacroAssembler::OopVerify_OutgoingArgument);
  // klass to R10 from the KID already in RAX.  Note that if the receiver is
  // null, then the KID in RAX is 0 and the loaded klass will be NULL.
  __ kid2klass(RInOuts::a,R10,R11,RKeepIns::a,RAX); // klass to R10, from KID in RAX
  __ verify_oop(R10, MacroAssembler::OopVerify_OutgoingArgument);
  __ cvta2va(R10);              // Strip to klassOop

  // load methodOop
  int npe_off = __ rel_pc() - start_off; // NPE's happen here: zero kid has a NULL Klass in R10
  if( !vtable_npe_offset ) vtable_npe_offset = npe_off;
  assert0( npe_off == vtable_npe_offset );
  int entry_offset  = instanceKlass::vtable_start_offset() + vtable_index * vtableEntry::size();
int vtable_offset=entry_offset*wordSize+vtableEntry::method_offset_in_bytes();
  __ ldref_lvb( RInOuts::a, R11, RAX_TMP, RKeepIns::a, R10, vtable_offset, true );
  __ cvta2va(R11);              // Strip to methodOop (unless +MultiMapMetaData)

  // load target address.  If R11 is NULL, throw AbstractMethodError
  int ame_off = __ rel_pc() - start_off;
  if( !vtable_ame_offset ) vtable_ame_offset = ame_off;
  assert0( ame_off == vtable_ame_offset );
  __ jmp8(R11, in_bytes(methodOopDesc::from_compiled_offset()));
}


int itable_npe_offset = 0;      // filled in lazily
int itable_ame_offset = 0;      // filled in lazily
int itable_ame_offset0= 0;      // filled in lazily
int itable_ame_offsetB= 0;      // filled in lazily

// --- pd_create_itable_stub -------------------------------------------------
void pd_create_itable_stub(MacroAssembler *masm, int itable_index) {
  // RDI: receiver
  // RAX: KID
  // RSI, RDX, RCX, R08, R09 - arguments
  // RBX, RBP, RSP, R12-R15 - callee save
  // R10 - address of interface klass
  // R11 - temp
  // [RSP+0] - return address
  int start_off = __ rel_pc();
  __ verify_oop(RDI, MacroAssembler::OopVerify_OutgoingArgument);

  Register R11_IFC = R11;
  Register R10_KLS = R10;
  { // Handle with care. We need an extra reg and so grab RBX, as the stack
    // frame is off we can't take the implicit NPE (which is taken at npe_off).
    // The stack must only hold the return address when exceptions can occur.
    Register RBX_TMP = RBX;
__ push(RBX);
    __ ldref_lvb(RInOuts::a,R11_IFC,RBX_TMP,RKeepIns::a,R10,0,false);  // Load interface klass into R11
    // klass to R10 from the KID already in RAX.  Note that if the receiver is
    // null, then the KID in RAX is 0 and the loaded klass will be NULL.
    __ kid2klass(RInOuts::a,R10_KLS,RBX_TMP,RKeepIns::a,RAX); // klass to R10, from KID in RAX
__ pop(RBX);
  }
  __ verify_oop(R10_KLS, MacroAssembler::OopVerify_OutgoingArgument);
  __ cvta2va(R10_KLS);          // Strip to klassOop

  int npe_off = __ rel_pc() - start_off;   // NPE's happen here: zero kid has a NULL Klass in R10
  if( !itable_npe_offset ) itable_npe_offset = npe_off;
  assert0( npe_off == itable_npe_offset );
  __ ldz4 (RAX, R10_KLS, instanceKlass::vtable_length_offset() * wordSize);
  // start pointing to first itableOffsetEntry (less instanceKlass::vtable_start_offset() * wordSize)
  __ lea  (RAX, R10_KLS, instanceKlass::vtable_start_offset() * wordSize, RAX, 3); 

  // search the itableOffsetEntries for the interfaceKlass
  // R10_KLS: receiver klass
  // R11_IFC: interface klass
  // RAX_TMP: current itableOffsetEntry
  Label search, icce;
__ push(RBP);
__ push(RBX);
  Register RBP_TMP = RBP;
  Register RBX_TMP = RBX;
__ bind(search);
  __ ldref_lvb(RInOuts::a, RBX_TMP, RBP_TMP, RKeepIns::a, RAX, itableOffsetEntry::interface_offset_in_bytes(), true);
  // Check that entry is non-null.  Null entries are probably a bytecode
  // problem.  If the interface isn't implemented by the reciever class,
  // the VM should throw IncompatibleClassChangeError.  linkResolver checks
  // this too but that's only if the entry isn't already resolved, so we
  // need to check again.
  // R10_KLS: receiver klass
  // R11_IFC: interface klass
  // RAX_TMP: current itableOffsetEntry
  // Rxx: current entry value (klass)
  __ test8(RBX_TMP,RBX_TMP);
  __ jeq  (icce);
  // Bump scan pointer in case compare fails
__ add8i(RAX,itableOffsetEntry::size()*wordSize);
  __ cmp8 (R11_IFC, RBX_TMP);
  __ jne  (search);

  // found entry, get the methodOop from the vtable based on the offset
  // RAX: current itableOffsetEntry
__ pop(RBX);
  __ ldz4 (RAX, RAX, itableOffsetEntry::offset_offset_in_bytes()
           /*Un-bump scan pointer*/- itableOffsetEntry::size() * wordSize);

const int method_offset=(itableMethodEntry::size()*wordSize*itable_index)+itableMethodEntry::method_offset_in_bytes();
  Register R11_MTH = R11;
  __ ldref_lvb(RInOuts::a, R11_MTH, RBP_TMP, RKeepIns::a, R10_KLS, method_offset, RAX, 0, true);
__ pop(RBP);
  __ cvta2va(R11);              // Strip to methodOop

  // Call the method
  // R11: methodOop or NULL
  int ame_off = __ rel_pc() - start_off;
  if( itable_index == 0 ) {     // shortest encoding
    if( !itable_ame_offset0) itable_ame_offset0= ame_off;
    assert0( ame_off == itable_ame_offset0);
  } else if( method_offset < 128 ) { // slightly longer encoding
    if( !itable_ame_offset ) itable_ame_offset = ame_off;
    assert0( ame_off == itable_ame_offset );
  } else {                      // slightly longer yet encoding
    if( !itable_ame_offsetB ) itable_ame_offsetB = ame_off;
    assert0( ame_off == itable_ame_offsetB );
  }
  __ jmp8(R11_MTH, in_bytes(methodOopDesc::from_compiled_offset()));

__ bind(icce);
__ pop(RBX);
__ pop(RBP);
  __ jmp (Runtime1::entry_for(Runtime1::throw_incompatible_class_change_error_id)); // cheaply reusing existing C1 entry point
}
