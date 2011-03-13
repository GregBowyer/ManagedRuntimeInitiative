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
#ifndef THREAD_PD_HPP
#define THREAD_PD_HPP


private:
  // In the safepoint trap handler blob, or any STA-related trap we want to be
  // able to save and find the high globals, allowing them to hold live oops
  // across such points.  These globals are not saved by the register window
  // mechanism, so we save them here.
  RegisterSaveArea* _saved_registers;

public:
  reg_t      saved_register                (Register reg) const { return _saved_registers->saved_register(reg); }
  reg_t*     saved_register_addr           (Register reg) const { return _saved_registers->saved_register_addr(reg); }
static int saved_registers_offset_in_bytes(){return offset_of(JavaThread,_saved_registers);}

  int sma_status() const { return 0; } // SMA not active on X86

#endif // THREAD_PD_HPP
