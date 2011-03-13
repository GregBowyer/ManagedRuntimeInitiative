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
#ifndef WLMUXER_HPP
#define WLMUXER_HPP

// An implementation of the native methods of weblogic.socket.PosixSockMuxer.

////////////////////////////////////////////////////////////////////////////////
// JNI calls implemented as JVM entries
////////////////////////////////////////////////////////////////////////////////

JNIEXPORT void JNICALL Java_weblogic_socket_PosixSocketMuxer_initStripes(JNIEnv*, jclass, jint, jint);
JNIEXPORT jboolean JNICALL Java_weblogic_socket_PosixSocketMuxer_pollStripe(JNIEnv*, jclass, jint, jobjectArray, jint);
JNIEXPORT void JNICALL Java_weblogic_socket_PosixSocketMuxer_wakeupStripe(JNIEnv*, jclass, jint);
JNIEXPORT jint JNICALL Java_weblogic_socket_PosixSocketMuxer_getSoftFdLimit(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_weblogic_socket_PosixSocketMuxer_getHardFdLimit(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_weblogic_socket_PosixSocketMuxer_getCurrentFdLimit(JNIEnv*, jclass);
JNIEXPORT jstring JNICALL Java_weblogic_socket_PosixSocketMuxer_getBuildTime(JNIEnv*, jclass);
JNIEXPORT void JNICALL Java_weblogic_socket_PosixSocketMuxer_setDebug(JNIEnv*, jclass, jboolean);

////////////////////////////////////////////////////////////////////////////////
// Global HotSpot interface to muxer internals
////////////////////////////////////////////////////////////////////////////////

class WLMuxer:public AllStatic{
public:
  static void print_xml(azprof::Request*, azprof::Response*);
  static void reset(azprof::Request*, azprof::Response*);
};
#endif
