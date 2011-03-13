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

#ifndef AUDITTRAIL_INLINE_HPP
#define AUDITTRAIL_INLINE_HPP
#include "auditTrail.hpp"

#ifndef AT_PRODUCT

#define TAG_MARK    (0xBEAD000000000000ULL)
#define COUNT_SHIFT 32

#define TAG(tag,count)  intptr_t(tag) | (intptr_t(count)<<32) | TAG_MARK


inline void AuditTrail::record(intptr_t x)
{
assert(_next>=0&&_next<_size,"AuditTrail ring buffer is corrupt");

  jlong next;
  jlong new_next;

  do {
    next     = _next;
    new_next = next + 1;
    if ( new_next == _size ) new_next = 0;
  } while ( next != Atomic::cmpxchg(new_next, &_next, next) );

  _buffer[next] = x;
}


inline void AuditTrail::log_time(JavaThread* thread, intptr_t tag)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,1));
    at->record(os::elapsed_counter());
  }
}

inline void AuditTrail::log_time(JavaThread* thread, intptr_t tag, intptr_t A)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,2));
    at->record(os::elapsed_counter());
    at->record(A);
  }
}

inline void AuditTrail::log_time(JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,3));
    at->record(os::elapsed_counter());
    at->record(A);
    at->record(B);
  }
}


inline void AuditTrail::log(JavaThread* thread, intptr_t tag, intptr_t A)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,1));
    at->record(A);
  }
}

inline void AuditTrail::log(JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,2));
    at->record(A);
    at->record(B);
  }
}

inline void AuditTrail::log(JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B, intptr_t C)
{
  if ( GPGCAuditTrail ) {
    AuditTrail* at = thread->audit_trail();
    at->record(TAG(tag,3));
    at->record(A);
    at->record(B);
    at->record(C);
  }
}


inline void AuditTrail::log_time(AuditTrail* at, intptr_t tag)
{
  if ( GPGCAuditTrail ) {
    at->record(TAG(tag,1));
    at->record(os::elapsed_counter());
  }
}

inline void AuditTrail::log_time(AuditTrail* at, intptr_t tag, intptr_t A, intptr_t B)
{
  if ( GPGCAuditTrail ) {
    at->record(TAG(tag,3));
    at->record(os::elapsed_counter());
    at->record(A);
    at->record(B);
  }
}


inline void AuditTrail::log(AuditTrail* at, intptr_t tag, intptr_t A, intptr_t B, intptr_t C)
{
  if ( GPGCAuditTrail ) {
    at->record(TAG(tag,3));
    at->record(A);
    at->record(B);
    at->record(C);
  }
}

#endif // ! PRODUCT
#endif // AUDITTRAIL_INLINE_HPP
