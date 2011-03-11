// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// types.h - Azul OS (Aztek) system services shared types.
//           Sub-system-specific types are in their respective header files.

#ifndef _OS_TYPES_H_
#define _OS_TYPES_H_ 1

#include <stdint.h>
#include <pthread.h>
#include <os/errors.h>
#include <sys/types.h>
#include <aznix/az_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *address_t;                // User address space pointer
typedef int64_t reg_t;                  // register type (always 64 bits, may be signed)
typedef uint64_t ureg_t;                // control register type (always 64 bits, unsigned)
typedef int64_t time_interval_t;        // System time interval (unspecified units)
typedef intptr_t os_handle_t;           // Everything interesting is opaque at user-level
typedef intptr_t kselectable_t;         // NOTE: Temporary should go away when we implement user level selectors

typedef uint64_t cnt_t;                 // A count of something

// Process types
typedef pid_t process_t;                // A handle to a process
typedef os_handle_t privilege_token_t;	// A handle to the user privilege token
typedef os_handle_t capabilities_t;	// A set of capabilities
typedef az_allocid_t process_id_t;      // Process identifier
typedef az_groupid_t process_group_id_t;// Process group identifier

// Thread types
typedef pthread_t thread_t;
typedef pthread_t user_thread_t;

// Thread-specific data
typedef pthread_key_t thread_key_t;

// Interrupts
enum _interrupt_code {
	INTERRUPT_SPINT   = 0x01,
};
typedef enum _interrupt_code interrupt_t;

// Miscellany

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef _OFFSET_T_
// Bracketed with the ifdef because some odd files also define offset_t.
#define _OFFSET_T_ 1
typedef uint64_t offset_t;
#endif

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_TYPES_H_
