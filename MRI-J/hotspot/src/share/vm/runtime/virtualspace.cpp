/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "modules.hpp"
#include "ostream.hpp"
#include "virtualspace.hpp"

#include "allocation.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"

// ReservedSpace
ReservedSpace::ReservedSpace(size_t size,bool aliasable){
initialize(size,0,false,aliasable);
}

ReservedSpace::ReservedSpace(size_t size, char* requested_address, size_t alignment, bool large, bool aliasable) {
  initialize(size, alignment, large, aliasable, requested_address);
}

// this constructor is used to support different 
// alignment for the different generations
// which we don't support yet
ReservedSpace::ReservedSpace(const size_t prefix_size, const size_t prefix_align, const size_t suffix_size,
                             const size_t suffix_align, char* requested_address, bool aliasable) 
                             
{
  assert(prefix_size != 0, "sanity");
  assert(prefix_align != 0, "sanity");
  assert(suffix_size != 0, "sanity");
  assert(suffix_align != 0, "sanity");
assert(((prefix_size&(prefix_align-1))==0),
      "prefix_size not divisible by prefix_align");
assert((suffix_size&(suffix_align-1))==0,
      "suffix_size not divisible by suffix_align");
assert((suffix_align&(prefix_align-1))==0,
      "suffix_align not divisible by prefix_align");

  // On systems where the entire region has to be reserved and committed up
  // front, the compound alignment normally done by this method is unnecessary.
const bool try_reserve_special=UseLargePages&&(prefix_align==os::large_page_size());
    
  if (!os::can_commit_large_page_memory() && try_reserve_special) {
initialize(prefix_size+suffix_size,prefix_align,true,aliasable);
    return;
  } else  {
    initialize(prefix_size + suffix_size, prefix_align, false, aliasable, requested_address);
  }
}

void ReservedSpace::initialize(size_t size, size_t alignment, bool large, 
bool aliasable,char*requested_address){
  const size_t granularity = os::vm_allocation_granularity();
assert((size&(granularity-1))==0,
         "size not aligned to os::vm_allocation_granularity()");
assert((alignment&(granularity-1))==0,
         "alignment not aligned to os::vm_allocation_granularity()");
  assert(alignment == 0 || is_power_of_2((intptr_t)alignment),
         "not a power of 2");

  _base = NULL;
  _size = 0;
  _special = false;
  _alignment = 0;
  if (size == 0) {
    return;
  }
  // If OS doesn't support demand paging for large page memory, we need 
  // to use reserve_memory_special() to reserve and pin the entire region.
  bool special = large && !os::can_commit_large_page_memory();
  char* base = NULL;

  if (special) {
    ShouldNotReachHere();
    // It's not hard to implement reserve_memory_special() such that it can
    // allocate at fixed address, but there seems no use of this feature 
    // for now, so it's not implemented.
    assert(requested_address == NULL, "not implemented");

    base = os::reserve_memory_special(size);
    
    if (base != NULL) {
      // Check alignment constraints
      if (alignment > 0) {
        assert((uintptr_t) base % alignment == 0, 
               "Large pages returned a non-aligned address"); 
      }
      _special = true;
    } else {
      // failed; try to reserve regular memory below
    }
  }

  {
    // Optimistically assume that the OSes returns an aligned base pointer.
    // When reserving a large address range, most OSes seem to align to at 
    // least 64K.

    // If the memory was requested at a particular address, use
    // os::attempt_reserve_memory_at() to avoid over mapping something
    // important.  If available space is not detected, return NULL.

    if (requested_address != 0) {
base=os::attempt_reserve_memory_at(size,requested_address,aliasable);
    } else {
base=os::reserve_memory(size,NULL,false,false,aliasable);
    }

    if (base == NULL) return;

    // Check alignment constraints
if(alignment>0&&((uintptr_t)base%alignment!=0)){
assert((uintptr_t)requested_address%alignment==0,"preferred_addr not aligned to forced_base_alignement.");
      // Base not aligned, retry
      os::release_memory(base, size);
      // Reserve size large enough to do manual alignment and
      // increase size to a multiple of the desired alignment
      size = align_size_up(size, alignment);
      size_t extra_size = size + alignment;
char*extra_base=os::reserve_memory(extra_size,requested_address,true,false,aliasable);
      if (extra_base == NULL) return;
      // Do manual alignement
      base = (char*) align_size_up((uintptr_t) extra_base, alignment);
      assert(base >= extra_base, "just checking");
      // Release unused areas
      size_t unused_bottom_size = base - extra_base;
      size_t unused_top_size = extra_size - size - unused_bottom_size;
      assert(unused_bottom_size % os::vm_allocation_granularity() == 0,
             "size not allocation aligned");
      assert(unused_top_size % os::vm_allocation_granularity() == 0,
             "size not allocation aligned");
      if (unused_bottom_size > 0) {
	os::release_memory(extra_base, unused_bottom_size);
      }
      if (unused_top_size > 0) {
	os::release_memory(base + size, unused_top_size);
      }
    }
  }
  // Done
  _base = base;
  _size = size;
  _alignment = MAX2(alignment, (size_t) os::vm_page_size());

  // Nice asserts; but Linux is handing me very large addresses for which this
  // fails... for JNIHandle blocks for which I don't care about treating as
  // objectRefs.
  //assert(markWord::pointer_fits_in_mark(_base),
  // "area must be distinguisable from marks for mark-sweep");
  //assert(markWord::pointer_fits_in_mark(&_base[size]),
  // "area must be distinguisable from marks for mark-sweep");
}


