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
#ifndef GPGC_NEWCOLLECTOR_INLINE_HPP
#define GPGC_NEWCOLLECTOR_INLINE_HPP


#include "gpgc_gcManagerNewStrong.inline2.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_thread.hpp"
#include "klassIds.hpp"
#include "oop.gpgc.inline.hpp"


// NewGC's treatment of objectRef's being traversed depends upon the original space-id
// of the objectRef, the space-id of the relocated objectRef, and if the traversal is
// through a C-heap/stack root, a card-mark, or a NewGen object:
//
//                          Original   Revised
//     Marking Action       ObjectRef  ObjectRef  Action
//     -------------------  ---------  ---------  -------
//     card mark scan
//                          new-space  new-space  Remap, NMT, and Mark
//                          new-space  old-space  Remap, NMT, if OldGC-marking Transmit
//                          old-space  new-space  ShouldNotReachHere()
//                          old-space  old-space  Ignore
//
//     root scan New
//                          new-space  new-space  Remap, NMT, and Mark
//                          new-space  old-space  Remap, NMT, if OldGC-marking Transmit
//                          old-space  new-space  ShouldNotReachHere()
//                          old-space  old-space  Ignore
//
//     root scan NewToOld
//                          new-space  new-space  Remap, NMT, and Mark
//                          new-space  old-space  Remap, NMT, and Transmit
//                          old-space  new-space  ShouldNotReachHere()
//                          old-space  old-space  Remap, NMT, and Transmit
//
//     new-space traversal
//                          new-space  new-space  Remap, NMT, and Mark
//                          new-space  old-space  Remap, NMT, if NewToOldGC Transmit
//                          old-space  new-space  ShouldNotReachHere()
//                          old-space  old-space  Remap, NMT, if NewToOldGC Transmit
//
//     new-space klass
//                          new-space  new-space  ShouldNotReachHere()
//                          new-space  old-space  ShouldNotReachHere()
//                          old-space  new-space  ShouldNotReachHere()
//                          old-space  old-space  Remap, NMT, if NewToOldGC Transmit
//
//
// OldGC-mark-NMT = BadNMT if OldGC is marking, else GoodNMT


// Ref handling rules for objectRefs sourced from card mark scanning:
//
//     Original   Revised
//     ObjectRef  ObjectRef  Action
//     ---------  ---------  ------
//     new-space  new-space  Remap, NMT, and Mark
//     new-space  old-space  Remap, NMT, if OldGC-marking Transmit
//     old-space  new-space  ShouldNotReachHere()
//     old-space  old-space  Ignore
//
inline objectRef
GPGC_NewCollector::cardmark_ref_needed_nmt(objectRef old_ref, bool& cas_ref, bool& mark_in_new, bool& mark_in_old)
{
  // Card marks are only marked through StrongLive, so there's no FinalLive handling here.
  cas_ref     = false;
  mark_in_new = false;
  mark_in_old = false;

  objectRef new_ref = old_ref;

  if ( new_ref.is_new() ) {
    if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
      // Remap the objectRef.
      assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(new_ref), "new-space refs should be new-space trapped");
      assert(!GPGC_NMT::has_desired_new_nmt(new_ref), "Unrelocated objectRef's should have bad NMT");

      // During marking, all relocating objects have already been copied, so we just lookup the new location.
      new_ref = GPGC_Collector::get_forwarded_object(new_ref);

      cas_ref = true;

      if ( new_ref.is_new() ) {
        mark_in_new = true;
      } else {
        assert0( new_ref.is_old() );
        // If the OldCollector is marking, we'd better mark through this ref.  If we don't the
        // OldCollector might miss it.  Worst case, we make cardmarked NewGen refs that get
        // relocated to OldGen live one cycle more than they should be.
        if ( GPGC_OldCollector::is_marking() ) {
          mark_in_old = true;
        }
      }
    }
    else if ( ! GPGC_NMT::has_desired_new_nmt(new_ref) ) {
      // Ref doesn't need to be relocated, but it's NewGC NMT is wrong.
      new_ref.set_nmt(GPGC_NMT::desired_new_nmt_flag());

      cas_ref = true;
      mark_in_new = true;
    }
    else {
      // Ref doesn't need to be relocated, and its NewGC NMT is correct.
    }
  }
  else {
    // If the old_ref isn't new-space, it must be either NULL or old-space.  Either way,
    // card marks of NULL objectRefs and objectRefs to old-space are completely ignored.
    assert0(new_ref.is_null() || new_ref.is_old());
  }

  return new_ref;
}


