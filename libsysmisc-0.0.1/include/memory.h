// Copyright 2003-2004 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// memory.h - Virtual Memory Subsystem for Aztek OS Services
//
#ifndef _OS_MEMORY_H_
#define _OS_MEMORY_H_ 1

#include <aznix/az_memory.h>
#include <os/pagesize.h>
#include <os/types.h>

// 4.	Virtual Memory Subsystem
// 
// A process has linear address space starting at VM_USER_ADDRESS_MIN and ending
// at VM_USER_ADDRESS_MAX. The virtual address VM_USER_ADDRESS_MAX is not part
// of the address space.
//
// A range of virtual address space can be reserved by making
// a call to memory_address_space_reserve() and can be unreserved by
// making a call to memory_address_space_unreserve(). The virtual
// address space reservation does not imply allocation of physical
// pages. However, the address space reservation is required for
// subsequent allocation of physical pages.
// 
// Once the address space is reserved, physical pages can be allocated
// and freed using one of the following 3 ways:
// 
// -	memory_allocate() / memory_deallocate()
// -	memory_allocate_rstack() / memory_deallocate_rstack()
// 
// After successful validation of the physical page quota, these
// calls guarantee availability of the physical pages when needed
// for use. Neither of these calls guarantees protection.
// memory_protect() can be used to change the access protection.
// 
// The allocation and deallocation APIs are not interchangeable.
// For example, memory allocated using memory_allocate() must be
// freed using memory_deallocate(). An attempt to use
// memory_deallocate_rstack() to free memory allocated using
// memory_allocate() will result in SYSERR_INVALID_OPERATION error
// return.


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROCESS_ACCOUNT_DEFAULT = 0,
    PROCESS_ACCOUNT_EMERGENCY_GC = 1,
    PROCESS_ACCOUNT_HEAP = 2,
    PROCESS_ACCOUNT_PAUSE_PREVENTION = 3
} process_account_num_t;

extern const char *
process_get_account_name(process_account_num_t account_num);

#define	ROUND_SMALL_PAGE(x)	(((uint64_t)(x) + VM_SMALL_PAGE_OFFSET) & VM_SMALL_PAGE_MASK)
#define	TRUNC_SMALL_PAGE(x)	((uint64_t)(x) & VM_SMALL_PAGE_MASK)

// Backwards compatibility
#define VM_PAGE_SIZE    VM_SMALL_PAGE_SIZE
#define	VM_PAGE_SHIFT	VM_SMALL_PAGE_SHIFT
#define	VM_PAGE_OFFSET	VM_SMALL_PAGE_OFFSET
#define	VM_PAGE_MASK	VM_SMALL_PAGE_MASK

#define	ROUND_PAGE(x)	(((uint64_t)(x) + VM_PAGE_OFFSET) & VM_PAGE_MASK)
#define	TRUNC_PAGE(x)	((uint64_t)(x) & VM_PAGE_MASK)

#define	ROUND_LARGE_PAGE(x)	(((uint64_t)(x) + VM_LARGE_PAGE_OFFSET) & VM_LARGE_PAGE_MASK)
#define	TRUNC_LARGE_PAGE(x)	((uint64_t)(x) & VM_LARGE_PAGE_MASK)

#define VM_USER_ADDRESS_MAX ((address_t)0xfc000000000UL)
#define VM_USER_ADDRESS_MIN ((address_t)0x0UL)

#define VM_USER_STACK_REGION_START ((address_t)0x80000000000UL)
#define VM_USER_STACK_REGION_SIZE  ((size_t)VM_USER_ADDRESS_MAX - (size_t)VM_USER_STACK_REGION_START)


/*
 * ==================================================================
 * Old aztek Memory system calls (for backward compatibility reasons)
 * ==================================================================
 */

// memory_address_space_reserve()
extern sys_return_t memory_address_space_reserve(process_t  _target,
						 uint64_t   _flags,
						 size_t     _size,
						 address_t *_address);
extern sys_return_t memory_address_space_reserve_self(uint64_t   _flags,
						      size_t     _size,
						      address_t *_address);

