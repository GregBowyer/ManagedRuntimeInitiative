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

// This code is necessary because the current Pthreads library does not allow
// us to readily use our own stack area.  The Pthreads calls do allow you to
// specify a separate area for stack, but provide no easy means of reclaiming
// this area on thread exit.  To that end we have built a means of allocating
// our own (very large) stack frames, without modifying the exiting Pthreads
// lirbary, or having to update/port each successive revision.
//
// The mechanism works as such: during a java thread creation, we allocate a 
// large stack frame from the previously established Java Thread Stack pool, by
// calling thread_stack_create().  This grabs an unused stack off of the stack
// free list.  Each stack is at least 2Meg in size, and properly aligned.  The 
// bottom most (lowest) address of the thread can also be used as the thread ID.
//
// The java thread creation logic then calls thread_init(), passing it (among
// other things) the address of the stack area it just allocated, the address
// of the top and bottom of the usable portion of this stack, and the address of
// the function to execute when the Pthread creation API is called.
// 
// We then call the normal pthread_create() function, which results in a new 
// thread that allocates its own stack (via malloc) from within the general 
// virtual memory pool.  This stack is created to be as small as possible.
//
// Instead of then jumping to the actual java thread creation logic, we goto
// a function called whack_thread_stack_asm, which manipulates the stacks and
// swaps context into the newly allocated, large Java Thread Stack.  We have
// "whacked" that stack pointer to point at this new stack frame, leaving the
// old Pthread stack frame intact.  We manipulate the thread stack such that
// on thread exit, another routine (dewhack_thread_stack_asm) will be called
// that reverts back to the Pthread stack, and returns the large stack frame
// (called the Java Thread Stack forwith) back to the pool.  The Pthread stack
// frame memory allocation is handled within its own library and we don't
// worry about it.  We specifically allocate as small a Pthread stack frame
// as possible, though.
//
// NOTE: According to Cliff Click, the interrupt handlers that might normally
// bloat a stack will be deactivated during the time the Pthreads stack is
// in use, so its small size is not a concern.
//
// This code will be used both for the initial "proto" thread, as well as the 
// nominal java threads, to allocate a large (2Meg?) stack in place of whatever
// stack was previously in use.  There are two separate but similar x86-64 asm
// routines, whack_main_stack_asm() & wack_thread_stack_asm(), only because
// the number of parameters varies between these two use cases, and the routine
// is small enough that duplicating the object code is ultimately less confusing
// than trying to arrange some clever use of parameter handling.  There is only
// a single dewhack_thread_stack_asm(), which both routines use, but the main 
// (proto) thread stack doesn't need to be "restored" if the AVM is exiting anyway.
//
// The Java Thread Stack layout is as follows: (address shown are based on an 
// imaginary memory allocations and given only for reference)
//
//  0x2020,0000     Top of Full Stack/ Top of Red Zone.   
//                  +--------------------------------------------------+
//  0x201F,FFF8     | Top of useable stack (stack grows down)          |
//                  |                                                  |
//                  |                                                  |
//                  ~ Various Stack Frames                             ~ 
//                  |                                                  |
//                  |                                                  |
//                  |                                                  |
//       %RBP==>    | Current Frame Pointer (%RBP)                     |
//                  | Temporary or Local Arguments                     |
//                  ~                                                  ~ 
//       %RSP==>    | Current Stack Pointer (%RSP)                     |
//                  |                                                  |
//                  | Leaf Area (Temporary Variables)                  |
//                  |                                                  |
//                  ~                                                  ~ 
//     %RSP-128==>  | Interrupt Handler Stack area                     |
//                  |                                                  |
//                  ~                                                  ~ 
//                  |                                                  |
//                  +--------------------------------------------------+
//                  |                                                  |
//                  | Yellow Zone: Used for stack overflow safepoints  |
//                  |                                                  |
//                  +--------------------------------------------------+
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXX DeadZone: Unmapped Memory. XXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  +--------------------------------------------------+
//                  |                                                  |
//                  | Alternate Stack for use in SIGSEGV handling.     |
//                  |                                                  |
//                  +--------------------------------------------------+
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXX DeadZone: Unmapped Memory. XXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  +--------------------------------------------------+
//                  |                                                  |
//                  | Type Specific Thread Memory based on Thread type |
//                  |                                                  |
//                  +--------------------------------------------------+
//                  |                                                  |
//                  |                                                  |
//                  | Type Stable Memory: Thread Class                 |
//                  |                                                  |
//                  +--------------------------------------------------+
//  0x0000,0000     Bottom of the stack 
//
//
//  
//  Functions altered or added in creating this logic are:
// 
//  ...hotspot6/src/os/acapulco/launcher/whack_main_stack.s (this file)
//  ...hotspot6/acapulco_port/acapulco/include/os/thread.c
//  ...hotspot6/src/os_cpu/acapulco_x86/vm/thread_acapulco.cpp 
//  ...hotspot6/src/os/shared_linux/launcher/main.cpp
//  ...hotspot6/src/os/acapulco/vm/os_acapulco.cpp
//  ...
//******************************************************************************
//
// Externals.
//
//******************************************************************************
    .text
    .globl whack_main_stack_asm
    .globl whack_thread_stack_asm
    .globl dewhack_thread_stack_asm
    .type whack_main_stack_asm, @function
    .type whack_thread_stack_asm, @function
    .type dewack_thread_stack_asm, @function