// Ref handling rules for objectRefs sourced from NewGC object traversal:
//
//     Original   Revised
//     ObjectRef  ObjectRef  Action
//     ---------  ---------  ------
//     new-space  new-space  Remap, NMT, and Mark
//     new-space  old-space  Remap, NMT, if NewToOldGC Transmit
//     old-space  new-space  ShouldNotReachHere()
//     old-space  old-space  Remap, NMT, if NewToOldGC Transmit
//
inline objectRef
GPGC_NewCollector::new_ref_needed_nmt(objectRef old_ref, bool& cas_ref, bool& mark_in_new, bool& mark_in_old)
{
  // This handles StrongLive marking only.  objectRef_needs_final_mark() is the equivalent for FinalLive marking.
  cas_ref     = false;
  mark_in_new = false;
  mark_in_old = false;

  if ( old_ref.is_null() ) { return old_ref; }

  objectRef new_ref = old_ref;

  if ( new_ref.is_new() ) {
    if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
      // Remap the objectRef.
      assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(new_ref), "new-space refs should be new-space trapped");
      assert(!GPGC_NMT::has_desired_new_nmt(new_ref), "Unrelocated objectRef's should have bad NMT");

      // During marking, all relocating objects have already been copied, so we just lookup the new location.
      new_ref = GPGC_Collector::get_forwarded_object(new_ref);

      cas_ref = true;

      if ( new_ref.is_new() ) {
        mark_in_new = true;
      } else {
        assert0( new_ref.is_old() );
        // Mark refs into old_space if the NewCollector is set to provide new_space->old_space roots to the OldCollector.
        mark_in_old = GPGC_NewCollector::mark_old_space_roots();
      }
    }
    else if ( ! GPGC_NMT::has_desired_new_nmt(new_ref) ) {
      // Ref doesn't need to be relocated, but it's NewGC NMT is wrong.
      new_ref.set_nmt(GPGC_NMT::desired_new_nmt_flag());

      cas_ref = true;
      mark_in_new = true;
    }
    else {
      // Ref doesn't need to be relocated, and its NewGC NMT is correct.
    }
  } else {
    assert(new_ref.is_old(), "NewGen GenPauselessGC sees non-heap objectRef");

    // If we find a ref from new_space to old_space, we ignore it unless the NMT bit isn't current for old_space.
    if ( ! GPGC_NMT::has_desired_old_nmt(new_ref) ) {
      assert(GPGC_NewCollector::mark_old_space_roots(), "Only expect bad NMT old objectRefs when marking for OldGC");

      if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(new_ref));

        // When the OldCollector flips the old NMT state, all relocating objects have alredy been copied.
        new_ref = GPGC_Collector::get_forwarded_object(new_ref);
      } else {
        // If we're not remapping the ref, we just update the NMT state.
        new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
      }

      cas_ref = true;
      mark_in_old = true;
    }
  }

  return new_ref;
}


