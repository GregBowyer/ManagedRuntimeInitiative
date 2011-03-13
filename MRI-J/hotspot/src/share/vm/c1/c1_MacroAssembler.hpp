/*
 * Copyright 2000-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_MACROASSEMBLER_HPP
#define C1_MACROASSEMBLER_HPP


#include "assembler_pd.hpp"
#include "codeBlob.hpp"
#include "ciInstanceKlass.hpp"

class CodeEmitInfo;
class CodeProfile;
class FrameMap;
class LIR_Assembler;

class C1_MacroAssembler: public MacroAssembler {
 public:
  // creation
  C1_MacroAssembler(const char *name, int compile_id) : MacroAssembler(Thread::current()->resource_area(),strcmp(name,"C1 stubs")?CodeBlob::c1:CodeBlob::runtime_stubs, compile_id, name) { pd_init(); }

  //----------------------------------------------------
  void explicit_null_check(Register base);

  void inline_cache_check(Register receiver, Register iCache);
  void build_frame(FrameMap* frame_map);
  void method_exit(FrameMap* frame_map);

  void entry( CodeProfile *cp );

#include "c1_MacroAssembler_pd.hpp"

  // runtime calls (return rel_pc/offset of call to be used by GC map)
  int call_RT(address entry, Register arg1, Register arg2, Register arg3);
  int call_RT(address entry, Register arg1, Register arg2, Register arg3, int save_mask);
};



class Address VALUE_OBJ_CLASS_SPEC {
 public:
  enum ScaleFactor {
    no_scale = -1,
    times_1  =  0,
    times_2  =  1,
    times_4  =  2,
    times_8  =  3
  };

 private:
  Register         _base;
  Register         _index;
  ScaleFactor      _scale;
  int              _disp;
  //RelocationHolder _rspec;

  // Easily misused constructors make them private
  //Address(int disp, address loc, relocInfo::relocType rtype);
  //Address(int disp, address loc, RelocationHolder spec);

 public:
  // creation
  Address()
    : _base(noreg),
      _index(noreg),
      _scale(no_scale),
      _disp(0) {
  }

  // No default displacement otherwise Register can be implicitly
  // converted to 0(Register) which is quite a different animal.

  Address(Register base, int disp)
    : _base(base),
      _index(noreg),
      _scale(no_scale),
      _disp(disp) {
  }

  Address(Register base, Register index, ScaleFactor scale, int disp = 0)
    : _base (base),
      _index(index),
      _scale(scale),
      _disp (disp) {
    assert(!is_valid_reg(index) == (scale == Address::no_scale), 
           "inconsistent address");
  }

  // The following two overloads are used in connection with the
  // ByteSize type (see sizes.hpp).  They simplify the use of
  // ByteSize'd arguments in assembly code. Note that their equivalent
  // for the optimized build are the member functions with int disp
  // argument since ByteSize is mapped to an int type in that case.
  //
  // Note: DO NOT introduce similar overloaded functions for WordSize
  // arguments as in the optimized mode, both ByteSize and WordSize
  // are mapped to the same type and thus the compiler cannot make a
  // distinction anymore (=> compiler errors).

#ifdef ASSERT
  Address(Register base, ByteSize disp)
    : _base(base),
      _index(noreg),
      _scale(no_scale),
      _disp(in_bytes(disp)) {
  }

  Address(Register base, Register index, ScaleFactor scale, ByteSize disp)
    : _base(base),
      _index(index),
      _scale(scale),
      _disp(in_bytes(disp)) {
    assert(!is_valid_reg(index) == (scale == Address::no_scale),
           "inconsistent address");
  }
#endif // ASSERT

  // accessors
  bool uses(Register reg) const { 
    return _base == reg || _index == reg; 
  }

  // Convert the raw encoding form into the form expected by the constructor for
  // Address.  An index of 4 (rsp) corresponds to having no index, so convert
  // that to noreg for the Address constructor.
  static Address make_raw(int base, int index, int scale, int disp);

  //static Address make_array(ArrayAddress);

 private:
  bool base_needs_rex() const {
    return _base != noreg && _base >= 8;
  }

  bool index_needs_rex() const {
    return _index != noreg && _index >= 8;
  }

  //relocInfo::relocType reloc() const { return _rspec.type(); }

  friend class Assembler;
  friend class MacroAssembler;
  friend class C1_MacroAssembler;
  friend class LIR_Assembler; // base/index/scale/disp
  friend class c1_FrameMap;
};

#endif // C1_MACROASSEMBLER_HPP
