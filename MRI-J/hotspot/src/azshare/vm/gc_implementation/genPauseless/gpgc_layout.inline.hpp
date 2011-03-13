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


#ifndef GPGC_LAYOUT_INLINE_HPP
#define GPGC_LAYOUT_INLINE_HPP

#include "gpgc_layout.hpp"
#include "objectRef_pd.hpp"


inline bool GPGC_Layout::is_shattered_address(intptr_t addr) {
  intptr_t stripped_addr = addr & objectRef::unknown_mask_in_place;
  return ( stripped_addr >= (intptr_t(start_of_mid_space)<<LogBytesPerGPGCPage) &&
           stripped_addr <  (intptr_t(  end_of_mid_space)<<LogBytesPerGPGCPage) );
}


#endif // GPGC_LAYOUT_INLINE_HPP
