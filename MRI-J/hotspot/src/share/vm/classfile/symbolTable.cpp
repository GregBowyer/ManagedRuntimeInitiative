/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "collectedHeap.hpp"
#include "constantPoolOop.hpp"
#include "gcLocker.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "gpgc_tlb.hpp"
#include "gpgc_readTrapArray.hpp"
#include "handles.hpp"
#include "javaClasses.hpp"
#include "markWord.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "symbolKlass.hpp"
#include "symbolTable.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "utf8.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "oop.inline2.hpp"

// -*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-
//
// GenPauseless GC garbage collects the string and symbol tables
// concurrently with them being accessed and added to by mutators.  You
// better be sure you understand how this works before you change things
// in here.  Start by looking at the design note in symbolTable.hpp.
//
// -*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-

SymbolTable* SymbolTable::_the_table = NULL;

// --------------------------------------------------------------------------


// DeadRef is the value of symbolOop->strength() that indicates the pauseless
// garbage collector is going to collect a symbol.
const jlong DeadRef = -1;


symbolOop SymbolTable::ensure_strong_ref(WeakHashtableEntry* e, symbolOop sym) {
  assert0(UseGenPauselessGC);
  WeakHashtableEntry::EnsuredStrength strength = e->ensure_strong_ref();
  
  if (strength == WeakHashtableEntry::IsDead) {
    return NULL;
  }

  if (strength == WeakHashtableEntry::MustMark) {
    GPGC_OldCollector::mutator_mark_leaf_conditional(sym);
    return sym;
  }
  
  // In addition the symbolOop being StrongRef, we have to make sure that its klassRef
  // is remapped and NMTed properly.  We don't mark through the klassRef, however.
  GPGC_OldCollector::remap_and_nmt_only(sym->klass_addr());

  return sym;
}


// Lookup a symbol in a bucket.

symbolOop SymbolTable::lookup(int index, const char* name,
                              int len, unsigned int hash) {
for(WeakHashtableEntry*e=bucket(index);e!=NULL;e=e->next()){
    if (e->hash() == hash) {
symbolOop sym=symbolOop(e->remapped_literal());
      if (sym->equals(name, len)) {
        if ( UseGenPauselessGC ) {
          // Pauseless GC engages in concurrent lockless collection of the SymbolTable.
          // See the design note in symbolTable.hpp for details.
          sym = ensure_strong_ref(e, sym);
          // If symbol becomes NULL, we don't have to continue to search down the
          // hash chain.  Any live SymbolOop will be inserted into the hash after
          // this dead one we've found, so it is earlier in the hash chain.
        }
        return sym;
      }
    }
  }
  return NULL;
}


// We take care not to be blocking while holding the
// SymbolTable_lock. Otherwise, the system might deadlock, since the
// symboltable is used during compilation (VM_thread) The lock free
// synchronization is simplified by the fact that we do not delete
// entries in the symbol table during normal execution (only during
// safepoints).

symbolOop SymbolTable::lookup(const char* name, int len, TRAPS) {  
  unsigned int hashValue = hash_symbol(name, len);
  int index = the_table()->hash_to_index(hashValue);

  symbolOop s = the_table()->lookup(index, name, len, hashValue);

  // Found
  if (s != NULL) return s;
  
  // Otherwise, add to symbol to table
  return the_table()->basic_add(index, (u1*)name, len, hashValue, CHECK_NULL);
}

symbolOop SymbolTable::lookup(symbolHandle sym, int begin, int end, TRAPS) {
  char* buffer;
  int index, len;
  unsigned int hashValue;
  char* name;
  {
    debug_only(No_Safepoint_Verifier nsv;)

    name = (char*)sym->base() + begin;
    len = end - begin;
    hashValue = hash_symbol(name, len);
    index = the_table()->hash_to_index(hashValue);
    symbolOop s = the_table()->lookup(index, name, len, hashValue);
  
    // Found
    if (s != NULL) return s;
  }
   
  // Otherwise, add to symbol to table. Copy to a C string first.
  char stack_buf[128];
  ResourceMark rm(THREAD);
  if (len <= 128) {
    buffer = stack_buf;
  } else {
    buffer = NEW_RESOURCE_ARRAY_IN_THREAD(THREAD, char, len);
  }
  for (int i=0; i<len; i++) {
    buffer[i] = name[i];
  }
  // Make sure there is no safepoint in the code above since name can't move.
  // We can't include the code in No_Safepoint_Verifier because of the
  // ResourceMark.

  symbolOop result = the_table()->basic_add(index, (u1*)buffer, len, hashValue, CHECK_NULL);
  return result;
}

