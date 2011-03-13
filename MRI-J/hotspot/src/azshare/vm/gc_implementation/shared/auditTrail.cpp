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


#include "allocation.hpp"
#include "auditTrail.hpp"
#include "thread.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "os_os.inline.hpp"

const char* AuditTrail::tag_names[AuditTrail::TAGS_MAX] = {
"UNDEFINED",
"GPGC_START_LVB_CLEAN",
"GPGC_END_LVB_CLEAN",
"GPGC_START_NEW_GC_CLEAN",
"GPGC_END_NEW_GC_CLEAN",
"GPGC_START_OLD_GC_CLEAN",
"GPGC_END_OLD_GC_CLEAN",
"GPGC_START_NEW_CKP_CLEAN",
"GPGC_END_NEW_CKP_CLEAN",
"GPGC_START_OLD_CKP_CLEAN",
"GPGC_END_OLD_CKP_CLEAN",
"GPGC_NEW_GC_SKIP_CLEAN",
"GPGC_OLD_GC_SKIP_CLEAN",
"GPGC_START_NEW_GC_TASK",
"GPGC_END_NEW_GC_TASK",
"GPGC_START_OLD_GC_TASK",
"GPGC_END_OLD_GC_TASK",
"GPGC_START_OLD_GC_CB_TASK",
"GPGC_END_OLD_GC_CB_TASK",
"INTERPRETED_FRAME",
"ENTRY_FRAME",
"CODEBLOB_FRAME",
"OOP_MAP_FOUND",
"DERIVED_OOP_MAP_VALUE",
"OOP_MAP_VALUE",
"GPGC_START_NEW_GC_CYCLE",
"GPGC_END_NEW_GC_CYCLE",
"GPGC_START_OLD_GC_CYCLE",
"GPGC_END_OLD_GC_CYCLE",
"GPGC_START_SAFEPOINT",
"GPGC_END_SAFEPOINT",
"JAVA_THREAD_START",
"MAKE_SELF_WALKABLE",
"MAKE_REMOTE_WALKABLE",
"GPGC_START_VERIFY_THREAD",
"GPGC_END_VERIFY_THREAD",
"GPGC_TOGGLE_NMT"
};


AuditTrail::AuditTrail(long size)
{
  _size   = size;
  _next   = 0;
  _buffer = NEW_C_HEAP_ARRAY(intptr_t, _size);
  memset(_buffer, 0, sizeof(intptr_t)*_size);
}
      

AuditTrail::~AuditTrail()
{
FREE_C_HEAP_ARRAY(intptr_t,_buffer);
  _size = 0;
  _next = 0;
_buffer=NULL;
}


void AuditTrail::unchecked_log_time(intptr_t tag, intptr_t A)
{
  assert0(GPGCAuditTrail);

  Thread* thread = Thread::current();
  if ( thread->is_Java_thread() ) {
    AuditTrail* at = ((JavaThread*)thread)->audit_trail();
    at->record(TAG(tag,2));
    at->record(os::elapsed_counter());
    at->record(A);
  } else {
assert(false,"Expected current thread to be a JavaThread");
  }
}
