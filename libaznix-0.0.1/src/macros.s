//------------------------------------------------------------------------------
// Various macros and constants for libos assembly files.
//------------------------------------------------------------------------------

// FIXME - consider removing frames from syscalls.

// Declare standard C-compatible function frameless prologue
.macro prolog
	pushq	%rbp
	movq	%rsp, %rbp
.endm

// Declare standard C-compatible function prologue with frame
.macro prolog_frame frame_size
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$\frame_size, %rsp
.endm

// Declare standard C-compatible function epilogue
.macro epilog
	leave
	ret
.endm

// Declare a C-compatible function in the text section.
.macro ENTRY function_name
.align 4
.text
.global \function_name
\function_name:
.endm

// Declare a C-compatible weak function in the text section.
.macro WEAK function_name
.align 4
.text
.weak \function_name
\function_name:
    ret
.endm

// Declare a syscall function in the text section, appropriately aligned.
.macro syscall_function name num
.align 32
.text
.global \name
\name:
    movq    $\num, %rax
    movq    %rcx, %r10  /* kernel expects the arg in rcx at r10, so move */
    syscall
    cmpq    $-4095, %rax   /* check %rax for error */
    jae     1f
    ret
1:
    movq    __az_syscall_error@GOTPCREL(%rip), %r11
    callq   *%r11
    ret
.endm


// Stub for fast system calls.
.macro fastcall_function name num
.align 32
.text
.global \name
\name:
  movq	$\num, %rax
  movq	%rcx, %r10
  syscall
  ret
.endm


// Stub for unimplemented system calls.
.macro unimplemented_syscall_function name num
.align 32
.text
.global \name
\name:
  movq	$\num, %rax
  movq	%rcx, %r10
  int3
  ret
.endm

// Declare BREAKPOINT to help debug asm code.
.macro BREAKPOINT
  int3
.endm

// Declare UNIMPLEMENTED as a breakpoint trap.
.macro UNIMPLEMENTED
  int3
.endm

// Declare DEPRECATED to indicate that routine should not be implemented.
.macro DEPRECATED
  int3
.endm


//--------------------------------------------------------------------------------------------
// Process callback flags
//--------------------------------------------------------------------------------------------
PROCESS_CB_PROTECTION_FAULT   =  0
PROCESS_CB_SAFE_POINT         =  1
PROCESS_CB_SMA_FAIL           =  2
PROCESS_CB_ADDRESS_FAULT      =  3
PROCESS_CB_RSTACK_OVERFLOW    =  4
PROCESS_CB_PERF0_OVERFLOW     =  5
PROCESS_CB_PERF1_OVERFLOW     =  6
PROCESS_CB_BREAK              =  7
PROCESS_CB_FP_UNIMPLEMENTED   =  8
PROCESS_CB_ILLEGAL_ADDRESS    =  9
PROCESS_CB_WATCH_POINT        = 10
PROCESS_CB_PREEMPTION         = 11
PROCESS_CB_TLB0_PERF_OVERFLOW = 12
PROCESS_CB_DEMAND_ALLOCATION  = 13
PROCESS_CB_DEMAND_ALLOC_SYS   = 14
PROCESS_CB_TLB1_PERF_OVERFLOW = 15
PROCESS_CB_PERF4_OVERFLOW     = 16
PROCESS_CB_PERF5_OVERFLOW     = 17
PROCESS_CB_ASTACK_OVERFLOW    = 18

//--------------------------------------------------------------------------------------------
// Process terminate flags
//--------------------------------------------------------------------------------------------
PROCESS_TERMINATE_ABNORMAL	    = 0x8
EXIT_DUE_TO_OOM                 = 63
EXIT_OOM_REASON                 = 61
EXIT_OOM_REASON_STACK           = 1
EXIT_OOM_STACK_CODE             = (1 << EXIT_DUE_TO_OOM) | (EXIT_OOM_REASON_STACK << EXIT_OOM_REASON)

//--------------------------------------------------------------------------------------------
// System call error codes
//--------------------------------------------------------------------------------------------
SYSERR_RESOURCE_LIMIT           =   6
SYSERR_KERNEL_LIMIT             =  17

//--------------------------------------------------------------------------------------------
// System call codes for those called directly from assembly code.
//--------------------------------------------------------------------------------------------
SYSCALL_PROCESS_TERMINATE       =  2
SYSCALL_PROCESS_SELF            = 11
SYSCALL_MEMORY_RESERVE          = 33
SYSCALL_MEMORY_UNRESERVE        = 34
SYSCALL_MEMORY_ALLOCATE         = 35
SYSCALL_MEMORY_DEALLOCATE       = 36
SYSCALL_MEMORY_RESERVE_SELF     = 44
SYSCALL_MEMORY_DEALLOCATE_RSTACK= 46
SYSCALL_MEMORY_UNRESERVE_SELF   = 47    
SYSCALL_MEMORY_ALLOCATE_SELF    = 142
SYSCALL_MEMORY_DEALLOCATE_SELF  = 145
SYSCALL_PROCESS_NOTIFY_DEBUGGER = 60

//--------------------------------------------------------------------------------------------
// Memory system call flags
//--------------------------------------------------------------------------------------------
MEMORY_ALLOCATE_REQUIRED_ADDRESS = 256
MEMORY_DEALLOCATE_ALLOW_HOLES    = 2

// FIXME - These values need to changed for the azul memory implementation
VM_SMALL_PAGE_SIZE_BITS          = 14
VM_LARGE_PAGE_SIZE_BITS          = 20
VM_SMALL_PAGE_SIZE               = 16384
VM_LARGE_PAGE_SIZE               = 1048576

VM_USER_ADDRESS_MAX              = 17317308137472
VM_USER_STACK_REGION_START       = 8796093022208
VM_USER_STACK_REGION_SIZE        = (VM_USER_ADDRESS_MAX - VM_USER_STACK_REGION_START)
    
//--------------------------------------------------------------------------------------------
// Chained exception handling flags
//--------------------------------------------------------------------------------------------
EXCEPTION_NOT_HANDLED       = -1
EXCEPTION_RESUME_EXECUTION  = -2
EXCEPTION_SKIP_INSTRUCTION  = -3
