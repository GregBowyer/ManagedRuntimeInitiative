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
#ifndef ITERATOR_HPP
#define ITERATOR_HPP


#include "memRegion.hpp"
#include "objectRef_pd.hpp"
#include "prefetch.hpp"
class CodeBlob;
class ReferenceProcessor;

// The following classes are C++ `closures` for iterating over objects, roots and spaces

// OopClosure is used for iterating through roots (oop*)

class OopClosure : public StackObj {
 public:
  ReferenceProcessor* _ref_processor;
  OopClosure(ReferenceProcessor* rp) : _ref_processor(rp) { }
  OopClosure() : _ref_processor(NULL) { }
  virtual void do_oop(objectRef* o) = 0;
  virtual void do_oop_v(objectRef* o) { do_oop(o); }
  virtual void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr);

  // In support of post-processing of weak links of KlassKlass objects;
  // see KlassKlass::oop_oop_iterate().
  virtual const bool should_remember_klasses() const { return false;    }
  virtual void remember_klass(Klass* k) { /* do nothing */ }

  // The methods below control how object iterations invoking this closure
  // should be performed:

  // If "true", invoke on header klass field.
  bool do_header() { return true; } // Note that this is non-virtual.
  // Controls how prefetching is done for invocations of this closure.
  Prefetch::style prefetch_style() { // Note that this is non-virtual.
    return Prefetch::do_none;
  } 
};

// WeakOopClosure is for iterating through roots where special handling is
// needed for weak roots.  

class WeakOopClosure:public OopClosure{
 public:
virtual void do_weak_oop(objectRef*o)=0;
};

// ObjectClosure is used for iterating through an object space

class ObjectClosure : public StackObj {
 public:
  // Called for each object.
  virtual void do_object(oop obj) = 0;
};


class BoolObjectClosure : public ObjectClosure {
 public:
  virtual bool do_object_b(oop obj) = 0;
};

class BoolObjectRefClosure:public BoolObjectClosure{
 public:
virtual bool do_object_b(objectRef ref)=0;
};

// Applies an oop closure to all ref fields in objects iterated over in an
// object iteration.
class ObjectToOopClosure: public ObjectClosure {
  OopClosure* _cl;
public:
  void do_object(oop obj);
  ObjectToOopClosure(OopClosure* cl) : _cl(cl) {}
};

// A version of ObjectClosure with "memory" (see _previous_address below)
class UpwardsObjectClosure: public BoolObjectClosure {
  HeapWord* _previous_address;
 public:
  UpwardsObjectClosure() : _previous_address(NULL) { }
  void set_previous(HeapWord* addr) { _previous_address = addr; }
  HeapWord* previous()              { return _previous_address; }
  // A return value of "true" can be used by the caller to decide
  // if this object's end should *NOT* be recorded in
  // _previous_address above.
  virtual bool do_object_bm(oop obj, MemRegion mr) = 0;
};

// A version of ObjectClosure that is expected to be robust
// in the face of possibly uninitialized objects.
class ObjectClosureCareful : public ObjectClosure {
 public:
  virtual size_t do_object_careful_m(oop p, MemRegion mr) = 0;
  virtual size_t do_object_careful(oop p) = 0;
};

// SpaceClosure is used for iterating over spaces

class Space;
class CompactibleSpace;

class SpaceClosure : public StackObj {
 public:
  // Called for each space
  virtual void do_space(Space* s) = 0;
};

class CompactibleSpaceClosure : public StackObj {
 public:
  // Called for each compactible space
  virtual void do_space(CompactibleSpace* s) = 0;
};



// MonitorClosure is used for iterating over monitors in the monitors cache

class ObjectMonitor;

class MonitorClosure : public StackObj {
 public:
  // called for each monitor in cache
  virtual void do_monitor(ObjectMonitor* m) = 0;
};

// A closure that is applied without any arguments.
class VoidClosure : public StackObj {
 public:
  // I would have liked to declare this a pure virtual, but that breaks
  // in mysterious ways, for unknown reasons.
  virtual void do_void();
};


// Simple counting closure.
class CountOopsClosure:public OopClosure{
private:
  int64_t _count;
public:
CountOopsClosure():_count(0){}
  void do_oop(objectRef* o)       { _count++; }

  int64_t count()           const { return _count; }
};

// YieldClosure is intended for use by iteration loops
// to incrementalize their work, allowing interleaving
// of an interruptable task so as to allow other
// threads to run (which may not otherwise be able to access
// exclusive resources, for instance). Additionally, the
// closure also allows for aborting an ongoing iteration
// by means of checking the return value from the polling
// call.
class YieldClosure : public StackObj {
  public:
   virtual bool should_return() = 0;
};

// Abstract closure for serializing data (read or write).

class SerializeOopClosure : public OopClosure {
public:
  // Return bool indicating whether closure implements read or write.
  virtual bool reading() const = 0;

  // Read/write the int pointed to by i.
  virtual void do_int(int* i) = 0;

  // Read/write the size_t pointed to by i.
  virtual void do_size_t(size_t* i) = 0;

  // Read/write the void pointer pointed to by p.
  virtual void do_ptr(void** p) = 0;

  // Read/write the HeapWord pointer pointed to be p.
  virtual void do_ptr(HeapWord** p) = 0;

  // Read/write the region specified.
  virtual void do_region(u_char* start, size_t size) = 0;

  // Check/write the tag.  If reading, then compare the tag against
  // the passed in value and fail is they don't match.  This allows
  // for verification that sections of the serialized data are of the
  // correct length.
  virtual void do_tag(int tag) = 0;
};
#endif // ITERATOR_HPP
