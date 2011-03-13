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
#ifndef GPGC_GCMANAGERNEWSTRONG_INLINE2_HPP
#define GPGC_GCMANAGERNEWSTRONG_INLINE2_HPP



#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"


inline void GPGC_GCManagerNewStrong::push_ref_to_nto_stack(objectRef ref, int referrer_kid)
{
  assert(GPGC_NewCollector::mark_old_space_roots(), "Only find NTO roots in a NewToOld cycle");
  assert(ref.is_old() && GPGC_NMT::has_desired_old_nmt(ref), "Invalid ref pushing to NTO stack");

  GPGC_GCManagerMark::push_ref_to_stack(ref, referrer_kid,
                                        &_new_to_old_marking_stack,
                                        GPGC_GCManagerOldStrong::full_ref_buffer_list(),
                                        GPGC_GCManagerOldStrong::free_ref_buffer_list());
}

#endif // GPGC_GCMANAGERNEWSTRONG_INLINE2_HPP
