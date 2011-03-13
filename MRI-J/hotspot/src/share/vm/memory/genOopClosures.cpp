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


#include "collectedHeap.hpp"
#include "genOopClosures.hpp"
#include "oop.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"

void VerifyOopClosure::do_oop(objectRef*p){
  objectRef ref = UNPOISON_OBJECTREF(*p,p);
  oop       obj = NULL;

  if (UseSBA) {
    if (Universe::heap()->is_in_reserved(p)) {
      obj = ref.as_oop();
guarantee(obj->is_oop_or_null(),"invalid oop");
    } else {
      // This can be in a local variable or in a stack object or somewhere else.
      obj = ref.as_oop();
guarantee(obj->is_oop_or_null(),"invalid oop");
    }
  } else {
    obj = ref.as_oop();
guarantee(obj->is_oop_or_null(),"invalid oop");
  }
#if defined(AZUL)
  if (obj != NULL) {
    guarantee(ref.klass_id() == objectRef::discover_klass_id(obj), "kid in objref doesn't match kid in oop");
  }
#endif
}

