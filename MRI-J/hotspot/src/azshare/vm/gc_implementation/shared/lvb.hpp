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
#ifndef LVB_HPP
#define LVB_HPP



#include "objectRef_pd.hpp"



// This class provides a Load-Value-Barrier for the C++ code that loads
// objectRefs, as well as support for the assembly LVB traps.  The code is
// generic, as it's used by both PauselessGC and GenPauselessGC.

#define lvb_ref(addr) LVB::lvb(addr)
#define lvb_loadedref(ref,addr) LVB::lvb_loaded(ref, addr)

class LVB:public AllStatic{
  public:
    inline static objectRef lvb                   (objectRef* addr);
    inline static objectRef lvb                   (objectRef volatile * addr)       { return lvb((objectRef*)addr); }
    inline static objectRef lvb                   (objectRef const * addr)          { return lvb((objectRef*)addr); }
    inline static objectRef lvb                   (objectRef const volatile * addr) { return lvb((objectRef*)addr); }

    inline static objectRef lvb_loaded            (objectRef ref, const objectRef* addr);

           static objectRef mutator_relocate_only (objectRef * addr);
           static objectRef mutator_relocate_only (const objectRef * addr)  { return mutator_relocate_only((objectRef*)addr); }

static objectRef mutator_relocate(objectRef ref);

           static objectRef lvb_or_relocated      (objectRef* addr);

           static void      lvb_clone_objectRefs  (oop from, oop to);

           static bool      verify_jvm_lock       ();


#ifdef ASSERT
    // For debugging only:
    static void permissive_poison_lvb (objectRef* addr);
#endif
};


#endif // LVB_HPP