//******************************************************************************
//
// FUNC: whack_main_stack_asm()
//
// SYNOPSIS: Called in place of make_java(), from within the main(), just
//   after the Java Thread Stack pool has been created, it substitutes a large 
//   previously allocated Java stack for the existing stack.
//
// DESCRIPTION: This is a small amount of trampoline code that is called
//   instead of the make_java() from within the main().  It will set up
//   the correct stack arguments on the new stack, and then call the 
//   make_java() routine, correctly passing the argc/argv arguments to
//   it.
//
// PARAMETERS: 
// 
//   extern "C" int whack_main_stack_asm( long long stk, void* make_java, int argc, char** argv ); 
//
//   INPUT long long stk: a pointer to the first allocated stack frame.  
//     This stack frame will be used as the "main" stack for the rest of 
//     the AVM.
//
//   INPUT void* make_java: the address of the function we will branch 
//     directly to from this routine, once we've massaged the stacks.
//
//   INPUT int argc: The standard ArgC value for the make_java().
//
//   INPUT char** argv: The standard ArgV pointer for the make_java().
//
// RETURNS: 
//
//   int - the value ultimately returned by the make_java().
//
// BASIC ALGORITHM: 
// 
//   %RDI = just past high end of the stack. NOTE: -8(%RDI) is the first valid 
//     stack word.
//   %RSI = address of the make_java().
//   %RDX = argc
//   %RCX = argv
//
//   When we enter the routine, the four parameters (stack, func, argc, argv), 
//   will be in the in the %RDI, %RSI, %RDX, %RCX, respectively.  
//
//   Step 1: Setup so return from make_java returns to this call's caller.
//     
//   Step 2: The %RDI register is pointing to the very top of the stack,
//     but the first useable word is 8 bytes below that (stacks grow down).
//     Move the return address to this first word on the stack.
//     
//   Step 3: Set the stack pointer to now point at this address. (i.e "whack"
//     the stack.) We are now in the large Java Thread Stack context.
//     
//   Step 4: Now we need to readjust the registers for proper argument
//     handling.  Move the make_java() (in %RSI) into the %RAX register,
//     we will jump to this in step 7.
//     
//   Step 5: Move the argc variable into the %RDI register, to be passed
//     as the first argument to the make_java().
//     
//   Step 6: Move the argv variable into the %RSI register, to be passed
//     as the second argument to the make_java().
//     
//   Step 7: Jump to the make_java(), passing argc, argv. We don't return
//     from this routine.
//
//   We don't return this thread stack, in that there is no "dewhack"
//   mechanism for the main thread, as once this thread exits, so does
//   the AVM, so cleanup isn't really an issue, although for completeness
//   we probably ought to do something else.
//
// AUTHOR: Cliff Click
//  
//******************************************************************************

whack_main_stack_asm:
        movq    0(%rsp),%rax  ; // 1. get return address into rax
        movq    %rax,-8(%rdi) ; // 2. return from make_java returns to this call's caller
        leaq    -8(%rdi),%rsp ; // 3. whack RSP
        movq    %rsi,%rax     ; // 4. need rdi,rsi as outgoing arg registers
        movq    %rdx,%rdi     ; // 5. argc for outgoing call
        movq    %rcx,%rsi     ; // 6. argv for outgoing call
        jmpq    *%rax         ; // 7. tail-call make_java, never return here
        int     $3            ; // break/crunch



