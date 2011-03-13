/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "cpCacheOop.hpp"
#include "markSweep.hpp"
#include "methodOop.hpp"
#include "orderAccess.hpp"
#include "ostream.hpp"
#include "psParallelCompact.hpp"

#include "atomic_os_pd.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "universe.inline.hpp"

// Implememtation of ConstantPoolCacheEntry

void ConstantPoolCacheEntry::set_initial_state(int index) {
  assert(0 <= index && index < 0x10000, "sanity check");
  _indices = index;
}

int ConstantPoolCacheEntry::as_flags(BasicType type,bool is_final,
                    bool is_vfinal, bool is_volatile,
                    bool is_method_interface, bool is_method) {
int f=type;

  assert((0 <= type) && (type < 16), "Invalid state in as_flags");

  f <<= 1;
  if (is_final) f |= 1;
  f <<= 1;
  if (is_vfinal) f |= 1;
  f <<= 1;
  if (is_volatile) f |= 1;
  f <<= 1;
  if (is_method_interface) f |= 1;
  f <<= 1;
  if (is_method) f |= 1;
  f <<= ConstantPoolCacheEntry::hotSwapBit;
  // Preserve existing flag bit values
#ifdef ASSERT
  BasicType old_type = (BasicType)((_flags >> tosBits) & 0x0F);
  assert(old_type == 0 || old_type == type,
         "inconsistent cpCache flags state");
#endif
  return (_flags | f) ;
}

void ConstantPoolCacheEntry::set_bytecode_1(Bytecodes::Code code) {
  // Read once.
  volatile Bytecodes::Code c = bytecode_1();
  assert(c == 0 || c == code || code == 0, "update must be consistent");
  if( c == code ) return;       // no need to write; avoid parallel useless writes
  // Need to flush pending stores here before bytecode is written.
  OrderAccess::release_store_ptr(&_indices, _indices | ((u_char)code << 16));
}

void ConstantPoolCacheEntry::set_bytecode_2(Bytecodes::Code code) {
  // Read once.
  volatile Bytecodes::Code c = bytecode_2();
  assert(c == 0 || c == code || code == 0, "update must be consistent");
  if( c == code ) return;       // no need to write; avoid parallel useless writes
  // Need to flush pending stores here before bytecode is written.
  OrderAccess::release_store_ptr(&_indices, _indices | ((u_char)code << 24));
}

#ifdef ASSERT
// It is possible to have two different dummy methodOops created
// when the resolve code for invoke interface executes concurrently
// Hence the assertion below is weakened a bit for the invokeinterface
// case.
bool ConstantPoolCacheEntry::same_methodOop(oop cur_f1, oop f1) {
  return (cur_f1 == f1 || ((methodOop)cur_f1)->name() ==
	 ((methodOop)f1)->name() || ((methodOop)cur_f1)->signature() == 
	 ((methodOop)f1)->signature());
}
#endif

// Note that concurrent update of both bytecodes can leave one of them
// reset to zero.  This is harmless; the interpreter will simply re-resolve
// the damaged entry.  More seriously, the memory synchronization is needed
// to flush other fields (f1, f2) completely to memory before the bytecodes
// are updated, lest other processors see a non-zero bytecode but zero f1/f2.
void ConstantPoolCacheEntry::set_field(Bytecodes::Code get_code, 
                                       Bytecodes::Code put_code,
                                       KlassHandle field_holder, 
                                       int         orig_field_index, 
                                       int         field_offset, 
BasicType field_type,
                                       bool        is_final,
                                       bool        is_volatile) {
  set_f1(field_holder());
  set_f2(field_offset);
  // The field index is used by jvm/ti and is the index into fields() array
  // in holder instanceKlass.  This is scaled by instanceKlass::next_offset.
  assert((orig_field_index % instanceKlass::next_offset) == 0, "wierd index");
  const int field_index = orig_field_index / instanceKlass::next_offset;
  assert(field_index <= field_index_mask,
         "field index does not fit in low flag bits");
set_flags((0xffffffff&as_flags(field_type,is_final,false,is_volatile,false,false))|
            (field_index & field_index_mask));
  Atomic::write_barrier();      // Force coherent before setting bytecode_x fields (which indicate field resolved)
  set_bytecode_1(get_code);
  set_bytecode_2(put_code);
  NOT_PRODUCT(verify(tty));
}

int  ConstantPoolCacheEntry::field_index() const {
  return (_flags & field_index_mask) * instanceKlass::next_offset;
}

