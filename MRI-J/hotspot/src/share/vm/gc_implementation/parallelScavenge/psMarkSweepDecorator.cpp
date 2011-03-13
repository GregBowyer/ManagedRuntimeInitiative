/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "markWord.hpp"
#include "parallelScavengeHeap.hpp"
#include "psMarkSweep.hpp"
#include "psMarkSweepDecorator.hpp"
#include "space.hpp"

#include "atomic_os_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "oop.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

PSMarkSweepDecorator* PSMarkSweepDecorator::_destination_decorator = NULL;


void PSMarkSweepDecorator::set_destination_decorator_tenured() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  _destination_decorator = heap->old_gen()->object_mark_sweep();
}

void PSMarkSweepDecorator::set_destination_decorator_perm_gen() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  _destination_decorator = heap->perm_gen()->object_mark_sweep();
}

void PSMarkSweepDecorator::advance_destination_decorator() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  
  assert(_destination_decorator != NULL, "Sanity");
  guarantee(_destination_decorator != heap->perm_gen()->object_mark_sweep(), "Cannot advance perm gen decorator");

  PSMarkSweepDecorator* first = heap->old_gen()->object_mark_sweep();
  PSMarkSweepDecorator* second = heap->young_gen()->eden_mark_sweep();
  PSMarkSweepDecorator* third = heap->young_gen()->from_mark_sweep();
  PSMarkSweepDecorator* fourth = heap->young_gen()->to_mark_sweep();

  if ( _destination_decorator == first ) {
    _destination_decorator = second;
  } else if ( _destination_decorator == second ) {
    _destination_decorator = third;
  } else if ( _destination_decorator == third ) {
    _destination_decorator = fourth;
  } else {
    fatal("PSMarkSweep attempting to advance past last compaction area");
  }
}

PSMarkSweepDecorator* PSMarkSweepDecorator::destination_decorator() {
  assert(_destination_decorator != NULL, "Sanity");

  return _destination_decorator;
}

// FIX ME FIX ME FIX ME FIX ME!!!!!!!!!
// The object forwarding code is duplicated. Factor this out!!!!!
//
// This method "precompacts" objects inside its space to dest. It places forwarding
// pointers into markOops for use by adjust_pointers. If "dest" should overflow, we
// finish by compacting into our own space.

