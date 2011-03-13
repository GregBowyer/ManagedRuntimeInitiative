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

#include "frame.hpp"
#include "stubRoutines.hpp"
#include "tickProfiler.hpp"

#include "frame_pd.inline.hpp"
#include "frame.inline.hpp"

void ProfileEntry::set_rpcs( intptr_t pc, intptr_t sp, intptr_t fp ) {
  int i;
for(i=0;i<_rpc_count;i++){
    if( !StubRoutines::returns_to_call_stub((address)pc) &&
        CodeCache::contains((address)pc) ) {
      frame fr((intptr_t*)sp,(address)pc);
      frame fr2 = fr.pd_sender();
      _rpcs[i] = pc = (intptr_t)fr2.pc();
      sp = (intptr_t)fr2.sp();
      // fp is only needed for C frames, once we start crawling JIT'd
      // frames we will not handle crawling back into C frames
      fp = 0;
    } else {
      // This test checks if the fp is in the same stack region as the sp.
      if( (sp & ~(BytesPerThreadStack-1)) != (fp & ~(BytesPerThreadStack-1)) ||
          // Valid FPs are >= the SP.
          fp < sp ) 
        break;
      intptr_t *nsp = (intptr_t*)fp;
      fp = *nsp++; // 'pop rbp'
      _rpcs[i] = pc = *nsp++; // 'ret' or 'pop return_address'
      sp = (intptr_t)nsp;
    }
  }
for(;i<_rpc_count;i++)
    _rpcs[i] = 0;               // We lost track of the crawl
}
