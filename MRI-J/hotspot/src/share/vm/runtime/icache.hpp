/*
 * Copyright 1997-2004 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef ICACHE_HPP
#define ICACHE_HPP

#include "allocation.hpp"

// Interface for updating the instruction cache.  Whenever the VM modifies
// code, part of the processor instruction cache potentially has to be flushed.

// Default implementation is in icache.cpp, and can be hidden per-platform.
// Most platforms must provide only ICacheStubGenerator::generate_icache_flush().
// Platforms that don't require icache flushing can just nullify the public
// members of AbstractICache in their ICache class.  AbstractICache should never
// be referenced other than by deriving the ICache class from it.
//
// The code for the ICache class and for generate_icache_flush() must be in
// architecture-specific files, i.e., icache_<arch>.hpp/.cpp

class AbstractICache : AllStatic {
 public:
  enum {
    stub_size      = 0, // Size of the icache flush stub in bytes
    line_size      = 0, // Icache line size in bytes
    log2_line_size = 0  // log2(line_size)
  };

  static void initialize();
  static void invalidate_word(address addr);
  static void invalidate_range(address start, int nbytes);
};

#include "icache_pd.hpp"
#endif // ICACHE_HPP