#define	MEMORY_RESERVE_NOFLAGS				0x0000
#define	MEMORY_RESERVE_ALLOCATE_REQUIRES_ADDRESS	0x0001
#define MEMORY_RESERVE_ALLOW_READ                       0x0002
#define MEMORY_RESERVE_ALLOW_WRITE                      0x0004
#define MEMORY_RESERVE_DEMAND_SMALL                     0x0010
#define MEMORY_RESERVE_DEMAND_LARGE                     0x0020
#define MEMORY_RESERVE_DEMAND_REQUEST_NOZERO            0x0040
#define	MEMORY_RESERVE_REQUIRED_ADDRESS			0x0100
#define	MEMORY_RESERVE_DISALLOW_ALLOCATIONS		0x1000

// Reserve a range of virtual address space. No physical
// pages are allocated.
//
// On success, start virtual address of the range is returned
// in "address". On success, the size of the reserved
// block will be the smallest multiple of VM_LARGE_PAGE_SIZE
// greater than or equal to the size requested. On success,
// the current and maximum protection on this range would
// be set to MEMORY_PROTECT_NONE.
//
// If MEMORY_RESERVE_REQUIRED_ADDRESS is set in the
// "flags", the call will fail with SYSERR_TRY_AGAIN
// if the block can not be reserved at value passed in
// "address". One of the possible reasons for failure in
// this case is if the "address" was not VM_LARGE_PAGE_SIZE aligned.
//
// If MEMORY_RESERVE_ALLOCATES_NEED_ADDRESS is set, the
// reserved range is marked to make allocation to skip over
// this range when auto selecting an address. For example,
// memory_allocate() will search in the reserved range if and
// only if it is called with MEMORY_ALLOCATE_REQUIRED_ADDRESS
// set.
//
// If MEMORY_RESERVE_DISALLOW_ALLOCATIONS is set, the
// reserved range is marked to make allocation to skip over
// this range when auto selecting an address. It makes the
// allocations fail if MEMORY_RESERVE_REQUIRED_ADDRESS was
// also set. This is needed for creation of guard address
// space ranges.  For example, page 0 can be protected this
// way to catch NULL pointers.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: Invalid "flags" value or 0 size requested.
// SYSERR_INVALID_ADDRESS: "address" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "address" is on a read/write
//				protected page.
// SYSERR_TRY_AGAIN: address and alignment constraints cannot
//		     be met.



// memory_address_space_unreserve()
extern sys_return_t memory_address_space_unreserve(process_t _target,
						   size_t    _size,
						   address_t _address);
extern sys_return_t memory_address_space_unreserve_self(size_t    _size,
							address_t _address);

// Unreserve a range of virtual address space. Address value passed in
// "address" is request for the start address of the block of memory
// and the size contains requested size.
// 
// memory_address_space_unreserve() will fail if there are
// allocations in the range. It is the responsibility of the caller
// to make sure all the allocations in the range are deallocated
// before calling memory_address_space_unreserve().
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: 0 size requested or the "size" is not a
// 			multiple of VM_LARGE_PAGE_SIZE.
// SYSERR_INVALID_ADDRESS: "address" is not VM_LARGE_PAGE_SIZE aligned.
// SYSERR_INVALID_OPERATION: failure to "reserve" the virtual address
// 			space prior to this call.
// SYSERR_INVALID_STATE: failure to deallocate the allocations in the
// 			range prior to this call.


// memory_allocate()
extern sys_return_t memory_allocate(process_t  _target,
				    uint64_t   _flags,
				    size_t     _size,
				    address_t *_address);

#define	MEMORY_ALLOCATE_NOFLAGS			0x0000
#define	MEMORY_ALLOCATE_REQUIRED_ADDRESS	0x0100
#define	MEMORY_ALLOCATE_HEAP			0x0200
#define	MEMORY_ALLOCATE_REQUEST_NOZERO		0x0400
#define	MEMORY_ALLOCATE_HAS_SEMAPHORE		0x0800
#define	MEMORY_ALLOCATE_NO_OVERDRAFT		0x1000


