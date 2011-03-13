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
#ifndef JVMTITAGMAP_HPP
#define JVMTITAGMAP_HPP

#include "jvmtiEnv.hpp"
#include "vm_operations.hpp"

// JvmtiTagMap 

#ifndef _JAVA_JVMTI_TAG_MAP_H_
#define _JAVA_JVMTI_TAG_MAP_H_

// forward references
class JvmtiTagHashmap;
class JvmtiTagHashmapEntry;
class JvmtiTagHashmapEntryClosure;
class JNILocalRootsClosure;
class BasicHeapWalkContext;
class AdvancedHeapWalkContext;

class JvmtiTagMap :  public CHeapObj {
 private:

  enum{	    
    n_hashmaps = 2,				    // encapsulates 2 hashmaps
    max_free_entries = 4096                         // maximum number of free entries per env
  };

  // memory region for young generation
  static MemRegion _young_gen;
  static void get_young_generation();

  JvmtiEnv*		_env;			    // the jvmti environment
AzLock _lock;//lock for this tag map
  JvmtiTagHashmap*	_hashmap[n_hashmaps];	    // the hashmaps 

  JvmtiTagHashmapEntry* _free_entries;		    // free list for this environment
intptr_t _free_entries_count;//number of entries on the free list

  // create a tag map
  JvmtiTagMap(JvmtiEnv* env);				  

  // accessors
inline AzLock*lock(){return&_lock;}
  inline JvmtiEnv* env() const	 { return _env; }

  // rehash tags maps for generation start to end
  void rehash(int start, int end);

  // indicates if the object is in the young generation
  static bool is_in_young(oop o);

  // iterate over all entries in this tag map
  void entry_iterate(JvmtiTagHashmapEntryClosure* closure);
 
 public:

  // indicates if this tag map is locked
  bool is_locked()			    { return lock()->is_locked(); }  

  // return the appropriate hashmap for a given object
  JvmtiTagHashmap* hashmap_for(oop o);

  // create/destroy entries
  JvmtiTagHashmapEntry* create_entry(jweak ref, jlong tag);
  void destroy_entry(JvmtiTagHashmapEntry* entry);

  // returns true if the hashmaps are empty
  bool is_empty();

  // return tag for the given environment
  static JvmtiTagMap* tag_map_for(JvmtiEnv* env);

  // destroy tag map
  ~JvmtiTagMap();

  // set/get tag
  void set_tag(jobject obj, jlong tag);
  jlong get_tag(jobject obj);

  // deprecated heap iteration functions
  void iterate_over_heap(jvmtiHeapObjectFilter object_filter,
                         KlassHandle klass,                          
			 jvmtiHeapObjectCallback heap_object_callback, 
                         const void* user_data); 

  void iterate_over_reachable_objects(jvmtiHeapRootCallback heap_root_callback, 
				      jvmtiStackReferenceCallback stack_ref_callback, 
				      jvmtiObjectReferenceCallback object_ref_callback, 
                                      const void* user_data);

  void iterate_over_objects_reachable_from_object(jobject object, 
						  jvmtiObjectReferenceCallback object_reference_callback,
                                                  const void* user_data);


  // advanced (JVMTI 1.1) heap iteration functions
  void iterate_through_heap(jint heap_filter, 
                            KlassHandle klass, 
			    const jvmtiHeapCallbacks* callbacks, 
                            const void* user_data);

  void follow_references(jint heap_filter, 
                         KlassHandle klass, 
			 jobject initial_object, 
                         const jvmtiHeapCallbacks* callbacks, 
			 const void* user_data);

  // get tagged objects
  jvmtiError get_objects_with_tags(const jlong* tags, jint count, 
				   jint* count_ptr, jobject** object_result_ptr, 
				   jlong** tag_result_ptr);

  // call post-GC to rehash the tag maps.
  static void gc_epilogue(bool full);

  // call after referencing processing has completed (GPGC)
  static void gpgc_ref_processing_epilogue();
};


// VM operation to iterate over all objects in the heap (both reachable
// and unreachable)
class VM_HeapIterateOperation: public VM_Operation {
 private:
  ObjectClosure* _blk;
  static jlong _number_of_callbacks;
  static jlong _callbacks_start_time;
  static bool _continue_heap_iteration;
  static bool _safe_to_iterate;
  static bool _timeout_reached_warning;

