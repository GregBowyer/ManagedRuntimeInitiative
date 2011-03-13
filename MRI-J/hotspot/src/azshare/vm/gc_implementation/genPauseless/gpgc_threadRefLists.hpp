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

#ifndef GPGC_THREADREFLISTS_HPP
#define GPGC_THREADREFLISTS_HPP

#include "allocation.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "iterator.hpp"


class GPGC_JavaLangRefHandler;

class GPGC_ThreadRefLists:public CHeapObj
{
  private:
    objectRef  _capturedRefs    [SubclassesOfJavaLangRefReference];
    intptr_t   _capturedRefCount[SubclassesOfJavaLangRefReference];

  public:
    GPGC_ThreadRefLists();

    void       reset                  ();
    void       oops_do                (OopClosure* f);
    void       verify_empty_ref_lists ();

    objectRef  capturedRefsHead       (ReferenceType ref_type) { return _capturedRefs[ref_type]; }
    objectRef* capturedRefsAddr       (ReferenceType ref_type) { return &_capturedRefs[ref_type]; }
    long       capturedRefCount       (ReferenceType ref_type) { return _capturedRefCount[ref_type]; }

    void       incrementRefCount      (ReferenceType ref_type) { _capturedRefCount[ref_type] ++; }
    void       resetRefCount          (ReferenceType ref_type) { _capturedRefCount[ref_type] = 0; }
};

#endif // GPGC_THREADREFLISTS_HPP
