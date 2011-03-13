/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "interpreter_pd.hpp"
#include "register_pd.hpp"

#include "allocation.inline.hpp"
#include "oop.inline.hpp"
#include "register_pd.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

const char* raw_reg_name_strs[nof_registers+nof_float_registers] = {
  "rax" , "rcx" , "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
"r08","r09","r10","r11","r12","r13","r14","r15",
"f00","f01","f02","f03","f04","f05","f06","f07",
"f08","f09","f10","f11","f12","f13","f14","f15",
};
const char* raw_reg_name(Register reg) {
  return is_valid_reg(reg) ? raw_reg_name_strs[reg] : "noreg";
}
static const char* raw_fp_reg_name_strs[nof_registers] = {
  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
  ,"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
};
const char* raw_reg_name(FRegister reg) {
  return ((reg >= F00 && reg <= F00 + nof_float_registers) ? 
           raw_fp_reg_name_strs[reg-F00] : "noreg");
}


static const char* x86_reg_name_strs[nof_registers] = {
  "rax" , "rcx" , "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
"r08","r09","r10","r11","r12","r13","r14","r15"
};
const char* x86_reg_name(Register reg) {
  return is_valid_reg(reg) ? x86_reg_name_strs[reg] : "noreg";
}

// No interpreter registers yet!
static const char* interpreter_reg_name_strs[nof_registers] = {
  "rax" , "rcx" , "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
"r08","r09","r10","r11","r12","r13","r14","r15"
};
const char* interpreter_reg_name(Register reg) {
  return is_valid_reg(reg) ? interpreter_reg_name_strs[reg] : "noreg";
}

static const char* full_reg_name_strs[2*nof_registers] = {
  // first set are general registers
  "rax" , "rcx" , "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
"r08","r09","r10","r11","r12","r13","r14","r15",
  // second set are interpreter registers
"rax","rcx_tos","rdx_loc","rbx","rsp","rbp","rsi_bcp","rdi_jex",
"r08_cpc","r09","r10","r11","r12","r13","r14","r15"
};
const char* full_reg_name(Register reg, address pc) {
  return is_valid_reg(reg) ? full_reg_name_strs[(Interpreter::contains(pc) ? nof_registers : 0) + reg] : "noreg";
}

void VReg::print_on( VReg::VR r, outputStream *st ) {
  if( !is_valid(r) ) st->print("rBad");
  else if( is_stack(r) ) st->print("[rsp+%d]",reg2stk(r));
  else if( is_gpr(r) ) st->print(raw_reg_name_strs[reg2gpr(r)]);
  else st->print(raw_fp_reg_name_strs[reg2fpr(r)]);
}

RegisterSaveArea::RegisterSaveArea() {
  for( int i = 0; i < nof_registers; i++) _saved_registers[i] = -1;
  _rsp     = 0;
  _rip     = 0;
}

