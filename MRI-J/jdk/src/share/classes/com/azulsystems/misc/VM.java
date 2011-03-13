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
package com.azulsystems.misc;

public class VM {
  
  private static native boolean useLockedCollections0();
  private static native boolean useCheckedCollections0();
  private static native Object  shallowCopy0(Object obj);
  private static native boolean referenceEquality0(java.lang.ref.Reference ref, Object obj);
  private static native int     getThreadLocalMapInitialCapacity0();
  
  public static boolean useLockedCollections() {
    return useLockedCollections0();
  }
  
  public static boolean useCheckedCollections() {
    return useCheckedCollections0();
  }
  
  public static Object shallowCopy(Object obj) {
    return shallowCopy0(obj);
  }
  
  public static boolean referenceEquality(java.lang.ref.Reference ref, Object obj) {
    return referenceEquality0(ref, obj);
  }

  public static int getThreadLocalMapInitialCapacity() {
    return getThreadLocalMapInitialCapacity0();
  }
}