ReservedSpace::ReservedSpace(char* base, size_t size, bool special, size_t alignment) {
  assert((size % os::vm_allocation_granularity()) == 0,
         "size not allocation aligned");
  _base = base;
  _size = size;
  _alignment = alignment;
  _special = special;
}


ReservedSpace ReservedSpace::first_part(size_t partition_size, size_t alignment,
                                        bool split, bool realloc) {
  assert(partition_size <= size(), "partition failed");
  if (split) {
    os::split_reserved_memory(_base, _size, partition_size, realloc);
  }
  ReservedSpace result(base(), partition_size, alignment, special());
  return result;
}

ReservedSpace
ReservedSpace::last_part(size_t partition_size, size_t alignment) {
  assert(partition_size <= size(), "partition failed");
  ReservedSpace result(base() + partition_size, size() - partition_size,
		       alignment, special());
  return result;
}

size_t ReservedSpace::page_align_size_up(size_t size) {
  return align_size_up(size, os::vm_page_size());
}


size_t ReservedSpace::page_align_size_down(size_t size) {
  return align_size_down(size, os::vm_page_size());
}


size_t ReservedSpace::allocation_align_size_up(size_t size) {
  return align_size_up(size, os::vm_allocation_granularity());
}


size_t ReservedSpace::allocation_align_size_down(size_t size) {
  return align_size_down(size, os::vm_allocation_granularity());
}


void ReservedSpace::release() {
  if (is_reserved()) {
    if (special()) {
      ShouldNotReachHere();
      os::release_memory_special(_base, _size);
    } else{
      os::release_memory(_base, _size);
    }
    _base = NULL;
    _size = 0;
    _special = false;
  }
}


// VirtualSpace

VirtualSpace::VirtualSpace() {
  _account                = os::CHEAP_COMMITTED_MEMORY_ACCOUNT;
  _low_boundary           = NULL;
  _high_boundary          = NULL;
  _low                    = NULL;
  _high                   = NULL;
  _lower_high             = NULL;
  _middle_high            = NULL;
  _upper_high             = NULL;
  _lower_high_boundary    = NULL;
  _middle_high_boundary   = NULL;
  _upper_high_boundary    = NULL;
  _lower_alignment        = 0;
  _middle_alignment       = 0;
  _upper_alignment        = 0;
}


