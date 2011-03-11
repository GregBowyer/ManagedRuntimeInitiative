// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// syscalls.s - (Linux + azul DLKM) system services atomic operations.

//------------------------------------------------------------------//
// Note that this file should not be used in the kernel modules.    //
// For user program use only. See macros.s for usage.               //
//__________________________________________________________________//

.include "src/macros.s"

	/*
	 * ===========================
	 * Thread related system calls
	 * ===========================
	 */
syscall_function gettid, 186

	/*
	 * ========================================
	 * Process group related system calls
	 * ========================================
	 */
syscall_function az_pgroup_create, 1024
syscall_function az_pgroup_destroy, 1025
syscall_function az_pgroup_get_list, 1026
syscall_function az_pgroup_set_data, 1027
syscall_function az_pgroup_get_data, 1028
syscall_function az_pgroup_set_rlimit, 1029
syscall_function az_pgroup_get_rlimit, 1030
syscall_function az_pgroup_uncommit_all, 1031
syscall_function az_process_add, 1032
syscall_function az_process_move, 1033
syscall_function az_process_get_gid, 1034
syscall_function az_process_get_list, 1035
syscall_function az_process_kill, 1036

	/*
	 * ========================================
	 * Process token related system calls
	 * ========================================
	 */
//syscall_function az_token_create, 1048
//syscall_function az_token_delete, 1049
//syscall_function az_token_set_data, 1050
//syscall_function az_token_get_data, 1051
//syscall_function az_token_get_list, 1052
//syscall_function az_token_delete_all, 1053

	/*
	 * ============================================
	 * memory management related system calls
	 * ============================================
	 */
syscall_function az_mreserve, 1057
syscall_function az_munreserve, 1058
syscall_function az_mmap, 1059
syscall_function az_munmap, 1060
syscall_function az_mremap, 1061
syscall_function az_mprotect, 1062
syscall_function az_mcopy, 1063
syscall_function az_mbatch_start, 1064
syscall_function az_mbatch_remap, 1065
syscall_function az_mbatch_protect, 1066
syscall_function az_mbatch_commit, 1067
syscall_function az_mbatch_abort, 1068
syscall_function az_tlb_invalidate, 1069
syscall_function az_pmem_set_maximum, 1070
syscall_function az_pmem_set_account_maximum, 1071
syscall_function az_pmem_fund_account, 1072
syscall_function az_pmem_account_transfer, 1073
syscall_function az_pmem_get_account_stats, 1074
syscall_function az_pmem_get_fund_stats, 1075
syscall_function az_pmem_set_account_funds, 1076
syscall_function az_pmem_set_accounts_to_credit, 1077
syscall_function az_pmem_fund_transfer, 1078
syscall_function az_pmem_reset_account_watermarks, 1079
syscall_function az_munshatter, 1080
syscall_function az_mprobe, 1081
syscall_function az_mflush, 1083
syscall_function az_mreserve_alias, 1084

// System calls with hooks for leak detection support
// TODO: Fix the syscall numbers to the correct azul kernel values
    
// sys_return_t az_memory_allocate_self(uint64_t   _account_number,
//                                      uint64_t   _flags,
//                                      size_t     _size,
//                                      address_t *_address);
// typedef void (*memory_allocate_self_hook_t)(size_t, address_t*);
ENTRY az_memory_allocate_self   
  prolog
  pushq  %rcx
  pushq  %rdx
  movq	 $142, %rax
  movq   %rcx, %r10
  syscall
  popq   %rdi
  popq   %rsi
  cmpq   $0, %rax
  js     1f
  movq   _memory_allocate_self_hook@GOTPCREL(%rip), %rax
  cmpq   $0, %rax
  jz     1f
  call	 *%rax
  movq	 $0, %rax
1:
  epilog

WEAK _memory_allocate_self_hook
    
// extern sys_return_t az_memory_deallocate_self(uint64_t  _flags,
//                                               size_t    _size,
//                                               address_t _address);
// typedef void (*memory_deallocate_self_hook_t)(size_t, address_t);
ENTRY az_memory_deallocate_self
  prolog
  pushq  %rdx
  pushq  %rsi
  movq	 $145, %rax
  movq   %rcx, %r10
  syscall
  popq   %rdi
  popq   %rsi
  cmpq   $0, %rax
  js     1f
  movq   _memory_deallocate_self_hook@GOTPCREL(%rip), %rax
  cmpq   $0, %rax
  jz     1f
  call	 *%rax
  movq	 $0, %rax
1:
  epilog

WEAK _memory_deallocate_self_hook

// sys_return_t az_memory_release_physical_self(uint64_t  _flags,
//                                              size_t    _size,
//                                              address_t _address);
// typedef void (*memory_release_physical_self_hook_t)(size_t, address_t);
ENTRY az_memory_release_physical_self
  prolog
  pushq  %rdx
  pushq  %rsi
  movq	 $143, %rax
  movq   %rcx, %r10
  syscall
  popq   %rdi
  popq   %rsi
  cmpq   $0, %rax
  js     1f
  movq   _memory_release_physical_self_hook@GOTPCREL(%rip), %rax
  cmpq   $0, %rax
  jz     1f
  call	 *%rax
  movq	 $0, %rax
1:
  epilog

WEAK _memory_release_physical_self_hook
    
// sys_return_t az_memory_relocate_self(size_t    size,
//                                      address_t source,
//                                      address_t destination,
//   				        uint64_t  flags);
// typedef void (*memory_relocate_self_hook_t)(size_t, address_t, address_t, uint64_t);
ENTRY az_memory_relocate_self
  prolog
  pushq  %rcx
  pushq  %rdx
  pushq  %rsi
  pushq  %rdi
  movq	 $128, %rax
  movq   %rcx, %r10
  syscall
  popq   %rdi
  popq   %rsi
  popq   %rdx
  popq   %rcx
  cmpq   $0, %rax
  js     1f
  movq   _memory_relocate_self_hook@GOTPCREL(%rip), %rax
  cmpq   $0, %rax
  jz     1f
  call	 *%rax
  movq	 $0, %rax
1:
  epilog

WEAK _memory_relocate_self_hook
