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
#ifndef OS_OS_PD_HPP
#define OS_OS_PD_HPP


//
// NOTE: we are back in class os here
//
static void setup_fpu() {}

/*
 * Returns a unique Id to distinguish each core.
 */
static inline uint32_t current_cpu_id() {
uint32_t core_id=0;
  uint32_t level=0x01;;

  uint32_t _eax, _ebx, _ecx, _edx;

__asm__ volatile("cpuid"
                     : /* Outputs */
                      "=a" (_eax),
                      "=b" (_ebx),
                      "=c" (_ecx),
                      "=d" (_edx)
                     : /* Inputs */
                      "a" (level) );

  core_id = ( _ebx >> 24 ) & 0xff;
  return core_id;
}

#endif // OS_OS_PD_HPP
