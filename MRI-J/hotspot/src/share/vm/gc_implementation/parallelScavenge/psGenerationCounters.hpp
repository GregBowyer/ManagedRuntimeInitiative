/*
 * Copyright 2004 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PSGENERATIONCOUNTERS_HPP
#define PSGENERATIONCOUNTERS_HPP

#include "generationCounters.hpp"
#include "psVirtualspace.hpp"
#include "resourceArea.hpp"

// A PSGenerationCounter is a holder class for performance counters
// that track a generation

class PSGenerationCounters: public GenerationCounters {
  friend class VMStructs;

 private:
  PSVirtualSpace*      _ps_virtual_space;

 public:
  PSGenerationCounters(const char* name, int ordinal, int spaces,
                     PSVirtualSpace* v);

  void update_all() {
    assert(_virtual_space == NULL, "Only one should be in use");
    _current_size->set_value(_ps_virtual_space->committed_size());
  }
};

#endif // PSGENERATIONCOUNTERS_HPP

