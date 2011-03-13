/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GCSTATS_HPP
#define GCSTATS_HPP

#include "allocation.hpp"
#include "gcUtil.hpp"

class GCStats : public CHeapObj {
 protected:
  // Avg amount promoted; used for avoiding promotion undo
  // This class does not update deviations if the sample is zero.
  AdaptivePaddedNoZeroDevAverage*   _avg_promoted;

 public:
  GCStats();

  enum Name {
    GCStatsKind,
  };

  virtual Name kind() {
    return GCStatsKind;
  }

  AdaptivePaddedNoZeroDevAverage*  avg_promoted() const { return _avg_promoted; }

  // Average in bytes
  size_t average_promoted_in_bytes() const {
    return (size_t)_avg_promoted->average();
  }

  // Padded average in bytes
  size_t padded_average_promoted_in_bytes() const {
    return (size_t)_avg_promoted->padded_average();
  }
};

#endif // GCSTATS_HPP

