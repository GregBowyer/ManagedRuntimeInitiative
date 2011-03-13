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
#ifndef GPGC_MARKINGQUEUE_HPP
#define GPGC_MARKINGQUEUE_HPP


//  This class manages the queue of references awaiting marking by the collector.
//  Set the NMT bit on the references enqueued here is the responsibility of the
//  enqueuer.  Race conditions allow the safe reference to be enqueued multiple
//  times.
//


// GPGC_MarkingQueue is actually a stack.
class GPGC_MarkingQueue:public CHeapObj{
  public:
    enum Constants {
      RefBlockLength = 256
    };

    // The GPGC_MarkingQueue contains a linked list of RefBlocks holding
    // the enqueued objectRefs.
class RefBlock:public CHeapObj{
      public:
        objectRef _refs[RefBlockLength];
RefBlock*_next;

        RefBlock() { _next = NULL; }
    };

  private:
    enum {
      BlockBits    = 48,
      TagBits      = 16
    };
    enum {
      BlockShift   = 0,
      TagShift     = BlockShift + BlockBits
    };
    enum {
      BlockMask    = right_n_bits(BlockBits) << BlockShift,
      TagMask      = right_n_bits(TagBits)   << TagShift,
      TagIncrement = 0x1L << TagShift
    };
    
    static          intptr_t  _blocks_requested;
    static          intptr_t  _blocks_allocated;

    static const    long      _max_free_lists = 8;
    static volatile intptr_t  _free_lists[_max_free_lists];

    static inline   uintptr_t extract_tag  (intptr_t stack_top) { return stack_top & TagMask; }
    static inline   RefBlock* extract_block(intptr_t stack_top) { return (RefBlock*) (stack_top & BlockMask); }

    static          RefBlock* pop_internal (volatile intptr_t* stack_top);
    static          void      push_internal(volatile intptr_t* stack_top, RefBlock* block);

  public:
    static          RefBlock* new_block    ();
    static          void      delete_block (RefBlock* block);

    static          long      blocks_requested()   { return (long) _blocks_requested; }
    static          long      blocks_allocated()   { return (long) _blocks_allocated; }
    static          long      count_free_blocks();

  private:
    // _stack_top is really a RefBlock*, but we CAS with a tag, so we're declaring it like this.
    volatile intptr_t _stack_top;

  public:

    GPGC_MarkingQueue();

    bool      is_empty      ()    { return ((_stack_top & BlockMask) == 0); }

    void      enqueue_block (RefBlock* block);
    void      enqueue_array (heapRef* array, uint length);

    RefBlock* dequeue_block ();
};

#endif // GPGC_MARKINGQUEUE_HPP
