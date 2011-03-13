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

// container to hold functions that we must guarantee do not get inlined.
#include "noinline.hpp"

bool    noinline::lt(int l, int r)   { return (l <  r); }
bool    noinline::le(int l, int r)   { return (l <= r); }
bool    noinline::eq(int l, int r)   { return (l == r); }
bool    noinline::ne(int l, int r)   { return (l != r); }
bool    noinline::ge(int l, int r)   { return (l >= r); }
bool    noinline::gt(int l, int r)   { return (l  > r); }

bool    noinline::lt(long l, long r)   { return (l <  r); }
bool    noinline::le(long l, long r)   { return (l <= r); }
bool    noinline::eq(long l, long r)   { return (l == r); }
bool    noinline::ne(long l, long r)   { return (l != r); }
bool    noinline::ge(long l, long r)   { return (l >= r); }
bool    noinline::gt(long l, long r)   { return (l  > r); }


