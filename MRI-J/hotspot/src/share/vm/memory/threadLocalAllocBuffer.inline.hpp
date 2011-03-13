/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef THREADLOCALALLOCBUFFER_INLINE_HPP
#define THREADLOCALALLOCBUFFER_INLINE_HPP


#include "prefetch_os_pd.inline.hpp"

HeapWord* ThreadLocalAllocBuffer::clear_tlab_for_allocation(HeapWord* obj, HeapWord* tlab_end, size_t word_size, bool zero_mem) {
  // What we would like to do here is to maintain the clz pipeline.
  // If zero_mem==true, we also need to zero the memory being used by the newly allocated object.
  // What we will do is issue enough CLZ's to cover the object, then mfence on clz's.
  // After the fence, we will issue the remaining CLZ's, which do not need to be
  // completed before we continue.
  assert(TLABPrefetchSize % BytesPerCacheLine == 0, "TLABPrefetchSize must be a multiple of cache line size");

  // Note that this assumes the CLZ pipeline is already initialized, and the TLAB is zeroed up to
  // obj_beg from a prior TLAB allocation pipeline zeroing TLABZeroRegion ahead.
  HeapWord *obj_beg = (HeapWord*)(round_down((uintptr_t(obj          ))+BytesPerCacheLine-1, BytesPerCacheLine)+TLABZeroRegion);
  HeapWord *obj_end = (HeapWord*)(round_down((uintptr_t(obj+word_size))                    , BytesPerCacheLine)               );
  HeapWord *clz_end = (HeapWord*)(round_down((uintptr_t(obj+word_size))+BytesPerCacheLine-1, BytesPerCacheLine)+TLABZeroRegion);

  assert(obj_end<=tlab_end, "Obj overruns TLAB!");

if(obj_end<obj_beg){
    obj_end = obj_beg;
  }
  if ( tlab_end < clz_end ) {
    clz_end = tlab_end;
  }

  // TODO: We're only ever zeroing memory that's aligned to a cacheline and a multiple of the cacheline size.
  //       There must be a more efficienct zero'ing algorithm than Copy::zero_to_words() in that scenario.

  // We assume that the clz pipeline is active at this point. There is no
  // need to worry about partial cache lines and lines before our first clz target,
  // clz's will already have been issued for them.

  // Always zero from obj_end to clz_end, so the next TLAB allocation can rely on an initial set of zeroed words.
  // This is done prior to the zeroing of the object, so the TLAB will be in a consistent state if incremental
  // zeroing contains a safepoint that tries to park this TLAB.
  size_t words = clz_end - obj_end;
  if ( words > 0 ) {
Copy::zero_to_words(obj_end,words);
  }

  // Only zero the object if we were asked to.
  if ( zero_mem ) {
    if ( word_size > IncrementalObjInitThresholdWords ) {
      obj = Universe::heap()->incremental_init_obj(obj, word_size);
    } else {
      size_t words = obj_end-obj_beg;

      if ( words > 0 ) {
        JavaThread* jt         = JavaThread::current();
jlong start_zero=os::elapsed_counter();

Copy::zero_to_words(obj_beg,words);

        jlong obj_zero_ticks = os::elapsed_counter() - start_zero;
        if ( obj_zero_ticks > jt->get_obj_zero_max_ticks() ) {
          jt->set_obj_zero_max_ticks(obj_zero_ticks, words);
        }
      }
    }
  }

  return obj;
}

HeapWord* ThreadLocalAllocBuffer::allocate(size_t size, bool zero_mem) {
  invariants();
  HeapWord* obj = top();

  // TLAB's have an "end", which may be artificially small to prevent size > MaxTLABObjectAllocationWords from
  // being allocated in the TLAB, and a "real end", which is the max a TLAB's end can be extended to, and a
  // "hard end", which is the actually for real truely honest end.  We may be in here with an allocation that
  // exceeds end but not real_end.
if(pointer_delta(real_end(),obj)>=size){
    // TODO: Some allocators, like SBA, allocate a chunk big enough for multiple objects, and it'd be nice
    //       to let them allocate chunks larger than the max single obj size.
    assert(size<=MaxTLABObjectAllocationWords, "Larger object can't allocate through TLABs without hurting GPGC.");

    // This addition is safe because we know that top is
    // at least size below real_end, so the add can't wrap.
    HeapWord* new_top  = obj + size;
    HeapWord* fake_end = new_top + MaxTLABObjectAllocationWords;

    if ( fake_end > real_end() ) {
      fake_end = real_end();
    }

    set_end(fake_end);  // We set this each slow path allocation to limit how often asm fast path alloc gets in here.
set_top(new_top);
    
    // Must do allocation clearing after set_top(), because incremental object initializiation may have safepoints
    // that try and park the TLAB, so it needs to be consistent before zeroing begins.
    obj = clear_tlab_for_allocation(obj, hard_end(), size, zero_mem);

    // We might be pointed at a different TLAB after clear_tlab_for_allocation() returns, but it should still
    // meet the invariants:
    invariants();

    return obj;
  }
  return NULL;
}

void ThreadLocalAllocBuffer::update_end_from_top(){
  invariants();

HeapWord*curr_top=top();
  HeapWord* fake_end = curr_top + MaxTLABObjectAllocationWords;

  if ( fake_end > real_end() ) {
    fake_end = real_end();
  }

set_end(fake_end);

  invariants();
}

void ThreadLocalAllocBuffer::record_slow_allocation(size_t obj_size) {
  // Raise size required to bypass TLAB next time. Why? Else there's
  // a risk that a thread that repeatedly allocates objects of one
  // size will get stuck on this slow path.

  set_refill_waste_limit(refill_waste_limit() + refill_waste_limit_increment());

  _slow_allocations++;

  if (PrintTLAB && Verbose) {
    Thread* thrd = myThread();
gclog_or_tty->print("TLAB: %s thread: "PTR_FORMAT" [id: %2d]"
                        " obj: "SIZE_FORMAT
                        " free: "SIZE_FORMAT
                        " waste: "SIZE_FORMAT"\n",
                        "slow", thrd, thrd->osthread()->thread_id(),
                        obj_size, free(), refill_waste_limit());
  }
}

#endif // THREADLOCALALLOCBUFFER_INLINE_HPP
