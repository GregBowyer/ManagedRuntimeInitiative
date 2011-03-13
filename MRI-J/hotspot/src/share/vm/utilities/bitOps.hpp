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

#ifndef BITOPS_HPP 
#define BITOPS_HPP 

#include "allocation.hpp"

class BitOps: public AllStatic
{
  public:

    // can use the lzcnt instruction instead.. 
    // not sure for the tall model
    // use the gcc-builtin
    inline static long first_bit(uintptr_t word)
    {
    #if (AZ_X86)
      long bit = -1;
      if ( word != 0 ) {
        bit = 63 - __builtin_clzll(word);
      }
      return bit;
    #else
    #error Unknown hardware so unknown layout
    #endif
    }

    inline static long first_bit(uint32_t word)
    {
    #if (AZ_X86)
      long bit = -1;
      if ( word != 0 ) {
        bit = 31 - __builtin_clz(word);
      }
      return bit;
    #else
    #error Unknown hardware so unknown layout
    #endif
    }

    // can use the popcnt instruction instead.. 
    // not sure for the tall model
    // use the gcc-builtin.. expect gcc to do the right thing based on the platform???
    inline static long number_of_ones(uintptr_t word)
    {
    #if (AZ_X86)
      return __builtin_popcountll(word);
    #else
    #error Unknown hardware so unknown layout
    #endif
    }
    inline static long number_of_ones(uint32_t word)
    {
    #if (AZ_X86)
      return __builtin_popcount(word);
    #else
    #error Unknown hardware so unknown layout
    #endif
    }

    inline static long bit_count(uintptr_t word)
    {
      long count = 0;
      long bit;

      while ((bit = first_bit(word)) > -1) {
        word ^= 1UL << bit;
        count ++;
      }

      return count;
    }
};

#endif // BITOPS_HPP 