// Ref handling rules for objectRefs sourced from NewGC root traversal.  Note that NewToOldGC root
// traversal has a different action table.
//
//     Original   Revised
//     ObjectRef  ObjectRef  Action
//     ---------  ---------  ------
//     new-space  new-space  Remap, NMT, and Mark
//     new-space  old-space  Remap, NMT, if OldGC-marking Transmit
//     old-space  new-space  ShouldNotReachHere()
//     old-space  old-space  Ignore
//
inline objectRef
GPGC_NewCollector::root_needed_nmt(objectRef old_ref, bool& cas_ref, bool& mark_in_new, bool& mark_in_old)
{ 
  
  // This method only deals with StrongLive marking.  FinalLive marking doesn't start with conventional roots.
  cas_ref     = false;
  mark_in_new = false;
  mark_in_old = false;

  objectRef new_ref = old_ref;

  if ( new_ref.is_new() ) {
    if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
      // Remap the objectRef.
      // expect remap traps 
      assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(new_ref), "new-space refs should be new-space trapped");
      assert(!GPGC_NMT::has_desired_new_nmt(new_ref), "Unrelocated objectRef's should have bad NMT");

      // During marking, all relocating objects have already been copied, so we just lookup the new location.
      new_ref = GPGC_Collector::get_forwarded_object(new_ref);

      cas_ref = true;

      if ( new_ref.is_new() ) {
        mark_in_new = true;
      } else {
        assert0( new_ref.is_old() );
        // If the OldCollector is marking, we'd better mark through this ref.  If we don't the
        // OldCollector might miss it.  Worst case, we make root NewGen refs that get
        // relocated to OldGen live one cycle more than they should be.
        if ( GPGC_OldCollector::is_marking() ) {
          mark_in_old = true;
        }
      }
    }
    else if ( ! GPGC_NMT::has_desired_new_nmt(new_ref) ) {
      // still trapped.. 
      // Ref doesn't need to be relocated, but it's NewGC NMT is wrong.
      new_ref.set_nmt(GPGC_NMT::desired_new_nmt_flag());

      cas_ref = true;
      mark_in_new = true;
    }
    else {
      // Ref doesn't need to be relocated, and its NewGC NMT is correct.
    }
  }
  else {
    // If the old_ref isn't new-space, it must be either NULL or old-space.  Either way,
    // roots of NULL objectRefs and objectRefs to old-space are completely ignored.
    assert0(new_ref.is_null() || new_ref.is_old());
  }

  return new_ref;
}


// Ref handling rules for klass objectRefs sourced from object headers during NewGC object traversal:
//
//     Original   Revised
//     ObjectRef  ObjectRef  Action
//     ---------  ---------  ------
//     new-space  new-space  ShouldNotReachHere()
//     new-space  old-space  ShouldNotReachHere()
//     old-space  new-space  ShouldNotReachHere()
//     old-space  old-space  Remap, NMT, if NewToOldGC Transmit
//
inline objectRef
GPGC_NewCollector::klass_ref_needed_nmt(objectRef old_ref, bool& cas_ref, bool& mark_in_old)
{
  // This method only deals with StrongLive marking.  FinalLive marking doesn't mark into PermGen.
  cas_ref     = false;
  mark_in_old = false;

  assert(old_ref.is_old(), "klass must be in old-space");
  assert(GPGC_NMT::has_desired_old_nmt(old_ref) || GPGC_NewCollector::mark_old_space_roots(),
"Only expect bad NMT klass objectRefs when marking for OldGC");

  objectRef new_ref = old_ref;

  if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(new_ref));

    // Klasses always have to be remapped to their new location.

    cas_ref = true;
    if ( ! GPGC_NMT::has_desired_old_nmt(new_ref) ) {
      mark_in_old = true;
    }

    // Interlocks prevent NewGC from seeing a trapped but un-relocated klass:
    new_ref = GPGC_Collector::get_forwarded_object(new_ref);
  }
  else if ( ! GPGC_NMT::has_desired_old_nmt(new_ref) ) {
    // We're not remapping the ref, but we need to update the NMT state.
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());

    cas_ref = true;
    mark_in_old = true;
  }

  return new_ref;
}


