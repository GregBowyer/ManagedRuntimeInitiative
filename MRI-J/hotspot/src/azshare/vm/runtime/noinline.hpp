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

#ifndef NOINLINE_HPP
#define NOINLINE_HPP
// -----------------------------------------------------------------------
// 
// dealing with warning:   -Wstrict-overflow
//
// -----------------------------------------------------------------------
// Ok, I know you're thinking, "what the heck do we need this for?"
// Well, I'll tell you. 
//  
// Here is the problem code:
//      bool foo(lo,hi)  { assert(lo<hi,"range error"); do(lo,hi); }
//
//      bool bar2(range) { lo=range.lo(); hi=range.hi(); foo(lo,hi); }
//      bool bar1(range) { lo=range.lo(); hi=lo+1      ; foo(lo,hi); }
//
// When compiled -O3, the compiler inlines foo() into both bar1() 
// and bar2().  In bar1(), it recognizeds the the assert is now
// decidable at compile time because it becomes assert( lo<lo+1, "..");
// So it generates a warning saying it has made the assumption and
// discared the assert.  The warning is generated because it is an
// unsafe optimization *IF* we are trying to detect overflow in
// the expression 'lo+1'.
//
// There is not a way to tell the compiler to not inline foo() into 
// bar1().  And there isn't a way to turn off this warning.  So I 
// have to prevent the assert from being evaluated at compile time.
//
// We do this by rewriting it like this:
//  bool foo(lo,hi) { assert( noinline::lt(lo,hi), "range error"); do(lo,hi); }
//
// -----------------------------------------------------------------------

class noinline {
    public: 
static bool    lt(int l, int r);
static bool    le(int l, int r);
static bool    eq(int l, int r);
static bool    ne(int l, int r);
static bool    ge(int l, int r);
static bool    gt(int l, int r);

static bool    lt(long l, long r);
static bool    le(long l, long r);
static bool    eq(long l, long r);
static bool    ne(long l, long r);
static bool    ge(long l, long r);
static bool    gt(long l, long r);
};

#endif  //NOINLINE_HPP