void ConstantPoolCacheEntry::set_method(Bytecodes::Code invoke_code,
                                        methodHandle method,
                                        int vtable_index) {

  assert(method->interpreter_entry() != NULL, "should have been set at this point");
  assert(!method->is_obsolete(),  "attempt to write obsolete method to cpCache");
  bool change_to_virtual = (invoke_code == Bytecodes::_invokeinterface);

  int byte_no = -1;
  bool needs_vfinal_flag = false;
  switch (invoke_code) {
    case Bytecodes::_invokevirtual:
    case Bytecodes::_invokeinterface: {
        if (method->can_be_statically_bound()) {
          // Gross!
          set_f2(ALWAYS_POISON_OBJECTREF(methodRef(method())).raw_value());
	  needs_vfinal_flag = true;
        } else {
          assert(vtable_index >= 0, "valid index");
          set_f2(vtable_index);
        }
        byte_no = 2;
        break;
    }
    case Bytecodes::_invokespecial:
      // Preserve the value of the vfinal flag on invokevirtual bytecode
      // which may be shared with this constant pool cache entry.
      needs_vfinal_flag = is_resolved(Bytecodes::_invokevirtual) && is_vfinal();
      // fall through
    case Bytecodes::_invokestatic:
      set_f1(method());
      byte_no = 1;
      break;
    default:
      ShouldNotReachHere();
      break;
  }

  set_flags(as_flags(method->result_type(), 
                     method->is_final_method(), 
                     needs_vfinal_flag, 
                     false, 
                     change_to_virtual,
                     true)|
            method()->size_of_parameters());

#ifdef DONT_USE_THIS
)) // Balance the parens for vim's syntax coloring.
#endif

  // Note:  byte_no also appears in TemplateTable::resolve.
  if (byte_no == 1) {
    set_bytecode_1(invoke_code);
  } else if (byte_no == 2)  {
    if (change_to_virtual) {
      // NOTE: THIS IS A HACK - BE VERY CAREFUL!!!
      //
      // Workaround for the case where we encounter an invokeinterface, but we
      // should really have an _invokevirtual since the resolved method is a 
      // virtual method in java.lang.Object. This is a corner case in the spec
      // but is presumably legal. javac does not generate this code.
      //
      // We set bytecode_1() to _invokeinterface, because that is the
      // bytecode # used by the interpreter to see if it is resolved.
      // We set bytecode_2() to _invokevirtual.
      // See also interpreterRuntime.cpp. (8/25/2000)
      // Only set resolved for the invokeinterface case if method is public.
      // Otherwise, the method needs to be reresolved with caller for each
      // interface call.
      if (method->is_public()) set_bytecode_1(invoke_code);
      set_bytecode_2(Bytecodes::_invokevirtual);
    } else {
      set_bytecode_2(invoke_code);
    }
  } else {
    ShouldNotReachHere();
  }
  NOT_PRODUCT(verify(tty));
}


void ConstantPoolCacheEntry::set_interface_call(methodHandle method, int index) {
  klassOop interf = method->method_holder();
  assert(instanceKlass::cast(interf)->is_interface(), "must be an interface");
  set_f1(interf);
  set_f2(index);
set_flags(as_flags(method->result_type(),method->is_final_method(),false,false,false,true)|method()->size_of_parameters());
  set_bytecode_1(Bytecodes::_invokeinterface);  
}


class LocalOopClosure: public OopClosure {
 private:
  void (*_f)(objectRef*);

 public:
  LocalOopClosure(void f(objectRef*))        { _f = f; }
  virtual void do_oop(objectRef* o)          { _f(o); }
};


void ConstantPoolCacheEntry::oops_do(void f(oop*)) {
  Unimplemented();
#if 0
  LocalOopClosure blk(f);
  oop_iterate(&blk);
#endif // 0
}


void ConstantPoolCacheEntry::oop_iterate(OopClosure* blk) {
  assert(in_words(size()) == 4, "check code below - may need adjustment");
  // field[1] is always oop or NULL
  blk->do_oop((objectRef*)&_f1);
  if (is_vfinal()) {
    blk->do_oop((objectRef*)&_f2);
  }
}


void ConstantPoolCacheEntry::oop_iterate_m(OopClosure* blk, MemRegion mr) {
  assert(in_words(size()) == 4, "check code below - may need adjustment");
  // field[1] is always oop or NULL
  if (mr.contains((objectRef *)&_f1)) blk->do_oop((objectRef*)&_f1);
  if (is_vfinal()) {
    if (mr.contains((objectRef *)&_f2)) blk->do_oop((objectRef*)&_f2);
  }
}


void ConstantPoolCacheEntry::adjust_pointers() {
  assert(in_words(size()) == 4, "check code below - may need adjustment");
  // field[1] is always oop or NULL
  MarkSweep::adjust_pointer((heapRef*)&_f1);
  if (is_vfinal()) {
    MarkSweep::adjust_pointer((heapRef*)&_f2);
  }
}

void ConstantPoolCacheEntry::update_pointers() {
  assert(in_words(size()) == 4, "check code below - may need adjustment");
  // field[1] is always oop or NULL
  PSParallelCompact::adjust_pointer((heapRef*)&_f1);
  if (is_vfinal()) {
    PSParallelCompact::adjust_pointer((heapRef*)&_f2);
  }
}

