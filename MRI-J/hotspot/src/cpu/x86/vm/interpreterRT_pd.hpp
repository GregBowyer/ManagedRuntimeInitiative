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
#ifndef INTERPRETERRT_PD_HPP
#define INTERPRETERRT_PD_HPP


class LookupswitchPair;

static int binary_search(int key, LookupswitchPair* array, int n);

static address iload (JavaThread* thread);
static address aload (JavaThread* thread);
static address istore(JavaThread* thread);
static address astore(JavaThread* thread);
static address iinc  (JavaThread* thread);

// Nailed down for all compilers and all signatures, because the
// various runtime pieces need it nailed down.
static VReg::VR receiver_out();

// Generate an I2C adapter: adjust the I-frame to make space for the C-frame
// layout.  Lesp was saved by the calling I-frame and will be restored on
// return.  Meanwhile, outgoing arg space is all owned by the callee C-frame,
// so we can mangle it at will.  After adjusting the frame size, hoist
// register arguments and repack other args according to the compiled code
// convention.  Finally, end in a jump to the compiled code.  Entry point is
// the start of the created buffer.
//
// Generate a C2I adapter: On entry we know R10 holds the methodOop.  The args
// start out packed in the compiled layout.  They need to be unpacked into the
// interpreter layout.  This will almost always require some stack space.  We
// grow the current (compiled) stack, then repack the args.  We finally end in
// a jump to the generic interpreter entry point.  On exit from the
// interpreter, the interpreter will restore our ASP (lest the compiled code,
// which relys solely on ASP and not FP, get sick).  Offset to the entry point
// is returned and points into the same created buffer.
static int generate_i2c2i_adapters( InterpreterMacroAssembler *_masm, int total_args_passed, VReg::VR max_arg, const BasicType *sig_bt, const VReg::VR *regs );

// Generate a native wrapper for a given method.  The method takes arguments in 
// the Java compiled code convention, marshals them to the native convention
// (handlizes oops, etc), unlocks the jvm_lock, makes the call, reclaims the
// jvm_lock (possibly blocking), unhandlizes any result and returns.
static int generate_native_wrapper( InterpreterMacroAssembler *_masm, methodHandle method, int total_args_passed, VReg::VR max_arg, BasicType *sig_bt, const VReg::VR *regs, BasicType ret_type );

#endif // INTERPRETERRT_PD_HPP