//******************************************************************************
//
// FUNC: whack_thread_stack_asm()
//
// SYNOPSIS: Called from within the thread_init(), it substitutes a previously
//   allocated large Java Thread Stack for the existing Pthreads library stack,
//   saving everything necessary to restore this on thread death.
//
// DESCRIPTION: This is a small amount of trampoline code that is called
//   in place of the what would have been the start function passed to
//   the pthread_create() call.  It will set up the correct stack arguments
//   on the new stack, and save the current stack information, so that on 
//   exit we will free the new stack and return back to the existing Pthread 
//   stack.
// 
//   This function relies on the fact that the thread_init() has previously
//   allocated the stack, and filled in the top five locations of the usable
//   stack area with the following fields:
//
//      1) The address of the high end of the usable stack,  NOTE: This address
//         will be one giant word (8 bytes) above the actual usable
//         stack, and is not a valid address within this stack.  Be
//         very careful not to use this as a parameter when trying
//         to deduce the thread stack id unless you've first subtracted
//         one from the address.
//      2) The parameter that will be passed to the function that will 
//         be called (via a "jmp" command) from this routine.
//      3) The function that will be called (via a "jmp" command) from
//         this routine.
//      4) A counter (initially set to zero) of the number of additional
//         stack segments that have been paged in (mmap'ed) onto the
//         stack.  If this value is XXXXXXXXXXXXXXXXXXXXX it means that
//         the thread has entered into the yellow zone and the entire
//         yellow zone, as definted at run time by the -XX:StackYellowPages
//         paramter (given in terms of stack segments) has been allocated
//         into memory.
//      5) The original Pthread RSP immediately before we jumped onto
//         the new Java stack.
//      6) The original Pthread RBP immediately before we jumped onto
//         the new Java stack.
//
// The Stack Information Block:
//
//   Assuming that the stack size was 0x20,0000 starting at address 0x0, it
//   would look like this:
//
//  0x20,0000     Top of a typical Java Thread Stack 
//              +--------------------------------------------------+
//  0x1F,FFF8   | Top of Stack Addr.(grows down from here)         |
//              +--------------------------------------------------+
//  0x1F,FFF0   | Start thread function parameter.                 |
//              +--------------------------------------------------+
//  0x1F,FFE8   | Start thread function pointer.                   |
//              +--------------------------------------------------+
//  0x1F,FFE0   | User Stack Extensions Counter.                   |
//              +--------------------------------------------------+
//  0x1F,FFE0   | Expression Stack Extensions Counter.             |
//              +--------------------------------------------------+
//  0x1F,FFE0   | Reserved/Unused (contains 0xDEADBEEF)            |
//              +--------------------------------------------------+
//  0x1F,FFD8   | Saved PThread stack RSP register value  (N/A)    |
//              +--------------------------------------------------+
//  0x1F,FFD0   | Saved PThread stack RBP register value           |
//              |   (filled in by the called fuction)              |
//              +--------------------------------------------------+
//
//    The PThread RSP/RBP fields are filled in by this routine, with the address
//    of the current (Pthreads) stack pointer.
//
// PARAMETERS: 
// 
// extern "C" int whack_thread_stack_asm( void* info ); 
//
//   INPUT void* info: the address of the structure (within the 2M stack)
//     that contains the function to call next, the parameter to pass it,
//     the address of the 2M stack, its size, and the address of the pThreads
//     created stack.  Note: on entry this last field should be zero, and
//     we will fill it in within this routine.
//
// RETURNS: void
//
// BASIC ALGORITHM: 
// 
//   Step 1: Write the address of the (currently active) Pthread Stack
//      frame into the reserved area towards the top of the Java Stack.
//      We will use this when we are popping back up the Java stack and
//      need to switch contexts back to the Pthread stack.  The %RDI
//      register is pointing to the top of the Java stack.
//
//   Step 2: Now switch stack contexts from the Pthreads to the Java
//      Stack Frame.
//
//   Step 3: Push the address of the dewhack_thread_stack_asm() onto the
//      current Java Thread Stack.  When everything else has finished
//      and we are popping up the stack, this is the function that will
//      get called and change stack contexts back to the Pthreads stack.
//
//   Step 4: Set the Frame Pointer to the new stack frame.
//
//   Step 5: Load the %RSI register (2st parameter once we make the function
//      call) with the Start Function address we want to eventually call.
//
//   Step 6: Load the %RDX register (3rd parameter once we make the function
//      call) with the Start Function's parameter. 
//
//   Step 7: The %RDI register (1st parameter once we make the function
//      call) already has the address of the new java stack.
// 
//   Step 8&9: Goto the alternate_stack_create() which will then issue an 
//      sigaltstack() to create an alternate stack area just above the 
//      thread id at the bottom of the stack (separated by unmapped deadzone 
//      areas) which then calls the start_thread(), or whatever function was 
//      originally intended, and actually wanted to call from the within the 
//      pthread_create().
//
//   We don't return from this function, the function it jumps to returns and 
//   bumps up the stack which eventually results in the  exection of the
//   dewhack_thread_stack_asm(), which changes stack contexts back to the
//   original Pthread stack context, and then jumps to the thread_stack_delete(),
//   which frees the Java Thread Stack, and then returns back up the original
//   calling stack back in the Pthread stack context.
//  
// AUTHOR: Robb Kane
//
//******************************************************************************
//
// NOTE:  Do not change this logic without an accompanying change in the
// thread_int(), which relies on these variables placed on the
// stack in this arrangement!