void ConstantPoolCacheEntry::update_pointers(HeapWord* beg_addr,
					     HeapWord* end_addr) {
  assert(in_words(size()) == 4, "check code below - may need adjustment");
  // field[1] is always oop or NULL
  PSParallelCompact::adjust_pointer((heapRef*)&_f1, beg_addr, end_addr);
  if (is_vfinal()) {
    PSParallelCompact::adjust_pointer((heapRef*)&_f2, beg_addr, end_addr);
  }
}

// RedefineClasses() API support:
// If this constantPoolCacheEntry refers to old_method then update it
// to refer to new_method.
bool ConstantPoolCacheEntry::adjust_method_entry(methodOop old_method,
       methodOop new_method, bool * trace_name_printed) {

  if (is_vfinal()) {
    // virtual and final so f2() contains method ptr instead of vtable index
    if ((intptr_t)methodRef(f2()).as_methodOop() == (intptr_t)old_method) {
      // match old_method so need an update
      set_f2(ALWAYS_POISON_OBJECTREF(methodRef(new_method)).raw_value());
      return true;
    }

    // f1() is not used with virtual entries so bail out
    return false;
  }

if(_f1.is_null()){
    // NULL f1() means this is a virtual entry so bail out
    // We are assuming that the vtable index does not need change.
    return false;
  }

  if (f1() == old_method) {
    POISON_AND_STORE_REF(&_f1, methodRef(new_method));
    NEEDS_CLEANUP;
    // Azul - I think we need a membar here!
    return true;
  }

  return false;
}

bool ConstantPoolCacheEntry::is_interesting_method_entry(klassOop k) {
  if (!is_method_entry()) {
    // not a method entry so not interesting by default
    return false;
  }

  methodOop m = NULL;
  if (is_vfinal()) {
    // virtual and final so _f2 contains method ptr instead of vtable index
    m = methodRef(f2()).as_methodOop();
}else if(_f1.is_null()){
    // NULL _f1 means this is a virtual entry so also not interesting
    return false;
  } else {
if(!(f1())->is_method()){
      // _f1 can also contain a klassOop for an interface
      return false;
    }
m=(methodOop)f1();
  }

  assert(m != NULL && m->is_method(), "sanity check");
  if (m == NULL || !m->is_method() || m->method_holder() != k) {
    // robustness for above sanity checks or method is not in
    // the interesting class
    return false;
  }

  // the method is in the interesting class so the entry is interesting
  return true;
}

void ConstantPoolCacheEntry::print(outputStream* st, int index) const {
  // print separator
  if (index == 0) tty->print_cr("                 -------------");
  // print entry
tty->print_cr("%3d  (%p)  [%02x|%02x|%5d]",index,this,bytecode_2(),bytecode_1(),constant_pool_index());
tty->print_cr("                 [   %08lx]",_f1.raw_value());
tty->print_cr("                 [   %08lx]",_f2);
tty->print_cr("                 [   %08lx]",_flags);
  tty->print_cr("                 -------------");
}

void ConstantPoolCacheEntry::verify(outputStream* st) const {
  // not implemented yet
}

// Implementation of ConstantPoolCache

void constantPoolCacheOopDesc::initialize(intArray& inverse_index_map) {
  assert(inverse_index_map.length() == length(), "inverse index map must have same length as cache");
  for (int i = 0; i < length(); i++) entry_at(i)->set_initial_state(inverse_index_map[i]);
}

// RedefineClasses() API support:
// If any entry of this constantPoolCache points to any of
// old_methods, replace it with the corresponding new_method.
void constantPoolCacheOopDesc::adjust_method_entries(methodRef* old_methods, methodRef* new_methods,
                                                     int methods_length, bool * trace_name_printed) {

  if (methods_length == 0) {
    // nothing to do if there are no methods
    return;
  }

  // get shorthand for the interesting class
klassOop old_holder=lvb_methodRef(&old_methods[0]).as_methodOop()->method_holder();

  for (int i = 0; i < length(); i++) {
    if (!entry_at(i)->is_interesting_method_entry(old_holder)) {
      // skip uninteresting methods
      continue;
    }

    // The constantPoolCache contains entries for several different
    // things, but we only care about methods. In fact, we only care
    // about methods in the same class as the one that contains the
    // old_methods. At this point, we have an interesting entry.

    for (int j = 0; j < methods_length; j++) {
methodOop old_method=lvb_methodRef(&old_methods[j]).as_methodOop();
methodOop new_method=lvb_methodRef(&new_methods[j]).as_methodOop();

      if (entry_at(i)->adjust_method_entry(old_method, new_method,
          trace_name_printed)) {
        // current old_method matched this entry and we updated it so
        // break out and get to the next interesting entry if there one
        break;
      }
    }
  }
}
