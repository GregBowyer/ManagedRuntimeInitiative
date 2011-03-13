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


#ifndef AZ_PROXIED // FIXME - Need to reconcile az_pgroup.h and process.h
#include <aznix/az_pgroup.h>
#include <os/process.h>
#else // AZ_PROXIED
#include <os/process.h>
#include <os/memstats.hpp>
#endif // AZ_PROXIED
#include <os/shared_user_data.h>

#include "vm_version_pd.hpp"
#include "arguments.hpp"
#include "safepoint.hpp"
#include "universe.hpp"
#include "collectedHeap.hpp"
#include "sharedUserData.hpp"

#ifdef AZ_PROXIED
#include <transport/atcpn.h>
#endif


#define SET_SUD_JVM_CONF_FIELD(name, str) { \
  const char *s = (str); \
  strncpy(jvm_conf.name, s ? s : "", sizeof(jvm_conf.name)); \
  jvm_conf.name[sizeof(jvm_conf.name)-1] = '\0'; \
}

void SharedUserData::init(){
  // Write out our configuration.
  sud_jvm_conf_rev1_t jvm_conf;
  memset(&jvm_conf, 0, sizeof(jvm_conf));
  jvm_conf.revision = SUD_JVM_CONF_REVISION;
  SET_SUD_JVM_CONF_FIELD(name,                  VM_Version::vm_name());
  SET_SUD_JVM_CONF_FIELD(vendor,                VM_Version::vm_vendor());
  SET_SUD_JVM_CONF_FIELD(info,                  VM_Version::vm_info_string());
  SET_SUD_JVM_CONF_FIELD(release,               VM_Version::vm_release());
  SET_SUD_JVM_CONF_FIELD(internal_info,         VM_Version::internal_vm_info_string());
  SET_SUD_JVM_CONF_FIELD(flags,                 Arguments::jvm_flags());
  SET_SUD_JVM_CONF_FIELD(args,                  Arguments::jvm_args());
  SET_SUD_JVM_CONF_FIELD(specification_version, "1.0");
  SET_SUD_JVM_CONF_FIELD(specification_name,    "Java Virtual Machine Specification");
  SET_SUD_JVM_CONF_FIELD(specification_vendor,  "Sun Microsystems Inc.");
  SET_SUD_JVM_CONF_FIELD(ext_dirs,              Arguments::get_ext_dirs());
  SET_SUD_JVM_CONF_FIELD(endorsed_dirs,         Arguments::get_endorsed_dir());
  SET_SUD_JVM_CONF_FIELD(library_path,          Arguments::get_library_path());
  SET_SUD_JVM_CONF_FIELD(java_home,             Arguments::get_java_home());
  SET_SUD_JVM_CONF_FIELD(classpath,             Arguments::get_property("java.class.path"));
  SET_SUD_JVM_CONF_FIELD(boot_library_path,     Arguments::get_dll_dir());
  SET_SUD_JVM_CONF_FIELD(boot_classpath,        Arguments::get_sysclasspath());
  SET_SUD_JVM_CONF_FIELD(command,               Arguments::java_command());

  az_allocid_t allocid = process_get_allocationid();
  sys_return_t ret = shared_user_data_set_jvm_conf_rev1 (allocid, &jvm_conf);
  if (ret != SYSERR_NONE) warning("Failed to set jvm_conf shared user data (%d)", ret);
}

void SharedUserData::engage(){
  (new SharedUserData())->enroll();
}

SharedUserData::SharedUserData() : PeriodicTask(1000) {}

