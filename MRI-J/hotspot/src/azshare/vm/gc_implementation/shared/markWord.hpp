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
#ifndef MARKWORD_HPP
#define MARKWORD_HPP



// Note: this is a complete rewrite of a file with a similar name in Sun's distribution.
// As far as I know, no code remained in common.

#include "debug.hpp"
#include "globalDefinitions_os.hpp"
#include "objectRef_pd.hpp"
class JavaThread;
class ObjectMonitor;
class oopDesc;

class markWord VALUE_OBJ_CLASS_SPEC{

  // Every object starts with a markWord!

  // markWord layout:
  // |      KID         |M|pad1 |sma| age |        hash/thr/mon          |LL|
  // |666655555555554444|4|444443|33|33333|332222222222111111111100000000|00|
  // |3  0    5    0   6|5|4   09|87|6   2|10    5    0    5    0    5  2|10|

  // Things to notice about markWord:

  // - There is no light-weight owner anymore; either: 
  //   the object is owned speculatively by some thread (biased) or else 
  //   there is a system hashCode, or else
  //   there is a heavyweight monitor.
  // - If thread-id has the low 2 bits both zero, then a biased-lock check can
  //   be done with just two instructions   "ld4 rX,[rOop+4]; beq rX,rThr".
  // - The hash can be extracted with 4 ops (3 on X86): 
  //      "ldu4 rX,[rOop+4]; and rY,rX.1; beq0 rY,slow; shr rX,rX,1"
  // - KID can be extracted with 2 ops: "ldu4 rX,[rOop]; shr rX,rX,9"
  // - An object both locked & hashed must have a heavy monitor which will
  //   keep the hash as well as track the locking.
  // - The layout has the KID laid out similar to the X86 REF layout:
#if defined(AZ_X86)
  // |          KID     |SSS|N|                   VA                  000|
  // |666655555555554444|444|4|443333333333222222222211111111110000000000|
  // |3 10    5    0   6|5 3|2|10    5    0    5    0    5    0    5  210|
#endif







 public:

  // Constants
#if defined(AZ_X86)
  enum {
    // Lock bits.
    // 00 - biased-locked to the thread-id in the next 30 bits
    // 10 - heavy-locked to the monitor in the next 30 bits
    // x1 - hashed to the next 30 bits plus the 'x' bit
    lock_bits = 2,
    // 30 bits of thread id, but 31 bits of hash (hash also uses 1 lock bit above).
    // All zeros means: never locked which is generally true for new objects.
    hash_bits = 30,
    // GC age bits - how many young-gen GC cycles an object survives before
    // being tenured into old-gen.  Currently, a max-score JBB on an Azul big
    // box (864 cpus) during a 4 minute timing interval takes 20-25 young-gen
    // GC cycles.  A 4-bit age means that current-run allocation crud survives
    // more than 4-bits of young-gen cycles and so gets promoted - about 200M.
    // If we have 5 bits, then the crud stays in young gen till the 4 minutes
    // are up (25 cycles is less than the 31 we can count in the age bits),
    // and dies there - meaning only 20M needs to get promoted per 4 minute
    // run, a factor of 10x smaller.  This in turn means we can dodge the
    // old-gc cycle for the entire 1600+ warehouse run.
    age_bits  = 5,
    // How many times has *this* object attempted & failed SMA?
    sma_bits = 2,
    // Line up the KID bits with REFs
    pad1_bits = 6, 
    // Used by ParallelGC
    mark_bits = 1,
    // Klass ID
    kid_bits  = 18,
    // Spare bits
    pad2_bits  = 64 - kid_bits-pad1_bits-sma_bits-age_bits-mark_bits-hash_bits-lock_bits
  };

  enum { lock_shift               = 0,
         hash_shift               = lock_shift + lock_bits,
         age_shift                = hash_shift + hash_bits,
         sma_shift                = age_shift + age_bits,
         pad1_shift               = sma_shift + sma_bits,
         mark_shift               = pad1_shift + pad1_bits,
         kid_shift                = mark_shift + mark_bits,
         pad2_shift               = kid_shift + kid_bits
  };
#else 
#error Unknown hardware so unknown layout
#endif

