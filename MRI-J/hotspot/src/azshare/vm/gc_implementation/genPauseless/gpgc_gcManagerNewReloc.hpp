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
#ifndef GPGC_GCMANAGERNEWRELOC_HPP
#define GPGC_GCMANAGERNEWRELOC_HPP



#include "gpgc_gcManager.hpp"


class GPGC_RelocBuffer;
class GPGC_RemapBuffer;


class GPGC_GCManagerNewReloc: public GPGC_GCManager {
  private:
static GPGC_GCManagerNewReloc**_manager_array;
    static long                     _manager_count;


  public:
    static void                    initialize();
    static GPGC_GCManagerNewReloc* get_manager(long manager)  { assert0(manager>=0 && manager<_manager_count);
                                                                return _manager_array[manager];                }

    // For small page relocation:
    static void                    set_generations();
    static void                    total_page_counts(long* new_pages, long* old_pages);

    // For mid page object remapping:
    static void                    reset_remap_buffers();


  private:
    GPGC_RelocBuffer* _new_relocation_buffer;
    GPGC_RelocBuffer* _old_relocation_buffer;

    GPGC_RemapBuffer* _new_remap_buffer;
    GPGC_RemapBuffer* _old_remap_buffer;

    GPGC_GCManagerNewReloc(long manager_number);


  public:
    GPGC_RelocBuffer* new_relocation_buffer()  { return _new_relocation_buffer; }
    GPGC_RelocBuffer* old_relocation_buffer()  { return _old_relocation_buffer; }

    GPGC_RemapBuffer* new_remap_buffer()       { return _new_remap_buffer;      }
    GPGC_RemapBuffer* old_remap_buffer()       { return _old_remap_buffer;      }
};

#endif // GPGC_GCMANAGERNEWRELOC_HPP