symbolOop SymbolTable::lookup_only(const char* name, int len,
                                   unsigned int& hash) {  
  hash = hash_symbol(name, len);
  int index = the_table()->hash_to_index(hash);

  return the_table()->lookup(index, name, len, hash);
}

void SymbolTable::add(constantPoolHandle cp, int names_count,
                      const char** names, int* lengths, int* cp_indices,
                      unsigned int* hashValues, TRAPS) {
  SymbolTable* table = the_table();
  bool added = table->basic_add(cp, names_count, names, lengths,
                                cp_indices, hashValues, CHECK);
  if (!added) {
    // do it the hard way
    for (int i=0; i<names_count; i++) {
      int index = table->hash_to_index(hashValues[i]);
      symbolOop sym = table->basic_add(index, (u1*)names[i], lengths[i],
                                       hashValues[i], CHECK);
      cp->symbol_at_put(cp_indices[i], sym);
    }
  }
}

// Needed for preloading classes in signatures when compiling.

symbolOop SymbolTable::probe(const char* name, int len) {
  unsigned int hashValue = hash_symbol(name, len);
  int index = the_table()->hash_to_index(hashValue);
  return the_table()->lookup(index, name, len, hashValue);
}


symbolOop SymbolTable::basic_add(int index, u1 *name, int len,
                                 unsigned int hashValue, TRAPS) {  
  // Only JavaThreads are allowed to insert new symbols.  The Pauseless GC
  // concurrent collection hasn't been designed to work with other types of
  // threads inserting.
assert0(Thread::current()->is_Java_thread());
  assert(!Universe::heap()->is_in_reserved(name) || GC_locker::is_active(),
         "proposed name of symbol must be stable");

  // We assume that lookup() has been called already, that it failed,
  // and symbol was not found.  We create the symbol here.
  symbolKlass* sk  = (symbolKlass*) Universe::symbolKlassObj()->klass_part();
  symbolOop s_oop = sk->allocate_symbol(name, len, CHECK_NULL);
  markWord *mark = s_oop->mark();
  guarantee(mark->is_unlocked(), "mark word locked unexpectedly");
  s_oop->set_mark(mark->copy_set_hash(hashValue));
  symbolHandle sym (THREAD, s_oop);

  // Allocation must be done before grapping the SymbolTable_lock lock
  MutexLocker ml(SymbolTable_lock, THREAD);

  assert(sym->equals((char*)name, len), "symbol must be properly initialized");

  // Since look-up was done lock-free, we need to check if another
  // thread beat us in the race to insert the symbol.

  symbolOop test = lookup(index, (char*)name, len, hashValue);
  if (test != NULL) {
    // A race occured and another thread introduced the symbol, this one
    // will be dropped and collected.
    return test;
  }  

  // Concurrent GC threads may unlink symbols without a lock,
  // so the insert has to be done with a CAS: see BasicHashtable::add_entry
  objectRef  s_ref     = sym.as_ref();

  WeakHashtableEntry* entry = new_entry(hashValue, ALWAYS_POISON_OBJECTREF(s_ref));

  // New symbols must be created as StrongRefs, which is the current value of the NMT bit.
  // Collectors besides GPGC don't concurrently collect here, and use a constant value of 0.
  jlong strength = UseGenPauselessGC ? GPGC_NMT::desired_old_nmt_flag() : 0;

  entry->set_strength(strength);

  add_entry(index, entry);

  return sym();
}

