/*
 * Copyright 2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef RESOLUTIONERRORS_HPP
#define RESOLUTIONERRORS_HPP


#include "constantPoolOop.hpp"
#include "handles.hpp"
#include "hashtable.hpp"

class ResolutionErrorEntry;

// ResolutionError objects are used to record errors encountered during
// constant pool resolution (JVMS 5.4.3).

class ResolutionErrorTable : public Hashtable {

public:
  ResolutionErrorTable(int table_size);

  ResolutionErrorEntry* new_entry(int hash, constantPoolRef pool, int cp_index, symbolRef error);

  ResolutionErrorEntry* bucket(int i) {
    return (ResolutionErrorEntry*)Hashtable::bucket(i);
  }

  ResolutionErrorEntry** bucket_addr(int i) {
    return (ResolutionErrorEntry**)Hashtable::bucket_addr(i);
  }

  void add_entry(int index, ResolutionErrorEntry* new_entry) {
    Hashtable::add_entry(index, (HashtableEntry*)new_entry);
  }
  
  void add_entry(int index, unsigned int hash,
		 constantPoolHandle pool, int which, symbolHandle error);
		 

  // find error given the constant pool and constant pool index
  ResolutionErrorEntry* find_entry(int index, unsigned int hash, 
				   constantPoolHandle pool, int cp_index);


  unsigned int compute_hash(constantPoolHandle pool, int cp_index) {
    return (unsigned int) pool->identity_hash() + cp_index;
  }

  // purges unloaded entries from the table
  void purge_resolution_errors(BoolObjectClosure* is_alive);	
  void GPGC_purge_resolution_errors_section(long section,long sections);
	
 
  // this table keeps symbolOops alive 
  void always_strong_classes_do(OopClosure* blk);

  // GC support.
  void oops_do(OopClosure* f);
};


class ResolutionErrorEntry : public HashtableEntry {
 private:
  int		    _cp_index;
  symbolRef	    _error;

 public:
constantPoolRef pool()const{return constantPoolRef(literal().raw_value());}
  constantPoolRef*   pool_addr()  		{ return (constantPoolRef*)literal_addr(); }

  int		     cp_index() const		{ return _cp_index; }
  void		     set_cp_index(int cp_index) { _cp_index = cp_index; }

  symbolRef          error() const 		{ return lvb_symbolRef(&_error); }
  void		     set_error(symbolRef e)	{ _error = e; }
  symbolRef*         error_addr()		{ return &_error; }

  ResolutionErrorEntry* next() const {
    return (ResolutionErrorEntry*)HashtableEntry::next();
  }

  ResolutionErrorEntry** next_addr() {
    return (ResolutionErrorEntry**)HashtableEntry::next_addr();
  }

  // GC support
  void oops_do(OopClosure* blk);
};

#endif // RESOLUTIONERRORS_HPP
