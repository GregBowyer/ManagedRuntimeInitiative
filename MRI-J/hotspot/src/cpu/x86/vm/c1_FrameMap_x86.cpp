/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "c1_FrameMap.hpp"
#include "register_pd.hpp"
#include "sharedRuntime.hpp"
#include "vreg.hpp"

#include "register_pd.inline.hpp"
#include "oop.inline.hpp"

// Frame for C1 methods
//
// | ..................... |
// | Incoming non-reg args |
// |         ...           | <- Old SP
// |-----------------------|
// | Return address        |
// |-----------------------|
// | Old frame pointer     | <- FP
// |-----------------------|
// | Callee save registers |
// |-----------------------|
// | Monitor N             |
// |         ...           |
// | Monitor 0             |
// |-----------------------|
// | Spill N               |
// |         ...           |
// | Spill 0               |
// |-----------------------|
// | JES(s) for debug      | <- SP + reserved_argument_area_size
// |         ...           |
// |-----------------------|
// | Outgoing non-reg args |
// |         ...           | <- SP (Old SP - frame size)
// |-----------------------|
// |    xxxx HOLE xxxx     |
// |-----------------------|
// | Saved frame pointer   |
// | ..................... |
//

bool FrameMap::validate_frame() {
  return (sp_offset_for_callee_save(0)           >= sp_offset_for_monitor_object(0)) &&
         (sp_offset_for_monitor_object(0)        >= sp_offset_for_spill(0)) &&
         (in_bytes(sp_offset_for_spill(0))       >= in_bytes(_reserved_argument_area_size)) &&
         (in_bytes(_reserved_argument_area_size) >= 0);
}

FRegister FrameMap::nr2xmmreg(int rnr){
  assert(_init_done, "tables not initialized");
  assert(rnr >= 0 && rnr < nof_xmm_regs, "out of bounds");
  return _xmm_regs[rnr];
}

Address FrameMap::make_new_address(ByteSize sp_offset) const {
return Address(RSP,in_bytes(sp_offset));
}

LIR_Opr FrameMap::stack_pointer() {
  return FrameMap::rsp_opr;
}

LIR_Opr FrameMap::map_to_opr(BasicType type, VReg::VR vreg, bool) {
  LIR_Opr opr = LIR_OprFact::illegalOpr;
  if (VReg::is_stack(vreg)) {
    // Convert stack slot to an SP offset
    int st_off = VReg::reg2stk(vreg);
    opr = LIR_OprFact::address(new LIR_Address(rsp_opr, st_off, type));
  } else if (is_gpr(vreg)) {
    Register reg = reg2gpr(vreg);
if(type==T_LONG){
opr=as_long_opr(reg,reg);
    } else if (type == T_OBJECT) {
      opr = as_oop_opr(reg);
    } else {
      opr = as_opr(reg);
    }
  } else if (is_fpr(vreg)) {
    assert(type == T_DOUBLE || type == T_FLOAT, "wrong type");
    int num = reg2fpr(vreg) - nof_registers;
    if (type == T_FLOAT) {
      opr = LIR_OprFact::single_xmm(num);
    } else {
      opr = LIR_OprFact::double_xmm(num);
    }
  } else {
    ShouldNotReachHere();
  }
  return opr;
}

const int FrameMap::pd_c_runtime_reserved_arg_size = 0;

LIR_Opr FrameMap::rsi_opr;
LIR_Opr FrameMap::rdi_opr;
LIR_Opr FrameMap::rbx_opr;
LIR_Opr FrameMap::rax_opr;
LIR_Opr FrameMap::rdx_opr;
LIR_Opr FrameMap::rcx_opr;
LIR_Opr FrameMap::rsp_opr;
LIR_Opr FrameMap::rbp_opr;
LIR_Opr FrameMap::r08_opr;
LIR_Opr FrameMap::r09_opr;
LIR_Opr FrameMap::r10_opr;
LIR_Opr FrameMap::r11_opr;
LIR_Opr FrameMap::r12_opr;
LIR_Opr FrameMap::r13_opr;
LIR_Opr FrameMap::r14_opr;
LIR_Opr FrameMap::r15_opr;

