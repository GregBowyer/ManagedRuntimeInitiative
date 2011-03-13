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

#ifndef GPGC_RELOCATIONSPIKE_HPP
#define GPGC_RELOCATIONSPIKE_HPP

#include "allocation.hpp"
#include "atomic.hpp"

#include "atomic_os_pd.inline.hpp"


class GPGC_RelocationSpike VALUE_OBJ_CLASS_SPEC
{
  private:
    long _page_count;
    long _peak_value;

  public:
    GPGC_RelocationSpike() : _page_count(0), _peak_value(0) {}

    void reset()                         { _page_count = _peak_value = 0; }
    void add_and_record_peak(long delta) { Atomic::add_and_record_peak(delta, &_page_count, &_peak_value); }
    void subtract           (long delta) { Atomic::add_ptr(-1 * delta, &_page_count); }

    long page_count()                    { return _page_count; }
    long peak_value()                    { return _peak_value; }
};


#endif // GPGC_RELOCATIONSPIKE_HPP

