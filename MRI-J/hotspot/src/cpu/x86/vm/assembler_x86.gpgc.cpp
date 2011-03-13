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


// Calculate the address of a GPGC_PageInfo for a PageNum.
/*
void MacroAssembler::GPGC_get_pageinfo_addr(Register page_info, Register page_num, Register tmp)
{
  // Multi-page object handling: Calculate the object header size from the GPGC_PageInfo for the page block.
  guarantee(GPGC_PageInfo::page_info_size()==64, "GPGC_PageInfo size has changed, asm needs updating.");

  shl8i   (tmp, page_num, 6);                                    // tmp = page_num * 64
  set_int (page_info, (int64)GPGC_PageInfo::page_info_base());   // page_info = base of _page_info array
  add8    (page_info, page_info, tmp);                           // page_info = &_page_info[page_num] = base + page_num*64
}


// This destroys the contents of the "index" register.
void MacroAssembler::GPGC_increment_pageinfo(Register page_info, Register index)
{
  guarantee(GPGC_PageInfo::page_info_size()==64, "GPGC_PageInfo size has changed, asm needs updating.");

  shl8i   (index, index, 6);                                     // index = index * 64
  add8    (page_info, page_info, index);                         // page_info = page_info + index*64 = &page_info[index]
}
*/

//  For GenPauselessGC only.  Marks a new object allocated in NewGen as being alive.
//  This produces inline assembly for marking an object live.  The object is not
//  marked live if NewGC object marking is not enabled. 
//
//  If the object is marked live, its mark-through is set if GPGCAuditTrail is set,
//  and its markid is set if GPGCAuditTrail is set.
//
//  This code requires four temp registers.  The input register (ref) contains the heapRef
//  to mark live.  The contents of the four temp registers are undefined at the end of this
//  block of assembly.
//
//  If the caller needs CasVal or CCodes preserved, it's his responsibility to do so.
//
//  This assembly has a C++ version: GPGC_Marks::atomic_mark_live_if_dead().  Any
//  substantive change to this algorithm also needs to be made in the C++ method.
void MacroAssembler::GPGC_mark_tlab_alloced_obj(Register ref, Register t0, Register t1, Register t2, Register t3, long markid)
{
  assert0(UseGenPauselessGC);
assert(GPGC_SmallSpaceMarksStartWord==0,"bad heap page layout");
assert(GPGC_MidSpaceMarksStartWord==0,"bad heap page layout");

  // TODO: maw: I think we only need to mark objects allocated in TLABs as live if we're
  // verifying the marking state.  We ought to make this function produce no assembly if
  // !GPGCVerifyHeap && !GPGCAuditTrail
  // That should work because we're never going to try and relocate active TLAB pages, so
  // we don't have to be able to find the live objects in those pages.

  Label   already_marked, done_marking;
  /*
  set_ptr (t0, GPGC_NewCollector::should_mark_new_objects_live_addr());
  ld8     (t0, t0, 0);
  beqi    (t0, 0, done_marking);  // Skip to the end if marking is not enabled.
  */

  // We only mark the object live if the GPGC_NewCollector has marking enabled.
  ld8   (R12, GPGC_NMT::should_mark_new_objects_live_addr());
  null_chk(R12, done_marking);      

#ifdef ASSERT
  // Make sure we're only getting new_space heapRefs
Label is_new_space;
  shr8i   (ref, objectRef::space_shift);
  and8i   (ref, objectRef::space_mask);
  cmp8i   (ref, objectRef::new_space_id);
  jeq     (is_new_space);
  should_not_reach_here("not a new_space heapRef");
bind(is_new_space);
#endif // ASSERT

/*
#ifdef ASSERT
  // TLAB allocated objects should never be multi-page objects
  Label   is_one_page_space;
  extracti(t0, ref, LogBytesPerGPGCPage, objectRef::unknown_bits-LogBytesPerGPGCPage-1, 0); // t0 = page_num from ref
  assert(GPGC_Layout::start_of_multi_space < GPGC_Layout::start_of_one_space, "heap layout changed");
  set_int (t1, GPGC_Layout::start_of_one_space);
  cmp8    (t2, t0, t1);                                // if ( page_num < start_of_one_space )
  bnei    (t2, 0, is_one_page_space);                  // then heapRef is a multi-page object
  should_not_reach_here("no multi-page tlab allocs allowed"); 
  bind    (is_one_page_space);
#endif // ASSERT
*/

  shru8i  (t0, ref, LogBytesPerGPGCPage);
  shl8i   (t0, t0, LogBytesPerGPGCPage);                    // t0 = page_base (with metadata) from ref
  movlo   (t1, GPGC_ObjectsStartWord);
  aadd8   (t1, t0, t1);                     // t1 = objects base address

  // Reg usage: t0=mark bitmap addr, t1=object base addr (both with metadata)

  subu8   (t1, ref, t1);                    // t1 = byte offset of ref from objects base
  shr8i   (t1, t1, LogBytesPerWord);        // t1 = bit index of ref in marking bitmap
  shr8i   (t2, t1, LogBitsPerWord);         // t2 = word_index(bit_index)
  aadd8   (t0, t0, t2);                     // t0 = word_addr = &mark_bitmap[word_index]
  pref    (t0, 0, EXCLUSIVE);               // prefetch exclusive the marking bitmap word
  andi_   (t1, t1, (BitsPerWord-1));        // t1 = bit_in_word(bit_index)
  movlo   (t2, 0x1);
  shl8    (t1, t2, t1);                     // t1 = nth = bitmask(bit_index)
  
  // Reg usage: t0=word_addr, t1=nth

  // Loop to CAS a mark bit in the marking bitmap.  Since we're marking an object allocated
  // in a TLAB, it shouldn't be possible for someone else to mark this object live.  In fact,
  // it shouldn't be possible for anyone to mark an adjacent object live, so the CAS should
  // never fail.

  { // CAS loop:
Label start_loop;
bind(start_loop);
    ldc8  (t2, t0, 0);                      // t2 = *word_addr, and load CasVal
    and_  (t3, t2, t1);                     // t3 = word & nth
    bnei  (t3, 0, already_marked);          // Bail out if bit is already set.
    xor_  (t3, t2, t1);                     // toggle nth bit in word
    cas8  (t0, 0, t3);                      // try to CAS new mark word
    bcc0  (CC_Cas, start_loop);             // Back start of the loop if the CAS fails.
  }
/*
#ifndef PRODUCT
  // When we get here, we've marked the object live.
  if ( GPGCAuditTrail ) {
    GPGC_set_tlab_alloced_mark_id       (ref, t0, t1, t2, t3, markid);
    GPGC_set_tlab_alloced_marked_through(ref, t0, t1, t2, t3);
  }
#endif // ! PRODUCT
  */
  
  br      (done_marking);

  // Failure case:
bind(already_marked);
  should_not_reach_here("TLAB alloced obj already marked live!");

bind(done_marking);
}


