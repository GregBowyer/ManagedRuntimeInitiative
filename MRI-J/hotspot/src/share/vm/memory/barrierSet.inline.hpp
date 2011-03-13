/*
 * Copyright 2001-2002 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef BARRIERSET_INLINE_HPP
#define BARRIERSET_INLINE_HPP


#include "cardTableModRefBS.hpp"
#include "gpgc_cardTable.hpp"

// Inline functions of BarrierSet, which de-virtualize certain
// performance-critical calls when when the barrier is the most common
// card-table kind.

void BarrierSet::write_ref_field(objectRef* field, objectRef new_val) {
if(kind()==GenPauselessBarrier){
((GPGC_CardTable*)this)->inline_write_ref_field(field,new_val);
  }
  else if (kind() == CardTableModRef) {
    ((CardTableModRefBS*)this)->inline_write_ref_field(field, new_val);
  }
  else if (kind() != NoBarriers) {
    write_ref_field_work(field, new_val);
  }
}

void BarrierSet::write_ref_array(MemRegion mr) {
if(kind()==GenPauselessBarrier){
((GPGC_CardTable*)this)->inline_write_ref_array(mr);
  }
  else if (kind() == CardTableModRef) {
    ((CardTableModRefBS*)this)->inline_write_ref_array(mr);
  }
  else if (kind() != NoBarriers) {
    write_ref_array_work(mr);
  }
}

void BarrierSet::write_region(MemRegion mr) {
if(kind()==GenPauselessBarrier){
((GPGC_CardTable*)this)->inline_write_region(mr);
  }
  else if (kind() == CardTableModRef) {
    ((CardTableModRefBS*)this)->inline_write_region(mr);
  }
  else if (kind() != NoBarriers) {
    write_region_work(mr);
  }
}

#endif // BARRIERSET_INLINE_HPP

