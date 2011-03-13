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
#ifndef GPGC_MARKITERATOR_HPP
#define GPGC_MARKITERATOR_HPP


#include "allocation.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_relocation.hpp"

#include "gpgc_relocation.inline.hpp"


class GPGC_MarkIterator: public AllStatic
{
  private:
    template <class T>
    static void iterate_live_obj_marks   (PageNum page, bool small_space, T* mark_closure);

  public:
    static void init_live_obj_relocation (PageNum page);
    static void remap_live_objs          (PageNum page, GPGC_RemapBuffer* remap_buffer, bool mark_copy, int64_t stripe);
};


#endif // GPGC_MARKITERATOR_HPP
