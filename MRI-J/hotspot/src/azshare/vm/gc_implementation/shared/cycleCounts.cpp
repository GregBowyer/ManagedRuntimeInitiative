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

#include "atomic.hpp"
#include "cycleCounts.hpp"
#include "os.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "os_os.inline.hpp"


CycleCounts CycleCounts::commit_memory;
CycleCounts CycleCounts::protect_memory;
CycleCounts CycleCounts::relocate_memory;
CycleCounts CycleCounts::uncommit_memory;
CycleCounts CycleCounts::tlb_resync;
CycleCounts CycleCounts::unshatter_all;
CycleCounts CycleCounts::partial_unshatter;
CycleCounts CycleCounts::mbatch_start;
CycleCounts CycleCounts::mbatch_commit;


void CycleCounts::add_datum(long dataum)
{
  assert0(dataum>0);

  // Find a bucket;
  long bucket;
  for ( bucket=1; bucket<MaxBucket; bucket++ ) {
    if ( bucket_start[bucket] > dataum )
      break;
  }
  bucket --;

  Atomic::inc_ptr((intptr_t*)&counts);
  Atomic::add_ptr      (dataum, (intptr_t*)&cycles);
  Atomic::inc_ptr((intptr_t*)&cycle_buckets[bucket]);
}


void CycleCounts::print_all()
{
commit_memory.print("Commit Memory");
protect_memory.print("Protect Memory");
relocate_memory.print("Relocate Memory");
uncommit_memory.print("Uncommit Memory");
tlb_resync.print("TLB Resync");
unshatter_all.print("Unshatter All");
partial_unshatter.print("Partial Unshatter");
mbatch_start.print("Start Batch");
mbatch_commit.print("Commit Batch");
}


void CycleCounts::print(const char* title)
{
  long average = 0;
  if ( counts != 0 ) {
    average = cycles / counts;
  }

  gclog_or_tty->print_cr("%s cycle counts: "INT64_FORMAT" average, "INT64_FORMAT" cycles in "INT64_FORMAT" times",
                         title, average, cycles, counts);

  long cycles_per_usec = os::elapsed_frequency() / 1000000;

  for ( long i=0; i<MaxBucket; i++ ) {
if(cycle_buckets[i]>0){
      long next_bucket = (i+1==MaxBucket) ? 0 : bucket_start[i+1];
gclog_or_tty->print_cr("\t%9ld-%9ld usecs : "INT64_FORMAT" times",
                             bucket_start[i]/cycles_per_usec,
                             next_bucket/cycles_per_usec,
cycle_buckets[i]);
    }
  }
}


CycleCounts::CycleCounts()
{
}


void CycleCounts::reset_all()
{
commit_memory.reset();
protect_memory.reset();
relocate_memory.reset();
uncommit_memory.reset();
tlb_resync.reset();
unshatter_all.reset();
partial_unshatter.reset();
mbatch_start.reset();
mbatch_commit.reset();
}


void CycleCounts::reset()
{
  cycles = 0;
  counts = 0;

  for ( long i=0; i<MaxBucket; i++ ) {
cycle_buckets[i]=0;
  }

  assert0(os::elapsed_frequency() != 0);

  long usec = os::elapsed_frequency() / 1000000;
  long msec = os::elapsed_frequency() /    1000;
  long sec  = os::elapsed_frequency()          ;

  bucket_start[ 0] =     0 * usec;
  bucket_start[ 1] =     1 * usec;
  bucket_start[ 2] =     5 * usec;
  bucket_start[ 3] =    10 * usec;
  bucket_start[ 4] =    15 * usec;
  bucket_start[ 5] =    20 * usec;
  bucket_start[ 6] =    30 * usec;
  bucket_start[ 7] =    40 * usec;
  bucket_start[ 8] =    50 * usec;
  bucket_start[ 9] =    60 * usec;
  bucket_start[10] =    80 * usec;
  bucket_start[11] =   100 * usec;
  bucket_start[12] =   120 * usec;
  bucket_start[13] =   140 * usec;
  bucket_start[14] =   160 * usec;
  bucket_start[15] =   180 * usec;
  bucket_start[16] =   200 * usec;
  bucket_start[17] =   220 * usec;
  bucket_start[18] =   240 * usec;
  bucket_start[19] =   260 * usec;
  bucket_start[20] =   280 * usec;
  bucket_start[21] =   300 * usec;
  bucket_start[22] =   325 * usec;
  bucket_start[23] =   350 * usec;
  bucket_start[24] =   375 * usec;
  bucket_start[25] =   400 * usec;
  bucket_start[26] =   425 * usec;
  bucket_start[27] =   450 * usec;
  bucket_start[28] =   475 * usec;
  bucket_start[29] =   500 * usec;
  bucket_start[30] =   550 * usec;
  bucket_start[31] =   600 * usec;
  bucket_start[32] =   650 * usec;
  bucket_start[33] =   700 * usec;
  bucket_start[34] =   800 * usec;
  bucket_start[35] =   900 * usec;
  bucket_start[36] =     1 * msec;
  bucket_start[37] =     2 * msec;
  bucket_start[38] =     3 * msec;
  bucket_start[39] =     4 * msec;
  bucket_start[40] =     5 * msec;
  bucket_start[41] =     6 * msec;
  bucket_start[42] =     7 * msec;
  bucket_start[43] =     8 * msec;
  bucket_start[44] =    10 * msec;
  bucket_start[45] =    15 * msec;
  bucket_start[46] =    20 * msec;
  bucket_start[47] =    30 * msec;
  bucket_start[48] =    40 * msec;
  bucket_start[49] =    50 * msec;
  bucket_start[50] =    60 * msec;
  bucket_start[51] =   100 * msec;
  bucket_start[52] =   120 * msec;
  bucket_start[53] =   140 * msec;
  bucket_start[54] =   160 * msec;
  bucket_start[55] =   180 * msec;
  bucket_start[56] =   200 * msec;
  bucket_start[57] =   250 * msec;
  bucket_start[58] =   300 * msec;
  bucket_start[59] =   350 * msec;
  bucket_start[60] =   400 * msec;
  bucket_start[61] =   450 * msec;
  bucket_start[62] =   500 * msec;
  bucket_start[63] =   750 * msec;
  bucket_start[64] =     1 *  sec;
  bucket_start[65] =     5 *  sec;
  bucket_start[66] =    10 *  sec;
  bucket_start[67] =    25 *  sec;
  bucket_start[68] =    50 *  sec;
  bucket_start[69] =   100 *  sec;

  assert0(70 == MaxBucket);
}
