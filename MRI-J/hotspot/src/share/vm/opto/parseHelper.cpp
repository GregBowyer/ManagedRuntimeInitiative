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


#include "ciObjArrayKlass.hpp"
#include "graphKit.hpp"
#include "objArrayKlass.hpp"
#include "parse.hpp"
#include "runtime.hpp"
#include "sharedRuntime.hpp"

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
#include "thread_os.inline.hpp"

//=============================================================================
//------------------------------do_checkcast-----------------------------------
void Parse::do_checkcast() {
  bool will_link;
  ciKlass* klass = iter().get_klass(will_link);

  Node *obj = peek();

  // Throw uncommon trap if class is not loaded or the value we are casting
  // _from_ is not loaded, and value is not null.  If the value _is_ NULL,
  // then the checkcast does nothing.
  const TypeInstPtr *tp = _gvn.type(obj)->isa_instptr();
  if (!will_link || (tp && !tp->is_loaded())) {
    do_null_assert(obj, T_OBJECT);
    assert( stopped() || _gvn.type(peek())->higher_equal(TypePtr::NULL_PTR), "what's left behind is null" );
    if (!stopped()) {
      profile_null_checkcast();
    }
    return;
  }

  // Unloaded plain klass not possible here, but an array-of-unloaded is
const TypeOopPtr*tobj=_gvn.type(obj)->isa_oopptr();
if(tobj&&!tobj->klass()->is_loaded()){
    uncommon_trap( Deoptimization::Reason_unloaded, NULL, "checkcast is unloaded", /*must_throw=*/false);
    return;
  }

  // Get sharpened superklass, or NULL if no instances can be made
  const TypeOopPtr *sharp_type = TypeOopPtr::make_from_klass_unique(klass)->isa_oopptr();
Node*superklass=NULL;
  if( sharp_type ) { // NO implementors possible, so throw for all except NULLs
ciKlass*sharp_klass=sharp_type->klass();
    // If original class is an interface (casting to an interface type) and
    // the sharpened klass (NEVER an interface type; C2 types no longer
    // directly represent interfaces) does not implement the interface
    // (usually it is only known as an Object) then we really do need to check
    // for being an interface type (which is an expensive slow check).
    // Otherwise checking for the sharp_klass also guarentees we pass the
    // interface cast and is a lot cheaper.
    superklass = makecon(TypeKlassPtr::make_kid( 
      (klass->is_interface() && !sharp_klass->is_subtype_of(klass)) ? klass : sharp_klass, true ));
      
  } 
  CPData_Null* cpdn = cpdata()->as_Null( Bytecodes::_checkcast );
  Node *res = gen_checkcast(obj, superklass, NULL, "null chk checkcast", cpdn );

  // Pop from stack AFTER gen_checkcast because it can uncommon trap and
  // the debug info has to be correct.
  pop();
  push(res);
}


//------------------------------do_instanceof----------------------------------
void Parse::do_instanceof() {
  if (stopped())  return;
  // We would like to return false if class is not loaded, emitting a
  // dependency, but Java requires instanceof to load its operand.

  // Throw uncommon trap if class is not loaded
  bool will_link;
  ciKlass* klass = iter().get_klass(will_link);

  if (!will_link) {
    do_null_assert(peek(), T_OBJECT);
    assert( stopped() || _gvn.type(peek())->higher_equal(TypePtr::NULL_PTR), "what's left behind is null" );
    if (!stopped()) {
      // The object is now known to be null.
      // Shortcut the effect of gen_instanceof and return "false" directly.
      pop();                   // pop the null
      push(_gvn.intcon(0));    // push false answer
    }
    return;
  }
  // Sharpen result class
  const TypeOopPtr *sharp_type = TypeOopPtr::make_from_klass_unique(klass)->isa_oopptr();
Node*superklass=NULL;
  if( sharp_type ) { // NO implementors possible, so throw for all except NULLs
ciKlass*sharp_klass=sharp_type->klass();
    // If original class is an interface (casting to an interface type) and
    // the sharpened klass (NEVER an interface type; C2 types no longer
    // directly represent interfaces) does not implement the interface
    // (usually it is only known as an Object) then we really do need to check
    // for being an interface type (which is an expensive slow check).
    // Otherwise checking for the sharp_klass also guarentees we pass the
    // interface cast and is a lot cheaper.
    superklass = makecon(TypeKlassPtr::make_kid( 
      (klass->is_interface() && !sharp_klass->is_subtype_of(klass)) ? klass : sharp_klass, true ));
      
  } 

  // Push the bool result back on stack
  CPData_Null* cpdn = cpdata()->as_Null( Bytecodes::_instanceof );
  Node *res = gen_instanceof( peek(), superklass, "null chk instanceof", cpdn );

  // Pop from stack AFTER gen_instanceof because it can uncommon trap and
  // the debug info has to be correct.
  pop();
push(_gvn.transform(res));
}