LIR_Opr FrameMap::rsi_long_opr;
LIR_Opr FrameMap::rdi_long_opr;
LIR_Opr FrameMap::rbx_long_opr;
LIR_Opr FrameMap::rax_long_opr;
LIR_Opr FrameMap::rdx_long_opr;
LIR_Opr FrameMap::rcx_long_opr;
LIR_Opr FrameMap::rsp_long_opr;
LIR_Opr FrameMap::rbp_long_opr;
LIR_Opr FrameMap::r08_long_opr;
LIR_Opr FrameMap::r09_long_opr;
LIR_Opr FrameMap::r10_long_opr;
LIR_Opr FrameMap::r11_long_opr;
LIR_Opr FrameMap::r12_long_opr;
LIR_Opr FrameMap::r13_long_opr;
LIR_Opr FrameMap::r14_long_opr;
LIR_Opr FrameMap::r15_long_opr;

LIR_Opr FrameMap::receiver_opr;

LIR_Opr FrameMap::rsi_oop_opr;
LIR_Opr FrameMap::rdi_oop_opr;
LIR_Opr FrameMap::rax_oop_opr;
LIR_Opr FrameMap::rdx_oop_opr;
LIR_Opr FrameMap::rcx_oop_opr;
LIR_Opr FrameMap::r08_oop_opr;
LIR_Opr FrameMap::r09_oop_opr;
LIR_Opr FrameMap::r10_oop_opr;
LIR_Opr FrameMap::r11_oop_opr;
LIR_Opr FrameMap::r12_oop_opr;
LIR_Opr FrameMap::r13_oop_opr;
LIR_Opr FrameMap::r14_oop_opr;
LIR_Opr FrameMap::r15_oop_opr;

LIR_Opr FrameMap::xmm0_float_opr;
LIR_Opr FrameMap::xmm0_double_opr;
LIR_Opr FrameMap::xmm15_float_opr;
LIR_Opr FrameMap::xmm15_double_opr;

LIR_Opr FrameMap::_caller_save_cpu_regs[] = { 0, };
LIR_Opr FrameMap::_caller_save_xmm_regs[] = { 0, };

FRegister FrameMap::_xmm_regs [nof_xmm_regs] = { F00, };