// Ref handling rules for weak roots:
//
//     Original   Revised
//     ObjectRef  ObjectRef  Action
//     ---------  ---------  ------
//     new-space  new-space  Remap, NMT, set to nullRef if not marked
//     new-space  old-space  Remap, NMT, if NewToOldGC Transmit
//     old-space  new-space  ShouldNotReachHere()
//     old-space  old-space  ignore
//
//
inline objectRef
GPGC_NewCollector::weak_root_needed_nmt(objectRef old_ref, bool& cas_ref, bool& mark_in_new, bool& mark_in_old)
{
  cas_ref     = false;
  mark_in_new = false;
  mark_in_old = false;

  objectRef new_ref = old_ref;

  if ( new_ref.is_new() ) {
    if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
      // Remap the objectRef.
      assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(new_ref), "new-space refs should be new-space trapped");
      assert(!GPGC_NMT::has_desired_new_nmt(new_ref), "Unrelocated objectRef's should have bad NMT");

      // During marking, all relocating objects have already been copied, so we just lookup the new location.
      new_ref = GPGC_Collector::get_forwarded_object(new_ref);

      cas_ref = true;

      if ( new_ref.is_new() ) {
        mark_in_new = true;
      } else {
        assert0( new_ref.is_old() );
        // If the OldCollector is marking, we'd better mark through this ref.  If we don't the
        // OldCollector might miss it.  Worst case, we make weak roots that get relocated to 
        // OldGen live one cycle more than they should be.
        if ( GPGC_OldCollector::is_marking() ) {
          mark_in_old = true;
        }
      }
    }
    else if ( ! GPGC_NMT::has_desired_new_nmt(new_ref) ) {
      // Ref doesn't need to be relocated, but it's NewGC NMT is wrong.
      new_ref.set_nmt(GPGC_NMT::desired_new_nmt_flag());

      cas_ref = true;
      mark_in_new = true;
    }
    else {
      // Ref doesn't need to be relocated, and its NewGC NMT is correct.
      DEBUG_ONLY( oop            obj   = new_ref.as_oop(); )
      DEBUG_ONLY( PageNum        page  = GPGC_Layout::addr_to_BasePageForSpace(obj); )
      DEBUG_ONLY( GPGC_PageInfo* info  = GPGC_PageInfo::page_info(page); )
      DEBUG_ONLY( uint64_t       flags = info->flags(); )
      assert0(((flags&GPGC_PageInfo::NoRelocateTLABFlags)==GPGC_PageInfo::NoRelocateTLABFlags) || GPGC_Marks::is_new_marked_strong_live(new_ref));
    }
  }
  else {
    // If the old_ref isn't new-space, it must be either NULL or old-space.  Either way, weak root
    // unlinking ignores it.
    assert0(new_ref.is_null() || new_ref.is_old());
  }

  return new_ref;
}


inline objectRef GPGC_NewCollector::objectRef_needs_final_mark(objectRef old_ref, bool& final_mark)
{
  // For FinalLive marking only.
  final_mark = false;

  DEBUG_ONLY( old_ref = ALWAYS_UNPOISON_OBJECTREF(old_ref); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  if ( ! old_ref.is_new() ) { return old_ref; }

  // We ignore refs with up-to-date NMT values.
  if ( GPGC_NMT::has_desired_new_nmt(old_ref) ) { return old_ref; }

  objectRef new_ref = old_ref;

  if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
    // Remap the objectRef.
    assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(new_ref), "new-space refs should be new-space trapped");

    new_ref = GPGC_Collector::get_forwarded_object(new_ref);

    // Final marking doesn't cross into OldSpace.
    if ( new_ref.is_old() ) { return new_ref; }
  }

  // Objects already marked strong live are ignored.
  if ( GPGC_Marks::is_new_marked_strong_live(new_ref) ) { return new_ref; }

  final_mark = true;

  return new_ref;
}


inline void GPGC_NewCollector::unlink_weak_root(objectRef* ref_addr, objectRef old_ref)
{
  assert(GPGC_Thread::new_gc_thread() == Thread::current(), "expecting the NewGC thread");
  assert(!GPGC_Space::is_in(ref_addr), "Weak root unlink shouldn't be looking at addresses inside the heap");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef new_ref = weak_root_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  if ( cas_ref ) {
    if ( mark_in_new ) {
      if ( GPGC_Marks::is_new_marked_strong_live(new_ref) ) {
        intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
        intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();
        if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
          // Successfully CAS updated the ref in memory.  
          GPGC_Marks::set_nmt_update_markid(ref_addr, 0x30);
        }
      } else {
        *ref_addr = nullRef;
      }
    }
    else {
      intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
      intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();
      if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
        // Successfully CAS updated the ref in memory.  
        GPGC_Marks::set_nmt_update_markid(ref_addr, 0x31);

        if ( mark_in_old ) {
          oop new_oop = new_ref.as_oop();
          if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
            GPGC_Marks::set_markid(new_oop, 0x32);

            GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager( 0 );

            gcm->push_ref_to_nto_stack(new_ref, KlassIds::new_weak_jni_root);
          }
        }
      }
    }
  }
}


