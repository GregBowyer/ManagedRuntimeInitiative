/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "ciObject.hpp"

#include "ciEnv.hpp"
#include "ciObjectFactory.hpp"
#include "ciUtilities.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"

// ciObject
//
// This class represents an oop in the HotSpot virtual machine.
// Its subclasses are structured in a hierarchy which mirrors
// an aggregate of the VM's oop and klass hierarchies (see
// oopHierarchy.hpp).  Each instance of ciObject holds a handle
// to a corresponding oop on the VM side and provides routines
// for accessing the information in its oop.  By using the ciObject
// hierarchy for accessing oops in the VM, the compiler ensures
// that it is safe with respect to garbage collection; that is,
// GC and compilation can proceed independently without
// interference.
//
// Within the VM, the oop and klass hierarchies are separate.
// The compiler interface does not preserve this separation --
// the distinction between `klassOop' and `Klass' are not
// reflected in the interface and instead the Klass hierarchy
// is directly modeled as the subclasses of ciKlass.

bool ciObject::is_fam_active(){
  return (UseFreezeAndMelt && ciEnv::current()) ? !!ciEnv::current()->freeze_and_melt() : false;
}

// ------------------------------------------------------------------
// ciObject::ciObject
ciObject::ciObject(oop o) {
  ASSERT_IN_VM;
  if (ciObjectFactory::is_initialized()) {
    _handle = JNIHandles::make_local(o);
  } else {
    _handle = JNIHandles::make_global(o);
  }
  _klass = NULL;
  _is_loaded_initialized = false;
  _ident = 0;
}

// ------------------------------------------------------------------
// ciObject::ciObject
//
ciObject::ciObject(Handle h) {
  ASSERT_IN_VM;
  if (ciObjectFactory::is_initialized()) {
    _handle = JNIHandles::make_local(h());
  } else {
    _handle = JNIHandles::make_global(h);
  }
  _klass = NULL;
  _is_loaded_initialized = false;
  _ident = 0;
}

// ------------------------------------------------------------------
// ciObject::ciObject
//
// Unloaded klass/method variant.  `klass' is the klass of the unloaded
// klass/method, if that makes sense.
ciObject::ciObject(ciKlass* klass) {
  ASSERT_IN_VM;
  assert(klass != NULL, "must supply klass");
  _handle = NULL;
  _klass = klass;
  _is_loaded_initialized = false;
  _ident = 0;
}

// ------------------------------------------------------------------
// ciObject::ciObject
//
// NULL variant.  Used only by ciNullObject.
ciObject::ciObject() {
  ASSERT_IN_VM_FAMSAFE;
  _handle = NULL;
  _klass = NULL;
  _is_loaded_initialized = false;
  _ident = 0;
}

ciObject::ciObject(FAMPtr old_cio){
  FAM->mapNewToOldPtr(this, old_cio);
  // _handle should ONLY be used as a unique identifier.. so poison it UNLESS IT IS NULL
  FAMPtr old_handle = FAM->getOldPtr("((struct ciObject*)%p)->_handle", old_cio);
  if (old_handle != 0) {
    _handle = (jobject)(~((long)old_handle)); // I could invert and add one, but this makes debugging simpler
  } else {
    _handle = NULL;
  }
  _klass = (ciKlass*)FAM->getOldPtr("((struct ciObject*)%p)->_klass", old_cio);
  _is_loaded = FAM->getInt("((struct ciObject*)%p)->_is_loaded", old_cio);
  _is_loaded_initialized = FAM->getInt("((struct ciObject*)%p)->_is_loaded_initialized", old_cio);
  _ident = FAM->getInt("((struct ciObject*)%p)->_ident", old_cio);
}

void ciObject::fixupFAMPointers() {
  _klass = (ciKlass*)FAM->getNewFromOldPtr((FAMPtr)_klass);
}

objectRef ciObject::get_ref() const {
assert(!FAM,"FAM access not allowed");
assert(_handle!=NULL,"null oop");
assert(JavaThread::current()->jvm_locked_by_self(),"accessing naked oop while in native code");
return JNIHandles::resolve_as_non_null_ref(_handle);
}

