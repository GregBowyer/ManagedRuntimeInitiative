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

#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_threadRefLists.hpp"


GPGC_ThreadRefLists::GPGC_ThreadRefLists()
{
  // If the number of java.lang.ref types has changed, than this class will
  // certainly need review, and likely need revision.
  assert0(0<=REF_SOFT    && SubclassesOfJavaLangRefReference>REF_SOFT   );
  assert0(0<=REF_WEAK    && SubclassesOfJavaLangRefReference>REF_WEAK   );
  assert0(0<=REF_FINAL   && SubclassesOfJavaLangRefReference>REF_FINAL  );
  assert0(0<=REF_PHANTOM && SubclassesOfJavaLangRefReference>REF_PHANTOM);

  assert0(SubclassesOfJavaLangRefReference == 4);

for(int i=0;i<SubclassesOfJavaLangRefReference;i++){
    _capturedRefs[i]     = nullRef;
_capturedRefCount[i]=0;
  }
}


void GPGC_ThreadRefLists::reset()
{
for(int i=0;i<SubclassesOfJavaLangRefReference;i++){
    _capturedRefs[i] = nullRef;
  }
}


void GPGC_ThreadRefLists::oops_do(OopClosure* f)
{
for(int i=0;i<SubclassesOfJavaLangRefReference;i++){
    f->do_oop(&_capturedRefs[i]);
  }
}


void GPGC_ThreadRefLists::verify_empty_ref_lists()
{
for(int i=0;i<SubclassesOfJavaLangRefReference;i++){
    guarantee(_capturedRefs[i] == nullRef, "found captured refs");
  }
}