// Return 1 if a ref to NewGen is found at the ref_addr, 0 otherwise.
// Card marks are only marked through StrongLive, so there's no FinalLive version of this method.
inline long GPGC_NewCollector::mark_and_push_from_cardmark(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");
  assert(GPGC_Space::is_valid_virtual_addr(ref_addr), "Card marking should only be looking at addresses inside the heap");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef old_ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);
  long      result  = old_ref.is_new() ? 1 : 0;
  objectRef new_ref = cardmark_ref_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  assert( (!mark_in_new) || (cas_ref), "If we're supposed to mark_in_new, we expect cas_ref to be true" );

  if ( cas_ref ) {
    // Try and update the objectRef in memory.
    assert0( new_ref.not_null() );

    intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
    intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x35);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_new ) {
        assert0(new_ref.is_new());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x36);
          gcm->push_ref_to_stack(new_ref, KlassIds::cardmark_root);
        }
      }
      else if ( mark_in_old ) {
        assert0(new_ref.is_old());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x37);
          gcm->push_ref_to_nto_stack(new_ref, KlassIds::cardmark_root);
        }
      }
    }
  }

  return result;
}


inline void GPGC_NewCollector::mark_and_push_from_New_root(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");
  assert(!GPGC_Space::is_in(ref_addr), "Root marking should only be looking at addresses outside the heap");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef old_ref = *ref_addr;

  DEBUG_ONLY( bool is_poisoned = old_ref.is_poisoned(); )
  old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr);
  objectRef new_ref = root_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  assert( (!mark_in_new) || (cas_ref), "If we're supposed to mark_in_new, we expect cas_ref to be true" );

  if ( cas_ref ) {
    // Try and update the objectRef in memory.
    assert0( new_ref.not_null() );

    intptr_t old_value = old_ref.raw_value();
    intptr_t new_value = new_ref.raw_value();

    DEBUG_ONLY( if (is_poisoned) { old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value(); } )
    DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x38);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_new ) {
        assert0(new_ref.is_new());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x39);
          gcm->push_ref_to_stack(new_ref, KlassIds::new_system_root);
        }
      }
      else if ( mark_in_old ) {
        assert0(new_ref.is_old());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x3A);
          gcm->push_ref_to_nto_stack(new_ref, KlassIds::new_system_root);
        }
      }
    }
  }
}


//  Mark&Push of roots for a NewToOld GC cycle have the same action table as standard NewGC ref traversal.
//  So, we use the mark_and_push() call to do the work here.  It's a separate function just to get the
//  benefit of different mark IDs.
//
//     Original Ref  Revised Ref  Marking Action       Action
//     ------------  -----------  ------------------   -------
//     new-space     new-space    root scan NewToOld   Remap/NMT & Mark
//     new-space     old-space    root scan NewToOld   Remap/NMT & Transmit
//     old-space     new-space    any                  ShouldNotReachHere()
//     old-space     old-space    root scan NewToOld   Remap/NMT & Transmit
//
inline void GPGC_NewCollector::mark_and_push_from_NewToOld_root(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef old_ref = *ref_addr;

  if ( old_ref.is_null() ) { return; }

  DEBUG_ONLY( bool is_poisoned = old_ref.is_poisoned(); )
  DEBUG_ONLY( old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  objectRef new_ref = new_ref_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  assert( ((!mark_in_new) && (!mark_in_old)) || cas_ref, "If we're supposed to mark, we expect cas_ref to be true" );

  if ( cas_ref ) {
    assert0( new_ref.not_null() );

    intptr_t old_value = old_ref.raw_value();
    intptr_t new_value = new_ref.raw_value();

    DEBUG_ONLY( if (is_poisoned) { old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value(); } )
    DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x3B);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_new ) {
        assert0(new_ref.is_new());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x3C);
          gcm->push_ref_to_stack(new_ref, KlassIds::new2old_root);
        }
      }
      else if ( mark_in_old ) {
        assert0(new_ref.is_old());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x3D);
          gcm->push_ref_to_nto_stack(new_ref, KlassIds::new2old_root);
        }
      }
    }
  }
}