bool ciObject::is_loaded()const{
  if (_is_loaded_initialized) {
    return _is_loaded;
  }
assert(!FAM,"Should have cached this");
  const_cast<ciObject*>(this)->_is_loaded = (handle() != NULL || is_classless());
  const_cast<ciObject*>(this)->_is_loaded_initialized = true;
  return _is_loaded;
}

// ------------------------------------------------------------------
// ciObject::klass
//
// Get the ciKlass of this ciObject.
ciKlass*ciObject::klass()const{
  if (_klass == NULL) {
    if (_handle == NULL) {
      // When both _klass and _handle are NULL, we are dealing
      // with the distinguished instance of ciNullObject.
      // No one should ask it for its klass.
      assert(is_null_object(), "must be null object");
      ShouldNotReachHere();
      return NULL;
    }

    GUARDED_VM_ENTRY(
      oop o = get_oop();
((ciObject*)this)->_klass=CURRENT_ENV->get_object(o->klass())->as_klass();
    );
  }
  return _klass;
}

// ------------------------------------------------------------------
// ciObject::set_ident
//
// Set the unique identity number of a ciObject.
void ciObject::set_ident(uint id) {
  assert((_ident >> FLAG_BITS) == 0, "must only initialize once");
  assert( id < ((uint)1 << (BitsPerInt-FLAG_BITS)), "id too big");
  _ident = _ident + (id << FLAG_BITS);
}

// ------------------------------------------------------------------
// ciObject::ident
//
// Report the unique identity number of a ciObject.
uint ciObject::ident()const{
  uint id = _ident >> FLAG_BITS;
  assert(id != 0, "must be initialized");
  return id;
}

// ------------------------------------------------------------------
// ciObject::equals
//
// Are two ciObjects equal?
bool ciObject::equals(ciObject* obj) {
  return (this == obj);
}

// ------------------------------------------------------------------
// ciObject::hash
//
// A hash value for the convenience of compilers.
//
// Implementation note: we use the address of the ciObject as the
// basis for the hash.  Use the _ident field, which is well-behaved.
int ciObject::hash() {
  return ident() * 31;
}

// ------------------------------------------------------------------
// ciObject::encoding
//
// The address which the compiler should embed into the
// generated code to represent this oop.  This address
// is not the true address of the oop -- it will get patched
// during nmethod creation.
//
//
//
// Implementation note: we use the handle as the encoding.  The
// nmethod constructor resolves the handle and patches in the oop.
//
// This method should be changed to return an generified address
// to discourage use of the JNI handle.
jobject ciObject::encoding() {
assert(FAM||is_null_object()||handle()!=NULL,"cannot embed null pointer");
//assert(has_encoding(), "oop must be NULL or perm"); // allowed in the oop table
  return handle();
}
  
// ------------------------------------------------------------------
// ciObject::has_encoding
bool ciObject::has_encoding() {
  return handle() == NULL || is_perm();
}


// ------------------------------------------------------------------
// ciObject::print
//
// Print debugging output about this ciObject.
//
// Implementation note: dispatch to the virtual print_impl behavior
// for this ciObject.
void ciObject::print(outputStream*out)const{
  ThreadInVMfromUnknown tivmfu();
  JavaThread* thread = JavaThread::current();
  HandleMarkCleaner __hm(thread);
  debug_only(VMNativeEntryWrapper __vew);
out->print("<%s",type_string());
print_impl(out);
out->print(" id=%d %saddress=%p>",ident(),
is_perm()?"PERM ":"",
        (address)this);
}

// ------------------------------------------------------------------
// ciObject::print_oop
//
// Print debugging output about the oop this ciObject represents.
void ciObject::print_oop(outputStream*out)const{
  if (is_null_object()) {
out->print_cr("NULL");
  } else if (!is_loaded()) {
out->print_cr("UNLOADED");
  } else {
    if (FAM) return;
GUARDED_VM_ENTRY(get_oop()->print(out);)
  }
}

