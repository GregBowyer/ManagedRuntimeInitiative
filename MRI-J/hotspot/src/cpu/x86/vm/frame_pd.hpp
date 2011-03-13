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
#ifndef FRAME_PD_HPP
#define FRAME_PD_HPP


#include "register_pd.hpp"

  // Azul specific frame layout stuff

public:
  // CALLING CONVENTIONS:
  // ====================
  //
  // Alignment:
  // ----------
  // ALL frames are fixed sized (known at compile-time) and 16byte aligned.
  // The interpreter's JES is kept off to the side in a C-malloc'd stack.
  //
  // Frame base pointer:
  // -------------------
  // Not used; same as C++ frame-less code.
  //
  // Native/C/Interpreter X86-64 ABI:
  // --------------------
  // RBP, RBX, R12-R15 are callee-save (non-volatile) registers.
  //   NO DEBUG INFO NOR OOPS IN CALLEE-SAVE REGISTERS
  // RDI, RSI, RDX, RCX, R08, R09 are parameter registers.
  // RAX is the return register.
  // R11 are scratch.
  // XMM0-XMM7 are parameter registers.
  // XMM0 is the float return register.
  // 
  // When active - during execution:
  // - RSP is the bottom of the frame but also 128bytes below are reserved and
  //   not touched by signal handlers (the red zone in x86-64 ABI)
  //
  // Native & JIT ABI:
  // Arg0 in RDI
  // Arg1 in RSI
  // Arg2 in RDX
  // Arg3 in RCX
  // Arg4 in R08
  // Arg5 in R09
  //
  // Additional native ABI:
  // ret adr [SP+ 0] <-- SP on entry/exit (NB. 16 byte aligned)
  // Arg6 in [SP+ 8] <-- args beyond 6 are passed in the X86_64 ABI
  // Arg7 in [SP+16]
  // Arg8 in ... and so on
  // ...
  //
  // Additional JIT'd ABI:
  // ----------------

  // First 6 args = RDI, RSI, RDX, RCX, R08, R09.  Args beyond 6 are passed on
  // the stack above a 4-word caller-owned hole.  Int (and sub-int values) are
  // sign-extended to 4-btye 'int' width, but the upper 4 bytes are ZERO.  RDI
  // is a bonus return value, and is the same value as is passed on entry (if
  // a objectRef).
  // 
  // Additional Interpreter ABI:
  // ----------------
  // - RCX is methodRef
  // - Normal incoming args are already on the JEXSTK as locals.
  // - RDX is locals ptr: same as old JES ptr; and args are at [RDX+0] thru [RDX+n]
  // - [RSP+0] is ret adr, standard 8mod16 aligned.
  // RDI is a bonus return value, and is the same value as is passed on entry (if a objectRef).
  // 
  // When active - during execution:
  // - RDI is JES ptr; stack grows UP so RDI >= RDX
  // - RCX is TOS cache
  // - RSI is BCP
  // - RDX is locals in increasing order: JL0 is at [RDX+0], JL1 is at [RDX+8], etc.
  // - R08 is CPCacheOop (not ref)
  // - [RSP+24] is ret adr
  // - [RSP+16] is when the iframe is packed: 
  //            2 bytes of locals offset
  //            2 bytes of JES top offset
  //            2 bytes of BCI
  //            1 byte of return type
  //            1 byte of num locks held right now
  // - [RSP+ 8] is methodRef
  // - [RSP+ 0] is cpCacheRef
  // 
  // Floats: TOS float is kept in RCX like all other TOS values.  Reason to
  // not keep float in F0 is that the 'dup' bytecode does not know whether
  // to dup RCX or F0 (unless we duplicate the interpreter based on TOS
  // state - which isn't worth either the effort or the i-cache miss rate).
  //
  // Java Execution Stack
  // --------------------
  // RDI & RDX refer to the Java Execution Stack or JEXSTK.  This is seperate
  // from the normal C++/JIT'd/RSP stack.  It grows UP (not down is the normal
  // X86 stack) to make Java local var addressing easier.  All locals and JES
  // words are kept on it.  JEXSTK frames *almost* overlap: at a call the
  // outgoing args are "popped" by rolling back RDI before making the call;
  // the start address of the outgoing args is passed in RDX for the caller
  // and become the start of his locals area.  The JEXSTK lazily grows using
  // realloc; growth is limited to function entry.  Growing moves the stack;
  // "packing" interpreter registers converts direct addresses to offsets so
  // we can tolerate a move.  The actual stack base & max is kept in a Thread
  // local variable.  
  //
  // GC obviously hits the 2 words on the RSP stack, plus then does a tagged
  // stack crawl of the JEXSTK.  There is no per-frame breakdown needed for GC.
  // 
  // Lock Stack: Each thread also maintains a stack of locked objects.
  //


  // Stub generated frames:
  // ----------------------
  //
  // Stubs call into the C++ runtime.  They are called from compiled code
  // which might have all registers alive.  Argument registers are fixed and
  // known to the templates (and the register allocator).  RAX is always blown
  // (and generally contains a return result).  Same for XMM0/F00 - always
  // blown and may contain a return result.  All other registers are alive and
  // MAY CONTAIN OOPS.  While registers RBX, RBP, R12-R15 are not blown by the
  // C ABI, they still need to be read if a deopt or GC happens and thus must
  // be saved in a known place.  Thus typical runtime stubs (e.g. allocation
  // or slow-path locking) end up saving basically 14 GPRs and 15 FPRs.
  //
  // Leaf Call Optimizations: Leaf calls do not deopt and thus never need to
  // read (nor save/restore) RBX, RBP, R12-R15.  Also some leaf calls will
  // probably never blow most FPRs (e.g. sin/cos/arraycopy probably blow a
  // small known set of FPRs and no others) and the save/restore paths can
  // skip some registers in the name of speed.
  // 
  // Other Optimizations: It's obvious that most VM calls will never have any
  // FP values live across them, much less 15 FPRs.  In the interpreter, most
  // registers are really free.  For any really hot VM calls we can teach the
  // register allocator that that particular call blows some set of registers,
  // then save only a subset of registers - or have multiple entry points, one
  // that saves all registers and one that saves, e.g. only those the
  // interpreter cares about.
  //
  enum runtime_stub_frame_layout {
    // Frame Layout:
    xStkArgs = 0x110, //   incoming_stk_args - except no VM call needs more than 6 args?
    //                    --------------------      <--- 16b aligned
    xRetPC   = 0x108, //   return pc from 'call' op  <--- RSP on entry
    xPAD     = 0x100, //   16b alignment (sometimes flags)
    xRAX     = 0x0F8, //   RAX  - live or OFTEN RETURN VALUE
    xRCX     = 0x0F0, //   RCX  - arg3, or live (possible oop)
    xRDX     = 0x0E8, //   RDX  - arg2, or live (possible oop)
    xRBX     = 0x0E0, //   RBX  - callee save
    xRSP     = 0x0D8, //   RSP  - not really save/restored, but here for completeness
    xRBP     = 0x0D0, //   RBP  - callee save
    xRSI     = 0x0C8, //   RSI  - arg1, or live (possible oop)
    xRDI     = 0x0C0, //   RDI  - arg0, or live (possible oop)
    xR08     = 0x0B8, //   R08  - arg4, or live (possible oop)
    xR09     = 0x0B0, //   R09  - arg5, or live (possible oop)
    xR10     = 0x0A8, //   R10  - temp, but live across compiler templates
    xR11     = 0x0A0, //   R11  - temp, but live across compiler templates
    xR12     = 0x098, //   R12  - callee save
    xR13     = 0x090, //   R13  - callee save
    xR14     = 0x088, //   R14  - callee save
    xR15     = 0x080, //   R15  - callee save
    xF00     = 0x078, //   XMM0 - farg0, OFTEN RETURN VALUE
    xF01     = 0x070, //   XMM1 - farg1, or live
    xF02     = 0x068, //   XMM2 - farg2, or live
    xF03     = 0x060, //   XMM3 - farg3, or live
    xF04     = 0x058, //   XMM4 - farg4, or live
    xF05     = 0x050, //   XMM5 - farg5, or live
    xF06     = 0x048, //   XMM6 - farg6, or live
    xF07     = 0x040, //   XMM7 - farg7, or live
    xF08     = 0x038, //   XMM8 - float temp, live
    xF09     = 0x030, //   XMM9 - float temp, live
    xF10     = 0x028, //   XMM10- float temp, live
    xF11     = 0x020, //   XMM11- float temp, live
    xF12     = 0x018, //   XMM12- float temp, live
    xF13     = 0x010, //   XMM13- float temp, live
    xF14     = 0x008, //   XMM14- float temp, live
    xF15     = 0x000, //   XMM15- float temp, live
    runtime_stub_frame_size = xStkArgs
  };

