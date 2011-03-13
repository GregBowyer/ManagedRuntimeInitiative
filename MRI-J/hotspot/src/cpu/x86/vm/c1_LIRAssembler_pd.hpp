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
#ifndef C1_LIRASSEMBLER_PD_HPP
#define C1_LIRASSEMBLER_PD_HPP


 private:

  void arithmetic_div(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr temp, LIR_Opr result, CodeEmitInfo* info, bool is_long);

  Address::ScaleFactor array_element_size(BasicType type) const;

  void monitorexit(LIR_Opr obj_opr, LIR_Opr lock_opr, Register new_hdr, int monitor_no, Register exception);

  void arith_fpu_implementation(LIR_Code code, int left_index, int right_index, int dest_index);

  void const2reg  (LIR_Opr src, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info, LIR_Opr tmp1, LIR_Opr tmp2, bool destroy_flags);

public:

  enum { call_stub_size = 15,
         exception_handler_size = DEBUG_ONLY(1*K) NOT_DEBUG(175),
         deopt_handler_size = 10
       };

#endif // C1_LIRASSEMBLER_PD_HPP