inline void GPGC_NewCollector::mark_and_push(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr, int referrer_klass_id)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef old_ref = *ref_addr;

  if ( old_ref.is_null() ) { return; }

  DEBUG_ONLY( bool is_poisoned = old_ref.is_poisoned(); )
  DEBUG_ONLY( old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  objectRef new_ref = new_ref_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  assert( ((!mark_in_new) && (!mark_in_old)) || cas_ref, "If we're supposed to mark, we expect cas_ref to be true" );

  if ( cas_ref ) {
    assert0( new_ref.not_null() );

    intptr_t old_value = old_ref.raw_value();
    intptr_t new_value = new_ref.raw_value();

    DEBUG_ONLY( if (is_poisoned) { old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value(); } )
    DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x3E);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_new ) {
        assert0(new_ref.is_new());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x3F);
          gcm->push_ref_to_stack(new_ref, referrer_klass_id);
        }
      }
      else if ( mark_in_old ) {
        assert0(new_ref.is_old());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x40);
          gcm->push_ref_to_nto_stack(new_ref, referrer_klass_id);
        }
      }
    }
  }
}


inline void GPGC_NewCollector::mark_and_push(GPGC_GCManagerNewFinal* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      final_mark;

  objectRef old_ref = *ref_addr;
  objectRef new_ref = objectRef_needs_final_mark(old_ref, final_mark);

  assert((!final_mark) || new_ref.is_new(), "If we're final marking, we should still be in NewGen");

  if ( final_mark ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_final_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid_final(new_oop, 0x41);
      gcm->push_ref_to_stack(new_ref, 0);
    }
  }
}


inline void GPGC_NewCollector::mark_and_push_klass(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      cas_ref;
  bool      mark_in_old;

  objectRef old_ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);

  assert(old_ref.is_old(), "Klass shouldn't be NULL or new-space");

  // We skip processing klassRefs when not running an NTO gc cycle.
  //
  // It's possible for a non-NTO cycle to find an entry in the KlassTable
  // that OldGC hasn't yet found and updated: compiled methods can allocate
  // NewGen objects without LVB'ing a compiled in KID.  OldGC will eventually
  // see the methodCodeOop that contains the klassRef of the KID embedded in
  // the assembly code, and will then update the KlassTable entry.
  //
  // It's not legal for anything but an NTO cycle to push objectRefs onto the
  // NTO stack, and the scenario is difficult to assert for, so we don't
  // bother checking the klassRef, we just return immediately.
  if( ! GPGC_NewCollector::mark_old_space_roots() ) {
    return;
  }

  objectRef new_ref = klass_ref_needed_nmt(old_ref, cas_ref, mark_in_old);

  assert( (!mark_in_old) || cas_ref, "If we're supposed to mark, we expect cas_ref to be true" );

  if ( cas_ref ) {
    assert0( new_ref.is_old() );

    intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
    intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x42);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_old ) {
        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x43);
          // XXX: this is bogus, callers to mark_and_push_klass should pass in a referring KID.
          gcm->push_ref_to_nto_stack(new_ref, Klass::cast((klassOop)new_oop)->klassId());
        }
      }
    }
  }
}


inline void GPGC_NewCollector::mark_and_follow(GPGC_GCManagerNewStrong* gcm, objectRef* ref_addr, int referrer_klass_id)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      cas_ref;
  bool      mark_in_new;
  bool      mark_in_old;

  objectRef old_ref = *ref_addr;

  if ( old_ref.is_null() ) { return; }

  DEBUG_ONLY( bool is_poisoned = old_ref.is_poisoned(); )
  DEBUG_ONLY( old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  objectRef new_ref = new_ref_needed_nmt(old_ref, cas_ref, mark_in_new, mark_in_old);

  assert( ((!mark_in_new) && (!mark_in_old)) || cas_ref, "If we're supposed to mark, we expect cas_ref to be true" );

  if ( cas_ref ) {
    assert0( new_ref.not_null() );

    intptr_t old_value = old_ref.raw_value();
    intptr_t new_value = new_ref.raw_value();

    DEBUG_ONLY( if (is_poisoned) { old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value(); } )
    DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x44);

      // We only need to mark through a objectRef if we're able to update memory.
      if ( mark_in_new ) {
        assert0(new_ref.is_new());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x45);
          GPGC_Marks::set_marked_through(new_oop);

          new_oop->GPGC_follow_contents(gcm);

int size=new_oop->size();
          GPGC_Space::add_live_object(new_oop, size);
          if (ProfileLiveObjects) {
            Thread::current()->live_objects()->add(new_ref.klass_id(), referrer_klass_id, 1, size);
          }
        }
      }
      else if ( mark_in_old ) {
        assert0(new_ref.is_old());

        oop new_oop = new_ref.as_oop();
        if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
          GPGC_Marks::set_markid(new_oop, 0x46);
          gcm->push_ref_to_nto_stack(new_ref, referrer_klass_id);
        }
      }
    }
  }
}


