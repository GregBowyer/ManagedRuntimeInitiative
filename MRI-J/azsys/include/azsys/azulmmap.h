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

// azulmmap.h - Azul Virtual Memory Map.
//
//

#ifndef _AZULMMAP_H 
#define _AZULMMAP_H  1

#ifdef __cplusplus
extern "C" {
#endif


// Azul Virtual Memmory Allocation & Layout Defines.
//
//

// TODO -
// With the current map, we have only 1G of card mark space.
// We can support only 512G of parallel gc heap space with 1G of card table space.
// Also, the code cache is limited to 256m.


// To prevent malloc from using low virtual address space, we reserve 1G upfront
// when the process starts up and have malloc allocate in higher address ranges.
#define __VM_STRUCTURES_START_ADDR__                    0x40000000ULL  // 1G
#define __VM_STRUCTURES_END_ADDR__                      0x80000000ULL  // 2G

#define __GPGC_READ_BARRIER_ARRAY_START_ADDR__          0x5B000000ULL  // 1G 432M
#define __GPGC_READ_BARRIER_ARRAY_END_ADDR__            0x5C800000ULL  // 1G 456M
#define __GPGC_DUPE_READ_BARRIER_ARRAY_START_ADDR__     0x5B800000ULL  // 1G 440M
#define __GPGC_SWAP_READ_BARRIER_ARRAY_ADDR__           0x5C000000ULL  // 1G 448M

// TODO - Verify whether the oop table can be moved out to a higher address
#define __OOP_TABLE_START_ADDR__                   0x60000000ULL  // 1G 512M
#define __OOP_TABLE_END_ADDR__                     0x70000000ULL  // 1G 768M

// Generated code needs to reside below 2G because code offsets need to fit into
// signed 32bit offsets in instructions.
#define __CODE_CACHE_START_ADDR__                  0x70000000ULL  // 1G 768M
#define __CODE_CACHE_END_ADDR__                    0x80000000ULL  // 2G

// byte_map_base is below the actual card table start addr.
// byte_map_base for the card table needs to be below 2G.
#define __CARD_TABLE_MOD_REF_BS_START_ADDR__       0x80000000ULL  // 2G           Parallel GC only
#define __CARD_TABLE_MOD_REF_BS_END_ADDR__         0xc0000000ULL  // 3G           Parallel GC only

#define __JNI_BAD_MEMORY_START_ADDR__              0xc0000000ULL  // 3G
#define __JNI_BAD_MEMORY_END_ADDR__               0x100000000ULL  // 4G

// JNIHandleBlock addresses should not exceed 44 bits of VA
#define __JNI_HANDLE_BLOCK_START_ADDR__           0x100000000ULL  // 4G
#define __JNI_HANDLE_BLOCK_END_ADDR__             0x180000000ULL  // 6G

#define __SBA_AREA_START_ADDR__                   0x180000000ULL  // 6G
#define __SBA_AREA_END_ADDR__                     0x400000000ULL  // 16G

#define __PARALLEL_SCAVENGE_HEAP_START_ADDR__     0x400000000ULL  // 16G          Parallel GC only
#define __PARALLEL_SCAVENGE_HEAP_END_ADDR__     0x40000000000ULL  // 4T           Parallel GC only

#define __GPGC_HEAP_START_ADDR__                  0x400000000ULL  // 16G          GPGC only
#define __GPGC_HEAP_END_ADDR__                  0x40000000000ULL  // 4T           GPGC only

// ObjectStartArray space sizes match the card table space size
#define __OBJECT_START_ARRAY_OLD_START_ADDR__   0x40000000000ULL  // 4T           Parallel GC only
#define __OBJECT_START_ARRAY_OLD_END_ADDR__     0x40040000000ULL  // 4T 1G        Parallel GC only

#define __OBJECT_START_ARRAY_PERM_START_ADDR__  0x40040000000ULL  // 4T 1G        Parallel GC only
#define __OBJECT_START_ARRAY_PERM_END_ADDR__    0x40080000000ULL  // 4T 2G        Parallel GC only

#define __PARALLEL_COMPACT_START_ADDR__         0x44080000000ULL  // 4T 256G      Parallel GC only
#define __PARALLEL_COMPACT_END_ADDR__           0x48080000000ULL  // 4T 512G      Parallel GC only

#define __THREAD_STACK_REGION_START_ADDR__      0x400000000000ULL // 64T
#define __THREAD_STACK_REGION_END_ADDR__        0x500000000000ULL // 80T

#define __NIO_START_ADDR__                      0x500000000000ULL // 80T
#define __NIO_END_ADDR__                        0x510000000000ULL // 81T

#define __PAGECACHE_START_ADDR__                0x510000000000ULL // 81T
#define __PAGECACHE_END_ADDR__                  0x520000000000ULL // 82T

#define __TRANSPORT_BUFF_START_ADDR__           0x520000000000ULL // 82T
#define __TRANSPORT_BUFF_END_ADDR__             0x530000000000ULL // 83T

#define __ITR_START_ADDR__                      0x530000000000ULL // 83T
#define __ITR_END_ADDR__                        0x534000000000ULL // 83T 256G

#define __GPGC_HEAP_STRUCTURES_START_ADDR__     0x540000000000ULL // 84T          GPGC only
#define __GPGC_HEAP_STRUCTURES_END_ADDR__       0x560000000000ULL // 86T          GPGC only

#define __MORECORE_START_ADDR__                 0x560000000000ULL // 86T
#define __MORECORE_END_ADDR__                   0x580000000000ULL // 88T

#define __GPGC_HEAP_MIRROR_START_ADDR__         0x580000000000ULL // 88T          GPGC only
#define __GPGC_HEAP_MIRROR_END_ADDR__           0x5c0000000000ULL // 92T          GPGC only

// TODO: Make this value be derived from the above invariants.
// This is a hack'n'slash for now, but if the above values change
// or the predicates in azulmmap.h change, we will need to reflect
// that in this value.  1<<46 = 0x400,0000,0000 = 64Terabytes.
#define AZUL_THREAD_STACKS_BASE_SHIFT  (46)


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif  // _AZULMMAP_H
