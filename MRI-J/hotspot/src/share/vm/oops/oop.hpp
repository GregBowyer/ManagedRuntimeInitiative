/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OOP_HPP
#define OOP_HPP


#include "atomic.hpp"
#include "exceptions.hpp"
#include "markWord.hpp"
#include "memRegion.hpp"
#include "oopsHierarchy.hpp"
#include "objectMonitor.hpp"
#include "specialized_oop_closures.hpp"

// oopDesc is the top baseclass for objects classes.  The {name}Desc classes describe
// the format of Java objects so the fields can be accessed from C++.
// oopDesc is abstract.
// (see oopHierarchy for complete oop class hierarchy)
//
// no virtual functions allowed

// Forward declarations.
class BarrierSet;
class BoolObjectClosure;
class FastScanClosure;
class FilteringClosure;
class GPGC_GCManagerNewFinal;
class GPGC_GCManagerNewStrong;
class GPGC_GCManagerOldFinal;
class GPGC_GCManagerOldStrong;
class OopClosure;
class PGC_FullGCManager;
class PSPromotionManager;
class ParCompactionManager;
class ScanClosure;
class klassRef;
class objectRef;

class oopDesc {
 private:
markWord*volatile _mark;//the pointer is really the mark word

  // Fast access to barrier set.  Must be initialized.
  static BarrierSet* _bs;

 public:
markWord*mark()const{return _mark;}
markWord**mark_addr()const{return(markWord**/*discards const*/)&_mark;}

//Not valid for possibly shared objects, but should work e.g. on new not-escaped objects
void set_mark(markWord*m){_mark=m;}
//markWord updates once the object is multi-threaded
markWord*cas_set_mark(markWord*new_mark,markWord*old_mark){
assert0(old_mark->kid()!=0);
    assert0( new_mark->kid() != 0 );
    return (markWord*) Atomic::cmpxchg_ptr(new_mark, &_mark, old_mark);
  }

  // Lock this object with standard java-visible locking.  
  // May block.  May GC.  May throw exceptions.
  void lock(MonitorInflateCallerId reason, TRAPS) { 
    if( mark()->is_self_spec_locked() ) return;
    slow_lock(reason, THREAD); // May block.  May GC.  May throw exceptions.
  }
  // May block.  May GC.  May throw exceptions (any async exception, but
  // e.g. also InterruptedException).
  void slow_lock(MonitorInflateCallerId reason, TRAPS);
  // Cannot block, GC, nor throw exceptions.
  void unlock() { 
    if( mark()->is_self_spec_locked() ) return;
    slow_unlock();
  }
  // Cannot block, GC, nor throw exceptions.
  void slow_unlock();
  // Check for being self-locked.  A little more efficient than
  // "mark->lock_owner()==JavaThread::current()"
  bool is_self_locked( ) { return mark()->is_self_locked(); }
  // Best-effort at thread that currently holds lock (if any)
  JavaThread* lock_owner( ) { return mark()->lock_owner(); }


  // Used only to re-initialize the mark word (e.g., of promoted
  // objects during a GC) -- requires a valid klass pointer
  void init_mark();
  void clear_mark();
  void clear_and_mark();
  void clear_fwd_ptr_mark();

  klassOop klass() const;
  klassRef klass_ref() const;
  klassOop GC_remapped_klass() const;
  klassRef* klass_addr() const;

  // size of object header
  static int header_size()      { return sizeof(oopDesc)/HeapWordSize; }
  static int header_size_in_bytes() { return sizeof(oopDesc); }

  Klass* blueprint() const;

  // Returns whether this is an instance of k or an instance of a subclass of k
  bool is_a(klassOop k)  const;

  // Returns the actual oop size of the object
  int size();
  
  // Sometimes (for complicated concurrency-related reasons), it is useful
  // to be able to figure out the size of an object knowing its klass.
  int size_given_klass(Klass* klass);

  // Same as size_given_klass() but safe for use by GC threads during a GC cycle,
  // when we can't be sure that oop->_klass is a valid heapRef.
int GC_size_given_klass(Klass*klass);

