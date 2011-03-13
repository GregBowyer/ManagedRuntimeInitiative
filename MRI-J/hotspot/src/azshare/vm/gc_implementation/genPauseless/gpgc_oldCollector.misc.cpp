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

#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "timer.hpp"

#include "gpgc_pageInfo.inline.hpp"
#include "os_os.inline.hpp"

void GPGC_OldCollector::reset_concurrency()
{
  _start_global_safepoint         = false;
  _start_1st_mark_remap_safepoint = true;
  _end_1st_mark_remap_safepoint   = true;
  _start_2nd_mark_remap_safepoint = true;
  _end_2nd_mark_remap_safepoint   = true;
  _start_3rd_mark_remap_safepoint = true;
  _end_3rd_mark_remap_safepoint   = true;
  _start_mark_verify_safepoint    = true;
  _end_mark_verify_safepoint      = true;
  _end_mark_phase_safepoint       = false;
  _start_relocation_safepoint     = true;
  _end_relocation_safepoint       = true;
  _start_relocation_safepoint2    = true;
  _end_relocation_safepoint2      = true;
  _end_global_safepoint           = false;

  if (GPGCSafepointAll) {
guarantee(!GPGCSafepointMark,"Can't set both GPGCSafepointAll and GPGCSafepointMark");
guarantee(!GPGCSafepointRelocate,"Can't set both GPGCSafepointAll and GPGCSafepointRelocate");

    _start_global_safepoint         = false;
    _start_1st_mark_remap_safepoint = true;
    _end_1st_mark_remap_safepoint   = false;
    _start_2nd_mark_remap_safepoint = false;
    _end_2nd_mark_remap_safepoint   = false;
    _start_3rd_mark_remap_safepoint = false;
    _end_3rd_mark_remap_safepoint   = false;
    _start_mark_verify_safepoint    = false;
    _end_mark_verify_safepoint      = false;
    _end_mark_phase_safepoint       = false;
    _start_relocation_safepoint     = false;
    _end_relocation_safepoint       = false;
    _start_relocation_safepoint2    = false;
    _end_relocation_safepoint2      = false;
    _end_global_safepoint           = true;
  }
  if (GPGCSafepointMark) {
guarantee(!GPGCSafepointAll,"Can't set both GPGCSafepointMark and GPGCSafepointAll");
guarantee(!GPGCSafepointRelocate,"Can't set both GPGCSafepointMark and GPGCSafepointRelocate");

    _end_1st_mark_remap_safepoint   = false;
    _start_2nd_mark_remap_safepoint = false;
    _end_2nd_mark_remap_safepoint   = false;
    _start_3rd_mark_remap_safepoint = false;
    _end_3rd_mark_remap_safepoint   = false;
    _start_mark_verify_safepoint    = false;
    _end_mark_verify_safepoint      = false;
    _end_mark_phase_safepoint       = true;
  }
  if (GPGCSafepointRelocate) {
guarantee(!GPGCSafepointAll,"Can't set both GPGCSafepointRelocate and GPGCSafepointAll");
guarantee(!GPGCSafepointMark,"Can't set both GPGCSafepointRelocate and GPGCSafepointMark");

    _start_relocation_safepoint     = true;
    _end_relocation_safepoint       = false;
    _start_relocation_safepoint2    = false;
    _end_relocation_safepoint2      = false;
    _end_global_safepoint           = true;
  }
}


jlong GPGC_OldCollector::millis_since_last_gc()
{
jlong now=os::elapsed_counter();

  if ( now < _time_of_last_gc ) {
    return 0;
  }

  jlong ret_val = (now - _time_of_last_gc) * 1000 / os::elapsed_frequency();

  return ret_val;
}


void GPGC_OldCollector::reset_millis_since_last_gc()
{
_time_of_last_gc=os::elapsed_counter();
}