//  For GenPauselessGC only.  Marks a heapRef caught in an NMT trap.  This produces inline
//  assembly for marking an object live.
//
//  If the object is marked live, its markid is set if GPGCAuditTrail is set.
//
//  This code requires three temp registers.  The input register (ref) contains the heapRef
//  to mark live.  The contents of the three temp registers and the ref register are undefined
//  at the end of this block of assembly.  If the code falls out the bottom, this function
//  marked the object live.  If someone else marks the object live, this code branches to
//  the already_marked label.
//
//  If the caller needs CasVal or CCodes preserved, it's his responsibility to do so.
//
//  This assembly has a C++ version: GPGC_Marks::atomic_mark_live_if_dead().  Any
//  substantive change to this algorithm also needs to be made in the C++ method.
//  
/*
void MacroAssembler::GPGC_mark_nmt_trapped_obj(Register ref, Register t0, Register t1, Register t2,
                                               Label& already_marked, long markid)
{
  assert0(UseGenPauselessGC);
  assert (GPGC_MarksStartWord==0, "mark bitmap offset changed, rewrite GPGC marking asm");

  // Use ref as a tmp register as well, destroying the original heapRef.
  Register t3 = ref;  

#ifdef ASSERT
  {
    Label   is_new_space, is_old_space, marking_state_ok;

    // We should only be getting NMT traps when marking is enabled on the generation of the object.
    extracti(t0, ref, objectRef::space_shift, objectRef::space_bits-1, 0);
    beqi    (t0, objectRef::new_space_id, is_new_space);
    beqi    (t0, objectRef::old_space_id, is_old_space);
    should_not_reach_here("ref is not a heapRef");

    bind    (is_new_space);
    set_ptr (t0, GPGC_NewCollector::should_mark_new_objects_live_addr());
    ld8     (t0, t0, 0);
    bnei    (t0, 0, marking_state_ok);  // Skip to the end the check if marking is enabled.
    should_not_reach_here("GPGC_mark_nmt_trapped_obj when NewCollector marking disabled");

    bind    (is_old_space);
    set_ptr (t0, GPGC_OldCollector::should_mark_new_objects_live_addr());
    ld8     (t0, t0, 0);
    bnei    (t0, 0, marking_state_ok);  // Skip to the end the check if marking is enabled.
    should_not_reach_here("GPGC_mark_nmt_trapped_obj when OldCollector marking disabled");

    bind    (marking_state_ok);
  }
#endif // ASSERT

  shru8i  (t0, ref, LogBytesPerGPGCPage);
  shl8i   (t0, t0, LogBytesPerGPGCPage);                    // t0 = page_base (with metadata) from ref
  movlo   (t1, GPGC_ObjectsStartWord);
  aadd8   (t1, t0, t1);                                     // t1 = objects base address

  // Reg usage: t0=mark bitmap addr, t1=object base addr (both with metadata) t3/ref=heapRef

  subu8   (t1, ref, t1);                                    // t1 = byte offset of ref from objects base
  shr8i   (t1, t1, LogBytesPerWord);                        // t1 = bit index of ref in marking bitmap
  shr8i   (t2, t1, LogBitsPerWord);                         // t2 = word_index(bit_index)
  aadd8   (t0, t0, t2);                                     // t0 = word_addr = &mark_bitmap[word_index]
  pref    (t0, 0, EXCLUSIVE);                               // prefetch exclusive the marking bitmap word
  andi_   (t1, t1, (BitsPerWord-1));                        // t1 = bit_in_word(bit_index)
  movlo   (t2, 0x1);
  shl8    (t1, t2, t1);                                     // t1 = nth = bitmask(bit_index)
  
  // Reg usage: t0=word_addr, t1=nth

  { // Loop to CAS a mark bit in the marking bitmap:
    Label start_loop;
    bind  (start_loop);
    ldc8  (t2, t0, 0);                                      // t2 = *word_addr, and load CasVal
    and_  (t3, t2, t1);                                     // t3 = word & nth
    bnei  (t3, 0, already_marked);                          // Bail out if bit is already set.
    xor_  (t3, t2, t1);                                     // toggle nth bit in word
    cas8  (t0, 0, t3);                                      // try to CAS new mark word
    bcc0  (CC_Cas, start_loop);                             // Back start of the loop if the CAS fails.
  }

  // When we get here, we've marked the object live

#ifndef PRODUCT
  // Reg usage: t0=word_addr, t1=nth
  if ( GPGCAuditTrail ) {
    GPGC_set_nmt_trapped_mark_id(t0, t1, t2, t3, markid);
  }
#endif // ! PRODUCT
}



void MacroAssembler::GPGC_set_nmt_trapped_mark_id(Register t0, Register t1, Register t2, Register t3, long markid)
{
  // As a debugging measure, we track where heapRefs get marked live.  We've destroyed the
  // original ref value, so we have to reverse engineer where to set the markid.

  // Reg usage: t0=mark_word_addr, t1=nth (bit within word)

  // The MarkID is stored in a byte array starting at PageBaseAddr + PageLayout::mark_ids_start_word()
  // Each two words of the objects gets a byte in the array.  We can calculate the byte index by reverse
  // engineering it from word_addr and nth bit that were calculated previously for setting the mark bit.

  // Reg usage: t0=word_addr, t1=nth (bit within word)
  shru8i  (t2, t0, LogBytesPerGPGCPage);
  shl8i   (t2, t2, LogBytesPerGPGCPage);                  // t2 = page_base addr from word_addr, aka marking bitmap base
  sub8    (t0, t0, t2);                                   // t0 = byte offset in marking bitmap of word_addr
  shru8i  (t0, t0, LogBytesPerWord);                      // t0 = word offset in marking bitmap of word_addr
  shl8i   (t0, t0, LogBitsPerWord);                       // t0 = # of object HeapWords represented by word_addr

  ctlz    (t1, t1);                                       // count the leading zeros in nth
  movlo   (t3, BitsPerWord-1);                            // t3 = 63
  sub4    (t1, t3, t1);                                   // t1 = bit_index of nth

  add8    (t0, t0, t1);                                   // t0 = byte_index, number of object words indicated by word_addr & nth

  movlo   (t3, GPGC_MarkIdsStartWord);
  aadd8   (t2, t2, t3);                                   // t2 = mark_ids_base = page_base_addr + GPGC_MarkIdsStartWord
  add8    (t0, t2, t0);                                   // t0 = byte addr = &mark_ids_base[byte_index]

  st1i    (t0, 0, markid);                                // Store the markID for the newly marked heapRef
}


void MacroAssembler::GPGC_set_tlab_alloced_mark_id(Register ref, Register t0, Register t1, Register t2, Register t3, long markid)
{
  assert0( GPGCAuditTrail );

#ifdef PRODUCT
  // There's no mark-ids array in the page header for PRODUCT builds!
  ShouldNotReachHere();
#endif // PRODUCT

  // As a debugging measure, we track where heapRefs get marked live:

  // Register usage:
  //   t0: nth        (bit within word)
  //   t1: word_addr

  // The MarkID is stored in a byte array starting at PageBaseAddr + GPGC_MarkIdsStartWord
  // Each two words of the object area gets a byte in the array.

  shru8i  (t0, ref, LogBytesPerGPGCPage);
  shl8i   (t0, t0, LogBytesPerGPGCPage);                    // t0 = page_base (with metadata) from ref
  movlo   (t1, GPGC_ObjectsStartWord);
  aadd8   (t1, t0, t1);                     // t1 = objects base address
  movlo   (t2, GPGC_MarkIdsStartWord); 
  aadd8   (t0, t0, t2);                     // t0 = mark IDs base address

  // Reg usage: t0=mark IDs addr, t1=object base addr (both with metadata)

  subu8   (t1, ref, t1);                    // t1 = markIDs_index, byte offset of ref from objects base
  shr8i   (t1, t1, LogBytesPerWord);        // t1 = markIDs_index = word_index of ref in objects 
  add8    (t0, t0, t1);                     // t0 = markIDs byte_addr = &markIDs[markIDs_index]

  st1i    (t0, 0, markid);
}


void MacroAssembler::GPGC_set_tlab_alloced_marked_through(Register ref, Register t0, Register t1, Register t2, Register t3)
{
  assert0( GPGCAuditTrail );

#ifdef PRODUCT
  // There's no mark-throughs array in the page header for PRODUCT builds!
  ShouldNotReachHere();
#endif // PRODUCT

  Label   already_marked;
  Label   done_marking;

  shru8i  (t0, ref, LogBytesPerGPGCPage);
  shl8i   (t0, t0, LogBytesPerGPGCPage);                    // t0 = page_base (with metadata) from ref
  movlo   (t1, GPGC_ObjectsStartWord);
  aadd8   (t1, t0, t1);                                     // t1 = objects base address
  movlo   (t2, GPGC_MarkThroughsStartWord); 
  aadd8   (t0, t0, t2);                                     // t0 = mark throughs base address

  // Reg usage: t0=mark throughs bitmap addr, t1=object base addr (both with metadata)

  subu8   (t1, ref, t1);                                    // t1 = byte offset of ref from objects base
  shr8i   (t1, t1, LogBytesPerWord);                        // t1 = bit index of ref in marking bitmap
  shr8i   (t2, t1, LogBitsPerWord);                         // t2 = word_index(bit_index)
  aadd8   (t0, t0, t2);                                     // t0 = word_addr = &mark_bitmap[word_index]
  pref    (t0, 0, EXCLUSIVE);                               // prefetch exclusive the marking bitmap word
  andi_   (t1, t1, (BitsPerWord-1));                        // t1 = bit_in_word(bit_index)
  movlo   (t2, 0x1);
  shl8    (t1, t2, t1);                                     // t1 = nth = bitmask(bit_index)
  
  // Reg usage: t0=word_addr, t1=nth

  // Loop to CAS a mark through bit in the marking bitmap.  There shouldn't ever be two threads
  // marking through the same object, so we shouldn't ever find the mark-through bit set already.

  { // CAS loop:
    Label start_loop;
    bind  (start_loop);
    ldc8  (t2, t0, 0);                      // t2 = *word_addr, and load CasVal
    and_  (t3, t2, t1);                     // t3 = word & nth
    bnei  (t3, 0, already_marked);          // Bail out if bit is already set.
    xor_  (t3, t2, t1);                     // toggle nth bit in word
    cas8  (t0, 0, t3);                      // try to CAS new mark word
    bcc0  (CC_Cas, start_loop);             // Back start of the loop if the CAS fails.
  }

  // When we get here, we've set the marked-through for the object.
  br      (done_marking);

  bind    (already_marked);
  should_not_reach_here("TLAB alloced obj already marked live!");

  // The marked_through bit for the ref is now set.
  bind    (done_marking);
}
*/
