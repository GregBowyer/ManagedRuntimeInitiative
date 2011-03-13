/*
 * Copyright 2002-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GCPOLICYCOUNTERS_HPP
#define GCPOLICYCOUNTERS_HPP

#include "allocation.hpp"
#include "perfData.hpp"

// GCPolicyCounters is a holder class for performance counters
// that track a generation

class GCPolicyCounters: public CHeapObj {
  private:

    // Constant PerfData types don't need to retain a reference.
    // However, it's a good idea to document them here.
    // PerfStringConstant*     _name;
    // PerfStringConstant*     _collector_size;
    // PerfStringConstant*     _generation_size;

    PerfVariable*     _tenuring_threshold;
    PerfVariable*     _desired_survivor_size;

    const char* _name_space;

 public:

  enum Name {
    NONE,
    GCPolicyCountersKind,
    GCAdaptivePolicyCountersKind,
PSGCAdaptivePolicyCountersKind
  };

  GCPolicyCounters(const char* name, int collectors, int generations);

    inline PerfVariable* tenuring_threshold() const  {
      return _tenuring_threshold;
    }

    inline PerfVariable* desired_survivor_size() const  {
      return _desired_survivor_size;
    }

    const char* name_space() const { return _name_space; }

    virtual void update_counters() {}

    virtual GCPolicyCounters::Name kind() const { 
      return GCPolicyCounters::GCPolicyCountersKind; 
    }
};

#endif // GCPOLICYCOUNTERS_HPP

