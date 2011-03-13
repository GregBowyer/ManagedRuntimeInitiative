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
#ifndef REGISTER_PD_INLINE_HPP
#define REGISTER_PD_INLINE_HPP


#include "register_pd.hpp"
#include "vreg.hpp"

static bool is_gpr( VReg::VR reg ) { return (int)reg >= 0 && (int)reg < nof_registers; }
static bool is_fpr( VReg::VR reg ) { return (int)reg >= nof_registers && (int)reg < nof_registers+nof_float_registers; }
 
static  Register reg2gpr( VReg::VR reg ) { assert0(is_gpr(reg)); return Register(reg); }
static FRegister reg2fpr( VReg::VR reg ) { assert0(is_fpr(reg)); return FRegister(reg); }
static VReg::VR gpr2reg(  Register reg ) { return VReg::VR(reg); }
static VReg::VR fpr2reg( FRegister reg ) { return VReg::VR(reg); }
static VOopReg::VR gpr2oopreg( Register reg ) { return VOopReg::VR(reg); }

#endif // REGISTER_PD_INLINE_HPP