bool SymbolTable::basic_add(constantPoolHandle cp, int names_count,
                            const char** names, int* lengths,
                            int* cp_indices, unsigned int* hashValues,
                            TRAPS) {
  symbolKlass* sk  = (symbolKlass*) Universe::symbolKlassObj()->klass_part();
  symbolOop sym_oops[symbol_alloc_batch_size];
  bool allocated = sk->allocate_symbols(names_count, names, lengths,
                                        sym_oops, CHECK_false);
  if (!allocated) {
    return false;
  }
  symbolHandle syms[symbol_alloc_batch_size];
  int i;
  for (i=0; i<names_count; i++) {
    // setting the hash value in the mark oop for these symbol_oops here...
    unsigned int hashValue = hash_symbol(names[i], lengths[i]);
    markWord *mark = sym_oops[i]->mark();
    guarantee(mark->is_unlocked(), "mark oop locked unexpectedly");
    sym_oops[i]->set_mark(mark->copy_set_hash(hashValue));
    syms[i] = symbolHandle(THREAD, sym_oops[i]);
  }

  // Allocation must be done before grabbing the SymbolTable_lock lock
  MutexLocker ml(SymbolTable_lock, THREAD);

  for (i=0; i<names_count; i++) {
    assert(syms[i]->equals(names[i], lengths[i]), "symbol must be properly initialized");
    // Since look-up was done lock-free, we need to check if another
    // thread beat us in the race to insert the symbol.
    int       index = hash_to_index(hashValues[i]);
    symbolOop test  = lookup(index, names[i], lengths[i], hashValues[i]);
    if (test != NULL) {
      // A race occured and another thread introduced the symbol, this one
      // will be dropped and collected. Use test instead.

      // should we be doing a symbol_put?
      cp->symbol_at_put(cp_indices[i], test);
    } else {
      symbolOop           sym   = syms[i]();
objectRef s_ref=objectRef(sym);
      WeakHashtableEntry* entry = new_entry(hashValues[i], ALWAYS_POISON_OBJECTREF(s_ref));
      // New symbols must be created as StrongRefs, which is the current value of the NMT bit.
      // Collectors besides GPGC don't concurrently collect here, and use a constant value of 0.
      jlong strength = UseGenPauselessGC ? GPGC_NMT::desired_old_nmt_flag() : 0;
      entry->set_strength(strength);
      add_entry(index, entry);
      cp->symbol_at_put(cp_indices[i], sym);
    }
  }

  return true;
}

void SymbolTable::GPGC_unlink_section(long section, long sections) {
  the_table()->WeakHashtable::GPGC_unlink_section(section, sections,
                                                  GPGC_OldCollector::mark_leaf_conditional,
                                                  GPGC_OldCollector::mark_leaf_guaranteed,
                                                  NULL);
}


void SymbolTable::GC_release_pending_free(){
  // Messing with the hashtable's entry free list must be done under a lock.
  MutexLocker ml(SymbolTable_lock);
  the_table()->WeakHashtable::GC_release_pending_free();
}


void SymbolTable::GC_verify_marks(WeakOopClosure* verify) {
for(int i=0;i<the_table()->table_size();i++){
    for (WeakHashtableEntry** pe = the_table()->bucket_addr(i); *pe != NULL; ) {
      WeakHashtableEntry* e = *pe;

      verify->do_weak_oop(e->literal_addr());
      // It's important that this come after the do_oop, to ensure that *pe has already
      // been remapped if needed:
      pe = e->next_addr();
    }
  }
}


void SymbolTable::verify() {
  for (int i = 0; i < the_table()->table_size(); ++i) {
WeakHashtableEntry*p=the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
symbolOop s=symbolOop(p->literal().as_oop());
      guarantee(s != NULL, "symbol is NULL");
      s->verify();
      guarantee(s->is_perm(), "symbol not in permspace");
      unsigned int h = hash_symbol((char*)s->bytes(), s->utf8_length());
      guarantee(p->hash() == h, "broken hash in symbol table entry");
      guarantee(the_table()->hash_to_index(h) == i,
                "wrong index in symbol table");
    }
  }
}


//---------------------------------------------------------------------------
// Non-product code

#ifndef PRODUCT

void SymbolTable::GPGC_verify_strength(int index, jlong expected) {
  for (WeakHashtableEntry** pe = the_table()->bucket_addr(index); *pe != NULL; ) {
    WeakHashtableEntry* e = *pe;
    symbolRef ref = symbolRef(e->literal().raw_value());
    guarantee(ref.is_old(), "Found SymbolTable symbol outside of old_space");
    guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "Found SymbolTable symbol in trapped page");
    guarantee(e->strength() == expected, "SymbolOop in SymbolTable has unexpected ref strength")
    pe = e->next_addr();
  }
}


void SymbolTable::GPGC_verify_strong_refs(){
  jlong StrongRef = GPGC_NMT::desired_old_nmt_flag();
for(int i=0;i<the_table()->table_size();i++){
      GPGC_verify_strength(i, StrongRef);
  }
}

