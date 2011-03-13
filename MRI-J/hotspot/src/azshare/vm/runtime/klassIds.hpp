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

#ifndef KLASSIDS_HPP
#define KLASSIDS_HPP

#include "allocation.hpp"

// Referfence chain Klass IDs.
class KlassIds VALUE_OBJ_CLASS_SPEC{
public:
  // Enums for various kinds of pts-to data where the actual referrer is hard to determine.
  enum {
    new2old_root       = -1,
    old_system_root    = -2,
    new_system_root    = -3,
    j_l_ref_root       = -4,
    new_weak_jni_root  = -5,
    string_intern_root = -6,
    cardmark_root      = -7,
    jvm_internal_lvb   = -8,
    mutator_stack_ref  = -9, 
    lvb_asm            = -10,
    max_fake_kid       = -11
  };

  static bool is_valid(int klass_id);

  KlassIds() : _klass_ids(0) {}

  KlassIds(int referant, int referrer) :
    _klass_ids(((uint64_t) referant) | (((uint64_t) referrer) << 32))
  {
    assert0(is_valid(referant));
    assert0((referrer >= max_fake_kid) && (referrer <= 0 || is_valid(referrer)));
  }

  bool operator==(KlassIds other) { return _klass_ids == other._klass_ids; }

  bool is_null()  const { return _klass_ids == 0; }
  int  hash()     const { return referant() + referrer(); }
  int  count()    const { return 2; }
  int  referant() const { return (int) _klass_ids; }
  int  referrer() const { return (int) (_klass_ids >> 32); }

  int at(int k) const {
    assert((0 <= k) && (k < count()), "klass ID index out of range");
    return (int) (_klass_ids >> (k << 5));
  }

private:
  uint64_t _klass_ids;
};

#endif // KLASSIDS_HPP
