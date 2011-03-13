/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciEnv.hpp"
#include "ciSymbol.hpp"
#include "interfaceSupport.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"

// ------------------------------------------------------------------
// ciSymbol::ciSymbol
//
// Preallocated handle variant.  Used with handles from vmSymboHandles.
ciSymbol::ciSymbol(symbolHandle h_s) : ciObject(h_s) {
  Arena* arena = CURRENT_ENV->arena();
  _length = h_s->utf8_length();
  _string = NEW_ARENA_ARRAY(arena,char,_length+1);
  _string = h_s->as_C_string(_string, _length+1);
}

ciSymbol::ciSymbol(symbolOop oop):ciObject(oop){
  Arena* arena = CURRENT_ENV->arena();
  _length = oop->utf8_length();
  _string = NEW_ARENA_ARRAY(arena,char,_length+1);
  _string = oop->as_C_string(_string, _length+1);
}

ciSymbol::ciSymbol(FAMPtr old_cis):ciObject(old_cis){
  FAM->mapNewToOldPtr(this, old_cis);
  _string = FAM->getString("((struct ciSymbol*)%p)->_string", old_cis);
  _length = strlen(_string);
}

void ciSymbol::fixupFAMPointers() {
  ciObject::fixupFAMPointers();
}

// ciSymbol
//
// This class represents a symbolOop in the HotSpot virtual
// machine.

// ------------------------------------------------------------------
// ciSymbol::as_utf8
//
// The text of the symbol as a null-terminated C string.
const char* ciSymbol::as_utf8() {
  return _string;
}

// ------------------------------------------------------------------
// ciSymbol::base
jbyte* ciSymbol::base() {
  return (jbyte*)_string;
}

// ------------------------------------------------------------------
// ciSymbol::byte_at
int ciSymbol::byte_at(int i) {
  return (int)_string[i];
}

// ------------------------------------------------------------------
// ciSymbol::utf8_length
int ciSymbol::utf8_length() {
  return _length;
}

// ------------------------------------------------------------------
// ciSymbol::print_impl
//
// Implementation of the print method
void ciSymbol::print_impl(outputStream*out)const{
out->print(" value=");
print_symbol(out);
}

// ------------------------------------------------------------------
// ciSymbol::print_symbol_on
//
// Print the value of this symbol on an outputStream
void ciSymbol::print_symbol_on(outputStream*st)const{
st->print(_string);
}

// ------------------------------------------------------------------
// ciSymbol::make_impl
//
// Make a ciSymbol from a C string (implementation).
ciSymbol* ciSymbol::make_impl(const char* s) {
  EXCEPTION_CONTEXT;
symbolOop sym=oopFactory::new_symbol(s,(int)strlen(s),THREAD);
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    CURRENT_THREAD_ENV->record_out_of_memory_failure();
    return ciEnv::_unloaded_cisymbol;
  }
  return CURRENT_THREAD_ENV->get_object(sym)->as_symbol();
}

// ------------------------------------------------------------------
// ciSymbol::make
//
// Make a ciSymbol from a C string.
ciSymbol* ciSymbol::make(const char* s) {
  if (FAM) {
    ciSymbol* cis = ciEnv::current()->get_symbol_from_string(s);  
    guarantee(cis, "Could not find ciSymbol");
    return cis;
  }
  GUARDED_VM_ENTRY(return make_impl(s);)
}