inline void GPGC_NewCollector::mark_and_follow(GPGC_GCManagerNewFinal* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

  bool      final_mark;

  objectRef old_ref = *ref_addr;
  objectRef new_ref = objectRef_needs_final_mark(old_ref, final_mark);

  assert((!final_mark) || new_ref.is_new(), "If we're final marking, we should still be in NewGen");

  if ( final_mark ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_final_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid_final(new_oop, 0x47);
      GPGC_Marks::set_marked_through_final(new_oop);

      new_oop->GPGC_follow_contents(gcm);
    }
  }
}


// Remap trapped new_space refs, and update the cardmark table for refs into new_space.
inline void GPGC_NewCollector::update_card_mark(objectRef* ref_addr)
{
  objectRef old_ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);

  if ( ! old_ref.is_new() ) { return; }


  if ( GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    assert(GPGC_ReadTrapArray::is_new_gc_remap_trapped(old_ref), "new-space refs should be new-space trapped");
    assert(GPGC_NMT::has_desired_new_nmt(old_ref), "Unremapped objectRef's should have good NMT at this point");
 
    objectRef new_ref = GPGC_Collector::get_forwarded_object(old_ref);

    intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
    intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();

    if ( old_value != Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x48);
    }

    old_ref = new_ref;
  }

  if ( old_ref.is_new() ) {
    GPGC_Marks::atomic_attempt_card_mark(ref_addr);
  }
}


inline void GPGC_NewCollector::mutator_update_card_mark(objectRef* ref_addr)
{
  assert0(!Thread::current()->is_gc_mode());
  assert0(!Thread::current()->is_GC_thread() || Thread::current()->is_VM_thread());

  objectRef ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);

  // TODO: maw: a prior version of the code needed to lvb the old space refs also.  Dunno why that was.

  if ( ! ref.is_new() ) {
    // If the old_ref isn't new-space, it must be either NULL or old-space.
    assert0(ref.is_null() || ref.is_old());
    return;
  }

  if ( GPGC_NewCollector::collection_state() == GPGC_Collector::ConcurrentMarking ) {
    // If NewGC is in concurrent marking, it may miss the card-marks, so we better make sure
    // it hears about this ref.
    // lvb's expect the ref to be poisoned.
    ref = lvb_loadedref(ALWAYS_POISON_OBJECTREF(ref), ref_addr);

    if ( ! ref.is_new() ) {
      // Don't need to update the card-mark if the ref isn't in NewGen anymore.
      return;
    }
  }

  // Make sure the card-mark bit is set.
  GPGC_Marks::atomic_attempt_card_mark(ref_addr);
}


// Determine if an object from instanceRefKlass:GPGC_oop_follow_contents() is
// claimed by the JLR handler or not.  If the JLR handler claims it, then false
// is returned to tell the caller that the referent field should not be marked
// through.
inline bool GPGC_NewCollector::mark_through_non_strong_ref(GPGC_GCManagerNewStrong* gcm,
objectRef referent,
                                                           oop obj,
                                                           ReferenceType ref_type)
{
  if ( referent.is_new() ) {
    referent = GPGC_Collector::remap(referent);

    if ( referent.is_new() &&
         ! GPGC_Marks::is_new_marked_strong_live(referent) &&
         jlr_handler()->capture_java_lang_ref(gcm->reference_lists(), obj, ref_type) )
    {
      if ( ref_type == REF_FINAL ) {
        // The JLR handler has captured this reference.  FinalRefs need to be concurrently
        // marked through for the GPGC concurrent final JLR handling algorithm.
        oop referent_oop = referent.as_oop();
        if ( GPGC_Marks::atomic_mark_final_live_if_dead(referent_oop) ) {
          GPGC_GCManagerNewFinal* final_gcm = GPGC_GCManagerNewFinal::get_manager(gcm->manager_number());
          GPGC_Marks::set_markid_final(referent_oop, 0x49);
          final_gcm->push_ref_to_stack(referent, 0);
        }
      }

      return false;
    }
  }

  return true;
}


#endif // GPGC_NEWCOLLECTOR_INLINE_HPP