void SymbolTable::print_histogram() {
  MutexLocker ml(SymbolTable_lock);
  const int results_length = 100;
  int results[results_length];
  int i,j;
  
  // initialize results to zero
  for (j = 0; j < results_length; j++) {
    results[j] = 0;
  }

  int total = 0;
  int max_symbols = 0;
  int out_of_range = 0;
  for (i = 0; i < the_table()->table_size(); i++) {
WeakHashtableEntry*p=the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
int counter=symbolOop(p->literal().as_oop())->utf8_length();
      total += counter;
      if (counter < results_length) {
        results[counter]++;
      } else {
        out_of_range++;
      }
      max_symbols = MAX2(max_symbols, counter);
    }
  }
  tty->print_cr("Symbol Table:");
  tty->print_cr("%8s %5d", "Total  ", total);
  tty->print_cr("%8s %5d", "Maximum", max_symbols);
  tty->print_cr("%8s %3.2f", "Average",
	  ((float) total / (float) the_table()->table_size()));
  tty->print_cr("%s", "Histogram:");
  tty->print_cr(" %s %29s", "Length", "Number chains that length");
  for (i = 0; i < results_length; i++) {
    if (results[i] > 0) {
      tty->print_cr("%6d %10d", i, results[i]);
    }
  }
  int line_length = 70;    
  tty->print_cr("%s %30s", " Length", "Number chains that length");
  for (i = 0; i < results_length; i++) {
    if (results[i] > 0) {
      tty->print("%4d", i);
      for (j = 0; (j < results[i]) && (j < line_length);  j++) {
        tty->print("%1s", "*");
      }
      if (j == line_length) {
        tty->print("%1s", "+");
      }
      tty->cr();
    }
  }  
  tty->print_cr(" %s %d: %d\n", "Number chains longer than",
	            results_length, out_of_range);
}

#endif // PRODUCT

// --------------------------------------------------------------------------

#ifdef ASSERT
class StableMemoryChecker : public StackObj {
  enum { _bufsize = wordSize*4 };

  address _region;
  jint    _size;
  u1      _save_buf[_bufsize];

  int sample(u1* save_buf) {
    if (_size <= _bufsize) {
      memcpy(save_buf, _region, _size);
      return _size;
    } else {
      // copy head and tail
      memcpy(&save_buf[0],          _region,                      _bufsize/2);
      memcpy(&save_buf[_bufsize/2], _region + _size - _bufsize/2, _bufsize/2);
      return (_bufsize/2)*2;
    }
  }

 public:
  StableMemoryChecker(const void* region, jint size) {
    _region = (address) region;
    _size   = size;
    sample(_save_buf);
  }

  bool verify() {
    u1 check_buf[sizeof(_save_buf)];
    int check_size = sample(check_buf);
    return (0 == memcmp(_save_buf, check_buf, check_size));
  }

  void set_region(const void* region) { _region = (address) region; }
};
#endif


// --------------------------------------------------------------------------


// Compute the hash value for a java.lang.String object which would
// contain the characters passed in. This hash value is used for at
// least two purposes.
//
// (a) As the hash value used by the StringTable for bucket selection
//     and comparison (stored in the WeakHashtableEntry structures).  This
//     is used in the String.intern() method.
//
// (b) As the hash value used by the String object itself, in
//     String.hashCode().  This value is normally calculate in Java code
//     in the String.hashCode method(), but is precomputed for String
//     objects in the shared archive file.
//
//     For this reason, THIS ALGORITHM MUST MATCH String.hashCode().

int StringTable::hash_string(jchar* s, int len) {
  unsigned h = 0;
  while (len-- > 0) {
    h = 31*h + (unsigned) *s;
    s++;
  }
  return h;
}


StringTable* StringTable::_the_table = NULL;

bool StringTable::ensure_strong_ref(WeakHashtableEntry* l, oop literal_string) {
  assert0(UseGenPauselessGC);
  assert0(literal_string == l->remapped_literal());

  WeakHashtableEntry::EnsuredStrength strength = l->ensure_strong_ref();
  
  if (strength == WeakHashtableEntry::IsDead) {
    return false;
  }

  if (strength == WeakHashtableEntry::MustMark) {
    GPGC_OldCollector::mutator_mark_leaf_string_conditional(literal_string);
    return true;
  }

  // In addition to the string being StrongRef, we have to make sure that its klassRef,
  // value, and value->klassRef fields are remapped and NMTed properly.  We don't mark
  // through these fields, however.
  //
  // What we're depending upon for this to work:
  //
  // 1. literal_string->klass_addr better be the klass for java.lang.String, which either
  // has been or will be marked via SystemDictionary::string_klass().
  // 
  // 2. literal_string->value better be a typeArray of Char
  //
  // 3. Type arrays better have no embedded heapRefs aside from their Klass, which either
  // has been or will be marked via the SystemDictionary.
  GPGC_OldCollector::remap_and_nmt_only(literal_string->klass_addr());
  oop value = GPGC_OldCollector::remap_and_nmt_only((heapRef*)java_lang_String::value_addr(literal_string)).as_oop();
  GPGC_OldCollector::remap_and_nmt_only(value->klass_addr());
  return true;
}