void PSMarkSweepDecorator::precompact() {
  // Reset our own compact top.
  set_compaction_top(space()->bottom());

  /* We allow some amount of garbage towards the bottom of the space, so
   * we don't start compacting before there is a significant gain to be made.
   * Occasionally, we want to ensure a full compaction, which is determined
   * by the MarkSweepAlwaysCompactCount parameter. This is a significant
   * performance improvement!
   */
  bool skip_dead = ((PSMarkSweep::total_invocations() % MarkSweepAlwaysCompactCount) != 0);

  size_t allowed_deadspace = 0;
  if (skip_dead) {
    int ratio = allowed_dead_ratio();
    allowed_deadspace = (space()->capacity_in_bytes() * ratio / 100) / HeapWordSize;
  }

  // Fetch the current destination decorator
  PSMarkSweepDecorator* dest = destination_decorator();
  ObjectStartArray* start_array = dest->start_array();

  HeapWord* compact_top = dest->compaction_top();
  HeapWord* compact_end = dest->space()->end();
                                           
  HeapWord* q = space()->bottom();                                                    
  HeapWord* t = space()->top();                                                
  
  HeapWord*  end_of_live= q;    /* One byte beyond the last byte of the last 
				   live object. */                           
  HeapWord*  first_dead = space()->end(); /* The first dead object. */                 
  _first_dead = first_dead;                                                  

  const intx interval = PrefetchScanIntervalInBytes;
                                                                             
  while (q < t) {                                                            
    assert(oop(q)->mark()->is_marked() || oop(q)->mark()->is_unlocked() ||
oop(q)->mark()->is_biased(),
	   "these are the only valid states during a mark sweep");           
    if (oop(q)->is_gc_marked()) {  
      /* prefetch beyond q */                                                
      Prefetch::write(q, interval);                          
      Prefetch::write(q, interval + BytesPerCacheLine);
      size_t size = oop(q)->size();

      size_t compaction_max_size = pointer_delta(compact_end, compact_top);

      // This should only happen if a space in the young gen overflows the
      // old gen. If that should happen, we null out the start_array, because
      // the young spaces are not covered by one.
      while(size > compaction_max_size) {
        // First record the last compact_top
        dest->set_compaction_top(compact_top);

        // Advance to the next compaction decorator
        advance_destination_decorator();
        dest = destination_decorator();

        // Update compaction info
        start_array = dest->start_array();
        compact_top = dest->compaction_top();
        compact_end = dest->space()->end();
        assert(compact_top == dest->space()->bottom(), "Advanced to space already in use");
        assert(compact_end > compact_top, "Must always be space remaining");
	compaction_max_size = 
	  pointer_delta(compact_end, compact_top);
      }

      // store the forwarding pointer into the mark word
      if (q != compact_top) {
oop(q)->forward_to_pointer(compact_top);
        assert(oop(q)->is_gc_marked(), "encoding the pointer should preserve the mark");
      } else {
	// Don't clear the mark since it's confuses parallel old
	// verification.
	if (!UseParallelOldGC || !VerifyParallelOldWithMarkSweep) {
          // if the object isn't moving we can just set the mark to the default
          // mark and handle it specially later on.  
oop(q)->clear_mark();
        }  
assert(oop(q)->forwarded_pointer()==NULL,"should be forwarded to NULL");
      }

      // Update object start array
      if (!UseParallelOldGC || !VerifyParallelOldWithMarkSweep) {
        if (start_array)
          start_array->allocate_block(compact_top);
      }

      compact_top += size;
      assert(compact_top <= dest->space()->end(), 
	"Exceeding space in destination");

      q += size;                                                             
      end_of_live = q;                                                       
    } else {                                                                 
      /* run over all the contiguous dead objects */                         
      HeapWord* end = q;                                                     
      do {                                                                   
        /* prefetch beyond end */                                            
        Prefetch::write(end, interval);                            
        Prefetch::write(end, interval + BytesPerCacheLine);
	end += oop(end)->size();
      } while (end < t && (!oop(end)->is_gc_marked()));

      /* see if we might want to pretend this object is alive so that
       * we don't have to compact quite as often.
       */
      if (allowed_deadspace > 0 && q == compact_top) {
	size_t sz = pointer_delta(end, q);
if(CompactibleSpace::insert_deadspace(allowed_deadspace,q,sz)){
          size_t compaction_max_size = pointer_delta(compact_end, compact_top);

          // This should only happen if a space in the young gen overflows the
          // old gen. If that should happen, we null out the start_array, because
          // the young spaces are not covered by one.
          while (sz > compaction_max_size) {
            // First record the last compact_top
            dest->set_compaction_top(compact_top);
            
            // Advance to the next compaction decorator
            advance_destination_decorator();
            dest = destination_decorator();
            
            // Update compaction info
            start_array = dest->start_array();
            compact_top = dest->compaction_top();
            compact_end = dest->space()->end();
            assert(compact_top == dest->space()->bottom(), "Advanced to space already in use");
            assert(compact_end > compact_top, "Must always be space remaining");
	    compaction_max_size = 
	      pointer_delta(compact_end, compact_top);
          }

          // store the forwarding pointer into the mark word
          if (q != compact_top) {
oop(q)->forward_to_pointer(compact_top);
            assert(oop(q)->is_gc_marked(), "encoding the pointer should preserve the mark");
          } else {
            // if the object isn't moving we can just set the mark to the default
	    // Don't clear the mark since it's confuses parallel old
	    // verification.
	    if (!UseParallelOldGC || !VerifyParallelOldWithMarkSweep) {
              // mark and handle it specially later on.  
oop(q)->clear_mark();
            }  
assert(oop(q)->forwarded_pointer()==NULL,"should be forwarded to NULL");
          }

          if (!UseParallelOldGC || !VerifyParallelOldWithMarkSweep) {
            // Update object start array
            if (start_array)
              start_array->allocate_block(compact_top);
	  }

          compact_top += sz;
          assert(compact_top <= dest->space()->end(), 
	    "Exceeding space in destination");

	  q = end;
	  end_of_live = end;
	  continue;
	}
      }

      /* Record a pointer to the next live object in the mark of the first
       * dead object. */
      oop(q)->set_mark((markWord*)end);

      /* see if this is the first dead region. */                            
      if (q < first_dead) {                                                  
	first_dead = q;                                                      
      }                                                                      
                                                                             
      /* move on to the next object */                                       
      q = end;                                                               
    }                                                                        
  }                                                                          
                                                                             
  assert(q == t, "just checking");                                           
  _end_of_live = end_of_live;                                                
  if (end_of_live < first_dead) {                                            
    first_dead = end_of_live;                                                
  }                                                                          
  _first_dead = first_dead;                                                  
        
  // Update compaction top
  dest->set_compaction_top(compact_top);
}

