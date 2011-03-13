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

// This is a kludge.  The header file for this should come from azsys,
// But I don't want to stir the pot too much now.
//
// The header file is actually coming from libsysmisc.  Probably both
// the header file and the C file should be in libaznix.
#include <os/memory.h>

// Callback registration for memory_allocate/deallocate hooks used for leak
// detection.
memory_allocate_self_hook_t _memory_allocate_self_hook = NULL;
memory_deallocate_self_hook_t _memory_deallocate_self_hook = NULL;
memory_release_physical_self_hook_t _memory_release_physical_self_hook = NULL;
memory_relocate_self_hook_t _memory_relocate_self_hook = NULL;

void
set_memory_allocate_self_hook(memory_allocate_self_hook_t f)
{
    _memory_allocate_self_hook = f;
}

void
set_memory_deallocate_self_hook(memory_deallocate_self_hook_t f)
{
    _memory_deallocate_self_hook = f;
}

void
set_memory_relocate_self_hook(memory_relocate_self_hook_t f)
{
    _memory_relocate_self_hook = f;
}

void
set_memory_release_physical_self_hook(memory_release_physical_self_hook_t f)
{
    _memory_release_physical_self_hook = f;
}