oop StringTable::lookup(int index, jchar* name,
                        int len, unsigned int hash) {
for(WeakHashtableEntry*l=bucket(index);l!=NULL;l=l->next()){
    if (l->hash() == hash) {
      // We extract literal_string once, so we don't keep running remap code on it.
      oop literal_string = l->remapped_literal();
      if (java_lang_String::GC_weak_equals(literal_string, name, len)) {
        if ( UseGenPauselessGC ) {
          // Pauseless GC engages in concurrent lockless collection of the StringTable.
          // See the design note in symbolTable.hpp for details.
          if ( ! ensure_strong_ref(l, literal_string) ) {
            // If the string isn't strongly referenced, we don't have to continue to
            // search down the hash chain.  Any live string will be inserted into the
            // hash chain after this dead one we've found, so it is earlier in the
            // hash chain.
            return NULL;
          }
        }
        return literal_string;
      }
    }
  }
  return NULL;
}


oop StringTable::basic_add(int index, Handle string_or_null, jchar* name,
                           int len, unsigned int hashValue, TRAPS) {  
  // Only JavaThreads are allowed to insert new strings.  The Pauseless GC
  // concurrent collection hasn't been designed to work with other types of
  // threads inserting.
assert0(Thread::current()->is_Java_thread());
  debug_only(StableMemoryChecker smc(name, len * sizeof(name[0])));
  assert(!Universe::heap()->is_in_reserved(name) || GC_locker::is_active(),
         "proposed name of symbol must be stable");

  Handle string;
  // try to reuse the string if possible
if(!string_or_null.is_null()&&
!string_or_null.as_ref().is_stack()&&
      string_or_null()->is_perm()) {
    string = string_or_null;
  } else {
    string = java_lang_String::create_tenured_from_unicode(name, len, CHECK_NULL);
  }

  // Allocation must be done before grabbing the SymbolTable_lock lock
  MutexLocker ml(StringTable_lock, THREAD);

  assert(java_lang_String::equals(string(), name, len),
         "string must be properly initialized");

  // Since look-up was done lock-free, we need to check if another
  // thread beat us in the race to insert the symbol.

  oop test = lookup(index, name, len, hashValue); // calls lookup(u1*, int)
  if (test != NULL) {
    // Entry already added
    return test;
  }  

  objectRef s_ref = objectRef(string());

  // Concurrent GC threads may unlink symbols without a lock,
  // so the insert has to be done with a CAS: see BasicHashtable::add_entry

  WeakHashtableEntry* entry = new_entry(hashValue, ALWAYS_POISON_OBJECTREF(s_ref));
  
  // New strings must be created as StrongRefs, which is the current value of the NMT bit.
  // Collectors besides GPGC don't concurrently collect here, and use a constant value of 0.
  jlong strength = UseGenPauselessGC ? GPGC_NMT::desired_old_nmt_flag() : 0;
  entry->set_strength(strength);

  add_entry(index, entry);

  return string();
}


static void GPGC_fully_mark_live_string(oop string) {
  // Just because the mutator marks a string instance live doesn't mean that the attached
  // character array got marked.  The heapRef for the char array may have been NMTed during
  // lookup, and thus avoided the normal ref traversal when the symbol got marked live.  We
  // can be sure that the heapRef for the char array has an updated NMT bit.
  heapRef* value_ref_addr = (heapRef*) java_lang_String::value_addr(string);
  heapRef  value_ref      = GPGC_OldCollector::remap_and_nmt_only(value_ref_addr);
  oop      value_oop      = value_ref.as_oop();

  assert0(value_ref.is_old());
  assert0(GPGC_NMT::has_desired_old_nmt(value_ref));
  assert0(!GPGC_ReadTrapArray::is_remap_trapped(value_ref));

  GPGC_OldCollector::mark_leaf_conditional(value_ref.as_oop());
}

