/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "locknode.hpp"
#include "parse.hpp"

#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

//=============================================================================
//-----------------------------hash--------------------------------------------
uint FastLockNode::hash() const { return NO_HASH; }

//------------------------------cmp--------------------------------------------
uint FastLockNode::cmp( const Node &n ) const {
  return (&n == this);                // Always fail except on self
}

const Type*FastLockNode::Value(PhaseTransform*phase)const{
  return TypeTuple::IFBOTH;     // No progress
}

//=============================================================================
//-----------------------------hash--------------------------------------------
uint FastUnlockNode::hash() const { return NO_HASH; }

//------------------------------cmp--------------------------------------------
uint FastUnlockNode::cmp( const Node &n ) const {
  return (&n == this);                // Always fail except on self
}

const Type*FastUnlockNode::Value(PhaseTransform*phase)const{
  return TypeTuple::IFBOTH;     // No progress
}

//=============================================================================
//------------------------------do_monitor_enter-------------------------------
void Parse::do_monitor_enter() {
  kill_dead_locals();
 
  // Null check; get casted pointer.
Node*obj=do_null_check(peek(),T_OBJECT,"null chk monitorenter");
  // Check for locking null object
  if (stopped()) return;

  // the monitor object is not part of debug info expression stack
  pop(); 

  // Insert a LockNode which takes as arguments the current thread pointer,
  // the obj pointer & the address of the stack slot pair used for the lock.
shared_lock(obj,cpdata_null());
}

//------------------------------do_monitor_exit--------------------------------
void Parse::do_monitor_exit() {
  kill_dead_locals();

  pop();                        // Pop oop to unlock
  // Because monitors are guarenteed paired (else we bail out), we know
  // the matching Lock for this Unlock.  Hence we know there is no need
  // for a null check on Unlock.
shared_unlock(map()->peek_monitor_obj());
} 



