// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

#ifndef GPGC_JAVALANGREFHANDLER_HPP
#define GPGC_JAVALANGREFHANDLER_HPP

#include "gpgc_gcManagerMark.hpp"
#include "referencePolicy.hpp"


class ReferencePolicy;
class GPGC_ReferenceList;
class GPGC_ThreadRefLists;


// This class is somewhat misnamed: while it does handle java.lang.ref.Reference instances,
// it also is responsible for GPGC's handling of JNI weak references.
class GPGC_JavaLangRefHandler:public CHeapObj{
  friend class GPGC_ThreadRefLists;

private:
  static bool          _should_clear_referent[SubclassesOfJavaLangRefReference];

  static bool          should_clear_referent(ReferenceType ref_type) { return _should_clear_referent[ref_type]; }

private:
  uint64_t             _scan_generation;
  uint64_t             _scan_space_id;

  bool                 _capture_jlrs;
  bool                 _new_pending_jlrs;

  ReferencePolicy*     _soft_ref_policy;

  GPGC_ReferenceList*  _captured_ref_lists[SubclassesOfJavaLangRefReference];

  // Scan parameters accessors:
  uint64_t             scan_generation()                { return _scan_generation; }
  uint64_t             scan_space_id()                  { return _scan_space_id; }

  // pending java.lang.refs setter/accessor:
void set_new_pending_jlrs(bool flag){_new_pending_jlrs=flag;}
  bool                 new_pending_jlrs()               { return _new_pending_jlrs; }

  // java.lang.ref.SoftReference clearing policy:
  void                 set_soft_ref_policy(ReferencePolicy* policy) { _soft_ref_policy = policy; }
  ReferencePolicy*     soft_ref_policy    ()                        { return _soft_ref_policy; }

  GPGC_ReferenceList** captured_ref_lists(ReferenceType ref_type)   { return & (_captured_ref_lists[ref_type]); }

  void                 save_captured_refs(GPGC_ThreadRefLists* thread_lists, ReferenceType ref_type);

  void                 do_refs_parallel(ReferenceType ref_type);
  void                 update_softref_clock();

public:
  GPGC_JavaLangRefHandler(uint64_t gen, uint64_t space_id);

  void      initialize();

  void      pre_mark(ReferencePolicy* policy); // Called by GPGC prior to a each mark phase

  // JLR capture on/off set/accesor:
  void      set_jlr_capture(bool flag);
  bool      is_capture_on()                  { return _capture_jlrs; }

  // Manage the pending list lock via the surrogate locker thread:
  void      acquire_pending_list_lock();
  void      release_pending_list_lock();

  void      do_soft_refs_parallel();
  void      do_weak_refs_parallel();
  void      do_final_refs_parallel();
  void      do_jni_weak_refs();
  void      do_phantom_refs_parallel();

  // iterate over oops
  void      mark_roots(GPGC_GCManagerNewStrong* gcm);
  void      mark_roots(GPGC_GCManagerOldStrong* gcm);

  bool      none_captured();

  bool      capture_java_lang_ref(GPGC_ThreadRefLists* thread_lists, oop obj, ReferenceType ref_type);
  void      save_ref_lists       (GPGC_ThreadRefLists* thread_lists);
  void      do_captured_ref_list (GPGC_GCManagerMark* gcm, GPGC_ReferenceList* list);
};


class GPGC_ReferenceList:public CHeapObj{
objectRef _list_head;
  ReferenceType       _list_type;
GPGC_ReferenceList*_next;

 public:
  GPGC_ReferenceList(objectRef list_head, ReferenceType ref_type)
                    : _list_head(list_head), _list_type(ref_type) {}

void set_next(GPGC_ReferenceList*next){_next=next;}

  objectRef*          refs_list()                         { return &_list_head; }
  ReferenceType       ref_type()                          { return _list_type; }
GPGC_ReferenceList*next(){return _next;}
};


#endif // GPGC_JAVALANGREFHANDLER_HPP

