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
#ifndef HASHTABLE_HPP
#define HASHTABLE_HPP



#include "codeBlob.hpp"
#include "lvb.hpp"
#include "objectRef_pd.hpp"
#include "symbolOop.hpp"
class BoolObjectClosure;
class OopClosure;

 // FIXME This is the current Azul code. Check for collisions.
class HashEntry:public ResourceObj{
 private:
HashEntry*_next;

  void* vtable() { return *(void**)this; }

 public:
  HashEntry() {
_next=NULL;
  }

HashEntry*next()const{return _next;}
void set_next(HashEntry*next){_next=next;}

  virtual uint hash_code() = 0;
  virtual bool equals(HashEntry* entry) = 0;

  static bool equal_type(HashEntry* left, HashEntry* right) {
    return (left->vtable() == right->vtable());
  }
};


class HashTable:public ResourceObj{
 private:
uint _table_size;
uint _entry_count;
HashEntry**_table;

 public:
  HashTable(int table_size) {
    assert0(table_size > 0);
    _table_size = (uint)table_size;
    _entry_count = 0;
    _table = (HashEntry**)resource_allocate_bytes(table_size * sizeof(HashEntry*));
NEEDS_CLEANUP;//is resource area cleaned?
for(int i=0;i<table_size;i++){
      _table[i] = NULL;
    }
  }

uint size()const{return _table_size;}
  HashEntry* at(uint idx) const { assert0((idx >= 0) && (idx < _table_size)); return _table[idx]; };
  uint entry_count()      const { return _entry_count; }

  HashEntry* find(HashEntry* key) {
    uint idx = key->hash_code() % _table_size;
    HashEntry* cur = _table[idx];
    while (cur != NULL) {
      if (cur->equals(key)) {
        return cur;
      }
      cur = cur->next();
    }
    return NULL;
  }


  void insert(HashEntry* entry) {
assert(find(entry)==NULL,"should not insert same entry twice");
assert(entry->next()==NULL,"cannot be part of existing chain");
    uint idx = entry->hash_code() % _table_size;
entry->set_next(_table[idx]);
_table[idx]=entry;
    _entry_count++;
  }


  void remove(HashEntry* key) {
    uint idx = key->hash_code() % _table_size;
    HashEntry* cur = _table[idx];
HashEntry*prev=NULL;
while(cur!=NULL){
      if (cur->equals(key)) {
        if (prev == NULL) {
_table[idx]=cur->next();
        } else {
prev->set_next(cur->next());
        }
        _entry_count--;
        return;
      }
      cur = cur->next();
    }
  }
};


class HashTableIterator VALUE_OBJ_CLASS_SPEC{
 private:
  HashTable* _hash_table;
uint _cur_idx;
  HashEntry* _cur_entry;

 public:
  HashTableIterator(HashTable* hash_table) {
    _hash_table = hash_table;
    _cur_idx = 0;
_cur_entry=NULL;
  }

  HashEntry* entry() const {
    return _cur_entry;
  }

  bool next() {
    if (_cur_entry != NULL) {
      // get next entry in chain
      _cur_entry = _cur_entry->next();
    }
    while ((_cur_entry == NULL) && (_cur_idx < _hash_table->size())) {
      _cur_entry = _hash_table->at(_cur_idx); // get header of next bucket
      _cur_idx++; // advance to next bucket
    }
return(_cur_entry!=NULL);
  }
};

// This is a generic hashtable, designed to be used for the symbol
// and string tables.
//
// It is implemented as an open hash table with a fixed number of buckets.
//
// %note:
//  - TableEntrys are allocated in blocks to reduce the space overhead.


class BasicHashtableEntry : public CHeapObj {
  friend class VMStructs;
private:
  unsigned int         _hash;           // 32-bit hash for item

  // Link to next element in the linked list for this bucket.  EXCEPT
  // bit 0 set indicates that this entry is shared and must not be
  // unlinked from the table. Bit 0 is set during the dumping of the
  // archive. Since shared entries are immutable, _next fields in the
  // shared entries will not change.  New entries will always be
  // unshared and since pointers are align, bit 0 will always remain 0
  // with no extra effort.
  BasicHashtableEntry* _next;

  // Windows IA64 compiler requires subclasses to be able to access these
protected:
  // Entry objects should not be created, they should be taken from the
  // free list with BasicHashtable.new_entry().
  BasicHashtableEntry() { ShouldNotReachHere(); }
  // Entry objects should not be destroyed.  They should be placed on
  // the free list instead with BasicHashtable.free_entry().
  ~BasicHashtableEntry() { ShouldNotReachHere(); }

public:

  unsigned int hash() const             { return _hash; }
  void set_hash(unsigned int hash)      { _hash = hash; }
  unsigned int* hash_addr()             { return &_hash; }

  static BasicHashtableEntry* make_ptr(BasicHashtableEntry* p) {
    return (BasicHashtableEntry*)((intptr_t)p & -2);
  }

  BasicHashtableEntry* next() const {
    return make_ptr(_next);
  }

  void set_next(BasicHashtableEntry* next) {
    _next = next;
  }

  BasicHashtableEntry** next_addr() {
    return &_next;
  }

  bool is_shared() const {
    return ((intptr_t)_next & 1) != 0;
  }

  void set_shared() {
    _next = (BasicHashtableEntry*)((intptr_t)_next | 1);
  }
};



class HashtableEntry : public BasicHashtableEntry {
  friend class VMStructs;
private:
  objectRef               _literal;          // ref to item in table.

public:
  // Literal
objectRef literal()const{return lvb_ref(&_literal);}
  oop        remapped_literal() const   { return LVB::mutator_relocate_only(&_literal).as_oop(); }
  objectRef* literal_addr()             { return &_literal; }
  void set_literal(objectRef s)         { _literal = s; }

  HashtableEntry* next() const {
    return (HashtableEntry*)BasicHashtableEntry::next();
  }
  HashtableEntry** next_addr() {
    return (HashtableEntry**)BasicHashtableEntry::next_addr();
  }
};


class WeakHashtableEntry:public HashtableEntry{
  friend class VMStructs;

private:
  jlong                _strength;  // Track this entry as being, Strong, Weak, or Dead.
  WeakHashtableEntry*  _next_free; // Linked list of entries being cleared.

public:
  enum EnsuredStrength {
    IsDead = 0,   // GC marked it DeadRef before we read it
    IsStrong,     // Someone else already marked it live
    MustMark      // We changed it to StrongRef, now we must mark the object
  };
    
  // strength access: Used by Pauseless GC for concurrent collection of SymbolTable and StringTable
  jlong strength()                { return _strength; }
  jlong* strength_addr()          { return &_strength; }
  void set_strength(jlong str)    { _strength = str; }

  void set_next_free(WeakHashtableEntry* next) { _next_free = next; }
  WeakHashtableEntry* get_next_free()          { return _next_free; }

  EnsuredStrength ensure_strong_ref();

WeakHashtableEntry*next()const{
return(WeakHashtableEntry*)HashtableEntry::next();
  }
WeakHashtableEntry**next_addr(){
return(WeakHashtableEntry**)HashtableEntry::next_addr();
  }
};


class HashtableBucket : public CHeapObj {
  friend class VMStructs;
private:
  // Instance variable
  BasicHashtableEntry*       _entry;

public:
  // Accessing
inline void clear(){_entry=NULL;}

  // The following methods use order access methods to avoid race
  // conditions in multiprocessor systems.
  BasicHashtableEntry* get_entry() const;
  void set_entry(BasicHashtableEntry* l);

  // The following method is not MT-safe and must be done under lock.
inline BasicHashtableEntry**entry_addr(){return&_entry;}
};


class BasicHashtable : public CHeapObj {
  friend class VMStructs;

public:
  BasicHashtable(int table_size, int entry_size);
  BasicHashtable(int table_size, int entry_size,
                 HashtableBucket* buckets, int number_of_entries);

  // Sharing support.
  void copy_buckets(char** top, char* end);
  void copy_table(char** top, char* end);

  // Bucket handling
inline int hash_to_index(unsigned int full_hash){
    int h = full_hash % _table_size;
    assert(h >= 0 && h < _table_size, "Illegal hash value");
    return h;
  }

  // Reverse the order of elements in each of the buckets.
  void reverse();

private:
  // Instance variables
  int                  _table_size;
  HashtableBucket*     _buckets;
  BasicHashtableEntry* _free_list;
  char*                _first_free_entry;
  char*                _end_block;
  int                  _entry_size;
jlong _number_of_entries;

protected:

#ifdef ASSERT
  int               _lookup_count;
  int               _lookup_length;
  void verify_lookup_length(double load);
#endif

  void initialize(int table_size, int entry_size, int number_of_entries);

  // Accessor
inline int entry_size()const{return _entry_size;}
inline int table_size()const{return _table_size;}

  // The following method is MT-safe and may be used with caution.
inline BasicHashtableEntry*bucket(int i)const;

  // The following method is not MT-safe and must be done under lock.
BasicHashtableEntry**bucket_addr(int i)const{return _buckets[i].entry_addr();}

  // Table entry management
  BasicHashtableEntry* new_entry(unsigned int hashValue);