// Allocate physical pages in a "reserved" range of virtual memory
// address space in the target process. Address value passed in "address"
// is request for the start virtual address of the block of memory and
// the size contains requested size. If the caller does not have any
// preference for the start virtual address, it is recommended to
// initialize the "address" to start address of the address space
// range the caller wants allocation to be in.
// 
// Successful return from this call guarantee availability of the
// physical pages. The physical pages allocated are charged against the
// physical page quota.
// 
// On success, start virtual address of the allocated memory is returned
// in "address".
// 
// On success, the size of the allocated block will be the smallest
// multiple of VM_PAGE_SIZE greater than or equal to the size requested
// and the current and maximum protection on this range would be set to
// (MEMORY_PROTECT_READ|MEMORY_PROTECT_WRITE).
// 
// If MEMORY_ALLOCATE_REQUIRED_ADDRESS is set in the "flags", the call
// will fail with SYSERR_TRY_AGAIN if the block can not be allocated at
// value passed in "address". One of the possible reasons for failure in
// this case is if the "address" was not VM_PAGE_SIZE aligned.
// 
// If MEMORY_ALLOCATE_REQUEST_NOZERO is set, the allocated block will
// not be zero filled by the kernel if and only if the page backing this
// address is recycled from the same process.
// 
// The MEMORY_ALLOCATE_HAS_SEMAPHORE is a bit reserved for future use.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: Invalid "flags" value or 0 size requested.
// SYSERR_INVALID_ADDRESS: "address" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "address" is on a read/write protected
// 			page.
// SYSERR_INVALID_OPERATION: failure to "reserve" the virtual address
// 			space prior to this call.
// SYSERR_RESOURCE_LIMIT: resource limit exceeded.
// SYSERR_TRY_AGAIN: address and alignment constraints can not be met.


// memory_deallocate()
extern sys_return_t memory_deallocate(process_t _target,
				      size_t    _size,
				      address_t _address);

extern sys_return_t memory_deallocate_self(uint64_t  _flags,
					   size_t    _size,
					   address_t _address);

#define MEMORY_DEALLOCATE_NOFLAGS               0x00
#define MEMORY_DEALLOCATE_NO_TLB_INVALIDATE     0x01
#define MEMORY_DEALLOCATE_ALLOW_HOLES           0x02

// Remove the specified virtual memory range from the virtual address of
// the address of the "target". This causes the physical pages
// backing this address range to get deallocated.
// 
// Address value passed in "address" is request for the start address of
// the block of memory and the size contains requested size.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: 0 size requested or the "size" is not a
// 			multiple of VM_PAGE_SIZE.
// SYSERR_INVALID_ADDRESS: "address" is not VM_PAGE_SIZE aligned.
// SYSERR_INVALID_OPERATION: The range requested was not allocated using
// 			memory_allocate().


// Memory protection
struct memory_protection {
	uint8_t	mprot_current;
	uint8_t	mprot_maximum;
};
typedef struct memory_protection memory_protection_t;

// memory_protect()
extern sys_return_t memory_protect(process_t            _target,
				   size_t               _size,
				   address_t            _address,
				   memory_protection_t  _new_protection);

extern sys_return_t memory_protect_self(uint64_t            _flags,
					size_t              _size,
					address_t           _address,
					memory_protection_t _new_protection);

// flags
#define MEMORY_PROTECT_NO_TLB_INVALIDATE	0x01

// protection values
#define	MEMORY_PROTECT_NONE	0x00
#define	MEMORY_PROTECT_SAME	0x1F

#define	MEMORY_PROTECT_GC_READ	0x02
#define	MEMORY_PROTECT_READ	0x03
#define	MEMORY_PROTECT_GC_WRITE	0x08
#define	MEMORY_PROTECT_WRITE	0x0C

// Change the protection on a range of virtual memory
// address space. If the requested current protection
// exceeds the maximum protection, the call will fail with
// SYSERR_PROTECTION_FAILURE.
//
// MEMORY_PROTECT_NONE can be used to revoke all the access.
//
// MEMORY_PROTECT_READ, MEMORY_PROTECT_WRITE, can be set to
// enable read, write access respectively.
//
// MEMORY_PROTECT_GC_READ, MEMORY_PROTECT_GC_WRITE, can be
// set to enable GC only read, write access respectively.
//
// MEMORY_PROTECT_SAME is used to leave the protection unchanged.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: 0 size requested or the "size" is not a multiple of VM_PAGE_SIZE.
// SYSERR_INVALID_ADDRESS: "address" is not VM_PAGE_SIZE aligned.
// SYSERR_INVALID_OPERATION: failure to "reserve" the virtual address space or failure to
//                      "allocate" the virtual address range before this call.
// SYSERR_PROTECTION_FAILURE: the requested current protection exceeds the maximum protection.


typedef void *memory_pager_t;

