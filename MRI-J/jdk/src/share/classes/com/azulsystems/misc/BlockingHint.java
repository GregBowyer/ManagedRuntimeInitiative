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

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.AbstractOwnableSynchronizer;

/** Per thread hint set when we think we're about to block.  Providing a
 * description of why we're blocked.  The cause of blocking could be I/O,
 * synchronization, sleeping, etc.
 */
public class BlockingHint {
  private static native void set0( String msg, Object lock, Object sync );
  private static void internal_set(String msg, Object lock, Object sync ) { set0(msg,lock,sync); }
  public static void set(final String msg) { internal_set(msg,null,null); }
  public static void acquiring( Lock lock, AbstractOwnableSynchronizer sync ) { internal_set("acquiring",lock,sync); }
  public static void clear() { internal_set(null,null,null); }
}
