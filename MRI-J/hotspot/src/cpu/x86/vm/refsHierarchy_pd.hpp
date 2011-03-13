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
#ifndef REFSHIERARCHY_PD_HPP
#define REFSHIERARCHY_PD_HPP

#include "heapRef_pd.hpp"
#include "lvb.hpp"


//
// The purpose of this header is to declare several subtypes
// of objectRef and heapRef. At this time, there are no specialized
// stackRef types.
//

//
// Class hierarchy:
//
// objectRef
//     instanceRef
//     arrayRef
//     objArrayRef
//     typeArrayRef
//     heapRef
//         methodRef
//         methodCodeRef
//         dependencyRef
//         constMethodRef
//         constantPoolRef
//         constantPoolCacheRef
//         symbolRef
//         klassRef
//         compiledICHolderRef
//     stackRef


//
// heapRef subtypes are pretty much limited to objects that live in the perm gen.
// They are allocated with an old_space_id.
//

// I tried to add asserts that the xxxOop constructors were actually
// getting an xxxOop, but this does not work. It turns out that in several
// places the VM cheats and creates klassRefs that are actually constantPoolOops
// and so on. 

#define DECLARE_HEAPREF_SUBTYPE(RefName) class RefName##Ref : public heapRef { \
  public:                                                               \
                                                                        \
    RefName##Ref() { _objref = 0; }                                     \
                                                                        \
    RefName##Ref(const uint64_t raw_value) {                            \
      _objref = raw_value;                                              \
    }                                                                   \
                                                                        \
    RefName##Ref(RefName##Oop o) {                                      \
if(o!=NULL){\
        uint64_t sid = objectRef::old_space_id;                         \
        set_value_base((oop)o, sid, objectRef::discover_klass_id((oop)o), objectRef::discover_nmt(sid, (oop)o)); \
      } else {                                                          \
        _objref = 0;                                                    \
      }                                                                 \
    }                                                                   \
                                                                        \
    /* We need this to be able to assign XXXRef = nullRef; */           \
    void operator = (const objectRef& right) {                          \
      DEBUG_ONLY (                                                      \
        if (right.is_poisoned()) {                                      \
          assert(ALWAYS_UNPOISON_OBJECTREF(right).is_heap(), "Must be heapRef");   \
        } else {                                                                  \
          assert(right.is_null() || right.is_heap(), "Must be heapRef");          \
        }                                                                         \
      )                                                                 \
      _objref = right._objref;                                          \
    }                                                                   \
                                                                        \
    /* We need this to be able to assign XXXRef = nullRef; */           \
    void operator = (const objectRef& right) volatile {                 \
      DEBUG_ONLY (                                                      \
        if (right.is_poisoned()) {                                      \
          assert(ALWAYS_UNPOISON_OBJECTREF(right).is_heap(), "Must be heapRef");   \
        } else {                                                                  \
          assert(right.is_null() || right.is_heap(), "Must be heapRef");          \
        }                                                                         \
      )                                                                 \
      _objref = right._objref;                                          \
    }                                                                   \
                                                                        \
    RefName##Oop as_##RefName##Oop() const {                            \
      return (RefName##Oop)as_oop();                                    \
    }                                                                   \
                                                                        \
    RefName##Oop as_##RefName##Oop() volatile const {                   \
      return (RefName##Oop)as_oop();                                    \
    }                                                                   \
                                                                        \
    friend RefName##Ref lvb_##RefName##Ref(const RefName##Ref* addr) {  \
      return RefName##Ref(lvb_ref(addr)._objref);                       \
    }                                                                   \
                                                                        \
    friend RefName##Ref lvb_##RefName##Ref_loaded(RefName##Ref ref, const RefName##Ref* addr) {  \
      return RefName##Ref(lvb_loadedref(ref, addr)._objref);               \
    }                                                                   \
                                                                        \
    friend RefName##Ref poison_##RefName##Ref(const RefName##Ref r) {   \
      if ( RefPoisoning && r.not_null() ) {                             \
        assert0( ! r.is_poisoned() );                                   \
        return RefName##Ref(r.raw_value() ^ -1);                        \
      }                                                                 \
      return r;                                                         \
    }                                                                   \
                                                                        \
    friend RefName##Ref unpoison_##RefName##Ref(const RefName##Ref r) { \
      if ( RefPoisoning && r.not_null() ) {                             \
        assert0( r.is_poisoned() );                                     \
        return RefName##Ref(r.raw_value() ^ -1);                        \
      }                                                                 \
      return r;                                                         \
    }                                                                   \
  }

//
// objectRef subtypes can be either heapRef or stackRef. They can be allocated
// in the young gen. They also dynamically look up their klass id. Because these
// subtypes are not specifically either heapRef or stackRef, they are more expensive
// to use than the heapRef subtypes.
//

// I tried to add asserts that the xxxOop constructors were actually
// getting an xxxOop, but this does not work. It turns out that in several
// places the VM cheats and creates klassRefs that are actually constantPoolOops
// and so on. 

#define DECLARE_OBJECTREF_SUBTYPE(RefName) class RefName##Ref : public objectRef { \
  public:                                                               \
                                                                        \
    RefName##Ref() { _objref = 0; }                                     \
                                                                        \
    RefName##Ref(const uint64_t raw_value) {                            \
      _objref = raw_value;                                              \
    }                                                                   \
                                                                        \
    RefName##Ref(RefName##Oop o) : objectRef((oop)o) { }                \
                                                                        \
    /* We need this to be able to assign XXXRef = nullRef; */           \
    void operator = (const objectRef& right) {                          \
      assert(right.is_null() || right.is_stack() || right.is_heap(), "Must not be ptrRef"); \
      _objref = right._objref;                                          \
    }                                                                   \
                                                                        \
    /* We need this to be able to assign XXXRef = nullRef; */           \
    void operator = (const objectRef& right) volatile {                 \
      assert(right.is_null() || right.is_stack() || right.is_heap(), "Must not be ptrRef"); \
      _objref = right._objref;                                          \
    }                                                                   \
                                                                        \
    RefName##Oop as_##RefName##Oop() const {                            \
      return (RefName##Oop)as_oop();                                    \
    }                                                                   \
                                                                        \
    RefName##Oop as_##RefName##Oop() volatile const {                   \
      return (RefName##Oop)as_oop();                                    \
    }                                                                   \
                                                                        \
    friend RefName##Ref lvb_##RefName##Ref(const RefName##Ref* addr) {  \
      return RefName##Ref(lvb_ref(addr)._objref);                       \
    }                                                                   \
  }

DECLARE_HEAPREF_SUBTYPE(method);
DECLARE_HEAPREF_SUBTYPE(methodCode);
DECLARE_HEAPREF_SUBTYPE(dependency);
DECLARE_HEAPREF_SUBTYPE(constMethod);
DECLARE_HEAPREF_SUBTYPE(constantPool);
DECLARE_HEAPREF_SUBTYPE(constantPoolCache);
DECLARE_HEAPREF_SUBTYPE(symbol);
DECLARE_HEAPREF_SUBTYPE(klass);

DECLARE_OBJECTREF_SUBTYPE(instance);
DECLARE_OBJECTREF_SUBTYPE(array);
DECLARE_OBJECTREF_SUBTYPE(objArray);
DECLARE_OBJECTREF_SUBTYPE(typeArray);

#endif // REFSHIERARCHY_PD_HPP