  // Some perm gen objects are not parseble immediately after
  // installation of their klass pointer.
  bool is_parsable();

  // type test operations (inlined in oop.inline.h)
  bool is_instance()           const;
  bool is_instanceRef()        const;
  bool is_array()              const;
  bool is_objArray()           const;
  bool is_symbol()             const;
  bool is_klass()              const;
  bool is_thread()             const;
  bool is_method()             const;
  bool is_methodCode()         const;
  bool is_dependency()         const;
  bool is_constMethod()        const;
  bool is_constantPool()       const;
  bool is_constantPoolCache()  const;
  bool is_typeArray()          const;
  bool is_javaArray()          const;

 private:
  // field addresses in oop
  // byte/char/bool/short fields are always stored as full words
  void*     field_base(int offset)        const;

  jbyte*    byte_field_addr(int offset)   const;
  jchar*    char_field_addr(int offset)   const;
  jboolean* bool_field_addr(int offset)   const;
  jint*     int_field_addr(int offset)    const;
  jshort*   short_field_addr(int offset)  const;
  jlong*    long_field_addr(int offset)   const;
  jfloat*   float_field_addr(int offset)  const;
  jdouble*  double_field_addr(int offset) const;

 public:
  // need this as public for garbage collection
  oop* obj_field_addr(int offset) const;
objectRef*ref_field_addr(int offset)const;

  oop obj_field(int offset) const;
objectRef ref_field(int offset)const;
oop GC_remapped_obj_field(int offset)const;
  void obj_field_put(int offset, oop value);
void ref_field_put(int offset,objectRef value);
  static void ref_field_put(objectRef thsi, int offset, objectRef value);

  jbyte byte_field(int offset) const;
  void byte_field_put(int offset, jbyte contents);

  jchar char_field(int offset) const;
  void char_field_put(int offset, jchar contents);

  jboolean bool_field(int offset) const;
  void bool_field_put(int offset, jboolean contents);

  jint int_field(int offset) const;
  void int_field_put(int offset, jint contents);

  jshort short_field(int offset) const;
  void short_field_put(int offset, jshort contents);

  jlong long_field(int offset) const;
  void long_field_put(int offset, jlong contents);

  jfloat float_field(int offset) const;
  void float_field_put(int offset, jfloat contents);

  jdouble double_field(int offset) const;
  void double_field_put(int offset, jdouble contents);

  oop obj_field_acquire(int offset) const;
  void release_obj_field_put(int offset, oop value);
objectRef ref_field_acquire(int offset)const;
  static void  release_ref_field_put(objectRef thsi, int offset, objectRef value);

  jbyte byte_field_acquire(int offset) const;
  void release_byte_field_put(int offset, jbyte contents);

  jchar char_field_acquire(int offset) const;
  void release_char_field_put(int offset, jchar contents);

  jboolean bool_field_acquire(int offset) const;
  void release_bool_field_put(int offset, jboolean contents);

  jint int_field_acquire(int offset) const;
  void release_int_field_put(int offset, jint contents);

  jshort short_field_acquire(int offset) const;
  void release_short_field_put(int offset, jshort contents);

  jlong long_field_acquire(int offset) const;
  void release_long_field_put(int offset, jlong contents);

  jfloat float_field_acquire(int offset) const;
  void release_float_field_put(int offset, jfloat contents);

  jdouble double_field_acquire(int offset) const;
  void release_double_field_put(int offset, jdouble contents);

  // printing functions for VM debugging
  void print_on(outputStream* st) const;         // First level print 
  void print_value_on(outputStream* st) const;   // Second level print.
  void print_address_on(outputStream* st) const; // Address printing

  // printing for ARTA
  void print_xml_on(xmlBuffer *xb, bool ref) const; 

  // printing on default output stream
void print(outputStream*st);
  void print_value();
  void print_address();

  // return the print strings
  char* print_string();
  char* print_value_string();

  // verification operations
  void verify_on(outputStream* st);
  void verify();
  void verify_old_oop(objectRef* p, bool allow_dirty);