 public:
  VM_HeapIterateOperation(ObjectClosure* blk) { _blk = blk; }

  VMOp_Type type() const { return VMOp_HeapIterateOperation; }

  void doit();

  static jlong number_of_callbacks()        { return _number_of_callbacks; }
  static void reset_number_of_callbacks()   { _number_of_callbacks = 0; }
  static jlong* number_of_callbacks_addr()  { return &_number_of_callbacks; }

  static void set_callbacks_start_time()    { _callbacks_start_time = os::elapsed_counter(); }

  static bool should_discontinue_heap_iteration() {
assert(HeapIterationCallbacksTimeout!=0,"HeapIteration when disabled?");
    if (HeapIterationCallbacksTimeout > 0) {
      double elapsedTime = (double) (os::elapsed_counter() - _callbacks_start_time) / (double) os::elapsed_frequency() / 60.0;
      if (elapsedTime > HeapIterationCallbacksTimeout) {
        if (!_timeout_reached_warning) {
warning("HeapIterationCallbacksTimeout (%lld min) reached.",HeapIterationCallbacksTimeout);
          _timeout_reached_warning = true;
        }
        return true;
      }
    }
    return (_continue_heap_iteration == false);
  }
  static void reset_continue_heap_iteration()  {
    _continue_heap_iteration = true;
    _timeout_reached_warning = false;
  }
  static void set_heap_iteration_control(jvmtiIterationControl value) {
    if (value != JVMTI_ITERATION_CONTINUE) {
      _continue_heap_iteration = false;
    }
  }

  static bool is_safe_to_iterate()          { return _safe_to_iterate; }
  static void set_safe_to_iterate(bool safe_to_iterate) { _safe_to_iterate = safe_to_iterate; }
};


// A VM operation to iterate over objects that are reachable from
// a set of roots or an initial object.
//
// For VM_HeapWalkOperation the set of roots used is :-
//
// - All JNI global references
// - All inflated monitors
// - All classes loaded by the boot class loader (or all classes
//     in the event that class unloading is disabled)
// - All java threads
// - For each java thread then all locals and JNI local references
//      on the thread's execution stack
// - All visible/explainable objects from Universes::oops_do
//
class VM_HeapWalkOperation: public VM_Operation {
 private:
  enum {
    initial_visit_stack_size = 4000
  };

  bool _is_advanced_heap_walk;	                    // indicates FollowReferences
  JvmtiTagMap* _tag_map;
  Handle _initial_object;
  GrowableArray<oop>* _visit_stack;		    // the visit stack

  bool _collecting_heap_roots;			    // are we collecting roots
  bool _following_object_refs;			    // are we following object references

  bool _reporting_primitive_fields;                 // optional reporting
  bool _reporting_primitive_array_values;
  bool _reporting_string_values;

  static jlong _number_of_callbacks;
  static jlong _callbacks_start_time;
  static bool _continue_heap_iteration;
  static bool _safe_to_iterate;
  static bool _timeout_reached_warning;

  GrowableArray<oop>* create_visit_stack() {
    return new (ResourceObj::C_HEAP) GrowableArray<oop>(initial_visit_stack_size, true);
  }

  // accessors
  bool is_advanced_heap_walk() const               { return _is_advanced_heap_walk; }
  JvmtiTagMap* tag_map() const                     { return _tag_map; }
  Handle initial_object() const                    { return _initial_object; }

  bool is_following_references() const             { return _following_object_refs; }

  bool is_reporting_primitive_fields()  const      { return _reporting_primitive_fields; }
  bool is_reporting_primitive_array_values() const { return _reporting_primitive_array_values; }
  bool is_reporting_string_values() const          { return _reporting_string_values; }

  GrowableArray<oop>* visit_stack() const          { return _visit_stack; }

  // iterate over the various object types
  inline bool iterate_over_array(oop o);
  inline bool iterate_over_type_array(oop o);
  inline bool iterate_over_class(klassOop o);
  inline bool iterate_over_object(oop o);

  // root collection
  inline bool collect_simple_roots();
  inline bool collect_stack_roots();
  inline bool collect_stack_roots(JavaThread* java_thread, JNILocalRootsClosure* blk);
 