bool VirtualSpace::initialize(ReservedSpace rs, size_t committed_size, os::MemoryAccount account) {
  if(!rs.is_reserved()) return false;  // allocation failed.
  assert(_low_boundary == NULL, "VirtualSpace already initialized");
  _account = account;
  _low_boundary  = rs.base();
  _high_boundary = low_boundary() + rs.size();

  _low = low_boundary();
  _high = low();

  _special = rs.special();

  // When a VirtualSpace begins life at a large size, make all future expansion
  // and shrinking occur aligned to a granularity of large pages.  This avoids
  // fragmentation of physical addresses that inhibits the use of large pages
  // by the OS virtual memory system.  Empirically,  we see that with a 4MB 
  // page size, the only spaces that get handled this way are codecache and 
  // the heap itself, both of which provide a substantial performance 
  // boost in many benchmarks when covered by large pages.
  //
  // No attempt is made to force large page alignment at the very top and 
  // bottom of the space if they are not aligned so already. 
  _lower_alignment = os::vm_page_size();
  // For azul virtual memory we virtually always want to align on a large page boundary.
  _middle_alignment = (UseLargePages && rs.size() >= os::large_page_size()) ?
                      os::large_page_size() : os::vm_page_size();
  _upper_alignment  = os::vm_page_size();

  // End of each region
  _lower_high_boundary = (char*) round_to((intptr_t) low_boundary(), middle_alignment());
  _middle_high_boundary = (char*) round_down((intptr_t) high_boundary(), middle_alignment());
  _upper_high_boundary = high_boundary();

  // High address of each region
  _lower_high = low_boundary();
  _middle_high = lower_high_boundary();
  _upper_high = middle_high_boundary();

  // commit to initial size
  if (committed_size > 0) {
if(!expand_by(committed_size,false)){
      return false;
    }
  }
  return true;
}


VirtualSpace::~VirtualSpace() { 
  release();
}


void VirtualSpace::release() {
os::release_memory(low_boundary(),reserved_size());
  _low_boundary           = NULL;
  _high_boundary          = NULL;
  _low                    = NULL;
  _high                   = NULL;
  _lower_high             = NULL;
  _middle_high            = NULL;
  _upper_high             = NULL;
  _lower_high_boundary    = NULL;
  _middle_high_boundary   = NULL;
  _upper_high_boundary    = NULL;
  _lower_alignment        = 0;
  _middle_alignment       = 0;
  _upper_alignment        = 0;
  _special                = false;
}


size_t VirtualSpace::committed_size() const { 
  return pointer_delta(high(), low(), sizeof(char));
}


size_t VirtualSpace::reserved_size() const {
  return pointer_delta(high_boundary(), low_boundary(), sizeof(char));
}


size_t VirtualSpace::uncommitted_size()  const {
  return reserved_size() - committed_size();
}


bool VirtualSpace::contains(const void* p) const {
  return low() <= (const char*) p && (const char*) p < high();
}

