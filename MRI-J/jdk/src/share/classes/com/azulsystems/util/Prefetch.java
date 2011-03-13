// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License version 2 only, as published by
// the Free Software Foundation.
//
// Azul designates this particular file as subject to the "Classpath" exception
// as provided by Azul in the LICENSE file that accompanied this code.
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
package com.azulsystems.util;

public final class Prefetch {
  // Sizes and offsets for arrays. These are currently hardcoded into this class.
  // There might need to be set by the AVM at classload time.
  private static final int _boolean_arr_offset = 20;
  private static final int _byte_arr_offset    = 20;
  private static final int _short_arr_offset   = 20;
  private static final int _char_arr_offset    = 20;
  private static final int _int_arr_offset     = 20;
  private static final int _long_arr_offset    = 24;
  private static final int _float_arr_offset   = 20;
  private static final int _double_arr_offset  = 24;
  private static final int _Object_arr_offset  = 24;
  
  private static final int _boolean_arr_scale = 0;
  private static final int _byte_arr_scale    = 0;
  private static final int _short_arr_scale   = 1;
  private static final int _char_arr_scale    = 1;
  private static final int _int_arr_scale     = 2;
  private static final int _long_arr_scale    = 3;
  private static final int _float_arr_scale   = 2;
  private static final int _double_arr_scale  = 3;
  private static final int _Object_arr_scale  = 3;  
  
  // Basic intrinsic operations recognized by the compiler.
  public static void shared   (Object o, long offset) { /* intrinsic operation */ }
  public static void exclusive(Object o, long offset) { /* intrinsic operation */ }
}