private:

  // Is the given PC associated with the Interpreter or an entry stub?
  static bool is_interpreter_or_entry_pc(address pc);
public:
  frame(intptr_t* sp, address pc) : _sp(sp), _pc(pc) { }

  // The initial physical frame as apposed to "is_first_frame()" which is the
  // first virtual frame.
  inline bool is_initial_frame() const {return pc() == 0; }

  // Get a register from a frame
  intptr_t *register_addr( Register reg ) const {
    Unimplemented();
    return 0;
  }
  intptr_t get_reg( Register reg ) const { return *register_addr(reg); }

  // return the next instruction after the call from this frame
  inline address   pc() const { return _pc; }
  // return the stack pointer for this frame
  inline intptr_t* sp() const { return _sp; }
  // Interpreter frame convenience function
  inline IFrame *ifr() const { return (IFrame*)_sp; }

  // Tagged interpreter stack support

  enum InterpreterStackSlotType {
    single_slot_ref_type = 1,
    single_slot_primitive_type = 2,
    double_slot_primitive_type = 3,
    double_slot_primitive_type_empty_slot_id = -2 // Note that this must fit in 8 bits!
  };

  static InterpreterStackSlotType tag_at_address(intptr_t* addr);

  static void all_threads_print_xml_on(xmlBuffer *xb, int start, int stride);
  static int thread_print_xml_on(xmlBuffer *xb, intptr_t t);
  static int thread_id_print_xml_on(xmlBuffer *xb, uint64_t id);
  int print_xml_on(xmlBuffer *xb);

private:
  
  inline void interpreter_frame_set_tos_address(intptr_t* x);

#endif // FRAME_PD_HPP
