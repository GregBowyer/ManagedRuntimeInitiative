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

#ifndef GPGC_TLB_HPP
#define GPGC_TLB_HPP

#include "allocation.hpp"
#include "heapRef_pd.hpp"
#include "gpgc_layout.hpp"

class TimeDetailTracer;
// depreciated GPGC_TLB code, this will eventually go away. The code now resides in GPGC_ReadTrapArray
class GPGC_TLB:public AllStatic{

  public:
    static void    tlb_resync               (TimeDetailTracer* tdt, const char* gc_tag);
    static heapRef lvb_trap_from_c          (Thread* thread, heapRef old_ref, heapRef* va);
    static heapRef lvb_trap_from_asm        (Thread* thread, heapRef old_ref, heapRef* va);
    static heapRef gc_thread_lvb_trap_from_c(Thread* thread, heapRef old_ref, heapRef* va);

};
#endif // GPGC_TLB_HPP