// Initialise FrameMap static data
void FrameMap::init() {
  if (_init_done) return;
MutexLocker ml(Patching_lock);
  if (_init_done) return;

assert(nof_cpu_regs==16,"wrong number of CPU registers");
  map_register( 0, RAX);  rax_opr = LIR_OprFact::single_cpu( 0);  rax_long_opr = LIR_OprFact::double_cpu( 0, 0);   rax_oop_opr = LIR_OprFact::single_cpu_oop( 0);
  map_register( 1, RCX);  rcx_opr = LIR_OprFact::single_cpu( 1);  rcx_long_opr = LIR_OprFact::double_cpu( 1, 1);   rcx_oop_opr = LIR_OprFact::single_cpu_oop( 1);
  map_register( 2, RDX);  rdx_opr = LIR_OprFact::single_cpu( 2);  rdx_long_opr = LIR_OprFact::double_cpu( 2, 2);   rdx_oop_opr = LIR_OprFact::single_cpu_oop( 2);
  map_register( 3, RBX);  rbx_opr = LIR_OprFact::single_cpu( 3);  rbx_long_opr = LIR_OprFact::double_cpu( 3, 3); 
  map_register( 4, RSP);  rsp_opr = LIR_OprFact::single_cpu( 4);  rsp_long_opr = LIR_OprFact::double_cpu( 4, 4); 
  map_register( 5, RBP);  rbp_opr = LIR_OprFact::single_cpu( 5);  rbp_long_opr = LIR_OprFact::double_cpu( 5, 5); 
  map_register( 6, RSI);  rsi_opr = LIR_OprFact::single_cpu( 6);  rsi_long_opr = LIR_OprFact::double_cpu( 6, 6);   rsi_oop_opr = LIR_OprFact::single_cpu_oop( 6);
  map_register( 7, RDI);  rdi_opr = LIR_OprFact::single_cpu( 7);  rdi_long_opr = LIR_OprFact::double_cpu( 7, 7);   rdi_oop_opr = LIR_OprFact::single_cpu_oop( 7);
  map_register( 8, R08);  r08_opr = LIR_OprFact::single_cpu( 8);  r08_long_opr = LIR_OprFact::double_cpu( 8, 8);   r08_oop_opr = LIR_OprFact::single_cpu_oop( 8);
  map_register( 9, R09);  r09_opr = LIR_OprFact::single_cpu( 9);  r09_long_opr = LIR_OprFact::double_cpu( 9, 9);   r09_oop_opr = LIR_OprFact::single_cpu_oop( 9);
  map_register(10, R10);  r10_opr = LIR_OprFact::single_cpu(10);  r10_long_opr = LIR_OprFact::double_cpu( 10, 10); r10_oop_opr = LIR_OprFact::single_cpu_oop(10);
  map_register(11, R11);  r11_opr = LIR_OprFact::single_cpu(11);  r11_long_opr = LIR_OprFact::double_cpu( 11, 11); r11_oop_opr = LIR_OprFact::single_cpu_oop(11);
  map_register(12, R12);  r12_opr = LIR_OprFact::single_cpu(12);  r12_long_opr = LIR_OprFact::double_cpu( 12, 12); r12_oop_opr = LIR_OprFact::single_cpu_oop(12);
  map_register(13, R13);  r13_opr = LIR_OprFact::single_cpu(13);  r13_long_opr = LIR_OprFact::double_cpu( 13, 13); r13_oop_opr = LIR_OprFact::single_cpu_oop(13);
  map_register(14, R14);  r14_opr = LIR_OprFact::single_cpu(14);  r14_long_opr = LIR_OprFact::double_cpu( 14, 14); r14_oop_opr = LIR_OprFact::single_cpu_oop(14);
  map_register(15, R15);  r15_opr = LIR_OprFact::single_cpu(15);  r15_long_opr = LIR_OprFact::double_cpu( 15, 15); r15_oop_opr = LIR_OprFact::single_cpu_oop(15);

  xmm0_float_opr   = LIR_OprFact::single_xmm(0);
  xmm0_double_opr  = LIR_OprFact::double_xmm(0);
xmm15_float_opr=LIR_OprFact::single_xmm(15);
xmm15_double_opr=LIR_OprFact::double_xmm(15);

  // X86-64 ABI: RBX, RBP, RSP, R12-R15 all preserved across a call
_caller_save_cpu_regs[0]=rax_opr;
_caller_save_cpu_regs[1]=rcx_opr;
_caller_save_cpu_regs[2]=rdx_opr;
_caller_save_cpu_regs[3]=rsi_opr;
_caller_save_cpu_regs[4]=rdi_opr;
_caller_save_cpu_regs[5]=r08_opr;
_caller_save_cpu_regs[6]=r09_opr;
_caller_save_cpu_regs[7]=r10_opr;
_caller_save_cpu_regs[8]=r11_opr;

_xmm_regs[0]=F00;
_xmm_regs[1]=F01;
_xmm_regs[2]=F02;
_xmm_regs[3]=F03;
_xmm_regs[4]=F04;
_xmm_regs[5]=F05;
_xmm_regs[6]=F06;
_xmm_regs[7]=F07;
_xmm_regs[8]=F08;
_xmm_regs[9]=F09;
_xmm_regs[10]=F10;
_xmm_regs[11]=F11;
_xmm_regs[12]=F12;
_xmm_regs[13]=F13;
_xmm_regs[14]=F14;
_xmm_regs[15]=F15;

for(int i=0;i<nof_xmm_regs;i++){
    _caller_save_xmm_regs[i] = LIR_OprFact::single_xmm(i);
  }

  _init_done = true;

  VReg::VR reg;
  BasicType sig_bt = T_OBJECT;
SharedRuntime::java_calling_convention(&sig_bt,&reg,1,true);
  receiver_opr = as_oop_opr(reg2gpr(reg));
assert(receiver_opr==rdi_oop_opr,"rcvr ought to be rdi");
}