  enum { lock_mask                = (1LL<<lock_bits)-1,
         lock_mask_in_place       = lock_mask << lock_shift,
         hash_mask                = (1LL<<hash_bits)-1,
         hash_mask_in_place       = (intptr_t)hash_mask << hash_shift,
         age_mask                 = (1LL<<age_bits)-1,
         age_mask_in_place        = age_mask << age_shift,
         sma_mask                 = (1LL<<sma_bits)-1,
         sma_mask_in_place        = sma_mask << sma_bits,
         kid_mask                 = (1LL<<kid_bits)-1,
         kid_mask_in_place        = kid_mask << kid_shift,
         mark_mask                = (1LL<<mark_bits)-1,
         mark_mask_in_place       = mark_mask << mark_shift
  };

  enum { max_age = age_mask,
         max_sma = sma_mask };

  // ----
  // Functions to return various flavors of markWords.
  static bool pointer_fits_in_mark( void *p ) { return ((intptr_t)p >> objectRef::unknown_bits)==0; }
  static markWord *prototype_without_kid() { return (markWord*)0; }
  static markWord *prototype_with_kid(int kid) {
    assert((kid & ~kid_mask) == 0, "invalid kid");
    return (markWord*)(((intptr_t) kid) << kid_shift);
  }
  markWord *set_kid(int kid) {
    assert((kid & ~kid_mask) == 0, "invalid kid");
    return (markWord*)(((intptr_t)this & ~kid_mask_in_place) | (((intptr_t) kid) << kid_shift));
  }
  bool is_cleared() const { return (((intptr_t)this) & ~kid_mask_in_place) == 0; }
  markWord *clear_and_save_kid() const { return (markWord*)(((intptr_t)this) & kid_mask_in_place); }
  markWord *clear_save_kid_and_mark() const { return clear_and_save_kid()->set_mark_bit(); }
  markWord *copy_set_hash(intptr_t hash) const {
    assert0( (((intptr_t)this) & hash_mask_in_place) == 0 &&
             (((intptr_t)this) & lock_mask_in_place) == 0 );
    return (markWord*) (((intptr_t)this) | 
                        (hash & 0x7FFFFFFF) << (hash_shift-1) |
                        1);
  }


  // ----
  // Field access
  int kid() const { return (((intptr_t)this)>>kid_shift) & kid_mask; }
  // Only valid if the result is not equal to 0.  This is the fast-hash access.
  // If it's zero, the object may never have been hashed, or may have a slow hash.
  intptr_t hash() const {
    intptr_t bits = (intptr_t)this;
    return (bits&1) ? ((bits>>1)&(0x7FFFFFFF)) : 0;
  }
  // True if no hash; racey, can change moment by moment.  Also does not
  // detect slow-hashes.
  bool has_no_hash() const { return hash() == 0; }
  
  // Return an existing hash, or make a new one otherwise.  The new hash is
  // not installed in the object (it's basically a trial hashCode and is being
  // racily installed into an ObjectMonitor).
  intptr_t hash_try() const;

  int age() const { return (((intptr_t)this)>>age_shift) & age_mask; }
  markWord *incr_age() { return age() == max_age ? this : (markWord*)(((intptr_t)this) + (1LL<<age_shift)); }

  int sma() const { return (((intptr_t)this)>>sma_shift) & sma_mask; }
  markWord *incr_sma() { return sma() == max_sma ? this : (markWord*)(((intptr_t)this) + (1LL<<sma_shift)); }

  // Monitor access
  ObjectMonitor *monitor() const { 
    intptr_t bits = (intptr_t) this;
    assert0( (bits & lock_mask_in_place) == 2 );
    return (ObjectMonitor*)(bits & hash_mask_in_place);
  }
  bool has_monitor() const { return (((intptr_t)this) & lock_mask_in_place) == 2; }

  bool is_biased() const { 
    return (((intptr_t)this) & lock_mask_in_place) == 0 &&
           (((intptr_t)this) & hash_mask_in_place) != 0; }

  // Checks for being speculatively locked by self thread
  bool is_self_spec_locked() const;

  // Check for being self-locked.  A little more efficient than
  // "mark->lock_owner()==JavaThread::current()"
  bool is_self_locked( ) const;
  // Best-effort lock owner.  The ownership can change moment-by-moment.  Can
  // throw 'false positives' - it might be speculatively locked by some thread
  // (including self) - but if we demand accurate ownership info the spec
  // owner might release the lock.  Demanding accurate ownership can be
  // expensive.
  JavaThread* lock_owner( ) const;
  // Best-effort is-unlocked.  The ownership can change moment-by-moment.  Can
  // throw 'false positives' - it might be speculatively locked by some thread
  // (including self) - but if we demand accurate ownership info the spec
  // owner might release the lock.  Demanding accurate ownership can be
  // expensive.
  bool is_unlocked() const;
  // Low order bits all zero; can be CAS'd to biased or hashed or what-not
  bool is_fresh() const;
  