whack_thread_stack_asm:

        movq    %rsp, -0x18(%rdi)             ; // 1. Save Pthread rsp value.
        movq    %rbp, -0x20(%rdi)             ; // 1. Save Pthread rbp value.
        leaq    -0x20(%rdi), %rsp             ; // 2. Switch contexts.
        movq    dewhack_thread_stack_asm@GOTPCREL(%rip), %rax ;
        push    %rax                          ; // 3. Setup to undo stack.
        movq    %rsp, %rbp                    ; // 4. Cleanup Stackframe.
        movq    -0x10(%rdi),%rsi              ; // 5. Start func.
        movq    -0x08(%rdi),%rdx              ; // 6. Start func param.
                                              ; // 7. Leave %rdi alone.
        movq    alternate_stack_create@GOTPCREL(%rip), %rax ;
        jmpq    *%rax                         ; // 8. Goto to alternate_stack_create().
        int     $3                            ; // break/crunch


//******************************************************************************
//
// FUNC: dewhack_thread_stack_asm()
//
// SYNOPSIS: Free back the large Thread Stack created for the thread,
//   return to the originating Pthread stack context, and then return the
//   large Thread Stack back to the free list.
//
//
// DESCRIPTION: This is the bookend piece of trampoline code that is called
//   when the thread has exited, and the large stack area must be
//   freed and context returned back to the existing Pthread stack.
//
//   It changes thread context back to the original Pthreads stack context
//   and jumps to delete_thread_stack(), which returns the stack to the 
//   free list and then returns back to the original pthread calling routine
//   that invoked the whack_thread_stack_asm().
//
// PARAMETERS: 
// 
//   extern "C" int dewhack_thread_stack_asm( void ); 
//
// RETURNS: 
//
//   N/A
//
// BASIC ALGORITHM: 
// 
//   Step 1:  The current stack pointer (once masked) is the bottom (lowest
//     address) of the information block created by the thread_init() & the 
//     whack_thread_stack() on the large Thread Stack.  Pass the address of 
//     the large Thread Stack to the func, as arg 1 by placing it in the 
//     %RDI (parameter one) register.
//
//   Step 2:  Switch contexts to the old (small) pthread stack, using the
//     value given in the information block saved on the large Thread Stack.
//
//   Step 3:  Set the stack frame to now point to the old Pthread Stack.
//
//   Step 4:  goto thread_stack_delete(), passing it the address of the Java
//     Thread Stack, which will return it to the free list.  Don't come back.
//
//
// AUTHOR: Robb Kane
//
//
//******************************************************************************
//
// NOTE:  Do not change this logic without an accompanying change in the
// thread_int(), which relies on these variables placed on the
// stack in this arrangement!

dewhack_thread_stack_asm:

        movq    %rsp, %rdi        ; // Step 1. Make the current stack address be arg 1.
        movq    0x00(%rsp), %rbp  ; // Step 2. Restore Pthread rbp value.
        movq    0x08(%rsp), %rsp  ; // Step 3. Now back in Pthread stack.
        movq    thread_stack_delete@GOTPCREL(%rip), %rax ;
        jmpq    *%rax             ; // Step 4. Goto Azul's thread stack delete func.
        int     $3                ; // break/crunch
        

// End of whack_stack.s file.