/* 
   First we need to determine if a particular virtual space is using large
   pages.  This is done at the initialize function and only virtual spaces
   that are larger than LargePageSizeInBytes use large pages.  Once we
   have determined this, all expand_by and shrink_by calls must grow and
   shrink by large page size chunks.  If a particular request 
   is within the current large page, the call to commit and uncommit memory
   can be ignored.  In the case that the low and high boundaries of this
   space is not large page aligned, the pages leading to the first large
   page address and the pages after the last large page address must be
   allocated with default pages.
*/
bool VirtualSpace::expand_by(size_t bytes, bool pre_touch) {
  if (uncommitted_size() < bytes) return false;

  if (special()) {
    ShouldNotReachHere();
    // don't commit memory if the entire space is pinned in memory
    _high += bytes;
    return true;
  }


  char* previous_high = high();
  char* unaligned_new_high = high() + bytes;
  assert(unaligned_new_high <= high_boundary(), 
         "cannot expand by more than upper boundary");

  // Calculate where the new high for each of the regions should be.  If
  // the low_boundary() and high_boundary() are LargePageSizeInBytes aligned
  // then the unaligned lower and upper new highs would be the 
  // lower_high() and upper_high() respectively.
  char* unaligned_lower_new_high = 
    MIN2(unaligned_new_high, lower_high_boundary());
  char* unaligned_middle_new_high = 
    MIN2(unaligned_new_high, middle_high_boundary());
  char* unaligned_upper_new_high = 
    MIN2(unaligned_new_high, upper_high_boundary());

  // Align the new highs based on the regions alignment.  lower and upper 
  // alignment will always be default page size.  middle alignment will be
  // LargePageSizeInBytes if the actual size of the virtual space is in
  // fact larger than LargePageSizeInBytes.
  char* aligned_lower_new_high = 
    (char*) round_to((intptr_t) unaligned_lower_new_high, lower_alignment());
  char* aligned_middle_new_high =
    (char*) round_to((intptr_t) unaligned_middle_new_high, middle_alignment());
  char* aligned_upper_new_high =
    (char*) round_to((intptr_t) unaligned_upper_new_high, upper_alignment());

  // Determine which regions need to grow in this expand_by call.
  // If you are growing in the lower region, high() must be in that
  // region so calcuate the size based on high().  For the middle and
  // upper regions, determine the starting point of growth based on the 
  // location of high().  By getting the MAX of the region's low address 
  // (or the prevoius region's high address) and high(), we can tell if it
  // is an intra or inter region growth.
  size_t lower_needs = 0;
  if (aligned_lower_new_high > lower_high()) {
    lower_needs =
      pointer_delta(aligned_lower_new_high, lower_high(), sizeof(char));
  }
  size_t middle_needs = 0;
  if (aligned_middle_new_high > middle_high()) {
    middle_needs =
      pointer_delta(aligned_middle_new_high, middle_high(), sizeof(char));
  }
  size_t upper_needs = 0;
  if (aligned_upper_new_high > upper_high()) {
    upper_needs =
      pointer_delta(aligned_upper_new_high, upper_high(), sizeof(char));
  }

  // Check contiguity.
  assert(low_boundary() <= lower_high() && 
         lower_high() <= lower_high_boundary(),
	 "high address must be contained within the region");
  assert(lower_high_boundary() <= middle_high() && 
	 middle_high() <= middle_high_boundary(),
	 "high address must be contained within the region");
  assert(middle_high_boundary() <= upper_high() &&
	 upper_high() <= upper_high_boundary(), 
	 "high address must be contained within the region");

  // Commit regions
  if (lower_needs > 0) {
    assert(low_boundary() <= lower_high() &&
	   lower_high() + lower_needs <= lower_high_boundary(), 
	   "must not expand beyond region");
    if (!os::commit_memory(_account, lower_high(), lower_needs, Modules::VirtualSpace)) {
      debug_only(warning("os::commit_memory(%d, %p, %lld) failed", _account, lower_high(), lower_needs));
      return false;
    } else {
      _lower_high += lower_needs;
     }
  }
  if (middle_needs > 0) {
    assert(lower_high_boundary() <= middle_high() &&
	   middle_high() + middle_needs <= middle_high_boundary(), 
	   "must not expand beyond region");
if(!os::commit_memory(_account,middle_high(),middle_needs,middle_alignment(),Modules::VirtualSpace)){
      debug_only(warning("os::commit_memory(%d, %p, %lld) failed", _account, middle_high(), middle_needs));
      return false;
    } 
    _middle_high += middle_needs;
  }
  if (upper_needs > 0) {
    assert(middle_high_boundary() <= upper_high() &&
	   upper_high() + upper_needs <= upper_high_boundary(), 
	   "must not expand beyond region");
    if (!os::commit_memory(_account, upper_high(), upper_needs, Modules::VirtualSpace)) {
      debug_only(warning("os::commit_memory(%d, %p, %lld) failed", _account, upper_high(), upper_needs));
      return false;
    } else {
      _upper_high += upper_needs;
    }
  }

  if (pre_touch || AlwaysPreTouch) {
    int vm_ps = os::vm_page_size();
    for (char* curr = previous_high;
 	 curr < unaligned_new_high;
 	 curr += vm_ps) {
      // Note the use of a write here; originally we tried just a read, but
      // since the value read was unused, the optimizer removed the read.
      // If we ever have a concurrent touchahead thread, we'll want to use
      // a read, to avoid the potential of overwriting data (if a mutator
      // thread beats the touchahead thread to a page).  There are various
      // ways of making sure this read is not optimized away: for example,
      // generating the code for a read procedure at runtime.
      *curr = 0;
    }
  }
  
  _high += bytes;
  return true;
}