void SharedUserData::task(){
#ifdef AZ_PROXIED
  // Static variables store peak values seen during the life of the run.
  static volatile sud_jvm_heap_rev1_t peak_jvm_heap;
  static sud_io_rev1_t io_stats;
  static volatile bool initialized = false;
  if (!initialized) {
    memset ((void*)(&peak_jvm_heap), 0, sizeof(peak_jvm_heap));
    initialized = true;
  }

  if (SafepointSynchronize::is_at_safepoint()) return;

  CollectedHeap *heap = Universe::heap();
  if (!heap) return;

  size_t l = heap->last_gc_live_bytes();
size_t u=heap->used();
  size_t c = heap->capacity();
  size_t m = heap->max_capacity();
  size_t pu = heap->permanent_used();
  size_t pc = heap->permanent_capacity();

  // Make sure that the numbers make sense when graphing.
  c = (u > c) ? u : c;
  m = (c > m) ? c : m;
  pc = (pu > pc) ? pu : pc;

  sud_jvm_heap_rev1_t jvm_heap;
  memset(&jvm_heap, 0, sizeof(jvm_heap));
  jvm_heap.revision = SUD_JVM_HEAP_REVISION;
  switch (heap->kind()) {
  case CollectedHeap::GenCollectedHeap: strcpy(jvm_heap.name, "GenCollectedHeap"); break;
  case CollectedHeap::ParallelScavengeHeap: strcpy(jvm_heap.name, "ParallelScavengeHeap"); break;
  case CollectedHeap::PauselessHeap: strcpy(jvm_heap.name, "PauselessHeap"); break;
  default: strcpy(jvm_heap.name, "");
  }
  if (heap->supports_tlab_allocation()) jvm_heap.flags |= SUD_JVM_HEAP_FLAG_TLAB_ALLOCATION;
  if (heap->supports_inline_contig_alloc()) jvm_heap.flags |= SUD_JVM_HEAP_FLAG_INLINE_CONTIG_ALLOC;

  uint64_t now = (uint64_t) os::javaTimeMillis();
  jvm_heap.timestamp_ms = now;
  jvm_heap.live_bytes = l;
  jvm_heap.used_bytes = u;
  jvm_heap.capacity_bytes = c;
  jvm_heap.max_capacity_bytes = m;
  jvm_heap.permanent_used_bytes = pu;
  jvm_heap.permanent_capacity_bytes = pc;
  jvm_heap.total_collections = heap->total_collections();

  libos::AccountInfo ai;
  az_allocid_t allocid = process_get_allocationid();
  sys_return_t ret = ai.inspectProcess (allocid);
  if (ret == SYSERR_NONE) {
    // Copy memory_accounting information into the sud structure.
    // Take care not to overflow the accounts past the maximum storable.
    const account_info_t *account_info = ai.getAccountInfo();
    uint64_t count =
      (account_info->ac_count < SUD_MAX_ACCOUNTS) ?
      account_info->ac_count :
      SUD_MAX_ACCOUNTS;
    jvm_heap.account_info.ac_count = count;
    for (uint64_t i = 0; i < count; i++) {
      jvm_heap.account_info.ac_array[i] = account_info->ac_array[i];
    }
  }
  else {
warning("Failed to inspect memory accounting info (%d)",ret);
  }

#define UPDATE_PEAK(struct_member,value) \
  if (peak_jvm_heap.peak_ ## struct_member ## _bytes < value) { \
    peak_jvm_heap.peak_ ## struct_member ## _bytes = value; \
    peak_jvm_heap.peak_ ## struct_member ## _timestamp_ms = now; \
  } \
  jvm_heap.peak_ ## struct_member ## _bytes = peak_jvm_heap.peak_ ## struct_member ## _bytes; \
  jvm_heap.peak_ ## struct_member ## _timestamp_ms = peak_jvm_heap.peak_ ## struct_member ## _timestamp_ms;

  UPDATE_PEAK (live,l);
  UPDATE_PEAK (used,u);
  UPDATE_PEAK (capacity,c);
  UPDATE_PEAK (max_capacity,m);
  UPDATE_PEAK (permanent_used,pu);
  UPDATE_PEAK (permanent_capacity,pc);

  UPDATE_PEAK (allocated,ai.getAllocatedBytes());
  UPDATE_PEAK (funded,ai.getFundedBytes());
  UPDATE_PEAK (overdraft,ai.getOverdraftBytes());
  UPDATE_PEAK (footprint,ai.getFootprintBytes());

  UPDATE_PEAK (committed,ai.getCommittedBytes());
  UPDATE_PEAK (grant,ai.getGrantBytes());
  UPDATE_PEAK (allocated_from_committed,ai.getAllocatedFromCommittedBytes());

  UPDATE_PEAK (default_allocated,ai.getDefaultAllocatedBytes());
  UPDATE_PEAK (default_committed,ai.getDefaultCommittedBytes());
  UPDATE_PEAK (default_footprint,ai.getDefaultFootprintBytes());
  UPDATE_PEAK (default_grant,ai.getDefaultGrantBytes());

  UPDATE_PEAK (heap_allocated,ai.getHeapAllocatedBytes());
  UPDATE_PEAK (heap_committed,ai.getHeapCommittedBytes());
  UPDATE_PEAK (heap_footprint,ai.getHeapFootprintBytes());
  UPDATE_PEAK (heap_grant,ai.getHeapGrantBytes());

  ret = shared_user_data_set_jvm_heap_rev1 (allocid, &jvm_heap);
  if (ret != SYSERR_NONE) warning("Failed to set jvm_heap shared user data (%d)", ret);

  memset ((void*)(&io_stats), 0, sizeof(io_stats));
  io_stats.revision = SUD_IO_REVISION;
  atcpn_stats_get_io_rev1(&io_stats);
  ret = shared_user_data_set_io_rev1 (allocid, &io_stats);
  if (ret != SYSERR_NONE) warning("Failed to set io_stats shared user data (%d)", ret);
#endif // AZ_PROXIED
}

