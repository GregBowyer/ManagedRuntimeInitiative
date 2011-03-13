/*
 * Copyright 2000-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_DEFS_PD_HPP
#define C1_DEFS_PD_HPP


// native word offsets from memory address (little endian)
enum {
  pd_lo_word_offset_in_bytes = 0,
  pd_hi_word_offset_in_bytes = BytesPerWord
};


// registers
enum {
pd_nof_cpu_regs_frame_map=16,//number of registers used during code emission
pd_nof_xmm_regs_frame_map=16,//number of registers used during code emission
pd_nof_caller_save_cpu_regs_frame_map=9,//number of registers killed by calls
pd_nof_caller_save_xmm_regs_frame_map=16,//number of registers killed by calls
  
pd_nof_cpu_regs_reg_alloc=16,//number of registers that are visible to register allocator

pd_nof_cpu_regs_linearscan=16,//number of registers visible to linear scan
pd_nof_xmm_regs_linearscan=16,//number of registers visible to linear scan
  pd_nof_extra_regs_linearscan = 1, // fix to make linear scan registers == ConcreteRegisterImpl
  pd_first_cpu_reg = 0,
pd_last_cpu_reg=15,
pd_first_xmm_reg=pd_nof_cpu_regs_frame_map,
pd_last_xmm_reg=pd_first_xmm_reg+15
};

#endif // C1_DEFS_PD_HPP