  // Return a version of this markWord as it would be, if it was bias-locked.
  // Handy for CAS'ing markWords during locking attempts.
  markWord *as_biaslocked( int tid ) const;

  // Return a version of this markWord as it would be, if it was fresh.
  // Handy for CAS'ing markWords during striping of bias-locks owned by dead threads.
  markWord *as_fresh() const;

  // Return a version of this markWord as it would be, if this ObjectMonitor
  // is install.  Handy for CAS'ing markWords during locking attempts.
  markWord *as_heavylocked( ObjectMonitor* ) const;

  // ----
  // Parallel GC needs a mark-bit and forwarding pointer support.  In a
  // forwarding pointer, the Reserved bit is set (and the NMT is not used
  // in parallel GC).  Other bits are exactly as given in an objectRef.
  //
  // |-SS|M|        KID        |                  VA                  000|
  // |666|6|5555555555444444444|43333333333222222222211111111110000000000|
  // |3 2|0|9   5    0    54  1|0    5    0    5    0    5    0    5  210|

  bool is_marked() { return (((intptr_t)this) >> mark_shift) & mark_mask; }
  markWord *set_mark_bit() { return (markWord*)(((intptr_t)this) | mark_mask_in_place); }

  // Prepare address of oopDesc for placement into mark.
  // Uses the KID + SpaceID bits from the 'this' pointer.
  markWord *encode_pointer_as_mark(void *p) const {
    return (markWord*)((((intptr_t)this) & ~objectRef::unknown_mask) |
                       (((intptr_t)p   ) &  objectRef::unknown_mask) |
                       mark_mask_in_place);
  }

  // Should this header be preserved during GC?
  bool must_be_preserved(oop obj_containing_mark) const { 
    return (((intptr_t)this) & (hash_mask_in_place|lock_mask_in_place)) != 0; 
  }
  // Should this header (including its age bits) be preserved in the
  // case of a promotion failure during scavenge?
  // Note that we special case this situation. We want to avoid
  // calling BiasedLocking::preserve_marks()/restore_marks() (which
  // decrease the number of mark words that need to be preserved
  // during GC) during each scavenge. During scavenges in which there
  // is no promotion failure, we actually don't need to call the above
  // routines at all, since we don't mutate and re-initialize the
  // marks of promoted objects using init_mark(). However, during
  // scavenges which result in promotion failure, we do re-initialize
  // the mark words of objects, meaning that we should have called
  // these mark word preservation routines. Currently there's no good
  // place in which to call them in any of the scavengers (although
  // guarded by appropriate locks we could make one), but the
  // observation is that promotion failures are quite rare and
  // reducing the number of mark words preserved during them isn't a
  // high priority.
  bool must_be_preserved_for_promotion_failure(oop obj_containing_mark) const {
return must_be_preserved(obj_containing_mark);
  }


  // Recover address of oopDesc from encoded form used in mark.
  oopDesc *decode_pointer() const {
    return (oopDesc *) ( ((intptr_t)this) & objectRef::unknown_mask);
  }

  // Prepare objectRef for placement into mark. 
  // Uses the KID bits in the objectRef, NOT from 'this'.
  // need a bit that is not already set.. could use the reserved_bit .. 
  // assert for the ref being unpoisoned
  static markWord *encode_ref_as_mark(objectRef p) {
#if defined(AZ_X86)
    const int64 x = KIDInRef ? 0 : ((int64)p.klass_id()<<kid_shift);
    return (markWord*)(p.raw_value() | objectRef::reserved_mask | x);
#else 
#error Unknown arch
#endif
  }

  // Recover ref from encoded form used in mark
  // (and all the lower bits as well to be able to decode to nullRef for prototypes).
objectRef decode_ref()const{
#if defined(AZ_X86)
    const int64 x = KIDInRef ? 0 : kid_mask_in_place;
    return objectRef( ((intptr_t)this) & ~objectRef::reserved_mask & ~x);
#else 
#error Unknown arch
#endif
  }

  // ----
  // Debugging
  void print_on(outputStream* st) const;
};


#endif // MARKWORD_HPP