// memory_sync()
extern sys_return_t memory_sync(process_t _target,
				uint64_t  _sync_flags,
				size_t    _size,
				address_t _address);

#define	MEMORY_SYNC_NOWAIT	0x01
#define	MEMORY_SYNC_WAIT	0x02
#define	MEMORY_SYNC_INVALIDATE	0x04
#define	MEMORY_SYNC_DATA2CODE	0x08

// Control the pages in the page cache. Used to write
// "dirty" pages to the pager. Also used to invalidate
// the pages in the page cache.
//
// If MEMORY_SYNC_NOWAIT is set the call returns immediately
// once all the write operations are initiated or queued for
// servicing.
//
// If MEMORY_SYNC_WAIT is set the call will not return until
// all write operations are completed.
//
// MEMORY_SYNC_NOWAIT and MEMORY_SYNC_WAIT are mutually
// exclusive. Setting both will result in a failed call
// with SYSERR_INVALID_ARGUMENT return code.
//
// If MEMORY_SYNC_INVALIDATE is set the call will
// invalidate all cached copies of mapped data that are
// inconsistent with the permanent storage.
//
// If MEMORY_SYNC_DATA2CODE is set the call will invalidate
// the instruction caches. In this case size can be less
// than a VM_PAGE_SIZE and address does not have to be
// VM_PAGE_SIZE aligned.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: 0 size requested or the "size"
// is not a multiple of VM_PAGE_SIZE
// and MEMORY_SYNC_DATA2CODE is not
// requested, or both MEMORY_SYNC_NOWAIT
// and MEMORY_SYNC_WAIT were set in flags.
// SYSERR_INVALID_ADDRESS: "address" is not VM_PAGE_SIZE
// aligned and MEMORY_SYNC_DATA2CODE
// is not requested. Address is not
// part of the "allocated" virtual address space.
// SYSERR_INVALID_OPERATION: failure to "reserve" the virtual address space or failure to
//                      "allocate" the virtual address range before this call.


// memory_read()
extern sys_return_t memory_read(process_t _target,
				size_t    _target_size,
				address_t _target_address,
				address_t _data_in);

// Read data from "target" virtual address space starting at
// "target_address". The caller is responsible for allocating and
// deallocating the "data_in" buffer. The "data_in" buffer is assumed
// to be at least "target_size" big. "target_size" needs to be at
// least 8 bytes.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: "target_size" smaller than 8 bytes.
// SYSERR_INVALID_ADDRESS: "data_in" or "target_address" is bad.
// SYSERR_PROTECTION_FAILURE: "data_in" is on a read/write protected
// 			page. One or more pages in the "target"
// 			address space range being read is read
// 			protected.


// memory_write()
extern sys_return_t memory_write(process_t _target,
				 size_t    _data_size,
				 address_t _data_out,
				 address_t _target_address);

// Write data to "target" virtual address space starting at
// "target_address". The "data_size" data is copied from caller's
// virtual address space starting at "data_out". "data_size" needs to be
// at least 8 bytes.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: "data_size" smaller than 8 bytes.
// SYSERR_INVALID_ADDRESS: "data_out" or "target_address" pointer is bad.
// SYSERR_PROTECTION_FAILURE: One or more pages in the caller's
// 			address space starting at "data_out" are
// 			read protected. One or more pages in the
// 			"target" address space starting at
// 			"target_address" are write protected.



// memory_release_physical()
extern sys_return_t memory_release_physical(process_t _target,
				            size_t    _size,
				            address_t _address);

extern sys_return_t memory_release_physical_self(uint64_t  _flags,
					         size_t    _size,
					         address_t _address);

#define MEMORY_RELEASE_PHYSICAL_NO_TLB_INVALIDATE	0x01

// Causes the physical pages backing the virtual address space range,
// starting at "address", to be release to the free pool. On success, the
// protection on the pages in the rage described by "address" and "size"
// would be set to {MEMORY_PROTECT_NONE, MEMORY_PROTECT_NONE}. This
// protection value will cause any subsequent memory_protect() that
// attempts to change this value, for this range, to fail.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: 0 "size" requested or the "size" is not a
// multiple of VM_PAGE_SIZE.
// SYSERR_INVALID_ADDRESS: "address" is not VM_PAGE_SIZE aligned.
// SYSERR_INVALID_OPERATION: failure to allocate the virtual address
// space, described by "address" and "size", before this call.


