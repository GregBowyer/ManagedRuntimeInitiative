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
#ifndef GLOBALDEFINITIONS_PD_HPP
#define GLOBALDEFINITIONS_PD_HPP

#include "globalDefinitions.hpp"

#define Nehalem (1)

// Make sure that the non_oop_word cannot look like a legal oop, by setting a low bit.
// This bit must be aligned to 8, so that objectRef translation works.
#define NON_OOP_MARK 8

//
////
// Define types here.

typedef short        int16;
typedef int64_t      int64;
typedef uint64_t     uint64;

typedef int64_t      reg_t;
typedef uint32_t instr_t;

const int LogBytesPerCacheLine = 6; // 64b lines on Nehalem
const int LogWordsPerCacheLine = LogBytesPerCacheLine - LogBytesPerWord;

const int BytesPerCacheLine    = 1 << LogBytesPerCacheLine;
const int WordsPerCacheLine    = 1 << LogWordsPerCacheLine;

#define CacheMissLatency 250

const int StackAlignmentInBytes = 16;

#define int8_size    1
#define int16_size   2
#define int32_size   4
#define int64_size   8
#define ptr_size     8
#define ref_size     8
#define log_ptr_size 3
#define log_ref_size 3

#define reg_size     8
#define log_reg_size 3


// The type for an index of page, which is:  (addr >> LogBytesPerPage)
typedef int64 PageNum;
const PageNum NoPage  = 1;   // Invalid PageNum.


// Bit sizes
#define instr_bitsize  instr_size * BitsPerByte
#define ref_bitsize    ref_size * BitsPerByte


inline bool is_uint5(int64 value) {
  return (((uint64)value << 59) >> 59) == (uint64)value;
}
inline bool is_uint6(int64 value) {
  return (((uint64)value << 58) >> 58) == (uint64)value;
}

// Make up for missing __builtin_clzll
inline unsigned count_leading_zeroes(uint64_t value) {
  if (!value) return 64;
  unsigned result = 0;

  for (unsigned bits = 64 >> 1; bits; bits >>= 1) {
    uint64_t tmp = value >> bits;
    if (tmp) {
value=tmp;
    } else {
result|=bits;
    }
  }
  
  return result;
}
inline unsigned count_trailing_zeroes(uint64_t value) {
  return 64 - count_leading_zeroes(~value & (value - 1));
}

// Mask detection
inline bool is_low_mask(uint64_t value) {
  return value && ((value + 1) & value) == 0;
}
inline bool is_mask(uint64_t value) {
  return value && is_low_mask((value - 1) | value);
}
inline unsigned mask_shift(uint64_t value) {
return count_trailing_zeroes(value);
}
inline unsigned mask_size(uint64_t value) {
  return 64 - count_leading_zeroes(value >> mask_shift(value));
}

#endif // GLOBALDEFINITIONS_PD_HPP
