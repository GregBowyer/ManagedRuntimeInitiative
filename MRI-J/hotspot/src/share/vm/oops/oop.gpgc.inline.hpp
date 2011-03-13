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
#ifndef OOP_GPGC_INLINE_HPP
#define OOP_GPGC_INLINE_HPP



#include "gpgc_newCollector.inline.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "klass.hpp"
#include "oop.hpp"
#include "oop.inline.hpp"


// GenPauselessGC methods


inline void oopDesc::GPGC_follow_contents(GPGC_GCManagerNewStrong* gcm)
{
  GPGC_NewCollector::mark_and_push_klass(gcm, klass_addr());
  blueprint()->GPGC_oop_follow_contents(gcm, this);
}


inline void oopDesc::GPGC_follow_contents(GPGC_GCManagerOldStrong* gcm)
{
  GPGC_OldCollector::mark_and_push(gcm, klass_addr(), mark()->kid());
  blueprint()->GPGC_oop_follow_contents(gcm, this);
}


inline void oopDesc::GPGC_follow_contents(GPGC_GCManagerNewFinal* gcm)
{
  blueprint()->GPGC_oop_follow_contents(gcm, this);
}


inline void oopDesc::GPGC_follow_contents(GPGC_GCManagerOldFinal* gcm)
{
  GPGC_OldCollector::mark_and_push(gcm, klass_addr());
  blueprint()->GPGC_oop_follow_contents(gcm, this);
}


inline void oopDesc::GPGC_newgc_update_cardmark()
{
blueprint()->GPGC_newgc_oop_update_cardmark(this);
}


inline void oopDesc::GPGC_oldgc_update_cardmark()
{
blueprint()->GPGC_oldgc_oop_update_cardmark(this);
}


inline void oopDesc::GPGC_mutator_update_cardmark()
{
blueprint()->GPGC_mutator_oop_update_cardmark(this);
}

#endif // OOP_GPGC_INLINE_HPP
