/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef THREAD_OS_PD_HPP
#define THREAD_OS_PD_HPP


// This header is included in thread.hpp, inside the JavaThread class.
// FIXME:   This file is no longer needed.  Any remaining functionality
//          should be included directly in thread.hpp

public:

  uint32_t _epc;                // NPE exception address
static ByteSize epc_offset(){return byte_offset_of(JavaThread,_epc);}

  void pd_set_hardware_poll_at_safepoint() {
    // No hardware safepoint bit on x86.
  }

  void pd_clr_hardware_poll_at_safepoint() {
    // No hardware safepoint bit on x86.
  }

  void pd_initialize();

private:
  void pd_destroy();

public:
  intptr_t* base_of_stack_pointer() {
    return NULL;
  }
  
  void set_base_of_stack_pointer(intptr_t* base_sp) {
    // Don't do anything here.
  }

  void record_base_of_stack_pointer() {
    // Don't do anything here.
  }

#endif // THREAD_OS_PD_HPP