void StringTable::GPGC_unlink_section(long section, long sections) {
  the_table()->WeakHashtable::GPGC_unlink_section(section, sections,
                                                  GPGC_OldCollector::mark_leaf_string_conditional,
                                                  GPGC_OldCollector::mark_leaf_string_guaranteed,
                                                  GPGC_fully_mark_live_string);
}


void StringTable::GC_release_pending_free(){
  // Messing with the hashtable's entry free list must be done under a lock.
MutexLocker ml(StringTable_lock);
  the_table()->WeakHashtable::GC_release_pending_free();
}


oop StringTable::lookup(symbolOop symbol) {
  ResourceMark rm;
  int length;
  jchar* chars = symbol->as_unicode(length);
  unsigned int hashValue = hash_string(chars, length);
  int index = the_table()->hash_to_index(hashValue);
  return the_table()->lookup(index, chars, length, hashValue);
}


oop StringTable::intern(Handle string_or_null, jchar* name,
                        int len, TRAPS) {
  unsigned int hashValue = hash_string(name, len);
  int index = the_table()->hash_to_index(hashValue);
  oop string = the_table()->lookup(index, name, len, hashValue);

  // Found
  if (string != NULL) return string;
  
  // Otherwise, add symbol to table
  return the_table()->basic_add(index, string_or_null, name, len,
                                hashValue, CHECK_NULL);  
}

oop StringTable::intern(symbolOop symbol, TRAPS) {
  if (symbol == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length;
  jchar* chars = symbol->as_unicode(length);
  Handle string;
  oop result = intern(string, chars, length, CHECK_NULL);
  return result;
}


oop StringTable::intern(oop string, TRAPS)
{
  if (string == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length;
  Handle h_string (THREAD, string);
  jchar* chars = java_lang_String::as_unicode_string(string, length);
  oop result = intern(h_string, chars, length, CHECK_NULL);
  return result;
}


oop StringTable::intern(const char* utf8_string, TRAPS) {
  if (utf8_string == NULL) return NULL;
  ResourceMark rm(THREAD);
  int length = UTF8::unicode_length(utf8_string);
  jchar* chars = NEW_RESOURCE_ARRAY(jchar, length);
  UTF8::convert_to_unicode(utf8_string, chars, length);
  Handle string;
  oop result = intern(string, chars, length, CHECK_NULL);
  return result;
}


void StringTable::GC_verify_marks(WeakOopClosure* verify) {
for(int i=0;i<the_table()->table_size();i++){
    for (WeakHashtableEntry** pe = the_table()->bucket_addr(i); *pe != NULL; ) {
      WeakHashtableEntry* e = *pe;

      assert(e->literal_addr()->not_null(), "just checking");
      verify->do_weak_oop(e->literal_addr());
      pe = e->next_addr();
    }
  }
}

void StringTable::verify() {
  for (int i = 0; i < the_table()->table_size(); ++i) {
WeakHashtableEntry*p=the_table()->bucket(i);
    for ( ; p != NULL; p = p->next()) {
oop s=p->literal().as_oop();
      guarantee(s != NULL, "interned string is NULL");
      guarantee(s->is_perm(), "interned string not in permspace");

      int length;
      jchar* chars = java_lang_String::as_unicode_string(s, length);
      unsigned int h = hash_string(chars, length);
      guarantee(p->hash() == h, "broken hash in string table entry");
      guarantee(the_table()->hash_to_index(h) == i,
                "wrong index in string table");
    }
  }
}

//-----------------------------------------------------------------------------------------------------
// Non-product code
#ifndef PRODUCT

void StringTable::GPGC_verify_strength(int index, jlong expected) {
WeakHashtableEntry*p=the_table()->bucket(index);
  for ( ; p != NULL; p = p->next()) {
objectRef string=p->literal();

    guarantee(string.is_old(), "Found StringTable string outside of old_space");
    guarantee(!GPGC_ReadTrapArray::is_remap_trapped(string), "Found StringTable string in trapped page");
guarantee(p->strength()==expected,"String in StringTable has unexpected ref strength");
  }
}


void StringTable::GPGC_verify_strong_refs(){
  jlong StrongRef = GPGC_NMT::desired_old_nmt_flag();
  for (int i = 0; i < the_table()->table_size(); ++i) {
    GPGC_verify_strength(i, StrongRef);
  }
}

#endif // PRODUCT
