/*
 * Copyright 1998-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifdef USE_PRAGMA_IDENT_SRC

#endif


void ExceptionHandlerTable::add_entry(HandlerTableEntry entry) {
  _nesting.check();
  if (_length >= _size) {
    // not enough space => grow the table (amortized growth, double its size)
    guarantee(_size > 0, "no space allocated => cannot grow the table since it is part of nmethod");
    int new_size = _size * 2;
    _table = REALLOC_RESOURCE_ARRAY(HandlerTableEntry, _table, _size, new_size);
    _size = new_size;
  }
  assert(_length < _size, "sanity check");
  _table[_length++] = entry;
}


HandlerTableEntry* ExceptionHandlerTable::subtable_for(int catch_pco) const {
  int i = 0;
  while (i < _length) {
    HandlerTableEntry* t = _table + i;
    if (t->pco() == catch_pco) {
      // found subtable matching the catch_pco
      return t;
    } else {
      // advance to next subtable
      i += t->len() + 1; // +1 for header
    }
  }
  return NULL;
}


ExceptionHandlerTable::ExceptionHandlerTable(int initial_size) {
  guarantee(initial_size > 0, "initial size must be > 0");
  _table  = NEW_RESOURCE_ARRAY(HandlerTableEntry, initial_size);
  _length = 0;
  _size   = initial_size;
}


ExceptionHandlerTable::ExceptionHandlerTable(const nmethod* nm) {
  _table  = (HandlerTableEntry*)nm->handler_table_begin();
  _length = nm->handler_table_size() / sizeof(HandlerTableEntry);
  _size   = 0; // no space allocated by ExeptionHandlerTable!
}


void ExceptionHandlerTable::add_subtable(
  int                 catch_pco,
  GrowableArray<intptr_t>* handler_bcis,
  GrowableArray<intptr_t>* scope_depths_from_top_scope,
  GrowableArray<intptr_t>* handler_pcos
) {
  assert(subtable_for(catch_pco) == NULL, "catch handlers for this catch_pco added twice");
  assert(handler_bcis->length() == handler_pcos->length(), "bci & pc table have different length");
  assert(scope_depths_from_top_scope == NULL || handler_bcis->length() == scope_depths_from_top_scope->length(), "bci & scope_depths table have different length");
  if (handler_bcis->length() > 0) {
    // add subtable header
    add_entry(HandlerTableEntry(handler_bcis->length(), catch_pco, 0));
    // add individual entries
    for (int i = 0; i < handler_bcis->length(); i++) {
      intptr_t scope_depth = 0;
      if (scope_depths_from_top_scope != NULL) {
        scope_depth = scope_depths_from_top_scope->at(i);
      }
      add_entry(HandlerTableEntry(handler_bcis->at(i), handler_pcos->at(i), scope_depth));
      assert(entry_for(catch_pco, handler_bcis->at(i), scope_depth)->pco() == handler_pcos->at(i), "entry not added correctly (1)");
      assert(entry_for(catch_pco, handler_bcis->at(i), scope_depth)->scope_depth() == scope_depth, "entry not added correctly (2)");
    }
  }
}


void ExceptionHandlerTable::copy_to(nmethod* nm) {
  assert(size_in_bytes() == nm->handler_table_size(), "size of space allocated in nmethod incorrect");
  memmove(nm->handler_table_begin(), _table, size_in_bytes());
}


HandlerTableEntry* ExceptionHandlerTable::entry_for(int catch_pco, int handler_bci, int scope_depth) const {
  HandlerTableEntry* t = subtable_for(catch_pco);
  if (t != NULL) {
    int l = t->len();
    while (l-- > 0) {
      t++;
      if (t->bci() == handler_bci && t->scope_depth() == scope_depth) return t;
    }
  }
  return NULL;
}


void ExceptionHandlerTable::print_subtable(HandlerTableEntry* t, outputStream* out) const {
  int l = t->len();
out->print_cr("catch_pco = 0x%x (%d entries)",t->pco(),l);
  while (l-- > 0) {
    t++;
out->print_cr("  bci %d at scope depth %d -> pco %d",t->bci(),t->scope_depth(),t->pco());
  }
}


void ExceptionHandlerTable::print(outputStream*out)const{
out->print_cr("ExceptionHandlerTable (size = %d bytes)",size_in_bytes());
  int i = 0;
  while (i < _length) {
    HandlerTableEntry* t = _table + i;
print_subtable(t,out);
    // advance to next subtable
    i += t->len() + 1; // +1 for header
  }
}

void ExceptionHandlerTable::print_subtable_for(int catch_pco, outputStream* out) const {
  HandlerTableEntry* subtable = subtable_for(catch_pco);

  if( subtable != NULL ) { print_subtable( subtable, out ); }
}

// ----------------------------------------------------------------------------
// Implicit null exception tables.  Maps an exception PC offset to a
// continuation PC offset.  During construction it's a variable sized
// array with a max size and current length.  When stored inside an
// nmethod a zero length table takes no space.  This is detected by
// nul_chk_table_size() == 0.  Otherwise the table has a length word
// followed by pairs of <excp-offset, const-offset>.
void ImplicitExceptionTable::set_size( uint size ) {
  _size = size;
  _data = NEW_RESOURCE_ARRAY(implicit_null_entry, (size*2));
  _len = 0;
}

void ImplicitExceptionTable::append( uint exec_off, uint cont_off ) {
  assert( (sizeof(implicit_null_entry) >= 4) || (exec_off < 65535), "" );
  assert( (sizeof(implicit_null_entry) >= 4) || (cont_off < 65535), "" );
  uint l = len();
  if (l == _size) {
    uint old_size_in_elements = _size*2;
    if (_size == 0) _size = 4;
    _size *= 2;
    uint new_size_in_elements = _size*2;
    _data = REALLOC_RESOURCE_ARRAY(uint, _data, old_size_in_elements, new_size_in_elements);
    assert0( _data );           // malloc returns null?
  }
  *(adr(l)  ) = exec_off;
  *(adr(l)+1) = cont_off;
  _len = l+1;
};

uint ImplicitExceptionTable::at( uint exec_off ) const {
  uint l = len();
  for( uint i=0; i<l; i++ )
    if( *adr(i) == exec_off )
      return *(adr(i)+1);
  return 0;                     // Failed to find any execption offset
}

void ImplicitExceptionTable::print(address base, outputStream* out) const {
out->print("{");
  for( uint i=0; i<len(); i++ )
out->print("< "INTPTR_FORMAT", "INTPTR_FORMAT" > ",base+*adr(i),base+*(adr(i)+1));
out->print_cr("}");
}

ImplicitExceptionTable::ImplicitExceptionTable(const nmethod* nm) {
  if (nm->nul_chk_table_size() == 0) {
    _len = 0;
    _data = NULL;
  } else {
    // the first word is the length if non-zero, so read it out and
    // skip to the next word to get the table.
    _data  = (implicit_null_entry*)nm->nul_chk_table_begin();
    _len = _data[0];
    _data++;
  }
  _size = len();
  assert(size_in_bytes() <= nm->nul_chk_table_size(), "size of space allocated in nmethod incorrect");
}

void ImplicitExceptionTable::copy_to( nmethod* nm ) {
  assert(size_in_bytes() <= nm->nul_chk_table_size(), "size of space allocated in nmethod incorrect");
  if (len() != 0) {
    implicit_null_entry* nmdata = (implicit_null_entry*)nm->nul_chk_table_begin();
    // store the length in the first uint
    nmdata[0] = _len;
    nmdata++;
    // copy the table after the length
    memmove( nmdata, _data, 2 * len() * sizeof(implicit_null_entry));
  } else {
    // zero length table takes zero bytes
    assert(size_in_bytes() == 0, "bad size");
    assert(nm->nul_chk_table_size() == 0, "bad size");
  }
}

void ImplicitExceptionTable::verify(nmethod *nm) const {
  for (uint i = 0; i < len(); i++) {
     if ((*adr(i) > (unsigned int)nm->code_size()) ||
         (*(adr(i)+1) > (unsigned int)nm->code_size()))
       fatal1("Invalid offset in ImplicitExceptionTable at %lx", _data);
  }
}

// ----------------------------------------------------------------------------
// Implicit null exception tables.  Maps an exception PC offset to a
// continuation PC offset.  During construction it's a variable sized
// array with a max size and current length.  When stored inside an
// nmethod a zero length table takes no space.  This is detected by
// nul_chk_table_size() == 0.  Otherwise the table has a length word
// followed by pairs of <excp-offset, const-offset>.
void ImplicitRangeCheckTable::set_size(uint size){
  _size = size;
  _data = NEW_RESOURCE_ARRAY(implicit_null_entry, (size*2));
  _len = 0;
}

void ImplicitRangeCheckTable::append(uint exec_off,uint cont_off){
  assert( (sizeof(implicit_null_entry) >= 4) || (exec_off < 65535), "" );
  assert( (sizeof(implicit_null_entry) >= 4) || (cont_off < 65535), "" );
  uint l = len();
  if (l == _size) {
    uint old_size_in_elements = _size*2;
    if (_size == 0) _size = 4;
    _size *= 2;
    uint new_size_in_elements = _size*2;
    _data = REALLOC_RESOURCE_ARRAY(uint, _data, old_size_in_elements, new_size_in_elements);
    assert0( _data );           // malloc returns null?
  }
  *(adr(l)  ) = exec_off;
  *(adr(l)+1) = cont_off;
  _len = l+1;
};

uint ImplicitRangeCheckTable::at(uint exec_off)const{
  uint l = len();
  for( uint i=0; i<l; i++ )
    if( *adr(i) == exec_off )
      return *(adr(i)+1);
  return 0;                     // Failed to find any execption offset
}

void ImplicitRangeCheckTable::print(address base, outputStream* out) const {
out->print("{");
  for( uint i=0; i<len(); i++ )
out->print("< "INTPTR_FORMAT", "INTPTR_FORMAT" > ",base+*adr(i),base+*(adr(i)+1));
out->print_cr("}");
}

ImplicitRangeCheckTable::ImplicitRangeCheckTable(const nmethod*nm){
if(nm->rng_chk_table_size()==0){
    _len = 0;
    _data = NULL;
  } else {
    // the first word is the length if non-zero, so read it out and
    // skip to the next word to get the table.
_data=(implicit_null_entry*)nm->rng_chk_table_begin();
    _len = _data[0];
    _data++;
  }
  _size = len();
assert(size_in_bytes()<=nm->rng_chk_table_size(),"size of space allocated in nmethod incorrect");
}

void ImplicitRangeCheckTable::copy_to(nmethod*nm){
assert(size_in_bytes()<=nm->rng_chk_table_size(),"size of space allocated in nmethod incorrect");
  if (len() != 0) {
implicit_null_entry*nmdata=(implicit_null_entry*)nm->rng_chk_table_begin();
    // store the length in the first uint
    nmdata[0] = _len;
    nmdata++;
    // copy the table after the length
    memmove( nmdata, _data, 2 * len() * sizeof(implicit_null_entry));
  } else {
    // zero length table takes zero bytes
    assert(size_in_bytes() == 0, "bad size");
assert(nm->rng_chk_table_size()==0,"bad size");
  }
}

void ImplicitRangeCheckTable::verify(nmethod*nm)const{
  for (uint i = 0; i < len(); i++) {
     if ((*adr(i) > (unsigned int)nm->code_size()) ||
         (*(adr(i)+1) > (unsigned int)nm->code_size()))
fatal1("Invalid offset in ImplicitRangeCheckTable at %lx",_data);
  }
}
