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


#include "ciSignature.hpp"

#include "ciEnv.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "ostream.hpp"
#include "signature.hpp"

// ciSignature
//
// This class represents the signature of a method.

// ------------------------------------------------------------------
// ciSignature::ciSignature
ciSignature::ciSignature(FAMPtr old_sig) {
  FAM->mapNewToOldPtr(this, old_sig);
  // Assumed called after all ciObject's are loaded into the ciObjectFactory
  _symbol = (ciSymbol*)FAM->getNewFromOldPtr(FAM->getOldPtr("((struct ciSignature*)%p)->_symbol", old_sig));
  _accessing_klass = (ciKlass*)FAM->getNewFromOldPtr(FAM->getOldPtr("((struct ciSignature*)%p)->_accessing_klass", old_sig));

  _size = FAM->getInt("((struct ciSignature*)%p)->_size", old_sig);
  _count = FAM->getInt("((struct ciSignature*)%p)->_count", old_sig);

  ciEnv* env = CURRENT_ENV;
  Arena* arena = env->arena();
  int numtypes = FAM->getInt("((struct ciSignature*)%p)->_types->_len", old_sig);
  _types = new (arena) GrowableArray<ciType*>(arena, numtypes, numtypes, NULL);
for(int i=0;i<numtypes;i++){
    FAMPtr old_cit = FAM->getOldPtr("((struct ciSignature*)%p)->_types->_data[%d]", old_sig, i);
    if (old_cit != 0) {
      _types->at_put_grow(i, (ciType*)FAM->getNewFromOldPtr(old_cit));
    }
  }
}

ciSignature::ciSignature(ciKlass* accessing_klass, ciSymbol* symbol) {
  ASSERT_IN_VM;
  EXCEPTION_CONTEXT;
  _accessing_klass = accessing_klass;
  _symbol = symbol;

  ciEnv* env = CURRENT_ENV;
  Arena* arena = env->arena();
  _types = new (arena) GrowableArray<ciType*>(arena, 8, 0, NULL);

  int size = 0;
  int count = 0;
  symbolHandle sh (THREAD, symbol->get_symbolOop());
  SignatureStream ss(sh);
  for (; ; ss.next()) {
    // Process one element of the signature
    ciType* type;
    if (!ss.is_object()) {
      type = ciType::make(ss.type());
    } else {
      symbolOop name = ss.as_symbol(THREAD);
      if (HAS_PENDING_EXCEPTION) {
type=ss.is_array()
?(ciType*)ciEnv::unloaded_ciobjarrayklass()
          : (ciType*)ciEnv::unloaded_ciinstance_klass();
        env->record_out_of_memory_failure();
        CLEAR_PENDING_EXCEPTION;
      } else {
        ciSymbol* klass_name = env->get_object(name)->as_symbol();
        type = env->get_klass_by_name_impl(_accessing_klass, klass_name, false);
      }
    }
    _types->append(type);
    if (ss.at_return_type()) {
      // Done processing the return type; do not add it into the count.
      break;
    }
    size += type->size();
    count++;
  }
  _size = size;
  _count = count;
}

// ------------------------------------------------------------------
// ciSignature::return_ciType
//
// What is the return type of this signature?
ciType* ciSignature::return_type() const {
  return _types->at(_count);
}

// ------------------------------------------------------------------
// ciSignature::ciType_at
//
// What is the type of the index'th element of this
// signature?
ciType* ciSignature::type_at(int index) const {
  assert(index < _count, "out of bounds");
  // The first _klasses element holds the return klass.
  return _types->at(index);
}

// ------------------------------------------------------------------
// ciSignature::print_signature
void ciSignature::print_signature(outputStream*out)const{
_symbol->print_symbol(out);
}

// ------------------------------------------------------------------
// ciSignature::print
void ciSignature::print(outputStream*out)const{
out->print("<ciSignature symbol=");
print_signature(out);
out->print(" accessing_klass=");
_accessing_klass->print(out);
out->print(" address=%p>",(address)this);
}

