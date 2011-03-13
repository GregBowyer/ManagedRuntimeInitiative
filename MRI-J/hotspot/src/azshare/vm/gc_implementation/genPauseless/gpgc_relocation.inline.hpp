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
#ifndef _GPGC_RELOCATION_INLINE_HPP
#define _GPGC_RELOCATION_INLINE_HPP



inline long GPGC_PageRelocation::insert(GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key) {
  long  size       = info->reloc_len();
  jlong loop_count = 0;
  long  index      = key & (size - 1);

  while (table[index].get_record() !=  0) {
    if (++index == size) index = 0;
    loop_count++;
    guarantee(loop_count < size, "should have found an empty location in the side-band hash");
  }
  assert0((index < size) && (index >= 0));
  return index;
}


inline long GPGC_PageRelocation::find(GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key) {
  long  size       = info->reloc_len();
  jlong loop_count = 0;
  long  index      = key & (size - 1);

  while (table[index].old_oop_offset_words() != key) {
    if (++index == size) index = 0;
    loop_count++;
    guarantee(loop_count < size, "should have found the relocation record");
  }
  return index;
}


inline long GPGC_PageRelocation::find_interior_ptr(GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key) {
  long size              = info->reloc_len();
  long index             = key & (size - 1);
  long oop_idx           = -1;
  long prev_offset_words = -1;
  long loop_count        = 0;

  while (loop_count <= size) {
    long old_oop_offset_words = table[index].old_oop_offset_words();
    if ( old_oop_offset_words == key ) { return index; }
    if ((old_oop_offset_words > prev_offset_words) && (old_oop_offset_words < key) ) {
oop_idx=index;
      prev_offset_words = old_oop_offset_words;
    }
    loop_count++;
    if (++index == size) index = 0;
  }
  return oop_idx;
}


inline GPGC_ObjectRelocation* GPGC_PageRelocation::find_object_generic(GPGC_PageInfo* info, long offset_words) {
  GPGC_ObjectRelocation* relocations = info->relocations();
  long                   idx         = find(relocations, info, offset_words);
  return &relocations[idx];
}


inline void GPGC_PageRelocation::init_object_hash(GPGC_PageInfo* info, oop obj, long offset_words)
{
  GPGC_ObjectRelocation* relocations = info->relocations();
  long                   index       = insert(relocations, info, offset_words);

  assert0(index < info->reloc_len());

  relocations[index].init_record_generic(offset_words);
}


#endif // _GPGC_RELOCATION_INLINE_HPP
