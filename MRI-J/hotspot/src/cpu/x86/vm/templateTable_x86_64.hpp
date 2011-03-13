/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef TEMPLATETABLE_PD_HPP
#define TEMPLATETABLE_PD_HPP


  // helper routines
  static void invokevfinal_helper   (Register Rcache, Register Rret);
  static void invokevirtual_helper  (int byte_no, bool fast);
  static void invokeinterface_helper(int byte_no, bool fast);
  static void invokespecial_helper  (int byte_no, bool fast);
  static void invokestatic_helper   (int byte_no, bool fast);

  static void instanceof_checkcast  ( bool is_ccast );

  static void volatile_barrier();

  static void locals_index0(Register reg, int offset);
static void locals_index0_wide(Register reg);
static void locals_index1(Register reg);

  static void resolve_cache_and_index0(RInOuts, Register Ridx, RKeepIns, Register R08_cpc, Register Rbcp,
                                       int byte_no, Label &do_resolve, Label &resolved);
  static void resolve_cache_and_index1(RInOuts, Register Rtmp0, Register Ridx,
                                       RKeepIns, Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
                                       int byte_no, Label &do_resolve, Label &resolved);

#endif // TEMPLATETABLE_PD_HPP