// memory_relocate()
extern sys_return_t memory_relocate(process_t target,
			            size_t    size,
			            address_t source,
			            address_t destination,
			            uint64_t  flags);

extern sys_return_t memory_relocate_self(size_t    size,
				         address_t source,
				         address_t destination,
				         uint64_t  flags);

#define	MEMORY_RELOCATE_RELEASE_PHYSICAL        0x01
#define	MEMORY_RELOCATE_NO_TLB_INVALIDATE       0x02
#define	MEMORY_RELOCATE_DUPLICATE_SHARED        0x04

// Causes the physical pages backing the virtual address space range,
// starting at "source", to be relocated to destination virtual address.
// On success, the virtual address range starting at "destination" will
// have the access protection as specified in "protection". On success,
// the protection on the pages in the range described by "source" and
// "size" would be set to {MEMORY_PROTECT_NONE, MEMORY_PROTECT_NONE}.
//
//  Error codes returned:
//  SYSERR_NONE: Success.
//  SYSERR_INVALID_HANDLE: "target" handle is invalid.
//  SYSERR_STALE_HANDLE: "target" handle is not usable.
//  SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
//  SYSERR_INVALID_ARGUMENT: 0 "size" requested or the "size" is not a
//                                  multiple of VM_PAGE_SIZE.
//  SYSERR_INVALID_ADDRESS: Either "source" or "destination" is not
//                                  VM_PAGE_SIZE aligned.
//  SYSERR_INVALID_OPERATION: Failure to allocate the virtual address
// space, described by "source" and "size",
//                                 before this call.
// Failure to reserve address space, described by
// "destination" and "size", before this call.
// SYSERR_INVALID_STATE:   One or more pages in the address space range,
// described by "destination" and "size", are already allocated.

// memory_read_force()

extern sys_return_t memory_read_force(process_t target,
                                      size_t target_size,
                                      address_t target_address,
                                      address_t data_in);

// Read data from "target" virtual address space starting at
// "target_address". The caller is responsible for allocating and
// deallocating the "data_in" buffer. The "data_in" buffer is assumed
// to be at least "target_size" big. The data is read even when one
// or more pages in the range being read from are read protected.
// The data is read even when MEMORY_RESERVE_ALLOW_READ is not set on
// the address space reservation containing the target address range.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: "target_size" is 0.
// SYSERR_INVALID_ADDRESS: "data_in" or "target_address" is bad.
// SYSERR_PROTECTION_FAILURE: "data_in" is on a write protected page.

// memory_write_force()

extern sys_return_t memory_write_force(process_t target,
                                       size_t data_size,
                                       address_t data_out,
                                       address_t target_address);

// Write data to "target" virtual address space starting at
// "target_address". The "data_size" data is copied from caller's
// virtual address space starting at "data_out".
// The data is written even when one or more pages being written
// to are write protected. The data is read even when
// MEMORY_RESERVE_ALLOW_WRITE is not set on the address space
// reservation containing the target address range.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ARGUMENT: "data_size" is 0.
// SYSERR_INVALID_ADDRESS: "data_out" or "target_address" pointer is bad.
// SYSERR_PROTECTION_FAILURE: One or more pages in the caller's
//                            address space starting at "data_out" are
//                            read protected.

// Data structures for memory address space map.
struct memory_address_space_map_header {
	size_t datasize;	/* number of bytes */
	uint64_t num_reservations;
	uint64_t flags;
	address_t end;
};
typedef struct memory_address_space_map_header
		memory_address_space_map_header_t;
#define MEMORY_MAP_HEADER_MORE		0x0001

struct memory_reservation_entry {
	uint64_t	num_allocations;
	uint64_t	flags;
	address_t	start;
	address_t	end;
	memory_protection_t	prot;
};
typedef struct memory_reservation_entry memory_reservation_entry_t;

struct memory_allocation_entry {
	uint64_t	flags;
	address_t	start;
	address_t	end;
	memory_protection_t	prot;
};
typedef struct memory_allocation_entry memory_allocation_entry_t;

#define	MEMORY_ENTRY_FLAGS_FROZEN	0x00004
#define	MEMORY_ENTRY_FLAGS_RSTACK	0x00010
#define	MEMORY_ENTRY_FLAGS_WIRED	0x00020

