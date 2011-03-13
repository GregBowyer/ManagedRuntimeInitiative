/*
 * Copyright 1999-2003 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciEnv.hpp"
#include "ciExceptionHandler.hpp"
#include "ciInstanceKlass.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "interfaceSupport.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"

// ciExceptionHandler
//
// This class represents an exception handler for a method.

// ------------------------------------------------------------------
// ciExceptionHandler::catch_klass
// 
// Get the exception klass that this handler catches.
ciExceptionHandler::ciExceptionHandler(FAMPtr old_cieh) {
  FAM->mapNewToOldPtr(this, old_cieh);

  _loading_klass     = (ciInstanceKlass*)FAM->getNewFromOldPtr(FAM->getOldPtr("((struct ciExceptionHandler*)%p)->_loading_klass", old_cieh));
  _catch_klass       = (ciInstanceKlass*)FAM->getNewFromOldPtr(FAM->getOldPtr("((struct ciExceptionHandler*)%p)->_catch_klass", old_cieh));

  _start             = FAM->getInt("((struct ciExceptionHandler*)%p)->_start", old_cieh);
  _limit             = FAM->getInt("((struct ciExceptionHandler*)%p)->_limit", old_cieh);
  _handler_bci       = FAM->getInt("((struct ciExceptionHandler*)%p)->_handler_bci", old_cieh);
  _catch_klass_index = FAM->getInt("((struct ciExceptionHandler*)%p)->_catch_klass_index", old_cieh);
}

ciInstanceKlass* ciExceptionHandler::catch_klass() {
  assert(!is_catch_all(), "bad index");
  if (_catch_klass == NULL) {
    bool will_link;
    ciKlass* k = CURRENT_ENV->get_klass_by_index(_loading_klass,
                                                 _catch_klass_index,
                                                 will_link);
    if (!will_link && k->is_loaded()) {
      GUARDED_VM_ENTRY(
        k = CURRENT_ENV->get_unloaded_klass(_loading_klass, k->name());
      )
    }
    _catch_klass = k->as_instance_klass();
  }
  return _catch_klass;
}

// ------------------------------------------------------------------
// ciExceptionHandler::print()
void ciExceptionHandler::print() {
  tty->print("<ciExceptionHandler start=%d limit=%d"
             " handler_bci=%d ex_klass_index=%d",
             start(), limit(), handler_bci(), catch_klass_index());
  if (_catch_klass != NULL) {
    tty->print(" ex_klass=");
_catch_klass->print(tty);
  }
  tty->print(">");
}
