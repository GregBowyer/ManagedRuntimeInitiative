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
#ifndef INVERTEDVIRTUALSPACE_HPP
#define INVERTEDVIRTUALSPACE_HPP



#include "virtualspace.hpp"

// InvertedVirtualSpace is a VirtualSpace that grows downward
// It is used for housing the OopTable, which grows down from 
// the KTB pointer

class InvertedVirtualSpace : public VirtualSpace {

 public:
  // Initialization
  bool initialize(ReservedSpace rs, size_t committed_byte_size);
  
  // Operations
  bool expand_by(size_t bytes, bool pre_touch);    // returns true on success, false otherwise
  void shrink_by(size_t bytes);

};

#endif // INVERTEDVIRTUALSPACE_HPP
