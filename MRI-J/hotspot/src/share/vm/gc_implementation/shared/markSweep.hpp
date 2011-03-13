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
#ifndef MARKSWEEP_HPP
#define MARKSWEEP_HPP

#include "codeBlob.hpp"
#include "growableArray.hpp"
#include "markWord.hpp"

class ReferenceProcessor;

// MarkSweep takes care of global mark-compact garbage collection for a
// GenCollectedHeap using a four-phase pointer forwarding algorithm.  All
// generations are assumed to support marking; those that can also support
// compaction.
//
// Class unloading will only occur when a full gc is invoked.

// declared at end
class PreservedMark;

class MarkSweep : AllStatic {
  //
  // In line closure decls
  //

  class FollowRootClosure: public OopsInGenClosure{ 
   public:
void do_oop(objectRef*p){
      assert0(p->is_null() || UNPOISON_OBJECTREF(*p, p).is_heap());
      follow_root((heapRef*)p);
    }
  };

  class MarkAndPushClosure: public OopClosure {
   public:
void do_oop(objectRef*p){
      assert0(p->is_null() || UNPOISON_OBJECTREF(*p, p).is_heap());
      mark_and_push((heapRef*)p);
    }
  };

  class FollowStackClosure: public VoidClosure {
   public:
    void do_void() { follow_stack(); }
  };

  class AdjustPointerClosure: public OopsInGenClosure {
    bool _skipping_codecache;
   public:
    AdjustPointerClosure(bool skipping_codecache) : _skipping_codecache(skipping_codecache) {}
    void do_oop(objectRef* p) {
      assert0(p->is_null() || UNPOISON_OBJECTREF(*p, p).is_heap());
_adjust_pointer((heapRef*)p,_skipping_codecache);
    }
  };

  // Used for java/lang/ref handling
  class IsAliveClosure: public BoolObjectClosure {
   public:
    void do_object(oop p) { assert(false, "don't call"); }
    bool do_object_b(oop p) { return p->is_gc_marked(); }
  };

  class KeepAliveClosure: public OopClosure {
   public:
    void do_oop(objectRef* p);
  };

  //
  // Friend decls
  //

  friend class AdjustPointerClosure;
  friend class KeepAliveClosure;
  friend class VM_MarkSweep;
  friend void marksweep_init();

  //
  // Vars
  //
 protected:
  // Traversal stack used during phase1
  static GrowableArray<oop>*             _marking_stack;   
  // Stack for live klasses to revisit at end of marking phase
  static GrowableArray<Klass*>*          _revisit_klass_stack;   

  // Space for storing/restoring mark word
static GrowableArray<markWord*>*_preserved_mark_stack;
static GrowableArray<uintptr_t>*_preserved_oop_stack;
  static size_t			         _preserved_count;
  static size_t			         _preserved_count_max;
  static PreservedMark*                  _preserved_marks;
  
  // Reference processing (used in ...follow_contents)
  static ReferenceProcessor*             _ref_processor;

  // Non public closures
  static IsAliveClosure is_alive;
  static KeepAliveClosure keep_alive;

  // Class unloading. Update subklass/sibling/implementor links at end of marking phase.
  static void follow_weak_klass_links();

  // Debugging
  static void trace(const char* msg) PRODUCT_RETURN;

 public:
  // Public closures
  static FollowRootClosure follow_root_closure;
  static MarkAndPushClosure mark_and_push_closure;
  static FollowStackClosure follow_stack_closure;
static AdjustPointerClosure adjust_pointer_closure_skipping_CodeCache;
  static AdjustPointerClosure adjust_pointer_closure;

  // Reference Processing
  static ReferenceProcessor* const ref_processor() { return _ref_processor; }

  // Call backs for marking
  static void mark_object(heapRef obj);
  static void follow_root(heapRef* p);           // Mark pointer and follow contents. Empty marking
                                                 // stack afterwards.

  static void mark_and_follow(heapRef* p);       // Mark pointer and follow contents.
  static void _mark_and_push(heapRef* p);        // Mark pointer and push obj on
                                                 // marking stack.

  
  static void mark_and_push(heapRef* p) {        // Check mark and maybe push on
                                                 // marking stack
    // assert(Universe::is_reserved_heap((oop)p), "we should only be traversing objects here");
    oop m = UNPOISON_OBJECTREF(*p, p).as_oop();
    if (m != NULL && !m->mark()->is_marked()) {
      _mark_and_push(p);
    }
  }

  static void follow_stack();             // Empty marking stack.


  static void preserve_mark(heapRef p, markWord *mark); // Save the mark word so it can be restored later
  static void adjust_marks();             // Adjust the pointers in the preserved marks table
  static void restore_marks();            // Restore the marks that we saved in preserve_mark

  inline static void _adjust_pointer(heapRef* p, bool isroot);
  
  inline static bool is_unmarked_and_discover_reference(objectRef referent, oop obj, ReferenceType type);

  static void adjust_pointer(heapRef* p)        { _adjust_pointer(p, false); }

  static void check_kid(oop, unsigned int);

  // Call backs for class unloading
  static void revisit_weak_klass_link(Klass* k);  // Update subklass/sibling/implementor links at end of marking.


};


class PreservedMark VALUE_OBJ_CLASS_SPEC {
private:
  heapRef _obj;
markWord*_mark;

public:
void init(heapRef obj,markWord*mark){
POISON_AND_STORE_REF(&_obj,obj);
    _mark = mark;
  }

  void adjust_pointer() {
    MarkSweep::adjust_pointer(&_obj);
  }

  void restore() {
    UNPOISON_OBJECTREF(_obj, &_obj).as_oop()->set_mark(_mark);
  }
};

#endif // MARKSWEEP_HPP