  // Free list management
void decrement_number_of_entries(int delta);
void append_free_entry(BasicHashtableEntry*entry);

public:
  void set_entry(int index, BasicHashtableEntry* entry);

  void add_entry(int index, BasicHashtableEntry* entry);

  void free_entry(BasicHashtableEntry* entry);

  inline int number_of_entries() { return _number_of_entries; }

  void verify() PRODUCT_RETURN;
};


class Hashtable : public BasicHashtable {
  friend class VMStructs;

public:
  Hashtable(int table_size, int entry_size)
    : BasicHashtable(table_size, entry_size) { }

  Hashtable(int table_size, int entry_size,
                   HashtableBucket* buckets, int number_of_entries)
    : BasicHashtable(table_size, entry_size, buckets, number_of_entries) { }

  // Invoke "f->do_oop" on the locations of all oops in the table.
  void oops_do(OopClosure* f);

  // Debugging
  void print()               PRODUCT_RETURN;

  // GC support
  //   Delete pointers to otherwise-unreachable objects.
  void unlink(BoolObjectClosure* cl); 

  // Reverse the order of elements in each of the buckets. Hashtable
  // entries which refer to objects at a lower address than 'boundary'
  // are separated from those which refer to objects at higher
  // addresses, and appear first in the list.
  void reverse(void* boundary = NULL);

protected:

  inline static unsigned int hash_symbol(const char* s, int len);

  unsigned int compute_hash(symbolHandle name) {
    return (unsigned int) name->identity_hash();
  }

  int index_for(symbolHandle name) {
    return hash_to_index(compute_hash(name));
  }

  // Table entry management
  HashtableEntry* new_entry(unsigned int hashValue, objectRef obj);

  // The following method is MT-safe and may be used with caution.
HashtableEntry*bucket(int i)const{
    return (HashtableEntry*)BasicHashtable::bucket(i);
  }

  // The following method is not MT-safe and must be done under lock.
HashtableEntry**bucket_addr(int i)const{
    return (HashtableEntry**)BasicHashtable::bucket_addr(i);
  }
};


//  Version of Hashtable where each entry in the hashtable is a weak ref to the
//  objectRef in question.
class WeakHashtable:public Hashtable{
  friend class VMStructs;

public:
  inline WeakHashtable(int table_size, int entry_size)
    : Hashtable(table_size, entry_size), _pending_free_head(NULL), _pending_free_tail(NULL) {}

  inline WeakHashtable(int table_size, int entry_size,
                       HashtableBucket* buckets, int number_of_entries)
:Hashtable(table_size,entry_size,buckets,number_of_entries){}

  // GC support
  //   Delete pointers to otherwise-unreachable objects.
  void GPGC_unlink_section(long section, long sections,
                           void (*conditional_mark)   (oop obj),
                           void (*guaranteed_mark)    (oop obj),
                           void (*fully_mark_live_obj)(oop obj));
  void GC_release_pending_free();

private:
  WeakHashtableEntry* _pending_free_head;
  WeakHashtableEntry* _pending_free_tail;

protected:
  // Table entry management
  WeakHashtableEntry* new_entry(unsigned int hashValue, objectRef obj) {
    return (WeakHashtableEntry*)Hashtable::new_entry(hashValue, obj);
  }

  // The following method is MT-safe and may be used with caution.
WeakHashtableEntry*bucket(int i){
return(WeakHashtableEntry*)Hashtable::bucket(i);
  }

  // The following method is not MT_safe and must be done under lock.
WeakHashtableEntry**bucket_addr(int i){
return(WeakHashtableEntry**)Hashtable::bucket_addr(i);
  }
};


//  Verions of hashtable where two handles are used to compute the index.
class TwoOopHashtable : public Hashtable {
  friend class VMStructs;
protected:
  TwoOopHashtable(int table_size, int entry_size)
    : Hashtable(table_size, entry_size) {}

  TwoOopHashtable(int table_size, int entry_size, HashtableBucket* t,
                  int number_of_entries)
    : Hashtable(table_size, entry_size, t, number_of_entries) {}

public:
  unsigned int compute_hash(symbolHandle name, Handle loader) {
    // Be careful with identity_hash(), it can safepoint and if this 
    // were one expression, the compiler could choose to unhandle each 
    // oop before calling identity_hash() for either of them.  If the first
    // causes a GC, the next would fail.
    unsigned int name_hash = name->identity_hash();
    unsigned int loader_hash = loader.is_null() ? 0 : loader->identity_hash();
    return name_hash ^ loader_hash;
  }

  int index_for(symbolHandle name, Handle loader) {
    return hash_to_index(compute_hash(name, loader));
  }
};

#endif // HASHTABLE_HPP
