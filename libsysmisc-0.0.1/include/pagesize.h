// Copyright 2009 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// pagesize.h - Virtual Memory constants for x86
//
#ifndef _OS_PAGESIZE_H_
#define _OS_PAGESIZE_H_ 1

#ifdef __cplusplus
extern "C" {
#endif


#define VM_SMALL_PAGE_SHIFT  12
#define VM_LARGE_PAGE_SHIFT  21

#define VM_SMALL_PAGE_SIZE   (1L<<VM_SMALL_PAGE_SHIFT)     // X86 small page size if 4KB.
#define VM_LARGE_PAGE_SIZE   (1L<<VM_LARGE_PAGE_SHIFT)     // X86 large page size is 2MB.

#define VM_SMALL_PAGE_OFFSET (VM_SMALL_PAGE_SIZE - 1)
#define VM_SMALL_PAGE_MASK   (~VM_SMALL_PAGE_OFFSET)

#define VM_LARGE_PAGE_OFFSET (VM_LARGE_PAGE_SIZE - 1)
#define VM_LARGE_PAGE_MASK   (~VM_LARGE_PAGE_OFFSET)


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_SIZES_H_