  // tells whether this oop is partially constructed (gc during class loading)
  bool partially_loaded();
  void set_partially_loaded();

  // asserts
  bool is_oop(bool ignore_mark_word = false) const;
  bool is_oop_or_null(bool ignore_mark_word = false) const;

  // garbage collection
  bool is_gc_marked() const;
  // Apply "MarkSweep::mark_and_push" to (the address of) every non-NULL
  // reference field in "this".
  void follow_contents();
  void follow_header();

  // Pauseless GC
  void PGC_follow_contents(PGC_FullGCManager* fgcm); // for full gc

  // Generational Pauseless GC
  void GPGC_follow_contents(GPGC_GCManagerNewStrong* gcm); // for NewGen GC strong marking
  void GPGC_follow_contents(GPGC_GCManagerOldStrong* gcm); // for OldGen GC strong marking
  void GPGC_follow_contents(GPGC_GCManagerNewFinal* gcm);  // for NewGen GC final marking
  void GPGC_follow_contents(GPGC_GCManagerOldFinal* gcm);  // for OldGen GC final marking
  void GPGC_newgc_update_cardmark();                       // For NewGen GC
  void GPGC_oldgc_update_cardmark();                       // For OldGen GC
  void GPGC_mutator_update_cardmark();                     // For Mutator relocation

  // Parallel Scavenge
  void copy_contents(PSPromotionManager* pm);
  void push_contents(PSPromotionManager* pm);

  // Parallel Old 
  void update_contents(ParCompactionManager* cm);
  void update_contents(ParCompactionManager* cm,
		       HeapWord* begin_limit,
		       HeapWord* end_limit);
  void update_contents(ParCompactionManager* cm,
		       klassOop old_klass,
		       HeapWord* begin_limit,
	               HeapWord* end_limit);

  void follow_contents(ParCompactionManager* cm);
  void follow_header(ParCompactionManager* cm);

  bool is_perm() const;
  bool is_shared() const;
  bool is_shared_readonly() const;
  bool is_shared_readwrite() const;

  // Forward pointer operations for scavenge
  bool is_forwarded() const { return mark()->is_marked(); }

void forward_to_pointer(void*p);

void forward_to_ref(objectRef p);
  bool cas_forward_to_ref(objectRef p, markWord *compare);

  void* forwarded_pointer() const;
objectRef forwarded_ref()const;

  // Age of object during scavenge
  int age() const { assert0(!is_forwarded()); return mark()->age(); }
  void incr_age() { assert0(!is_forwarded()); set_mark(mark()->incr_age()); }

  // SMA abandons
  int sma_abandons() const { assert0(!is_forwarded()); return mark()->sma(); }
  void incr_sma_abandons() { assert0(!is_forwarded()); set_mark(mark()->incr_sma()); }

  // Adjust all pointers in this object to point at it's forwarded location and
  // return the size of this oop.  This is used by the MarkSweep collector.
  int adjust_pointers();

  // mark-sweep support
  void follow_body(int begin, int end);

  // Fast access to barrier set
  static BarrierSet* bs()            { return _bs; }
  static void set_bs(BarrierSet* bs) { _bs = bs; }

  // iterators, returns size of object
#define OOP_ITERATE_DECL(OopClosureType, nv_suffix)                             \
  int oop_iterate(OopClosureType* blk);                                  \
  int oop_iterate(OopClosureType* blk, MemRegion mr);  // Only in mr.

  ALL_OOP_OOP_ITERATE_CLOSURES_1(OOP_ITERATE_DECL) 
  ALL_OOP_OOP_ITERATE_CLOSURES_3(OOP_ITERATE_DECL) 

  void oop_iterate_header(OopClosure* blk);
  void oop_iterate_header(OopClosure* blk, MemRegion mr);

  // identity hash; returns the identity hash key (computes it if necessary)
  intptr_t identity_hash();
  intptr_t slow_identity_hash();

  // for code generation
  static int mark_offset_in_bytes()    { return offset_of(oopDesc, _mark); }
};
#endif // OOP_HPP
