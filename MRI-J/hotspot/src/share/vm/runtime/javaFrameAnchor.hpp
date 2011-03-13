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
#ifndef JAVAFRAMEANCHOR_HPP
#define JAVAFRAMEANCHOR_HPP

#include "allocation.hpp"
#include "sizes.hpp"

//
// An object for encapsulating the machine/os dependent part of a JavaThread frame state
//
class JavaThread;

class JavaFrameAnchor VALUE_OBJ_CLASS_SPEC {
private:
  // No volatile keywords - they do not work - not properly honored by C
  // compilers everywhere, so instead we will be correct without them.  Adding
  // the keyword in makes you believe the compilers will do something special
  // here to save you - but they do not (always).
  intptr_t _last_Java_sp;	// SP of Java frame that called into C code
  intptr_t _last_Java_pc; 	// PC of Java frame that called into C code

public:
   enum Constants {
     // We use _last_Java_sp == NULL to indicate a missing value and is
     // generally asserted against.  This value indicates that there is no
     // prior java frame and we are at the stack base.  This value has to
     // otherwise look like a valid SP.
     no_prior_java_frame = -16
   };

  // See if we believe last_java_sp was set sanely.  
  void verify() const {
#ifdef ASSERT
    // Ok to see the 'root' Java frame
    if( pd_last_Java_sp() == no_prior_java_frame ) return;
assert(_last_Java_pc!=0,"last_Java_pc not setup");
    // THIS NICE TEST FAILS CROSS-THREAD :-(
    // See if there's less than 100K between here & there....  Fails for NULL,
    // so don't ask.
    // int _x; return ((intptr_t)_last_Java_sp-100000) < (intptr_t)&_x  &&  (intptr_t)&_x < (intptr_t)_last_Java_sp;
assert(pd_last_Java_sp()!=0,"last_Java_sp not set");
    pd_verify();		// Verify other bits
#endif
  }

  // Make the SP invalid to trigger asserts.  Crucially it only zaps the
  // last_Java_sp field leaving the rest of the anchor OK.  This is so I can
  // re-set only the last_Java_sp and again have a valid anchor.
  void zap_last_Java_sp() { debug_only(_last_Java_sp = 0;) }

  // Zap the whole anchor to trigger asserts.  Can be done in any order 'cause
  // no one is looking at us.  Also used by JavaCallWrapper to clear out any
  // notion of a 'root_Java_frame'.
  void zap() {
    _last_Java_sp = 0;
    _last_Java_pc = 0;
    pd_zap();
  }

  // bool has_last_Java_sp() const { ShouldNotReachHere(); }
  //
  // There's no longer a notion of missing the last_Java_sp.  JavaFrameAnchors
  // only exist in JavaCallWrappers and Thread structures.  The one in
  // JavaCallWrappers is always valid (copied from the Thread's valid JFA).
  // The JFA in the Thread is only looked at when it's valid, i.e. when the
  // Thread's _jvm_lock is not locked by self.

  void verify_walkable() const {
    verify();
    assert( is_walkable(_last_Java_sp), "Asking for anchor bits but stack not walkable" );
  }
  // The raw version is used by thread.hpp to unlock the jvm lock from VM
  intptr_t last_Java_sp_raw() const { verify(); return _last_Java_sp; }

  // Setup last Java sp.  Other pd-specific JavaFrameAnchor bits must already
  // be valid.  Keeps existing walkable bits.
  void set_last_Java_sp( intptr_t last_Java_sp ) { 
assert(_last_Java_pc!=0,"Last Java PC must already be setup");
assert((last_Java_sp&3)==0,"last_Java_sp must be word aligned! (SparcV9 SPs must be unbiased already)");
assert(last_Java_sp!=0,"use zap_last_Java_sp() to clear");
    _last_Java_sp = last_Java_sp;
    verify();
  }

  void init_to_root() { 
    pd_zap(); 
    _last_Java_pc = no_prior_java_frame; // Any non-zero PC will do
    _last_Java_sp = no_prior_java_frame; 
    verify(); 
  }
  
  // Return true if this is a root JavaFrameAnchor, with no more Java
  // frames above it.  Never asked for normally from the Thread's JFA,
  // only from JavaCallWrappers during stack crawls.
  bool root_Java_frame() const { 
    verify(); 
    return clr_walkable(_last_Java_sp) == no_prior_java_frame; 
  }

#include "javaFrameAnchor_pd.hpp"

public:
JavaFrameAnchor(){
zap();
}


  // Assembly stub generation helpers.  A set of a clean _last_Java_sp clears
  // the pd-specific walkable bits.
  static ByteSize last_Java_sp_offset()          { return byte_offset_of(JavaFrameAnchor, _last_Java_sp); }
  static ByteSize last_Java_pc_offset()          { return byte_offset_of(JavaFrameAnchor, _last_Java_pc); }

};

#endif // JAVAFRAMEANCHOR_HPP