//------------------------------array_store_check------------------------------
// pull array from stack and check that the store is valid
void Parse::array_store_check() {

  // Shorthand access to array store elements
  Node *obj = stack(_sp-1);
  Node *idx = stack(_sp-2);
  Node *ary = stack(_sp-3);

  if (_gvn.type(obj) == TypePtr::NULL_PTR) {
    // There's never a type check on null values.
    // This cutout lets us avoid the uncommon_trap(Reason_array_check)
    // below, which turns into a performance liability if the
    // gen_checkcast folds up completely.
    return;
  }

const TypeAryPtr*tary=_gvn.type(ary)->is_aryptr();
  const TypeKlassPtr *taryk = TypeKlassPtr::make(tary->klass())->cast_to_exactness(tary->klass_is_exact())->is_klassptr();
  Node* array_kid   = _gvn.transform(new (C, 2) GetKIDNode(0/*array is known not-null here*/,ary,taryk->cast_to_kid(true)));
Node*array_klass=_gvn.transform(new(C,2)KID2KlassNode(array_kid));
  // Get the array klass
  const TypeKlassPtr *tak = _gvn.type(array_klass)->is_klassptr();

  // array_klass's type is generally INexact array-of-oop.  Heroically
  // cast the array klass to EXACT array and uncommon-trap if the cast
  // fails.

  // Get profile-data for this array store
  CPData_Null* cpdn = cpdata()->as_Null( Bytecodes::_aastore );
  bool always_see_exact_class = MonomorphicArrayCheck && !cpdn->did_fail(); // Never failed before?

  // Is the array klass is exactly its defined type?
  if (always_see_exact_class && !tak->klass_is_exact()) {
    // Make a constant out of the inexact array klass
    const TypeKlassPtr *extak = tak->cast_to_exactness(true)->is_klassptr();
    Node* con = makecon(extak);
    Node* cmp = _gvn.transform(new (C, 3) CmpPNode( array_klass, con ));
    Node* bol = _gvn.transform(new (C, 2) BoolNode( cmp, BoolTest::eq ));
    Node* ctrl= control();
    { BuildCutout unless(this, bol, PROB_MAX);
uncommon_trap(Deoptimization::Reason_array_store_check,tak->klass(),"array is polymorphic",false);
    }
    if (stopped()) {          // MUST uncommon-trap?
      set_control(ctrl);      // Then Don't Do It, just fall into the normal checking
    } else {                  // Cast array klass to exactness: 
      // Use the exact constant value we know it is.
      replace_in_map(array_klass,con);
      array_klass = con;      // Use cast value moving forward
      tak = extak;
    }
  }

  // Come here for polymorphic array klasses

  // Extract the array element class
Node*a_e_kid;
  if ( tary->elem()->isa_oopptr() ) {
    int element_kid_offset = objArrayOopDesc::ekid_offset_in_bytes();
Node*p2=basic_plus_adr(ary,ary,element_kid_offset);
    const TypeKlassPtr *etype = TypeKlassPtr::make_kid(tary->elem()->is_oopptr()->klass(),tak->klass_is_exact());
    a_e_kid = _gvn.transform(new (C, 3) LoadKlassNode(0, memory(TypeAryPtr::EKID), p2, tak,etype));
  }
  else {
    assert(tary->klass()->is_obj_array_klass(), "Handle non obj_array_klass in Parse::array_store_check.");
    // Load element klass
    Node* lekp = basic_plus_adr(array_klass, objArrayKlass::element_klass_offset_in_bytes() + sizeof(oopDesc));
const TypePtr*tek=_gvn.type(lekp)->is_ptr();
    // This eklass load cannot happen unless we pass the guard that we are
    // really an Object[] and e.g. a byte[]
    Node *eklass = _gvn.transform(new (C, 3) LoadKlassNode(control(), immutable_memory(), lekp, tek, TypeKlassPtr::OBJECT));
    // Load element KID
    Node *kid_adr = basic_plus_adr(eklass, Klass::klassId_offset_in_bytes() + (int)sizeof(oopDesc));
    a_e_kid = _gvn.transform(new(C,3) LoadKlassNode(NULL,immutable_memory(),kid_adr,_gvn.type(kid_adr)->is_ptr(),TypeKlassPtr::KID));    
  }
  
  // Check (the hard way) and throw if not a subklass.
  // Result is ignored, we just need the CFG effects.
  CPData_Null oop_cpdn;
  oop_cpdn = *cpdn;             // Init structure
  oop_cpdn._null = 1;           // Expect to be *storing* nulls
  gen_checkcast( obj, a_e_kid, NULL, "null chk arraystore", &oop_cpdn );
}


//------------------------------do_new-----------------------------------------
void Parse::do_new() {
  kill_dead_locals();

  bool will_link;
  ciInstanceKlass* klass = iter().get_klass(will_link)->as_instance_klass();
  assert(will_link, "_new: typeflow responsibility");

  // Should initialize, or throw an InstantiationError?
  if (!klass->is_initialized() ||
      klass->is_abstract() || klass->is_interface() ||
      klass->name() == ciSymbol::java_lang_Class() ||
      iter().is_unresolved_klass()) {
uncommon_trap(Deoptimization::Reason_uninitialized,klass,"new is uninitialized",false);
    return;
  }

  Node* kls = makecon(TypeKlassPtr::make_kid(klass,true/*exact*/));
  Node* obj = new_instance(kls, intcon(0)/*no extra slow test*/, cpdata());

  // Push resultant oop onto stack
  push(obj);
}

#ifndef PRODUCT
//------------------------------dump_map_adr_mem-------------------------------
// Debug dump of the mapping from address types to MergeMemNode indices.
void Parse::dump_map_adr_mem() const {
C2OUT->print_cr("--- Mapping from address types to memory Nodes ---");
  MergeMemNode *mem = map() == NULL ? NULL : (map()->memory()->is_MergeMem() ? 
                                      map()->memory()->as_MergeMem() : NULL);
  for (uint i = 0; i < (uint)C->num_alias_types(); i++) {
C->alias_type(i)->print_on(C2OUT);
C2OUT->print("\t");
    // Node mapping, if any
    if (mem && i < mem->req() && mem->in(i) && mem->in(i) != mem->empty_memory()) {
      mem->in(i)->dump();
    } else {
C2OUT->cr();
    }
  }
}

#endif