void PSMarkSweepDecorator::adjust_pointers() {
  // adjust all the interior pointers to point at the new locations of objects
  // Used by MarkSweep::mark_sweep_phase3()

  HeapWord* q = space()->bottom();
  HeapWord* t = _end_of_live;  // Established by "prepare_for_compaction".

  assert(_first_dead <= _end_of_live, "Stands to reason, no?");

  // CNC - JAVA6 port: was        oop(q)->is_gc_marked()) {
  if (q < t && _first_dead > q && oop(q)->mark()->is_cleared()) {
    // we have a chunk of the space which hasn't moved and we've
    // reinitialized the mark word during the previous pass, so we can't
    // use is_gc_marked for the traversal.
    HeapWord* end = _first_dead;

    while (q < end) {
      // point all the oops to the new location
      size_t size = oop(q)->adjust_pointers();
	      
      q += size;
    }

    if (_first_dead == t) {
      q = t;
    } else {
//Read the previously recorded next live object.
q=(HeapWord*)oop(_first_dead)->mark();
    }
  }
  const intx interval = PrefetchScanIntervalInBytes;

  debug_only(HeapWord* prev_q = NULL);
  while (q < t) {
    // prefetch beyond q
    Prefetch::write(q, interval);
    Prefetch::write(q, interval + BytesPerCacheLine);
    if (oop(q)->is_gc_marked()) {
      // q is alive
      // point all the oops to the new location
      size_t size = oop(q)->adjust_pointers();
      debug_only(prev_q = q);
      q += size;
    } else {
      // q is not a live object, so its mark should point at the next
      // live object
      debug_only(prev_q = q);
q=(HeapWord*)oop(q)->mark();
      assert(q > prev_q, "we should be moving forward through memory");
    }
  }

  assert(q == t, "just checking");
}

void PSMarkSweepDecorator::compact(bool mangle_free_space ) {
  // Copy all live objects to their new location
  // Used by MarkSweep::mark_sweep_phase4()

  HeapWord*       q = space()->bottom();
  HeapWord* const t = _end_of_live;
  debug_only(HeapWord* prev_q = NULL);

  // CNC - JAVA6 port: was        oop(q)->is_gc_marked()) {
  if (q < t && _first_dead > q && oop(q)->mark()->is_cleared()) {
#ifdef ASSERT
    // we have a chunk of the space which hasn't moved and we've reinitialized the
    // mark word during the previous pass, so we can't use is_gc_marked for the
    // traversal.
    HeapWord* const end = _first_dead;
      
    while (q < end) {
      size_t size = oop(q)->size();
      assert(oop(q)->mark()->is_cleared(), "mark should be cleared");
      debug_only(prev_q = q);
      q += size;
    }
#endif
      
    if (_first_dead == t) {
      q = t;
    } else {
      // $$$ Funky
q=(HeapWord*)oop(_first_dead)->mark();
    }
  }

  const intx scan_interval = PrefetchScanIntervalInBytes;
  const intx copy_interval = PrefetchCopyIntervalInBytes;

  while (q < t) {
    if (!oop(q)->is_gc_marked()) {
      // mark is pointer to next marked oop
      debug_only(prev_q = q);
q=(HeapWord*)oop(q)->mark();
      assert(q > prev_q, "we should be moving forward through memory");
    } else {
      // prefetch beyond q
      Prefetch::read(q, scan_interval);
      Prefetch::read(q, scan_interval + BytesPerCacheLine);
      Prefetch::read(q, scan_interval + (BytesPerCacheLine*2));

      // size and destination
      size_t size = oop(q)->size();
HeapWord*compaction_top=(HeapWord*)oop(q)->forwarded_pointer();

      // prefetch beyond compaction_top
      Prefetch::write(compaction_top, copy_interval);
      Prefetch::write(compaction_top, copy_interval + BytesPerCacheLine);
      Prefetch::write(compaction_top, copy_interval + (BytesPerCacheLine*2));

      // copy object and reinit its mark
      assert(q != compaction_top, "everything in this pass should be moving");
      Copy::aligned_conjoint_words(q, compaction_top, size);
oop(compaction_top)->clear_mark();
      assert(oop(compaction_top)->klass() != NULL, "should have a class");

      debug_only(prev_q = q);
      q += size;
    }
  }

  assert(compaction_top() >= space()->bottom() && compaction_top() <= space()->end(),
         "should point inside space");
  space()->set_top(compaction_top());

  if (mangle_free_space) space()->mangle_unused_area();
}