  // visit an object
  inline bool visit(oop o);

 public:        
  VM_HeapWalkOperation(JvmtiTagMap* tag_map, 
                       Handle initial_object, 
		       BasicHeapWalkContext callbacks, 
		       const void* user_data);

  VM_HeapWalkOperation(JvmtiTagMap* tag_map, 
                       Handle initial_object, 
		       AdvancedHeapWalkContext callbacks, 
		       const void* user_data);

  ~VM_HeapWalkOperation();

  VMOp_Type type() const { return VMOp_HeapWalkOperation; }
  void doit(); 

  static jlong number_of_callbacks()        { return _number_of_callbacks; }
  static void reset_number_of_callbacks()   { _number_of_callbacks = 0; }
  static jlong* number_of_callbacks_addr()  { return &_number_of_callbacks; }

  static void set_callbacks_start_time()    { _callbacks_start_time = os::elapsed_counter(); }

  static bool should_discontinue_heap_iteration() {
assert(HeapIterationCallbacksTimeout!=0,"HeapIteration when disabled?");
    if (HeapIterationCallbacksTimeout > 0) {
      double elapsedTime = (double) (os::elapsed_counter() - _callbacks_start_time) / (double) os::elapsed_frequency() / 60.0;
      if (elapsedTime > HeapIterationCallbacksTimeout) {
        if (!_timeout_reached_warning) {
warning("HeapIterationCallbacksTimeout (%lld min) reached.",HeapIterationCallbacksTimeout);
          _timeout_reached_warning = true;
        }
        return true;
      }
    }
    return (_continue_heap_iteration == false);
  }
  static void reset_continue_heap_iteration()  {
    _continue_heap_iteration = true;
    _timeout_reached_warning = false;
  }
  static void set_heap_iteration_control(jvmtiIterationControl value) {
    if (value != JVMTI_ITERATION_CONTINUE) {
      _continue_heap_iteration = false;
    }
  }

  static bool is_safe_to_iterate()          { return _safe_to_iterate; }
  static void set_safe_to_iterate(bool safe_to_iterate) { _safe_to_iterate = safe_to_iterate; }
};


// A CallbackWrapper is a support class for querying and tagging an object
// around a callback to a profiler. The constructor does pre-callback
// work to get the tag value, klass tag value, ... and the destructor 
// does the post-callback work of tagging or untagging the object.
//
// {
//   CallbackWrapper wrapper(tag_map, o); 
//
//   (*callback)(wrapper.klass_tag(), wrapper.obj_size(), wrapper.obj_tag_p(), ...)
//
// } // wrapper goes out of scope here which results in the destructor 
//      checking to see if the object has been tagged, untagged, or the
//	tag value has changed.
//	
class CallbackWrapper:public CHeapObj{
 private:
  JvmtiTagMap* _tag_map;
  JvmtiTagHashmap* _hashmap;
  JvmtiTagHashmapEntry* _entry;
  oop _o;
  jlong _obj_size;
  jlong _obj_tag;
  klassOop _klass;         // the object's class
  jlong _klass_tag;  

 protected:
  JvmtiTagMap* tag_map() const	    { return _tag_map; }

  // invoked post-callback to tag, untag, or update the tag of an object
  void inline post_callback_tag_update(oop o, JvmtiTagHashmap* hashmap, 
                                       JvmtiTagHashmapEntry* entry, jlong obj_tag);
  
 public:
  CallbackWrapper(JvmtiTagMap* tag_map, oop o);

  ~CallbackWrapper() {
    post_callback_tag_update(_o, _hashmap, _entry, _obj_tag);
  }

  inline jlong* obj_tag_p()			{ return &_obj_tag; } 
  inline void set_tag(jlong tag)    { _obj_tag = tag; }
  inline jlong obj_size() const			{ return _obj_size; }
  inline jlong obj_tag() const                  { return _obj_tag; }
  inline klassOop klass() const                 { return _klass; }
  inline jlong klass_tag() const		{ return _klass_tag; } 
};

#endif   /* _JAVA_JVMTI_TAG_MAP_H_ */

#endif // JVMTITAGMAP_HPP
