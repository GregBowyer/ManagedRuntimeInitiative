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
#ifndef GPGC_GCMANAGEROLDRELOC_HPP
#define GPGC_GCMANAGEROLDRELOC_HPP


#include "gpgc_gcManager.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_relocation.hpp"

class GPGC_RelocBuffer;
class GPGC_GCManagerOldReloc: public GPGC_GCManager {
  private:
static GPGC_GCManagerOldReloc**_manager_array;
    static long                     _manager_count;


  public:
    static void                    initialize();
    static GPGC_GCManagerOldReloc* get_manager(long manager)  { assert0(manager>=0 && manager<_manager_count);
                                                                return _manager_array[manager];                }

    // For small page relocation:
    static void                    set_generations(GPGC_PageInfo::Gens gen);
    static long                    total_page_count();

    // For mid page object remapping:
    static void                    reset_remap_buffer(GPGC_PageInfo::Gens gen);

  private:
    GPGC_RelocBuffer* _relocation_buffer;
    GPGC_RemapBuffer* _remap_buffer;

    GPGC_GCManagerOldReloc(long manager_number);


  public:
    GPGC_RelocBuffer* relocation_buffer()  { return _relocation_buffer; }
    GPGC_RemapBuffer* remap_buffer()       { return _remap_buffer; }
};

#endif // GPGC_GCMANAGEROLDRELOC_HPP

