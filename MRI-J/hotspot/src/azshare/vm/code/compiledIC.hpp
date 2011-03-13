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
#ifndef COMPILEDIC_HPP
#define COMPILEDIC_HPP


#include "allocation.hpp"
#include "bytecodes.hpp"
#include "nativeInst_pd.hpp"

class CallInfo;

// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

//-----------------------------------------------------------------------------
// The CompiledIC represents a compiled inline cache.
//
// It has 5 legal states:
// (1) Clean: First instruction calls/traps to fixup code.  Remaining
// instructions are junk.  No CPU can be executing past the 1st instruction,
// so it is possible to do a MT-safe transition to states (2) or (3).
// (2) Static: Using a large IC call-site for a simple static call.  This
// allows the call-site to be converted later to a real IC without recompiling
// the code.  C1 will likely use this alot; it can happen for C2 if class
// unloading makes a previously-vcall-able method statically bindable.

// (3) Caching: Does a cached v-call or i-call, by loading up the receiver's
// class (hence does a null-check), loading up the expected class, comparing
// and trapping if they are not equal.  After passing this test, the call is
// known to go to the correct target.
//     ld [Rrecv+4],Rklass
//     sethi/lo #expected_klass,Rtemp
//     cmp Rklass,Rtemp
//     call expected_target
//     delayed->TRAP_NE  handle_ic_miss
// (4) Vcall: Implement a v-call inline: 
//     ld [Rrecv+4],Rklass
//     ld [Rklass+#vtble_off],Rtemp; 
//     ld [Rtemp+#from_compiled_entry],Rtemp
//     jmpl Rtemp
//     delayed->nop
// (5) Icall: Implement an i-call out-of-line:
//     sethi/lo #iface_klass,Rklass
//     call istub
//     delayed->nop
//
// In order to make patching of the inline cache MT-safe, we only allow the
// following transitions (when not at a safepoint):
// [1] Clean to any other state
// [2] Caching to Vcall or Icall
// At a Safepoint only we also allow:
// [3] Any state to Clean
// No other transitions are allowed.
// Finally, we also allow changing the expected target of a Clean or Caching
// IC, in case the target nmethod gets removed or recompiled.

class CompiledIC: public ResourceObj {
  NativeInlineCache* _ic;	// the inline cache template
  CompiledIC(NativeCall* ic_call);
 public:
  static CompiledIC* make_before(address return_addr) { 
    CompiledIC* c_ic = new CompiledIC(nativeCall_before(return_addr));
debug_only(c_ic->verify());
    return c_ic;
  }

  address end_of_call() const { return _ic->return_address(); }

  // State
  bool is_clean  () const { return _ic->is_clean  (); }
  bool is_static () const { return _ic->is_static (); }
  bool is_caching() const { return _ic->is_caching(); }
  bool is_vcall  () const { return _ic->is_vcall  (); }
  bool is_icall  () const { return _ic->is_icall  (); }
  // More state: what fixed code to we jump to, or NULL for clean & vcalls 
  address ic_destination() const { return _ic->destination(); }
  address pc() { return (address)_ic; }

  // Transitions
  void set_clean();
  address patch_as_needed(CallInfo *callinfo, klassOop receiver_klass, Bytecodes::Code bc);
  void set_new_destination(address code) { _ic->set_new_destination(code); }

  // Misc
void print()const PRODUCT_RETURN;
void verify()const PRODUCT_RETURN;
  static int64_t _reset_to_clean_ctr;
  static int64_t _set_to_vcall_ctr;
  static int64_t _set_to_icall_ctr;
  static int64_t _set_to_static_ctr;
  static int64_t _set_new_dest_ctr;
  static int64_t _double_set_under_lock_ctr;
  static void print_statistics();
  static void print_statistics(xmlBuffer* xb);  
};
#endif // COMPILEDIC_HPP