// A page is uncommitted if the contents of the entire page is deemed unusable.
// Continue to decrement the high() pointer until it reaches a page boundary
// in which case that particular page can now be uncommitted.
void VirtualSpace::shrink_by(size_t size) {
  if (committed_size() < size) 
    fatal("Cannot shrink virtual space to negative size");

  if (special()) {
    ShouldNotReachHere();
    // don't uncommit if the entire space is pinned in memory
    _high -= size;
    return;
  }

  char* unaligned_new_high = high() - size;
  assert(unaligned_new_high >= low_boundary(), "cannot shrink past lower boundary");

  // Calculate new unaligned address
  char* unaligned_upper_new_high = 
    MAX2(unaligned_new_high, middle_high_boundary());
  char* unaligned_middle_new_high = 
    MAX2(unaligned_new_high, lower_high_boundary());
  char* unaligned_lower_new_high = 
    MAX2(unaligned_new_high, low_boundary());

  // Align address to region's alignment
  char* aligned_upper_new_high = 
    (char*) round_to((intptr_t) unaligned_upper_new_high, upper_alignment());
  char* aligned_middle_new_high = 
    (char*) round_to((intptr_t) unaligned_middle_new_high, middle_alignment());
  char* aligned_lower_new_high = 
    (char*) round_to((intptr_t) unaligned_lower_new_high, lower_alignment());
  
  // Determine which regions need to shrink
  size_t upper_needs = 0;
  if (aligned_upper_new_high < upper_high()) {
    upper_needs = 
      pointer_delta(upper_high(), aligned_upper_new_high, sizeof(char));
  }
  size_t middle_needs = 0;
  if (aligned_middle_new_high < middle_high()) {
    middle_needs = 
      pointer_delta(middle_high(), aligned_middle_new_high, sizeof(char));
  }
  size_t lower_needs = 0;
  if (aligned_lower_new_high < lower_high()) {
    lower_needs = 
      pointer_delta(lower_high(), aligned_lower_new_high, sizeof(char));
  }
  
  // Check contiguity.
  assert(middle_high_boundary() <= upper_high() &&
	 upper_high() <= upper_high_boundary(), 
	 "high address must be contained within the region");
  assert(lower_high_boundary() <= middle_high() && 
	 middle_high() <= middle_high_boundary(),
	 "high address must be contained within the region");
  assert(low_boundary() <= lower_high() && 
	 lower_high() <= lower_high_boundary(),
	 "high address must be contained within the region");

  // Uncommit
  if (upper_needs > 0) {
    assert(middle_high_boundary() <= aligned_upper_new_high &&
	   aligned_upper_new_high + upper_needs <= upper_high_boundary(), 
	   "must not shrink beyond region");
os::uncommit_memory(aligned_upper_new_high,upper_needs,Modules::VirtualSpace);
    _upper_high -= upper_needs;
  } 
  if (middle_needs > 0) {
    assert(lower_high_boundary() <= aligned_middle_new_high &&
	   aligned_middle_new_high + middle_needs <= middle_high_boundary(), 
	   "must not shrink beyond region");
os::uncommit_memory(aligned_middle_new_high,middle_needs,Modules::VirtualSpace);
    _middle_high -= middle_needs;
  }
  if (lower_needs > 0) {
    assert(low_boundary() <= aligned_lower_new_high &&
	   aligned_lower_new_high + lower_needs <= lower_high_boundary(), 
	   "must not shrink beyond region");
os::uncommit_memory(aligned_lower_new_high,lower_needs,Modules::VirtualSpace);
    _lower_high -= lower_needs;
  }
  
  _high -= size;
}

#ifndef PRODUCT
void VirtualSpace::check_for_contiguity() {
  // Check contiguity.
  assert(low_boundary() <= lower_high() && 
         lower_high() <= lower_high_boundary(),
         "high address must be contained within the region");
  assert(lower_high_boundary() <= middle_high() && 
         middle_high() <= middle_high_boundary(),
         "high address must be contained within the region");
  assert(middle_high_boundary() <= upper_high() &&
         upper_high() <= upper_high_boundary(),
         "high address must be contained within the region");
  assert(low() >= low_boundary(), "low");
  assert(low_boundary() <= lower_high_boundary(), "lower high boundary");
  assert(upper_high_boundary() <= high_boundary(), "upper high boundary");
  assert(high() <= upper_high(), "upper high");
}

void VirtualSpace::print() {  
  tty->print   ("Virtual space:");
  if (special()) {
    ShouldNotReachHere();
tty->print(" (pinned in memory)");
  }
  tty->cr();
  tty->print_cr(" - committed: %ld", committed_size());
  tty->print_cr(" - reserved:  %ld", reserved_size());
tty->print_cr(" - [low, high]:     ["PTR_FORMAT", "PTR_FORMAT"]",low(),high());
tty->print_cr(" - [low_b, high_b]: ["PTR_FORMAT", "PTR_FORMAT"]",low_boundary(),high_boundary());
}

#endif