// memory_allocate_account()
extern sys_return_t memory_allocate_account(process_t  _target,
                                            uint64_t   _account_number,
                                            uint64_t   _flags,
                                            size_t     _size,
                                            address_t *_address);

extern sys_return_t memory_allocate_self(uint64_t   _account_number,
					 uint64_t   _flags,
					 size_t     _size,
					 address_t *_address);

// TBD
//
//  Error codes returned:
//  SYSERR_NONE: Success.
//  SYSERR_INVALID_HANDLE: "target" handle is invalid.
//  SYSERR_STALE_HANDLE: "target" handle is not usable.
//  SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
//  SYSERR_INVALID_ARGUMENT: 0 "size" requested or the "size" is not a
//                                  multiple of VM_PAGE_SIZE.
//                           Invalid account number.
//  SYSERR_INVALID_ADDRESS: "address" is not VM_PAGE_SIZE aligned.
//  SYSERR_INVALID_OPERATION: Failure to reserve the virtual address
//  space, described by "address" and "size", before this call.
//  SYSERR_INVALID_STATE:   One or more pages in the address space range,
//  described by "address" and "size", are already allocated.

#define MEMORY_ACCOUNT_DEFAULT 0UL

extern sys_return_t memory_add_physical(process_t	_target,
		                        uint64_t	_account_number,
		                        uint64_t	_flags,
		                        size_t		_size,
		                        address_t	_address);

extern sys_return_t memory_account_set_funding(process_t	_target,
			                       uint64_t		_account_number,
			                       size_t		_size);

extern sys_return_t memory_account_deposit(process_t	_target,
		                           uint64_t	_account_number,
		                           int64_t	_size);

extern sys_return_t memory_account_transfer(process_t	_target,
					    uint64_t	_destination_account_number,
					    uint64_t	_source_account_number,
					    size_t	_size);

extern sys_return_t memory_account_set_maximum(process_t	_target,
			   		       uint64_t		_account_number,
			   		       size_t		_size);

extern sys_return_t memory_tlb_invalidate_self(uint64_t  _flags,
			   		       size_t    _size,
			   		       address_t _address);

#define MEMORY_TLB_INVALIDATE_ALL	0x01

extern sys_return_t memory_flush_deallocated_self(uint64_t  _flags,
			      			  uint64_t  _account_number,
			      			  size_t *  _allocated,
			      			  size_t *  _flushed);
#define MEMORY_FLUSH_DEALLOCATED_HEAP		0x01
#define MEMORY_FLUSH_DEALLOCATED_OVERDRAFT_ONLY	0x02

extern sys_return_t memory_get_address_map(process_t	_target,
		       			   address_t	_buffer,
		       			   size_t       *_length);

// Callback registration for memory_allocate/deallocate hooks used for leak
// detection.
typedef void (*memory_allocate_self_hook_t)(size_t, address_t*);
typedef void (*memory_deallocate_self_hook_t)(size_t, address_t);
typedef void (*memory_release_physical_self_hook_t)(size_t, address_t);
typedef void (*memory_relocate_self_hook_t)(size_t, address_t, address_t, uint64_t);
extern void set_memory_allocate_self_hook(memory_allocate_self_hook_t);
extern void set_memory_deallocate_self_hook(memory_deallocate_self_hook_t);
extern void set_memory_release_physical_self_hook(memory_deallocate_self_hook_t);
extern void set_memory_relocate_self_hook(memory_relocate_self_hook_t);



/*
 * DANGER! DANGER! Will Robinson!!  Alien hack approaching!!
 *
 * Because it is absurdly difficult to include the $SANDBOX/aznix/include/os/azulmmap.h
 * file from inside the GNU libraries (this has to do with the fact that the proxy layer
 * creates a psuedo libos set of includes and including the real ones confuses everything)
 * we are adding these externs to accessor functions that return the defines.  This isn't
 * the bestest way of doing it, but I'm not certain there is a better way that doesn't
 * include hardcoding something.
 *
 * We do the same thing for the az_mreserve() & az_mmap() calls for much the same reazon.
 *
 */
extern intptr_t azul_morecore_start_addr(void);
extern intptr_t azul_morecore_end_addr(void);
extern void*    azul_mmap_wrapper(void* addr, size_t len, int prot, int flags, int fd, off_t offset);


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
  
#endif // _OS_MEMORY_H_
