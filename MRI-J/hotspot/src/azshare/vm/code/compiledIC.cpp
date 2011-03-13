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


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

#include "compiledIC.hpp"
#include "linkResolver.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"

#include "handles.inline.hpp"

int64_t CompiledIC::_reset_to_clean_ctr;
int64_t CompiledIC::_set_to_vcall_ctr;
int64_t CompiledIC::_set_to_icall_ctr;
int64_t CompiledIC::_set_to_static_ctr;
int64_t CompiledIC::_set_new_dest_ctr;
int64_t CompiledIC::_double_set_under_lock_ctr;

// Every time a compiled IC is changed or its type is being accessed,
// either the CompiledIC_lock must be set or we must be at a safe point.

CompiledIC::CompiledIC(NativeCall* ic_call) : 
  _ic(NativeInlineCache::at(ic_call)) {
}


void CompiledIC::set_clean(){
  assert(SafepointSynchronize::is_at_safepoint(), "");  
  _reset_to_clean_ctr++;
_ic->set_clean_unsafely();
}

// Patch the IC to take into account the given code & receiver.  Ignore
// "empty" transitions: do nothing for ICs that are already megamorphic, or
// calling the same code, same receiver.  Due to race conditions we might have
// several threads attempt to update the same IC to the same location.
address CompiledIC::patch_as_needed(CallInfo *callinfo, klassOop receiver_klass, Bytecodes::Code bc) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(end_of_call()));

  address code = callinfo->selected_method()->from_compiled_entry();

  // Using an inline-cache for a statically bindable call site is possible.
  // It means that class loading which brings in more targets won't force a
  // recompile, just that the IC needs to be moved from a static call to a
  // full IC test at the same time the extra classes got loaded (at a
  // safepoint).  This can happen even in C2 code, if classes get unloaded
  // between code-gen time and fixup time.
  if (callinfo->resolved_method()->can_be_statically_bound()) {
    if (is_static() && ic_destination() == code) {
      _double_set_under_lock_ctr++;
      return code;		// Ignore the empty transition
    }
    // Patch from either clean or static-but-wrong-target to static-good-target
    _ic->set_static(code, receiver_klass);
    _set_to_static_ctr++;
    return code;
  }
assert(!is_static(),"bad IC transition");
  
  // Check for the empty megamorphic transitions
  if (is_vcall()) {
    assert0(bc == Bytecodes::_invokevirtual || callinfo->has_vtable_index());
    _double_set_under_lock_ctr++;
    return code;		// Already megamorphic
  }
  if (is_icall()) {
    assert0(bc == Bytecodes::_invokeinterface && !callinfo->has_vtable_index());
    _double_set_under_lock_ctr++;
    return code;		// Already megamorphic
  }

  // Must be clean or caching already.
  // See if we are targeting a different receiver.
  if (is_caching()) {
    assert0(bc == Bytecodes::_invokevirtual ||
	    bc == Bytecodes::_invokeinterface);
    if (_ic->expected_klass() != receiver_klass) {
      methodCodeOop mcoop = CodeCache::find_blob(_ic)->owner().as_methodCodeOop();
      // This site goes megamorphic
bool is_invoke_interface=(bc==Bytecodes::_invokeinterface&&!callinfo->has_vtable_index());
      if (is_invoke_interface) {
int index=klassItable::compute_itable_index(callinfo->resolved_method()());
        address istub_entry = mcoop->get_itable_stub(index);
assert(istub_entry!=NULL,"entry not computed");
        KlassHandle ki = callinfo->resolved_method()->method_holder();
        assert(Klass::cast(ki())->is_interface(), "sanity check");
        // Here I make a I-stub once per megamorphic call-site, instead of
        // once per interface class that has any call-site that goes
        // megamorphic.  The issue is that I need a private I-stub per itable
        // index and the engineering to share these isn't worth effort: even
        // for very large apps only a handful of interface call sites go
        // megamorphic, so at best I'd be sharing 100 bytes total.  Note that
        // the actual lookup generated by VtableStubs::create_stub is never
        // shared so hardware BTB's will handle the register jump better.
        address entry = MethodStubBlob::generate(ki.as_ref(), istub_entry);
assert(entry,"should not run out because of CodeCacheReserve");
        _set_to_icall_ctr++;
        _ic->set_icall(entry,ki());
      } else {
        // Can be different than method->vtable_index(), due to package-private etc.
int vtable_index=callinfo->vtable_index();
        address entry = mcoop->get_vtable_stub(vtable_index);
        _set_to_vcall_ctr++;
        _ic->set_vcall(entry);
      }
    } else if (ic_destination() != code) {
      // Just changing the branch target
      _set_new_dest_ctr++;
      _ic->set_new_destination(code);
    }
    return code;
  }
  
  // Must be a clean to caching transition
assert(is_clean(),"bad IC transition");
  _ic->set_caching(code,receiver_klass);
  return code;
}

// --- print_statistics ------------------------------------------------------
void CompiledIC::print_statistics(){
if(_reset_to_clean_ctr)tty->print_cr("%5ld inline_cache reset to clean",_reset_to_clean_ctr);
if(_set_to_vcall_ctr)tty->print_cr("%5ld inline_cache set to vcall",_set_to_vcall_ctr);
if(_set_to_icall_ctr)tty->print_cr("%5ld inline_cache set to icall",_set_to_icall_ctr);
if(_set_to_static_ctr)tty->print_cr("%5ld inline_cache set to static",_set_to_static_ctr);
if(_set_new_dest_ctr)tty->print_cr("%5ld inline_cache set new dest",_set_new_dest_ctr);
if(_double_set_under_lock_ctr)tty->print_cr("%5ld inline_cache double set under lock",_double_set_under_lock_ctr);
}

//-----------------------------------------------------------------------------
// Non-product mode code
#ifndef PRODUCT 

void CompiledIC::verify()const{
  // make sure code pattern is actually a NativeInlineCache pattern
_ic->verify();
}


void CompiledIC::print()const{
  const char *s;
  if (is_clean())   { s = "clean";   }
  if (is_static())  { s = "static";  }
  if (is_caching()) { s = "caching"; }
  if (is_vcall())   { s = "vcall";   }
  if (is_icall())   { s = "icall";   }
  
tty->print("Inline cache at "PTR_FORMAT", calling "PTR_FORMAT"is %s with Klass "PTR_FORMAT,
	     _ic->addr_at(0), ic_destination(),s,_ic->expected_klass());
  tty->cr();
}

#endif

