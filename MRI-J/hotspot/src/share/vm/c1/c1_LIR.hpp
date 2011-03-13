/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_LIR_HPP
#define C1_LIR_HPP


#include "array.hpp"
#include "c1_Compilation.hpp"
#include "c1_ValueType.hpp"
#include "c1_globals.hpp"
#include "commonAsm.hpp"
#include "register_pd.hpp"

class BlockBegin;
class BlockList;
class CodeEmitInfo;
class CodeStub;
class CodeStubList;
class FpuStackSim;
class LIR_Assembler;
class LIR_Op;
class LIR_OpVisitState;
class ValueType;
class ciType;

//---------------------------------------------------------------------
//                 LIR Operands
//  LIR_OprDesc
//    LIR_OprPtr
//      LIR_Const
//      LIR_Address
//---------------------------------------------------------------------
class LIR_OprDesc;
class LIR_OprPtr;
class LIR_Const;
class LIR_Address;
class LIR_OprVisitor;


typedef LIR_OprDesc* LIR_Opr;
typedef int          RegNr;

define_array(LIR_OprArray, LIR_Opr)
define_stack(LIR_OprList, LIR_OprArray)

define_array(LIR_OprRefArray, LIR_Opr*)
define_stack(LIR_OprRefList, LIR_OprRefArray)

define_array(CodeEmitInfoArray, CodeEmitInfo*)
define_stack(CodeEmitInfoList, CodeEmitInfoArray)

define_array(LIR_OpArray, LIR_Op*)
define_stack(LIR_OpList, LIR_OpArray)

// define LIR_OprPtr early so LIR_OprDesc can refer to it
class LIR_OprPtr: public CompilationResourceObj {
 public:
  bool is_oop_pointer() const                    { return (type() == T_OBJECT); }
  bool is_float_kind() const                     { BasicType t = type(); return (t == T_FLOAT) || (t == T_DOUBLE); }

  virtual LIR_Const*  as_constant()              { return NULL; }
  virtual LIR_Address* as_address()              { return NULL; }
  virtual BasicType type() const                 = 0;
  virtual void print_value_on(outputStream* out) const = 0;
};



// LIR constants
class LIR_Const: public LIR_OprPtr {
 private:
  JavaValue _value;

  void type_check(BasicType t) const   { assert(type() == t, "type check"); }
  void type_check(BasicType t1, BasicType t2) const   { assert(type() == t1 || type() == t2, "type check"); }

 public:
  LIR_Const(jint i)                              { _value.set_type(T_INT);     _value.set_jint(i); }
  LIR_Const(jlong l)                             { _value.set_type(T_LONG);    _value.set_jlong(l); }
  LIR_Const(jfloat f)                            { _value.set_type(T_FLOAT);   _value.set_jfloat(f); }
  LIR_Const(jdouble d)                           { _value.set_type(T_DOUBLE);  _value.set_jdouble(d); }
  LIR_Const(jobject o)                           { _value.set_type(T_OBJECT);  _value.set_jobject(o); }
  LIR_Const(void* p) {
    assert(sizeof(jlong) >= sizeof(p), "too small");;
    _value.set_type(T_LONG);    _value.set_jlong((jlong)p);
  }
                                       
  virtual BasicType type()       const { return _value.get_type(); }
  virtual LIR_Const* as_constant()     { return this; }

  jint      as_jint()    const         { type_check(T_INT   ); return _value.get_jint(); }
  jlong     as_jlong()   const         { type_check(T_LONG  ); return _value.get_jlong(); }
  jfloat    as_jfloat()  const         { type_check(T_FLOAT ); return _value.get_jfloat(); }
  jdouble   as_jdouble() const         { type_check(T_DOUBLE); return _value.get_jdouble(); }
  jobject   as_jobject() const         { type_check(T_OBJECT); return _value.get_jobject(); }
  jint      as_jint_lo() const         { type_check(T_LONG  ); return low(_value.get_jlong()); }
  jint      as_jint_hi() const         { type_check(T_LONG  ); return high(_value.get_jlong()); }

  address   as_pointer() const         { type_check(T_LONG  ); return (address)_value.get_jlong(); }


  jint      as_jint_bits() const       { type_check(T_FLOAT, T_INT); return _value.get_jint(); }
  jlong     as_jlong_bits() const      {
    if (type() == T_DOUBLE) {
      return jlong_cast(_value.get_jdouble());
    } else {
      return as_jlong();
    }
  }
  jint      as_jint_lo_bits() const    {
    if (type() == T_DOUBLE) {
      return low(jlong_cast(_value.get_jdouble()));
    } else {
      return as_jint_lo();
    }
  }
  jint      as_jint_hi_bits() const    {
    if (type() == T_DOUBLE) {
      return high(jlong_cast(_value.get_jdouble()));
    } else {
      return as_jint_hi();
    }
  }

  virtual void print_value_on(outputStream* out) const PRODUCT_RETURN;


  bool is_zero_float() {
    jfloat f = as_jfloat();
    jfloat ok = 0.0f;
    return jint_cast(f) == jint_cast(ok);
  }
  
  bool is_one_float() {
    jfloat f = as_jfloat();
    return !g_isnan(f) && g_isfinite(f) && f == 1.0;
  }
  
  bool is_zero_double() {
    jdouble d = as_jdouble();
    jdouble ok = 0.0;
    return jlong_cast(d) == jlong_cast(ok);
  }
  
  bool is_one_double() {
    jdouble d = as_jdouble();
    return !g_isnan(d) && g_isfinite(d) && d == 1.0;
  }
};


//---------------------LIR Operand descriptor------------------------------------
//
// The class LIR_OprDesc represents a LIR instruction operand;
// it can be a register (ALU/FPU), stack location or a constant;
// Constants and addresses are represented as resource area allocated 
// structures (see above).
// Registers and stack locations are inlined into the this pointer
// (see value function).

class LIR_OprDesc: public CompilationResourceObj {
 public:
  // value structure:
  //     data       opr-type opr-kind
  // +--------------+-------+-------+
  // [max...........|7 6 5 4|3 2 1 0]
  //                             ^
  //                    is_pointer bit
  // 
  // lowest bit cleared, means it is a structure pointer
  // we need  4 bits to represent types

 private:
  friend class LIR_OprFact;

  // Conversion
  intptr_t value() const                         { return (intptr_t) this; }

  bool check_value_mask(intptr_t mask, intptr_t masked_value) const {
    return (value() & mask) == masked_value;
  }

  enum OprKind {
      pointer_value      = 0
    , stack_value        = 1
    , cpu_register       = 3
    , fpu_register       = 5
    , illegal_value      = 7
  };

  enum OprBits {
      pointer_bits   = 1
    , kind_bits      = 3
    , type_bits      = 4
    , size_bits      = 2
    , destroyed_bits  = 1
    , virtual_bits   = 1
    , is_xmm_bits    = 1
    , last_use_bits  = 1
,non_data_bits=kind_bits+type_bits+size_bits+destroyed_bits+last_use_bits+
                       virtual_bits + is_xmm_bits
    , data_bits      = BitsPerInt - non_data_bits
    , reg_bits       = data_bits / 2      // for two registers in one value encoding
  };

  // X | VLDS | STTT | TKKK
  // Kind: 0=pointer, 1=stack, 3=cpu, 5=fpu
  // Type: 1=int, 2=long, 3=obj
  //   
  enum OprShift {
      kind_shift      = 0
    , type_shift      = kind_shift     + kind_bits
    , size_shift      = type_shift     + type_bits
,destroyed_shift=size_shift+size_bits
    , last_use_shift  = destroyed_shift + destroyed_bits
,virtual_shift=last_use_shift+last_use_bits
    , is_xmm_shift    = virtual_shift + virtual_bits
    , data_shift      = is_xmm_shift + is_xmm_bits
    , reg1_shift      = data_shift
    , reg2_shift      = data_shift + reg_bits

  };

  enum OprSize {
      single_size = 0 << size_shift
    , double_size = 1 << size_shift
  };

  enum OprMask {
      kind_mask      = right_n_bits(kind_bits)
    , type_mask      = right_n_bits(type_bits) << type_shift
    , size_mask      = right_n_bits(size_bits) << size_shift
    , destroyed_mask = right_n_bits(destroyed_bits) << destroyed_shift
    , last_use_mask  = right_n_bits(last_use_bits) << last_use_shift
    , virtual_mask   = right_n_bits(virtual_bits) << virtual_shift
    , is_xmm_mask    = right_n_bits(is_xmm_bits) << is_xmm_shift
    , pointer_mask   = right_n_bits(pointer_bits)
    , lower_reg_mask = right_n_bits(reg_bits)
,no_type_mask=(int)(~(type_mask|last_use_mask))
  };

  uintptr_t data() const                         { return value() >> data_shift; }
  int lo_reg_half() const                        { return data() & lower_reg_mask; }
  int hi_reg_half() const                        { return (data() >> reg_bits) & lower_reg_mask; }
  OprKind kind_field() const                     { return (OprKind)(value() & kind_mask); }
  OprSize size_field() const                     { return (OprSize)(value() & size_mask); }

  static char type_char(BasicType t);

 public:
  enum {
    vreg_base = ConcreteRegisterImpl::number_of_registers,
    vreg_max = (1 << data_bits) - 1
  };

  static inline LIR_Opr illegalOpr();

  enum OprType {
      unknown_type  = 0 << type_shift    // means: not set (catch uninitialized types)
    , int_type      = 1 << type_shift    
    , long_type     = 2 << type_shift    
    , object_type   = 3 << type_shift    
    , pointer_type  = 4 << type_shift    
    , float_type    = 5 << type_shift    
    , double_type   = 6 << type_shift    
  };
  friend OprType as_OprType(BasicType t);
  friend BasicType as_BasicType(OprType t);

  OprType type_field_valid() const               { assert(is_register() || is_stack(), "should not be called otherwise"); return (OprType)(value() & type_mask); }
  OprType type_field() const                     { return is_illegal() ? unknown_type : (OprType)(value() & type_mask); }

  static OprSize size_for(BasicType t) {
    switch (t) {
      case T_LONG:
      case T_DOUBLE:
        return double_size;
        break;

      case T_FLOAT:
      case T_BOOLEAN:
      case T_CHAR:
      case T_BYTE:
      case T_SHORT:
      case T_INT:
      case T_OBJECT:
      case T_ARRAY:
        return single_size;
        break;
        
      default:
        ShouldNotReachHere();
        return single_size;
      }
  }


  void validate_type() const PRODUCT_RETURN;

  BasicType type() const {
    if (is_pointer()) {
      return pointer()->type();
    }
    return as_BasicType(type_field());
  }


  ValueType* value_type() const                  { return as_ValueType(type()); }

  char type_char() const                         { return type_char((is_pointer()) ? pointer()->type() : type()); }

  bool is_equal(LIR_Opr opr) const         { return this == opr; }
  // checks whether types are same
  bool is_same_type(LIR_Opr opr) const     {
    assert(type_field() != unknown_type &&
           opr->type_field() != unknown_type, "shouldn't see unknown_type");
    return type_field() == opr->type_field();
  }
  bool is_same_register(LIR_Opr opr) {
    return (is_register() && opr->is_register() &&
            kind_field() == opr->kind_field() &&
            (value() & no_type_mask) == (opr->value() & no_type_mask));
  }

  bool is_pointer() const      { return check_value_mask(pointer_mask, pointer_value); }
  bool is_illegal() const      { return kind_field() == illegal_value; }
  bool is_valid() const        { return kind_field() != illegal_value; }

bool is_register()const{return is_cpu_register()||is_xmm_register();}
bool is_virtual()const{return is_virtual_cpu()||is_virtual_xmm();}

  bool is_constant() const     { return is_pointer() && pointer()->as_constant() != NULL; }
  bool is_address() const      { return is_pointer() && pointer()->as_address() != NULL; }

  bool is_float_kind() const   { return is_pointer() ? pointer()->is_float_kind() : (kind_field() == fpu_register); }
  bool is_oop() const;

  // semantic for fpu- and xmm-registers:
  // * is_float and is_double return true for xmm_registers 
  //   (so is_single_xmm are true)
  // * So you must always check for is_???_xmm prior to is_???_fpu to
  //   distinguish between fpu- and xmm-registers
  
  bool is_stack() const        { validate_type(); return check_value_mask(kind_mask,                stack_value);                 }
  bool is_single_stack() const { validate_type(); return check_value_mask(kind_mask | size_mask,    stack_value  | single_size);  }
  bool is_double_stack() const { validate_type(); return check_value_mask(kind_mask | size_mask,    stack_value  | double_size);  }

  bool is_cpu_register() const { validate_type(); return check_value_mask(kind_mask,                cpu_register);                }
  bool is_virtual_cpu() const  { validate_type(); return check_value_mask(kind_mask | virtual_mask, cpu_register | virtual_mask); }
  bool is_fixed_cpu() const    { validate_type(); return check_value_mask(kind_mask | virtual_mask, cpu_register);                }
  bool is_single_cpu() const   { validate_type(); return check_value_mask(kind_mask | size_mask,    cpu_register | single_size);  }
  bool is_double_cpu() const   { validate_type(); return check_value_mask(kind_mask | size_mask,    cpu_register | double_size);  }

  bool is_virtual_xmm() const  { validate_type(); return check_value_mask(kind_mask | is_xmm_mask | virtual_mask, fpu_register | is_xmm_mask | virtual_mask); }
  bool is_xmm_register() const { validate_type(); return check_value_mask(kind_mask | is_xmm_mask,             fpu_register | is_xmm_mask); }
  bool is_single_xmm() const   { validate_type(); return check_value_mask(kind_mask | size_mask | is_xmm_mask, fpu_register | single_size | is_xmm_mask); }
  bool is_double_xmm() const   { validate_type(); return check_value_mask(kind_mask | size_mask | is_xmm_mask, fpu_register | double_size | is_xmm_mask); }

  // fast accessor functions for special bits that do not work for pointers
  // (in this functions, the check for is_pointer() is omitted)
  bool is_single_word() const      { assert(is_register() || is_stack(), "type check"); return check_value_mask(size_mask, single_size); }
  bool is_double_word() const      { assert(is_register() || is_stack(), "type check"); return check_value_mask(size_mask, double_size); }
  bool is_virtual_register() const { assert(is_register(),               "type check"); return check_value_mask(virtual_mask, virtual_mask); }
  bool is_oop_register() const     { assert(is_register() || is_stack(), "type check"); return type_field_valid() == object_type; }
  BasicType type_register() const  { assert(is_register() || is_stack(), "type check"); return as_BasicType(type_field_valid());  }

  bool is_last_use() const         { assert(is_register(), "only works for registers"); return (value() & last_use_mask) != 0; }
  LIR_Opr make_last_use()          { assert(is_register(), "only works for registers"); return (LIR_Opr)(value() | last_use_mask); }

bool is_destroyed()const{assert(is_register(),"only works for registers");return(value()&destroyed_mask)!=0;}
  LIR_Opr set_destroyed()          { assert(is_register(), "only works for registers"); assert0(!is_destroyed()); return (LIR_Opr)(value() | destroyed_mask); }

  int single_stack_ix() const  { assert(is_single_stack() && !is_virtual(), "type check"); return (int)data(); }
  int double_stack_ix() const  { assert(is_double_stack() && !is_virtual(), "type check"); return (int)data(); }
RegNr cpu_regnr()const{assert(is_cpu_register()&&!is_virtual(),"type check");return(RegNr)lo_reg_half();}
  RegNr cpu_regnrLo() const    { assert(is_double_cpu()   && !is_virtual(), "type check"); return (RegNr)lo_reg_half(); }
  RegNr cpu_regnrHi() const    { assert(is_double_cpu()   && !is_virtual(), "type check"); return (RegNr)hi_reg_half(); }
RegNr xmm_regnr()const{assert(is_xmm_register()&&!is_virtual(),"type check");return(RegNr)data();}
  int   vreg_number() const    { assert(is_virtual(),                       "type check"); return (RegNr)data(); }

  LIR_OprPtr* pointer()  const                   { assert(is_pointer(), "type check");      return (LIR_OprPtr*)this; }
  LIR_Const* as_constant_ptr() const             { return pointer()->as_constant(); }
  LIR_Address* as_address_ptr() const            { return pointer()->as_address(); }

  Register as_register()    const;
  Register as_register_lo() const;
  Register as_register_hi() const;

  Register as_pointer_register() {
    if (is_double_cpu()) {
      assert(as_register_lo() == as_register_hi(), "should be a single register");
      return as_register_lo();
    }
    return as_register();
  }

#ifdef X86_64
FRegister as_xmm_float_reg()const;
FRegister as_xmm_double_reg()const;
#endif
  // for compatibility with RInfo
  int fpu () const                                  { return lo_reg_half(); }

  jint      as_jint()    const { return as_constant_ptr()->as_jint(); }
  jlong     as_jlong()   const { return as_constant_ptr()->as_jlong(); }
  jfloat    as_jfloat()  const { return as_constant_ptr()->as_jfloat(); }
  jdouble   as_jdouble() const { return as_constant_ptr()->as_jdouble(); }
  jobject   as_jobject() const { return as_constant_ptr()->as_jobject(); }

  void print() const PRODUCT_RETURN;
  void print(outputStream* out) const PRODUCT_RETURN;
};


inline LIR_OprDesc::OprType as_OprType(BasicType type) {
  switch (type) {
  case T_INT:      return LIR_OprDesc::int_type;
  case T_LONG:     return LIR_OprDesc::long_type;
  case T_FLOAT:    return LIR_OprDesc::float_type;
  case T_DOUBLE:   return LIR_OprDesc::double_type;
  case T_OBJECT:
  case T_ARRAY:    return LIR_OprDesc::object_type;
  case T_ILLEGAL:  // fall through
  default: ShouldNotReachHere(); return LIR_OprDesc::unknown_type;
  }
}

inline BasicType as_BasicType(LIR_OprDesc::OprType t) {
  switch (t) {
  case LIR_OprDesc::int_type:     return T_INT;
  case LIR_OprDesc::long_type:    return T_LONG;
  case LIR_OprDesc::float_type:   return T_FLOAT;
  case LIR_OprDesc::double_type:  return T_DOUBLE;
  case LIR_OprDesc::object_type:  return T_OBJECT;
  case LIR_OprDesc::unknown_type: // fall through
  default: ShouldNotReachHere();  return T_ILLEGAL;
  }
}


// LIR_Address
class LIR_Address: public LIR_OprPtr {
 friend class LIR_OpVisitState;

 public:
  // NOTE: currently these must be the log2 of the scale factor (and
  // must also be equivalent to the ScaleFactor enum in
  // assembler_i486.hpp)
  enum Scale {
    times_1  =  0,
    times_2  =  1,
    times_4  =  2,
    times_8  =  3
  };

 private:
  LIR_Opr   _base;
  LIR_Opr   _index;
  Scale     _scale;
  intx      _disp;
  BasicType _type;

 public:
  LIR_Address(LIR_Opr base, LIR_Opr index, BasicType type): 
       _base(base)
     , _index(index)
     , _scale(times_1)
     , _type(type)
     , _disp(0) { verify(); }

  LIR_Address(LIR_Opr base, int disp, BasicType type): 
       _base(base)
     , _index(LIR_OprDesc::illegalOpr())
     , _scale(times_1)
     , _type(type)
     , _disp(disp) { verify(); }

  LIR_Address(LIR_Opr base, LIR_Opr index, Scale scale, int disp, BasicType type): 
       _base(base)
     , _index(index)
     , _scale(scale)
     , _type(type)
     , _disp(disp) { verify(); }

  LIR_Opr base()  const                          { return _base;  }
  LIR_Opr index() const                          { return _index; }
  Scale   scale() const                          { return _scale; }
  intx    disp()  const                          { return _disp;  }

  bool equals(LIR_Address* other) const          { return base() == other->base() && index() == other->index() && disp() == other->disp() && scale() == other->scale(); }

  virtual LIR_Address* as_address()              { return this;   }
  virtual BasicType type() const                 { return _type; }
  virtual void print_value_on(outputStream* out) const PRODUCT_RETURN;

  void verify() const PRODUCT_RETURN;

  static Scale scale(BasicType type);
};


// operand factory
class LIR_OprFact: public AllStatic {
 public:

  static LIR_Opr illegalOpr;

  static LIR_Opr single_cpu(int reg) {
assert(reg>=0&&reg<nof_registers,"out of bounds");
    return (LIR_Opr)((reg  << LIR_OprDesc::reg1_shift) |                                     LIR_OprDesc::int_type    | LIR_OprDesc::cpu_register | LIR_OprDesc::single_size);
  }
  static LIR_Opr single_cpu_oop(int reg) {
assert(reg>=0&&reg<nof_registers,"out of bounds");
    return (LIR_Opr)((reg  << LIR_OprDesc::reg1_shift) |                                     LIR_OprDesc::object_type | LIR_OprDesc::cpu_register | LIR_OprDesc::single_size);
  }
  static LIR_Opr double_cpu(int reg1, int reg2) {
    assert (reg1 >= 0 && reg1 < nof_registers, "out of bounds");
    return (LIR_Opr)((reg1 << LIR_OprDesc::reg1_shift) | (reg2 << LIR_OprDesc::reg2_shift) | LIR_OprDesc::long_type   | LIR_OprDesc::cpu_register | LIR_OprDesc::double_size);
  }

  // Create an LIR operand for the given single precision XMM register number
  static LIR_Opr single_xmm(int reg) {
#ifdef AZ_X86
assert(reg>=0&&reg<nof_float_registers,"out of bounds");
    return (LIR_Opr)((reg  << LIR_OprDesc::reg1_shift) |                                     LIR_OprDesc::float_type  | LIR_OprDesc::fpu_register | LIR_OprDesc::single_size | LIR_OprDesc::is_xmm_mask);
#else
ShouldNotReachHere();//have no notion of XMM registers
    return illegalOpr;
#endif
  }

  // Create an LIR operand for the given double precision XMM register number
  static LIR_Opr double_xmm(int reg) {
#ifdef AZ_X86
assert(reg>=0&&reg<nof_float_registers,"out of bounds");
    return (LIR_Opr)((reg  << LIR_OprDesc::reg1_shift) |                                    LIR_OprDesc::double_type | LIR_OprDesc::fpu_register | LIR_OprDesc::double_size | LIR_OprDesc::is_xmm_mask);
#else
ShouldNotReachHere();//have no notion of XMM registers
    return illegalOpr;
#endif
  }

  static LIR_Opr virtual_register(int index, BasicType type) {
    LIR_Opr res;
    switch (type) {
      case T_OBJECT: // fall through
      case T_ARRAY:  res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::object_type | LIR_OprDesc::cpu_register | LIR_OprDesc::single_size | LIR_OprDesc::virtual_mask); break;
      case T_INT:    res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::int_type    | LIR_OprDesc::cpu_register | LIR_OprDesc::single_size | LIR_OprDesc::virtual_mask); break;
      case T_LONG:   res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::long_type   | LIR_OprDesc::cpu_register | LIR_OprDesc::double_size | LIR_OprDesc::virtual_mask); break;
case T_FLOAT:res=(LIR_Opr)((index<<LIR_OprDesc::data_shift)|LIR_OprDesc::float_type|LIR_OprDesc::fpu_register|LIR_OprDesc::single_size|LIR_OprDesc::virtual_mask|LIR_OprDesc::is_xmm_mask);break;
case T_DOUBLE:res=(LIR_Opr)((index<<LIR_OprDesc::data_shift)|LIR_OprDesc::double_type|LIR_OprDesc::fpu_register|LIR_OprDesc::double_size|LIR_OprDesc::virtual_mask|LIR_OprDesc::is_xmm_mask);break;

      default:       ShouldNotReachHere(); res = illegalOpr;
    }

#ifdef ASSERT
    res->validate_type();
    assert(res->vreg_number() == index, "conversion check");
    assert(index >= LIR_OprDesc::vreg_base, "must start at vreg_base");
    assert(index <= (max_jint >> LIR_OprDesc::data_shift), "index is too big");

    // old-style calculation; check if old and new method are equal
    LIR_OprDesc::OprType t = as_OprType(type);
    LIR_Opr old_res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | t |
((type==T_FLOAT||type==T_DOUBLE)?LIR_OprDesc::fpu_register|LIR_OprDesc::is_xmm_mask:LIR_OprDesc::cpu_register)|
                               LIR_OprDesc::size_for(type) | LIR_OprDesc::virtual_mask);
    assert(res == old_res, "old and new method not equal");
    assert0( (intptr_t)res >= -1 );
#endif
  
    return res;
  }

  // 'index' is computed by FrameMap::local_stack_pos(index); do not use other parameters as
  // the index is platform independent; a double stack useing indeces 2 and 3 has always
  // index 2.
  static LIR_Opr stack(int index, BasicType type) {
    LIR_Opr res;
    switch (type) {
      case T_OBJECT: // fall through
      case T_ARRAY:  res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::object_type | LIR_OprDesc::stack_value | LIR_OprDesc::single_size); break;
      case T_INT:    res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::int_type    | LIR_OprDesc::stack_value | LIR_OprDesc::single_size); break;
      case T_LONG:   res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::long_type   | LIR_OprDesc::stack_value | LIR_OprDesc::double_size); break;
      case T_FLOAT:  res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::float_type  | LIR_OprDesc::stack_value | LIR_OprDesc::single_size); break;
      case T_DOUBLE: res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::double_type | LIR_OprDesc::stack_value | LIR_OprDesc::double_size); break;

      default:       ShouldNotReachHere(); res = illegalOpr;
    }

#ifdef ASSERT
    assert(index >= 0, "index must be positive");
    assert(index <= (max_jint >> LIR_OprDesc::data_shift), "index is too big");

    LIR_Opr old_res = (LIR_Opr)((index << LIR_OprDesc::data_shift) | LIR_OprDesc::stack_value | as_OprType(type) | LIR_OprDesc::size_for(type));
    assert(res == old_res, "old and new method not equal");
    assert0( (intptr_t)res >= -1 );
#endif

    return res;
  }

  static LIR_Opr intConst(jint i)                { return (LIR_Opr)(new LIR_Const(i)); }
  static LIR_Opr longConst(jlong l)              { return (LIR_Opr)(new LIR_Const(l)); }
  static LIR_Opr floatConst(jfloat f)            { return (LIR_Opr)(new LIR_Const(f)); }
  static LIR_Opr doubleConst(jdouble d)          { return (LIR_Opr)(new LIR_Const(d)); }
  static LIR_Opr oopConst(jobject o)             { return (LIR_Opr)(new LIR_Const(o)); }
  static LIR_Opr address(LIR_Address* a)         { return (LIR_Opr)a; }
  static LIR_Opr intptrConst(void* p)            { return (LIR_Opr)(new LIR_Const(p)); }
  static LIR_Opr intptrConst(intptr_t v)         { return (LIR_Opr)(new LIR_Const((void*)v)); }
  static LIR_Opr illegal()                       { return (LIR_Opr)-1; }

  static LIR_Opr value_type(ValueType* type);
  static LIR_Opr dummy_value_type(ValueType* type);
};


//-------------------------------------------------------------------------------
//                   LIR Instructions
//-------------------------------------------------------------------------------
//
// Note: 
//  - every instruction has a result operand
//  - every instruction has an CodeEmitInfo operand (can be revisited later)
//  - every instruction has a LIR_OpCode operand
//  - LIR_OpN, means an instruction that has N input operands
//
// class hierarchy:
//
class  LIR_Op;
class    LIR_Op0;
class      LIR_OpLabel;
class    LIR_Op1;
class      LIR_OpConvert;
class      LIR_OpAlloc;
class      LIR_OpSafepoint;
class    LIR_Op2;
class    LIR_Op3;
class    LIR_OpBranch;
class    LIR_OpCall;
class      LIR_OpJavaCall;
class      LIR_OpRTCall;
class    LIR_OpArrayCopy;
class    LIR_OpStringEquals;
class    LIR_OpLock;
class    LIR_OpTypeCheck;
class    LIR_OpCompareAndSwap;
class    LIR_OpProfileCall;


// LIR operation codes
enum LIR_Code {
    lir_none
  , begin_op0
      , lir_word_align
      , lir_label
      , lir_nop
      , lir_backwardbranch_target
      , lir_std_entry
      , lir_osr_entry
      , lir_build_frame
      , lir_breakpoint
      , lir_rtcall
      , lir_membar
      , lir_membar_acquire
      , lir_membar_release
      , lir_get_thread
      , lir_profile_invoke
  , end_op0
  , begin_op1
      , lir_push
      , lir_pop
      , lir_null_check
      , lir_return
      , lir_leal
      , lir_neg
      , lir_branch
      , lir_cond_float_branch
      , lir_move
      , lir_prefetchr
      , lir_prefetchw
      , lir_klassTable_oop_load
      , lir_safepoint
      , lir_convert
      , lir_alloc_object
      , lir_monaddr
      , lir_bit_test
  , end_op1
  , begin_op2
      , lir_cmp
      , lir_cmp_l2i
      , lir_ucmp_fd2i
      , lir_cmp_fd2i
      , lir_cmove
      , lir_add
      , lir_sub
      , lir_mul
      , lir_mul_strictfp
      , lir_div
      , lir_div_strictfp
      , lir_rem
      , lir_sqrt
      , lir_abs
      , lir_sin
      , lir_cos
      , lir_tan
      , lir_log
      , lir_log10
      , lir_logic_and
      , lir_logic_or
      , lir_logic_xor
      , lir_shl
      , lir_shr
      , lir_ushr
      , lir_alloc_array
      , lir_throw
      , lir_unwind
      , lir_compare_to
  , end_op2
  , begin_op3
      , lir_idiv
      , lir_irem
      , lir_ldiv
      , lir_lrem
  , end_op3
  , begin_opJavaCall
      , lir_static_call
      , lir_optvirtual_call
      , lir_icvirtual_call
      , lir_virtual_call
  , end_opJavaCall
  , begin_opArrayCopy
      , lir_arraycopy
  , end_opArrayCopy
  , begin_opStringEquals
      , lir_stringequals
  , end_opStringEquals
  , begin_opLock
    , lir_lock
    , lir_unlock
  , end_opLock
  , begin_opTypeCheck
    , lir_instanceof
    , lir_checkcast
    , lir_store_check
  , end_opTypeCheck
  , begin_opCompareAndSwap
    , lir_cas_long
    , lir_cas_obj
    , lir_cas_int
  , end_opCompareAndSwap
  , begin_opMDOProfile
    , lir_profile_call
  , end_opMDOProfile
};


enum LIR_Condition {
    lir_cond_equal
  , lir_cond_notEqual
  , lir_cond_less
  , lir_cond_lessEqual
  , lir_cond_greaterEqual
  , lir_cond_greater
  , lir_cond_belowEqual
  , lir_cond_aboveEqual
  , lir_cond_always
  , lir_cond_carry
  , lir_cond_notCarry
  , lir_cond_unknown = -1
};


enum LIR_PatchCode { 
  lir_patch_none,
  lir_patch_low,
  lir_patch_high,
  lir_patch_normal
};


enum LIR_MoveKind {
  lir_move_normal,
  lir_move_volatile,
  lir_move_unaligned,
  lir_move_max_flag
};


// --------------------------------------------------
// LIR_Op
// --------------------------------------------------
class LIR_Op: public CompilationResourceObj {
 friend class LIR_OpVisitState;

 protected:
  LIR_Opr       _result;
  unsigned short _code;
  unsigned short _flags;
  CodeEmitInfo* _info;
  int           _id;     // value id for register allocation
  int           _fpu_pop_count;
  Instruction*  _source; // for debugging

#ifdef ASSERT
 private:
  const char *  _file;
  int           _line;
#endif
 protected:

  static void print_condition(outputStream* out, LIR_Condition cond) PRODUCT_RETURN;

  static bool is_in_range(LIR_Code test, LIR_Code start, LIR_Code end)  { return start < test && test < end; }

 public:
  LIR_Op() 
    : _result(LIR_OprFact::illegalOpr)
    , _code(lir_none)
    , _flags(0)
    , _info(NULL)
    , _id(-1)
    , _fpu_pop_count(0)
    , _source(NULL)
#ifdef ASSERT
    , _file(NULL)
    , _line(0)
#endif
{}

  LIR_Op(LIR_Code code, LIR_Opr result, CodeEmitInfo* info)
    : _result(result)
    , _code(code) 
    , _flags(0)
    , _info(info)
    , _id(-1)
    , _fpu_pop_count(0)
    , _source(NULL)
#ifdef ASSERT
    , _file(NULL)
    , _line(0)
#endif
{}

  CodeEmitInfo* info() const                  { return _info;   }
  LIR_Code code()      const                  { return (LIR_Code)_code;   }
  LIR_Opr result_opr() const                  { return _result; }
  void    set_result_opr(LIR_Opr opr)         { _result = opr;  }

#ifdef ASSERT
  void set_file_and_line(const char * file, int line) {
    _file = file;
    _line = line;
  }
#endif

  virtual const char * name() const PRODUCT_RETURN0;

  int id()             const                  { return _id;     }
  void set_id(int id)                         { _id = id; }

  Instruction* source() const                 { return _source; }
  void set_source(Instruction* ins)           { _source = ins; }

  virtual void emit_code(LIR_Assembler* masm) = 0;
  virtual void print_instr(outputStream* out) const   = 0;
  virtual void print_on(outputStream* st) const PRODUCT_RETURN;

  virtual LIR_OpCall* as_OpCall() { return NULL; }
  virtual LIR_OpJavaCall* as_OpJavaCall() { return NULL; }
  virtual LIR_OpLabel* as_OpLabel() { return NULL; }
  virtual LIR_OpLock* as_OpLock() { return NULL; }
  virtual LIR_OpAlloc* as_OpAlloc() { return NULL; }
  virtual LIR_OpBranch* as_OpBranch() { return NULL; }
  virtual LIR_OpRTCall* as_OpRTCall() { return NULL; }
  virtual LIR_OpConvert* as_OpConvert() { return NULL; }
  virtual LIR_OpSafepoint* as_OpSafepoint() { return NULL; }
  virtual LIR_Op0* as_Op0() { return NULL; }
  virtual LIR_Op1* as_Op1() { return NULL; }
  virtual LIR_Op2* as_Op2() { return NULL; }
  virtual LIR_Op3* as_Op3() { return NULL; }
  virtual LIR_OpArrayCopy* as_OpArrayCopy() { return NULL; }
  virtual LIR_OpStringEquals* as_OpStringEquals() { return NULL; }
  virtual LIR_OpTypeCheck* as_OpTypeCheck() { return NULL; }
  virtual LIR_OpCompareAndSwap* as_OpCompareAndSwap() { return NULL; }
  virtual LIR_OpProfileCall* as_OpProfileCall() { return NULL; }

  virtual void verify() const {}
};

// for calls
class LIR_OpCall: public LIR_Op {
 friend class LIR_OpVisitState;

 protected:
  address      _addr;
  LIR_OprList* _arguments;
 protected:
  LIR_OpCall(LIR_Code code, address addr, LIR_Opr result,
             LIR_OprList* arguments, CodeEmitInfo* info = NULL)
    : LIR_Op(code, result, info)
    , _addr(addr)
    , _arguments(arguments) {}

 public:
  address addr() const                           { return _addr; }
  const LIR_OprList* arguments() const           { return _arguments; }
  virtual LIR_OpCall* as_OpCall()                { return this; }
};


// --------------------------------------------------
// LIR_OpJavaCall
// --------------------------------------------------
class LIR_OpJavaCall: public LIR_OpCall {
 friend class LIR_OpVisitState;

 private:
  ciMethod*       _method;
  LIR_Opr         _receiver;

 public:
  LIR_OpJavaCall(LIR_Code code, ciMethod* method,
                 LIR_Opr receiver, LIR_Opr result,
                 address addr, LIR_OprList* arguments,
                 CodeEmitInfo* info)
  : LIR_OpCall(code, addr, result, arguments, info)
,_method(method)
,_receiver(receiver){assert(is_in_range(code,begin_opJavaCall,end_opJavaCall),"code check");}

  LIR_OpJavaCall(LIR_Code code, ciMethod* method,
                 LIR_Opr receiver, LIR_Opr result, intptr_t vtable_offset,
                 LIR_OprList* arguments, CodeEmitInfo* info)
  : LIR_OpCall(code, (address)vtable_offset, result, arguments, info)
,_method(method)
,_receiver(receiver){assert(is_in_range(code,begin_opJavaCall,end_opJavaCall),"code check");}

  LIR_Opr receiver() const                       { return _receiver; }
  ciMethod* method() const                       { return _method;   }

  intptr_t vtable_offset() const {
    assert(_code == lir_virtual_call, "only have vtable for real vcall");
    return (intptr_t) addr();
  }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpJavaCall* as_OpJavaCall() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// --------------------------------------------------
// LIR_OpLabel
// --------------------------------------------------
// Location where a branch can continue
class LIR_OpLabel: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  Label* _label;
 public:
  LIR_OpLabel(Label* lbl)
   : LIR_Op(lir_label, LIR_OprFact::illegalOpr, NULL)
   , _label(lbl)                                 {}
  Label* label() const                           { return _label; }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpLabel* as_OpLabel() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// LIR_OpArrayCopy
class LIR_OpArrayCopy: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Opr   _src;
  LIR_Opr   _src_pos;
  LIR_Opr   _dst;
  LIR_Opr   _dst_pos;
  LIR_Opr   _length;
  LIR_Opr   _tmp;
  LIR_Opr   _tmp1;
  LIR_Opr   _tmp2;
  ciArrayKlass* _expected_type;
  int       _flags;

public:
  enum Flags {
    src_null_check         = 1 << 0,
    dst_null_check         = 1 << 1,
    src_pos_positive_check = 1 << 2,
    dst_pos_positive_check = 1 << 3,
    length_positive_check  = 1 << 4,
    src_range_check        = 1 << 5,
    dst_range_check        = 1 << 6,
    type_check             = 1 << 7,
overlap_check=1<<8,
all_flags=(1<<9)-1
  };

  LIR_OpArrayCopy(LIR_Opr src, LIR_Opr src_pos, LIR_Opr dst, LIR_Opr dst_pos, LIR_Opr length,
                  LIR_Opr tmp, ciArrayKlass* expected_type, int flags, CodeEmitInfo* info);

  LIR_Opr src() const                            { return _src; }
  LIR_Opr src_pos() const                        { return _src_pos; }
  LIR_Opr dst() const                            { return _dst; }
  LIR_Opr dst_pos() const                        { return _dst_pos; }
  LIR_Opr length() const                         { return _length; }
  int flags() const                              { return _flags; }
  ciArrayKlass* expected_type() const            { return _expected_type; }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpArrayCopy* as_OpArrayCopy() { return this; }
  void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// LIR_OpStringEquals
class LIR_OpStringEquals:public LIR_Op{
 friend class LIR_OpVisitState;

 private:
LIR_Opr _this_string;
LIR_Opr _other_string;
LIR_Opr _res;
LIR_Opr _tmp1;
LIR_Opr _tmp2;
LIR_Opr _tmp3;
LIR_Opr _tmp4;
LIR_Opr _tmp5;
LIR_Opr _tmp6;

 public:

  LIR_OpStringEquals(LIR_Opr this_string, LIR_Opr other_string, LIR_Opr res,
                     LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3,
                     LIR_Opr tmp4, LIR_Opr tmp5, LIR_Opr tmp6, CodeEmitInfo* info);

  LIR_Opr this_string() const                   { return _this_string;  }
  LIR_Opr other_string() const                  { return _other_string; }
  LIR_Opr res() const                           { return _res;          }
  LIR_Opr tmp1() const                          { return _tmp1;         }
  LIR_Opr tmp2() const                          { return _tmp2;         }
  LIR_Opr tmp3() const                          { return _tmp3;         }
  LIR_Opr tmp4() const                          { return _tmp4;         }
  LIR_Opr tmp5() const                          { return _tmp5;         }
  LIR_Opr tmp6() const                          { return _tmp6;         }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpStringEquals* as_OpStringEquals() { return this; }
  void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// --------------------------------------------------
// LIR_Op0
// --------------------------------------------------
class LIR_Op0: public LIR_Op {
  friend class LIR_OpVisitState;
  LIR_Opr         _tmp1;  // platform dependent temp 1

 public:
  LIR_Op0(LIR_Code code)
:LIR_Op(code,LIR_OprFact::illegalOpr,NULL)
   , _tmp1(LIR_OprFact::illegalOpr) { assert(is_in_range(code, begin_op0, end_op0), "code check"); }
  LIR_Op0(LIR_Code code, LIR_Opr result, CodeEmitInfo* info = NULL)
   : LIR_Op(code, result, info)
   , _tmp1(LIR_OprFact::illegalOpr) { assert(is_in_range(code, begin_op0, end_op0), "code check"); }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_Op0* as_Op0() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;

void set_tmp1_opr(LIR_Opr opr){_tmp1=opr;}
LIR_Opr tmp1_opr()const{return _tmp1;}
};


// --------------------------------------------------
// LIR_Op1
// --------------------------------------------------

class LIR_Op1: public LIR_Op {
 friend class LIR_OpVisitState;

 protected:
  LIR_Opr         _opr;   // input operand
  LIR_PatchCode   _patch; // only required with patchin (NEEDS_CLEANUP: do we want a special instruction for patching?)
  BasicType       _type;  // Operand types
  LIR_Opr         _tmp1;  // platform dependent temp 1
  LIR_Opr         _tmp2;  // platform dependent temp 2
  LIR_Opr         _tmp3;  // platform dependent temp 3

  static void print_patch_code(outputStream* out, LIR_PatchCode code);

  void set_kind(LIR_MoveKind kind) {
    assert(code() == lir_move, "must be");
    _flags = kind;
  }

 public:
  LIR_Op1(LIR_Code code, LIR_Opr opr, LIR_Opr result = LIR_OprFact::illegalOpr, BasicType type = T_ILLEGAL, LIR_PatchCode patch = lir_patch_none, CodeEmitInfo* info = NULL)
    : LIR_Op(code, result, info)
,_tmp1(LIR_OprFact::illegalOpr)
,_tmp2(LIR_OprFact::illegalOpr)
,_tmp3(LIR_OprFact::illegalOpr)
    , _opr(opr)
    , _patch(patch)
    , _type(type)                      { 
    assert(is_in_range(code, begin_op1, end_op1), "code check"); 
    assert0( (intptr_t)opr >= -1 );
  }

  LIR_Op1(LIR_Code code, LIR_Opr opr, LIR_Opr result, LIR_Opr tmp1, BasicType type = T_ILLEGAL, LIR_PatchCode patch = lir_patch_none, CodeEmitInfo* info = NULL)
    : LIR_Op(code, result, info)
,_tmp1(tmp1)
,_tmp2(LIR_OprFact::illegalOpr)
,_tmp3(LIR_OprFact::illegalOpr)
    , _opr(opr)
    , _patch(patch)
    , _type(type)                      {
    assert(is_in_range(code, begin_op1, end_op1), "code check");
    assert0( (intptr_t)opr >= -1 );
  }

  LIR_Op1(LIR_Code code, LIR_Opr opr, LIR_Opr result, BasicType type, LIR_PatchCode patch, CodeEmitInfo* info, LIR_MoveKind kind)
    : LIR_Op(code, result, info)
,_tmp1(LIR_OprFact::illegalOpr)
,_tmp2(LIR_OprFact::illegalOpr)
,_tmp3(LIR_OprFact::illegalOpr)
    , _opr(opr)
    , _patch(patch)
    , _type(type)                      {
    assert(code == lir_move, "must be");
    set_kind(kind);
    if( (intptr_t)opr < -1 ) ShouldNotReachHere();
  }

  LIR_Op1(LIR_Code code, LIR_Opr opr, CodeEmitInfo* info)
    : LIR_Op(code, LIR_OprFact::illegalOpr, info)
,_tmp1(LIR_OprFact::illegalOpr)
,_tmp2(LIR_OprFact::illegalOpr)
,_tmp3(LIR_OprFact::illegalOpr)
    , _opr(opr)
    , _patch(lir_patch_none)
,_type(T_ILLEGAL){
    assert(is_in_range(code, begin_op1, end_op1), "code check"); 
    assert0( (intptr_t)opr >= -1 );
  }

  LIR_Opr in_opr()           const               { return _opr;   }
  LIR_PatchCode patch_code() const               { return _patch; }
  BasicType type()           const               { return _type;  }
LIR_Opr tmp1_opr()const{return _tmp1;}
LIR_Opr tmp2_opr()const{return _tmp2;}
LIR_Opr tmp3_opr()const{return _tmp3;}

  LIR_MoveKind move_kind() const {
    assert(code() == lir_move, "must be");
    return (LIR_MoveKind)_flags;
  }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_Op1* as_Op1() { return this; }
  virtual const char * name() const PRODUCT_RETURN0;

  void set_in_opr(LIR_Opr opr)   { assert0( (intptr_t)opr >= -1 ); _opr = opr; }
void set_tmp1_opr(LIR_Opr opr){_tmp1=opr;}
void set_tmp2_opr(LIR_Opr opr){_tmp2=opr;}
void set_tmp3_opr(LIR_Opr opr){_tmp3=opr;}

  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
  virtual void verify() const;
};


// for runtime calls
class LIR_OpRTCall: public LIR_OpCall {
 friend class LIR_OpVisitState;
 public:
LIR_OpRTCall(address addr,
               LIR_Opr result, LIR_OprList* arguments, CodeEmitInfo* info = NULL)
:LIR_OpCall(lir_rtcall,addr,result,arguments,info){}

  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpRTCall* as_OpRTCall() { return this; }

  virtual void verify() const;
};


// LIR_Op2
class LIR_Op2: public LIR_Op {
 friend class LIR_OpVisitState;

 protected:
  LIR_Opr   _opr1;
  LIR_Opr   _opr2;
  BasicType _type;
  LIR_Condition _condition;
  int  _fpu_stack_size; // for sin/cos implementation on Intel
  LIR_Opr   _tmp; // for throw/unwind

  void verify() const;

 public:
  LIR_Op2(LIR_Code code, LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, CodeEmitInfo* info = NULL)
    : LIR_Op(code, LIR_OprFact::illegalOpr, info)
    , _opr1(opr1)
    , _opr2(opr2)
    , _type(T_ILLEGAL)
    , _condition(condition)
    , _fpu_stack_size(0)
    , _tmp(LIR_OprFact::illegalOpr) {
  }

  LIR_Op2(LIR_Code code, LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, LIR_Opr tmp = LIR_OprFact::illegalOpr)
    : LIR_Op(code, result, NULL)
    , _opr1(opr1)
    , _opr2(opr2)
    , _type(T_ILLEGAL)
,_condition(condition)
    , _fpu_stack_size(0)
    , _tmp(tmp) {
  }

  LIR_Op2(LIR_Code code, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result = LIR_OprFact::illegalOpr,
          CodeEmitInfo* info = NULL, BasicType type = T_ILLEGAL, LIR_Opr tmp = LIR_OprFact::illegalOpr)
:LIR_Op(code,result,info)
    , _opr1(opr1)
    , _opr2(opr2)
,_type(type)
    , _condition(lir_cond_unknown)
    , _fpu_stack_size(0)
    , _tmp(tmp) {
  }

  LIR_Op2(LIR_Code code, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr result, LIR_Opr tmp)
    : LIR_Op(code, result, NULL)
    , _opr1(opr1)
    , _opr2(opr2)
    , _type(T_ILLEGAL)
    , _condition(lir_cond_unknown)
    , _fpu_stack_size(0)
    , _tmp(tmp) {
    assert(code != lir_cmp && is_in_range(code, begin_op2, end_op2), "code check");
    assert(tmp == LIR_OprFact::illegalOpr, "Ian think's that tmp is never used!");
  }

  LIR_Opr in_opr1() const                        { return _opr1; }
  LIR_Opr in_opr2() const                        { return _opr2; }
  BasicType type()  const                        { return _type; }
  LIR_Opr tmp_opr() const                        { return _tmp; }
  LIR_Condition condition() const  {
    assert(code() == lir_cmp || code() == lir_cmove, "only valid for cmp and cmove"); return _condition; 
  }

  void set_fpu_stack_size(int size)              { _fpu_stack_size = size; }
  int  fpu_stack_size() const                    { return _fpu_stack_size; }

  void set_in_opr1(LIR_Opr opr)                  { _opr1 = opr; }
  void set_in_opr2(LIR_Opr opr)                  { _opr2 = opr; }
void set_tmp_opr(LIR_Opr opr){_tmp=opr;}

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_Op2* as_Op2() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};

#ifdef AZ_X86
// compare-into-flags / branch-into-flags style
class LIR_OpBranch: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Condition _condition;
  BasicType     _type;
  Label*        _label;
  BlockBegin*   _block;  // if this is a branch to a block, this is the block
  BlockBegin*   _ublock; // if this is a float-branch, this is the unorderd block
  CodeStub*     _stub;   // if this is a branch to a stub, this is the stub
  
 public:
  LIR_OpBranch(LIR_Condition cond, Label* lbl)
    : LIR_Op(lir_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*) NULL)
,_condition(cond)
    , _label(lbl)
    , _block(NULL)
    , _ublock(NULL)
    , _stub(NULL) { }

  LIR_OpBranch(LIR_Condition cond, BasicType type, BlockBegin* block);
  LIR_OpBranch(LIR_Condition cond, BasicType type, CodeStub* stub);

  LIR_OpBranch(BlockBegin* block);
  LIR_OpBranch(CodeStub* cs);

  // for unordered comparisons
  LIR_OpBranch(LIR_Condition cond, BasicType type, BlockBegin* block, BlockBegin* ublock);

LIR_Condition cond()const{return _condition;}
  BasicType     type()        const              { return _type;        }
  Label*        label()       const              { return _label;       }
  BlockBegin*   block()       const              { return _block;       }
  BlockBegin*   ublock()      const              { return _ublock;      }
  CodeStub*     stub()        const              { return _stub;        }

  void          change_block(BlockBegin* b);
  void          change_ublock(BlockBegin* b);
  void          negate_cond();

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpBranch* as_OpBranch() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};
#endif // X86_64

class ConversionStub;

class LIR_OpConvert: public LIR_Op1 {
 friend class LIR_OpVisitState;

 private:
   ConversionStub* _stub;
   Bytecodes::Code _bytecode;

 public:
   LIR_OpConvert(Bytecodes::Code code, LIR_Opr opr, LIR_Opr result, ConversionStub* stub)
     : LIR_Op1(lir_convert, opr, result)
     , _stub(stub)
     , _bytecode(code)                           {}

  Bytecodes::Code bytecode() const               { return _bytecode; }
  ConversionStub* stub() const                   { return _stub; }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpConvert* as_OpConvert() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;

  static void print_bytecode(outputStream* out, Bytecodes::Code code) PRODUCT_RETURN;
};

class SafepointStub;

class LIR_OpSafepoint:public LIR_Op1{
 friend class LIR_OpVisitState;

 private:
SafepointStub*_stub;

 public:
   LIR_OpSafepoint(LIR_Opr thread_reg, SafepointStub* stub)
     : LIR_Op1(lir_safepoint, thread_reg, LIR_OprFact::illegalOpr)
     , _stub(stub)                              {}

SafepointStub*stub()const{return _stub;}

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpSafepoint* as_OpSafepoint() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// LIR_OpTypeCheck
class LIR_OpTypeCheck: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Opr       _object;
  LIR_Opr       _array;
  ciKlass*      _klass;
  LIR_Opr       _tmp1;
  LIR_Opr       _tmp2;
  LIR_Opr       _tmp3;
LIR_Opr _tmp4;
LIR_Opr _tmp5;
  LIR_Opr       _cp_reg;        // profile start
  int           _cpdoff;        // offset to CPData_Null
  bool          _fast_check;
  CodeEmitInfo* _info_for_patch;
  CodeEmitInfo* _info_for_exception;
  CodeStub*     _stub;
  // Helpers for Tier1UpdateMethodData
  ciMethod*     _profiled_method;
  int           _profiled_bci;

public:
  LIR_OpTypeCheck(LIR_Code code, LIR_Opr result, LIR_Opr object, ciKlass* klass,
                  LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5, bool fast_check,
                  CodeEmitInfo* info_for_exception, CodeEmitInfo* info_for_patch, CodeStub* stub,
                  ciMethod* profiled_method, int profiled_bci, LIR_Opr cp_reg, int cpdoff);
  LIR_OpTypeCheck(LIR_Code code, LIR_Opr object, LIR_Opr array,
LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr tmp3,LIR_Opr tmp4,LIR_Opr tmp5,CodeEmitInfo*info_for_exception,
ciMethod*profiled_method,int profiled_bci,LIR_Opr cp_reg,int cpdoff);

  LIR_Opr object() const                         { return _object;         }
  LIR_Opr array() const                          { assert(code() == lir_store_check, "not valid"); return _array;         }
  LIR_Opr tmp1() const                           { return _tmp1;           }
  LIR_Opr tmp2() const                           { return _tmp2;           }
  LIR_Opr tmp3() const                           { return _tmp3;           }
  LIR_Opr tmp4() const                           { return _tmp4;           }
  LIR_Opr tmp5() const                           { return _tmp5;           }
  LIR_Opr cp_reg() const                         { return _cp_reg;         }
  int cpdoff() const                             { return _cpdoff;         }
  ciKlass* klass() const                         { assert(code() == lir_instanceof || code() == lir_checkcast, "not valid"); return _klass;          }
  bool fast_check() const                        { assert(code() == lir_instanceof || code() == lir_checkcast, "not valid"); return _fast_check;     }
  CodeEmitInfo* info_for_patch() const           { return _info_for_patch;  }
  CodeEmitInfo* info_for_exception() const       { return _info_for_exception; }
  CodeStub* stub() const                         { return _stub;           }

  // methodDataOop profiling
  ciMethod* profiled_method()                    { return _profiled_method; }
  int       profiled_bci()                       { return _profiled_bci; }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpTypeCheck* as_OpTypeCheck() { return this; }
  void print_instr(outputStream* out) const PRODUCT_RETURN;
};


// Fast path allocation
class LIR_OpAlloc:public LIR_Op{
 friend class LIR_OpVisitState;
  LIR_Opr   _bytesize;          // bytesize, pinned to R09
  LIR_Opr   _len;               // zero for objects, pinned to R10
  LIR_Opr   _kid;               // Usually a constant, pinned to R11
  LIR_Opr   _tmp1;              // temp pinned to RAX
  LIR_Opr   _klass_reg;         // register to hold klass object for unloaded klasses
  BasicType _type;
  CodeStub* _stub;
  ciKlass   *_klass;
  bool      _init_test;
  bool      _always_slow_path;

public:
  // Primitive array allocation
  LIR_OpAlloc(LIR_Opr bytesize, LIR_Opr len, LIR_Opr kid, LIR_Opr result, LIR_Opr tmp1, LIR_Opr klass_reg, BasicType type, CodeStub* stub);
  // Object array allocation
  LIR_OpAlloc(LIR_Opr bytesize, LIR_Opr len, LIR_Opr kid, LIR_Opr result, LIR_Opr tmp1, LIR_Opr klass_reg, ciType *elem_type, bool always_slow_path, CodeStub* stub);
  // Object allocation
  LIR_OpAlloc(ciInstanceKlass *ik, LIR_Opr bytesize, LIR_Opr len_is_zero, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, bool init_test, bool always_slow_path, CodeStub* stub)
:LIR_Op(lir_alloc_object,result,NULL)
    , _bytesize (bytesize)
,_len(len_is_zero)
    , _kid  (kid)
,_tmp1(tmp1)
    , _klass_reg(klass_reg)
,_type(T_ARRAY)
    , _init_test(init_test)
    , _always_slow_path(always_slow_path)
,_klass(ik)
    , _stub (stub) {}

  LIR_Opr   bytesize()const    { return _bytesize;    }
  LIR_Opr   len()     const    { return _len;         }
  LIR_Opr   kid()     const    { return _kid;         }
  LIR_Opr   tmp1()    const    { return _tmp1;        }
  LIR_Opr   klass_reg() const  { return _klass_reg;   }
  LIR_Opr   obj()     const    { return result_opr(); }
  BasicType type()    const    { return _type;        }
  bool      init_test() const  { return _init_test;   }
  bool      always_slow_path( )const { return _always_slow_path; }
  CodeStub* stub()    const    { return _stub;        }
ciKlass*klass()const{return _klass;}

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpAlloc * as_OpAlloc() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};


class LIR_Op3: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Opr _opr1;
  LIR_Opr _opr2;
  LIR_Opr _opr3;
 public:
  LIR_Op3(LIR_Code code, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr opr3, LIR_Opr result, CodeEmitInfo* info = NULL)
    : LIR_Op(code, result, info)
    , _opr1(opr1)
    , _opr2(opr2)
    , _opr3(opr3)                                { assert(is_in_range(code, begin_op3, end_op3), "code check"); }
  LIR_Opr in_opr1() const                        { return _opr1; }
  LIR_Opr in_opr2() const                        { return _opr2; }
  LIR_Opr in_opr3() const                        { return _opr3; }
  
  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_Op3* as_Op3() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};


//--------------------------------
class LabelObj: public CompilationResourceObj {
 private:
  Label _label;
 public:
  LabelObj()                                     {}
  Label* label()                                 { return &_label; }
};


class LIR_OpLock: public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Opr _obj;
LIR_Opr _tid;
LIR_Opr _mark;
  LIR_Opr _tmp;
  int _mon_num;
  CodeStub* _stub;
 public:
LIR_OpLock(LIR_Code code,LIR_Opr obj,LIR_Opr tid,LIR_Opr mark,LIR_Opr tmp,int mon_num,CodeStub*stub,CodeEmitInfo*info)
    : LIR_Op(code, LIR_OprFact::illegalOpr, info)
    , _obj(obj)
    , _tid(tid)
    , _mark(mark)
    , _tmp(tmp)
    , _mon_num(mon_num)
    , _stub(stub)                      {}

  LIR_Opr obj_opr() const                        { return _obj; }
  LIR_Opr tid()  const                           { return _tid; }
LIR_Opr mark()const{return _mark;}
  LIR_Opr tmp()  const                           { return _tmp; }
  int mon_num() const                            { return _mon_num; }
  CodeStub* stub() const                         { return _stub; }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpLock* as_OpLock() { return this; }
  void print_instr(outputStream* out) const PRODUCT_RETURN;
};


// LIR_OpCompareAndSwap
class LIR_OpCompareAndSwap : public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  LIR_Opr _obj;
LIR_Opr _offset;
  LIR_Opr _cmp_value;
  LIR_Opr _new_value;
  LIR_Opr _tmp1;
  LIR_Opr _tmp2;

 public:
LIR_OpCompareAndSwap(LIR_Code code,LIR_Opr obj,LIR_Opr offset,LIR_Opr cmp_value,LIR_Opr new_value,LIR_Opr t1,LIR_Opr t2)
    : LIR_Op(code, LIR_OprFact::illegalOpr, NULL)  // no result, no info
    , _obj(obj)
    , _offset(offset)
    , _cmp_value(cmp_value)
    , _new_value(new_value)
    , _tmp1(t1)
    , _tmp2(t2)                                  { }

LIR_Opr obj()const{return _obj;}
LIR_Opr offset()const{return _offset;}
  LIR_Opr cmp_value()   const                    { return _cmp_value; }
  LIR_Opr new_value()   const                    { return _new_value; }
  LIR_Opr tmp1()        const                    { return _tmp1;      }
  LIR_Opr tmp2()        const                    { return _tmp2;      }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpCompareAndSwap * as_OpCompareAndSwap () { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};

// LIR_OpProfileCall
class LIR_OpProfileCall : public LIR_Op {
 friend class LIR_OpVisitState;

 private:
  ciMethod* _profiled_method;
  int _profiled_bci;
  LIR_Opr _mdo;
  LIR_Opr _recv;
  LIR_Opr _tmp1;
  ciKlass* _known_holder;

 public:
  // Destroys recv
  LIR_OpProfileCall(LIR_Code code, ciMethod* profiled_method, int profiled_bci, LIR_Opr mdo, LIR_Opr recv, LIR_Opr t1, ciKlass* known_holder)
    : LIR_Op(code, LIR_OprFact::illegalOpr, NULL)  // no result, no info
    , _profiled_method(profiled_method)
    , _profiled_bci(profiled_bci)
    , _mdo(mdo)
    , _recv(recv)
    , _tmp1(t1)
    , _known_holder(known_holder)                { }

  ciMethod* profiled_method() const              { return _profiled_method;  }
  int       profiled_bci()    const              { return _profiled_bci;     }
  LIR_Opr   mdo()             const              { return _mdo;              }
  LIR_Opr   recv()            const              { return _recv;             }
  LIR_Opr   tmp1()            const              { return _tmp1;             }
  ciKlass*  known_holder()    const              { return _known_holder;     }

  virtual void emit_code(LIR_Assembler* masm);
  virtual LIR_OpProfileCall* as_OpProfileCall() { return this; }
  virtual void print_instr(outputStream* out) const PRODUCT_RETURN;
};


class LIR_InsertionBuffer;

//--------------------------------LIR_List---------------------------------------------------
// Maintains a list of LIR instructions (one instance of LIR_List per basic block)
// The LIR instructions are appended by the LIR_List class itself; 
//
// Notes:
// - all offsets are(should be) in bytes
// - local positions are specified with an offset, with offset 0 being local 0

class LIR_List: public CompilationResourceObj {
 private:
  LIR_OpList  _operations;

  Compilation*  _compilation;
#ifndef PRODUCT
  BlockBegin*   _block;
#endif
#ifdef ASSERT
  const char *  _file;
  int           _line;
#endif

  void append(LIR_Op* op) {
    if (op->source() == NULL)
      op->set_source(_compilation->current_instruction());
#ifndef PRODUCT
    if (PrintIRWithLIR) {
      _compilation->maybe_print_current_instruction();
op->print(tty);tty->cr();
    }
#endif // PRODUCT

    _operations.append(op);

#ifdef ASSERT
    op->verify();
    op->set_file_and_line(_file, _line);
    _file = NULL;
    _line = 0;
#endif
  }

 public:
  LIR_List(Compilation* compilation, BlockBegin* block = NULL);

#ifdef ASSERT
  void set_file_and_line(const char * file, int line);
#endif
  
  //---------- accessors ---------------
  LIR_OpList* instructions_list()                { return &_operations; }
  int         length() const                     { return _operations.length(); }
  LIR_Op*     at(int i) const                    { return _operations.at(i); }

  NOT_PRODUCT(BlockBegin* block() const          { return _block; });

  // insert LIR_Ops in buffer to right places in LIR_List
  void append(LIR_InsertionBuffer* buffer);

  //---------- mutators ---------------
  void insert_before(int i, LIR_List* op_list)   { _operations.insert_before(i, op_list->instructions_list()); }
  void insert_before(int i, LIR_Op* op)          { _operations.insert_before(i, op); }

  //---------- printing -------------
  void print_instructions() PRODUCT_RETURN;


  //---------- instructions ------------- 
  void call_opt_virtual(ciMethod* method, LIR_Opr receiver, LIR_Opr result,
                        address dest, LIR_OprList* arguments,
                        CodeEmitInfo* info) {
    append(new LIR_OpJavaCall(lir_optvirtual_call, method, receiver, result, dest, arguments, info));
  }
  void call_static(ciMethod* method, LIR_Opr result,
                   address dest, LIR_OprList* arguments, CodeEmitInfo* info) {
    append(new LIR_OpJavaCall(lir_static_call, method, LIR_OprFact::illegalOpr, result, dest, arguments, info));
  }
  void call_icvirtual(ciMethod* method, LIR_Opr receiver, LIR_Opr result,
                      address dest, LIR_OprList* arguments, CodeEmitInfo* info) {
    append(new LIR_OpJavaCall(lir_icvirtual_call, method, receiver, result, dest, arguments, info));
  }
  void call_virtual(ciMethod* method, LIR_Opr receiver, LIR_Opr result,
                    intptr_t vtable_offset, LIR_OprList* arguments, CodeEmitInfo* info) {
    append(new LIR_OpJavaCall(lir_virtual_call, method, receiver, result, vtable_offset, arguments, info));
  }
  
  void get_thread(LIR_Opr result)                { append(new LIR_Op0(lir_get_thread, result)); }
  void word_align()                              { append(new LIR_Op0(lir_word_align)); }
  void membar()                                  { append(new LIR_Op0(lir_membar)); }
  void membar_acquire()                          { append(new LIR_Op0(lir_membar_acquire)); }
  void membar_release()                          { append(new LIR_Op0(lir_membar_release)); }

  void nop()                                     { append(new LIR_Op0(lir_nop)); }
  void build_frame()                             { append(new LIR_Op0(lir_build_frame)); }

  void insert_invoke_profiler(CodeEmitInfo* info){ append(new LIR_Op0(lir_profile_invoke, LIR_OprFact::illegalOpr, info)); }

  void std_entry(LIR_Opr receiver)               { append(new LIR_Op0(lir_std_entry, receiver)); }
  void osr_entry(LIR_Opr osrPointer)             { append(new LIR_Op0(lir_osr_entry, osrPointer)); }

  void branch_destination(Label* lbl)            { append(new LIR_OpLabel(lbl)); }

  void negate(LIR_Opr from, LIR_Opr to)          { append(new LIR_Op1(lir_neg, from, to)); }
  void leal(LIR_Opr from, LIR_Opr result_reg)    { append(new LIR_Op1(lir_leal, from, result_reg)); }

  // result is a stack location for old backend and vreg for UseLinearScan
  // stack_loc_temp is an illegal register for old backend
  void unaligned_move(LIR_Address* src, LIR_Opr dst) { append(new LIR_Op1(lir_move, LIR_OprFact::address(src), dst, dst->type(), lir_patch_none, NULL, lir_move_unaligned)); }
  void unaligned_move(LIR_Opr src, LIR_Address* dst) { append(new LIR_Op1(lir_move, src, LIR_OprFact::address(dst), src->type(), lir_patch_none, NULL, lir_move_unaligned)); }
  void unaligned_move(LIR_Opr src, LIR_Opr dst) { append(new LIR_Op1(lir_move, src, dst, dst->type(), lir_patch_none, NULL, lir_move_unaligned)); }
  void move(LIR_Opr src, LIR_Opr dst, CodeEmitInfo* info = NULL) { append(new LIR_Op1(lir_move, src, dst, dst->type(), lir_patch_none, info)); }
  void move(LIR_Address* src, LIR_Opr dst, CodeEmitInfo* info = NULL) { append(new LIR_Op1(lir_move, LIR_OprFact::address(src), dst, src->type(), lir_patch_none, info)); }
  void move(LIR_Opr src, LIR_Address* dst, CodeEmitInfo* info = NULL) { append(new LIR_Op1(lir_move, src, LIR_OprFact::address(dst), dst->type(), lir_patch_none, info)); }

  void volatile_move(LIR_Opr src, LIR_Opr dst, BasicType type, CodeEmitInfo* info = NULL, LIR_PatchCode patch_code = lir_patch_none) { append(new LIR_Op1(lir_move, src, dst, type, patch_code, info, lir_move_volatile)); }

  void oop2reg  (jobject o, LIR_Opr reg)         { append(new LIR_Op1(lir_move, LIR_OprFact::oopConst(o),    reg));   }
  void oop2reg_patch(jobject o, LIR_Opr reg, CodeEmitInfo* info);

  void return_op(LIR_Opr result)                 { append(new LIR_Op1(lir_return, result)); }
 
  // Azul
  void ref2klass(LIR_Opr klass, LIR_Opr ref, LIR_Opr tmp)  { append(new LIR_Op1(lir_klassTable_oop_load, ref, klass, tmp)); }

  void safepoint(LIR_Opr thread, SafepointStub* slowpath)  { append(new LIR_OpSafepoint(thread, slowpath)); }
 
  void convert(Bytecodes::Code code, LIR_Opr left, LIR_Opr dst, ConversionStub* stub = NULL/*, bool is_32bit = false*/) { append(new LIR_OpConvert(code, left, dst, stub)); }

  void logical_and (LIR_Opr left, LIR_Opr right, LIR_Opr dst) { append(new LIR_Op2(lir_logic_and,  left, right, dst)); }
  void logical_or  (LIR_Opr left, LIR_Opr right, LIR_Opr dst) { append(new LIR_Op2(lir_logic_or,   left, right, dst)); }
  void logical_xor (LIR_Opr left, LIR_Opr right, LIR_Opr dst) { append(new LIR_Op2(lir_logic_xor,  left, right, dst)); }

  void null_check(LIR_Opr opr, CodeEmitInfo* info)         { append(new LIR_Op1(lir_null_check, opr, info)); }
void throw_exception(LIR_Opr threadReg,LIR_Opr exceptionOop,LIR_Opr exceptionPC,CodeEmitInfo*info){append(new LIR_Op2(lir_throw,threadReg,exceptionOop,LIR_OprFact::illegalOpr,info,T_ILLEGAL,exceptionPC));}
void unwind_exception(LIR_Opr threadReg,LIR_Opr exceptionOop,LIR_Opr exceptionPC,CodeEmitInfo*info){append(new LIR_Op2(lir_unwind,threadReg,exceptionOop,LIR_OprFact::illegalOpr,info,T_ILLEGAL,exceptionPC));}

  void compare_to (LIR_Opr left, LIR_Opr right, LIR_Opr dst) {
    append(new LIR_Op2(lir_compare_to,  left, right, dst));
  }

  void push(LIR_Opr opr)                                   { append(new LIR_Op1(lir_push, opr)); }
  void pop(LIR_Opr reg)                                    { append(new LIR_Op1(lir_pop,  reg)); }

void cmp(LIR_Condition condition,LIR_Opr left,LIR_Opr right,LIR_Opr res){
append(new LIR_Op2(lir_cmp,condition,left,right,res));
  }
  void cmp(LIR_Condition condition, LIR_Opr left, LIR_Opr right, CodeEmitInfo* info = NULL) {
    append(new LIR_Op2(lir_cmp, condition, left, right, info));
  }
  void cmp(LIR_Condition condition, LIR_Opr left, int right, CodeEmitInfo* info = NULL) {
    cmp(condition, left, LIR_OprFact::intConst(right), info);
  }

  void cmp_mem_int(LIR_Condition condition, LIR_Opr base, int disp, int c, CodeEmitInfo* info);
  void cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Address* addr, CodeEmitInfo* info);

  void cmove(LIR_Condition condition, LIR_Opr src1, LIR_Opr src2, LIR_Opr dst, LIR_Opr tmp) {
    append(new LIR_Op2(lir_cmove, condition, src1, src2, dst, tmp));
  }

  void cas_long(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2);
  void cas_obj(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2);
  void cas_int(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2);

  void abs (LIR_Opr from, LIR_Opr to, LIR_Opr tmp)                { append(new LIR_Op2(lir_abs , from, tmp, to)); }
  void sqrt(LIR_Opr from, LIR_Opr to, LIR_Opr tmp)                { append(new LIR_Op2(lir_sqrt, from, tmp, to)); }
  void log (LIR_Opr from, LIR_Opr to, LIR_Opr tmp)                { append(new LIR_Op2(lir_log,  from, tmp, to)); }
  void log10 (LIR_Opr from, LIR_Opr to, LIR_Opr tmp)              { append(new LIR_Op2(lir_log10, from, tmp, to)); }
  void sin (LIR_Opr from, LIR_Opr to, LIR_Opr tmp1, LIR_Opr tmp2) { append(new LIR_Op2(lir_sin , from, tmp1, to, tmp2)); }
  void cos (LIR_Opr from, LIR_Opr to, LIR_Opr tmp1, LIR_Opr tmp2) { append(new LIR_Op2(lir_cos , from, tmp1, to, tmp2)); }
  void tan (LIR_Opr from, LIR_Opr to, LIR_Opr tmp1, LIR_Opr tmp2) { append(new LIR_Op2(lir_tan , from, tmp1, to, tmp2)); }
 
  void add (LIR_Opr left, LIR_Opr right, LIR_Opr res)      { append(new LIR_Op2(lir_add, left, right, res)); }
  void sub (LIR_Opr left, LIR_Opr right, LIR_Opr res, CodeEmitInfo* info = NULL) { append(new LIR_Op2(lir_sub, left, right, res, info)); }
  void mul (LIR_Opr left, LIR_Opr right, LIR_Opr res) { append(new LIR_Op2(lir_mul, left, right, res)); }
  void mul_strictfp (LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp) { append(new LIR_Op2(lir_mul_strictfp, left, right, res, tmp)); }
  void div (LIR_Opr left, LIR_Opr right, LIR_Opr res, CodeEmitInfo* info = NULL)      { append(new LIR_Op2(lir_div, left, right, res, info)); }
  void div_strictfp (LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp) { append(new LIR_Op2(lir_div_strictfp, left, right, res, tmp)); }
  void rem (LIR_Opr left, LIR_Opr right, LIR_Opr res, CodeEmitInfo* info = NULL)      { append(new LIR_Op2(lir_rem, left, right, res, info)); }

  void volatile_load_mem_reg(LIR_Address* address, LIR_Opr dst, CodeEmitInfo* info, LIR_PatchCode patch_code = lir_patch_none);
  void volatile_load_unsafe_reg(LIR_Opr base, LIR_Opr offset, LIR_Opr dst, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code);

  void load(LIR_Address* addr, LIR_Opr src, CodeEmitInfo* info = NULL, LIR_PatchCode patch_code = lir_patch_none);

  void prefetch(LIR_Address* addr, bool is_store);

  void store_mem_int(jint v,    LIR_Opr base, int offset_in_bytes, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code = lir_patch_none);
  void store_mem_oop(jobject o, LIR_Opr base, int offset_in_bytes, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code = lir_patch_none);
  void store(LIR_Opr src, LIR_Address* addr, CodeEmitInfo* info = NULL, LIR_PatchCode patch_code = lir_patch_none);
  void volatile_store_mem_reg(LIR_Opr src, LIR_Address* address, CodeEmitInfo* info, LIR_PatchCode patch_code = lir_patch_none);
  void volatile_store_unsafe_reg(LIR_Opr src, LIR_Opr base, LIR_Opr offset, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code);

  void idiv(LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info);
  void idiv(LIR_Opr left, int   right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info);
  void irem(LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info);
  void irem(LIR_Opr left, int   right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info);

void ldiv(LIR_Opr left,LIR_Opr right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info);
void ldiv(LIR_Opr left,int right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info);
void lrem(LIR_Opr left,LIR_Opr right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info);
void lrem(LIR_Opr left,int right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info);

  void allocate_object(ciInstanceKlass *ik, LIR_Opr size, LIR_Opr len_is_zero, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, bool init_check, bool always_slow_path, CodeStub* stub);
  void allocate_prim_array(LIR_Opr size, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, BasicType type, CodeStub* stub) ;
  void allocate_obj_array (LIR_Opr size, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, ciType *elem_type, bool always_slow_path, CodeStub* stub) ;

#ifdef AZ_X86
  // jump is an unconditional branch
void jump(BlockBegin*block){append(new LIR_OpBranch(block));}
void jump(CodeStub*stub){append(new LIR_OpBranch(stub));}

  // compare-into-flags / branch-into-flags style
  void branch(LIR_Condition cond, Label* lbl)        { append(new LIR_OpBranch(cond, lbl)); }
  void branch(LIR_Condition cond, BasicType type, BlockBegin* block) {
    assert(type != T_FLOAT && type != T_DOUBLE, "no fp comparisons");
    append(new LIR_OpBranch(cond, type, block));
  }
  void branch(LIR_Condition cond, BasicType type, CodeStub* stub)    {
    assert(type != T_FLOAT && type != T_DOUBLE, "no fp comparisons");
    append(new LIR_OpBranch(cond, type, stub));
  }
  void branch(LIR_Condition cond, BasicType type, BlockBegin* block, BlockBegin* unordered) {
    assert(type == T_FLOAT || type == T_DOUBLE, "fp comparisons only");
    append(new LIR_OpBranch(cond, type, block, unordered));
  }
  void branch_cmp(LIR_Condition cond, BasicType bt, LIR_Opr left, int right, BlockBegin* block) {
    cmp(cond, left, LIR_OprFact::intConst(right), (CodeEmitInfo*)NULL);
assert(bt!=T_FLOAT&&bt!=T_DOUBLE,"no fp comparisons");
append(new LIR_OpBranch(cond,bt,block));
  }
  void branch_cmp(LIR_Condition cond, BasicType bt, LIR_Opr left, int right, Label *lbl) {
    cmp(cond, left, LIR_OprFact::intConst(right), (CodeEmitInfo*)NULL);
assert(bt!=T_FLOAT&&bt!=T_DOUBLE,"no fp comparisons");
    append(new LIR_OpBranch(cond, lbl));
  }
#endif // AZ_X86

  void shift_left(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp);
  void shift_right(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp);
  void unsigned_shift_right(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp);

  void shift_left(LIR_Opr value, int count, LIR_Opr dst)       { shift_left(value, LIR_OprFact::intConst(count), dst, LIR_OprFact::illegalOpr); }
  void shift_right(LIR_Opr value, int count, LIR_Opr dst)      { shift_right(value, LIR_OprFact::intConst(count), dst, LIR_OprFact::illegalOpr); }
  void unsigned_shift_right(LIR_Opr value, int count, LIR_Opr dst) { unsigned_shift_right(value, LIR_OprFact::intConst(count), dst, LIR_OprFact::illegalOpr); }
  void bit_test(LIR_Opr value, LIR_Opr bitnum);
  void bit_test(LIR_Opr value, int bitnum) { bit_test(value, LIR_OprFact::intConst(bitnum)); }

  void lcmp2int(LIR_Opr left, LIR_Opr right, LIR_Opr dst)      { append(new LIR_Op2(lir_cmp_l2i,  left, right, dst)); }
  void fcmp2int(LIR_Opr left, LIR_Opr right, LIR_Opr dst, bool is_unordered_less);

  void call_runtime_leaf(address routine, LIR_Opr result, LIR_OprList* arguments) {
    append(new LIR_OpRTCall(routine, result, arguments));
  }
void call_runtime(address routine,LIR_Opr result,
                    LIR_OprList* arguments, CodeEmitInfo* info) {
append(new LIR_OpRTCall(routine,result,arguments,info));
  }

  void unlock_object(LIR_Opr obj, LIR_Opr mark, LIR_Opr tmp, int mon_num, CodeStub* stub);
  void lock_object(LIR_Opr obj, LIR_Opr tid, LIR_Opr mark, LIR_Opr tmp, int mon_num, CodeStub* stub, CodeEmitInfo* info);

  void breakpoint() {
    append(new LIR_Op0(lir_breakpoint));
  }
  void arraycopy(LIR_Opr src, LIR_Opr src_pos, LIR_Opr dst, LIR_Opr dst_pos, LIR_Opr length, ciArrayKlass* expected_type, int flags, CodeEmitInfo* info) {
    append(new LIR_OpArrayCopy(src, src_pos, dst, dst_pos, length, LIR_OprFact::illegalOpr, expected_type, flags, info));
  }
  void stringequals(LIR_Opr this_string, LIR_Opr other_string, LIR_Opr res,
LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr tmp3,LIR_Opr tmp4,
                    LIR_Opr tmp5, LIR_Opr tmp6, CodeEmitInfo* info) {
    append(new LIR_OpStringEquals(this_string, other_string, res, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, info));
  }

  void checkcast (LIR_Opr result, LIR_Opr object, ciKlass* klass,
                  LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5, bool fast_check,
                  CodeEmitInfo* info_for_exception, CodeEmitInfo* info_for_patch, CodeStub* stub,
ciMethod*profiled_method,int profiled_bci,LIR_Opr cp_reg,int cpdoff);
void instanceof(LIR_Opr result,LIR_Opr object,ciKlass*klass,
LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr tmp3,LIR_Opr tmp4,LIR_Opr tmp5,bool fast_check,
CodeEmitInfo*info_for_patch,LIR_Opr cp_reg,int cpdoff);
void store_check(LIR_Opr object,LIR_Opr array,
LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr tmp3,LIR_Opr tmp4,LIR_Opr tmp5,
CodeEmitInfo*info_for_exception,LIR_Opr cp_reg);

  // methodDataOop profiling
  void profile_call(ciMethod* method, int bci, LIR_Opr mdo, LIR_Opr recv, LIR_Opr t1, ciKlass* cha_klass) { append(new LIR_OpProfileCall(lir_profile_call, method, bci, mdo, recv, t1, cha_klass)); }
};

void print_LIR(BlockList* blocks);

class LIR_InsertionBuffer : public CompilationResourceObj {
 private:
  LIR_List*   _lir;   // the lir list where ops of this buffer should be inserted later (NULL when uninitialized)

  // list of insertion points. index and count are stored alternately:
  // _index_and_count[i * 2]:     the index into lir list where "count" ops should be inserted
  // _index_and_count[i * 2 + 1]: the number of ops to be inserted at index
  intStack    _index_and_count; 

  // the LIR_Ops to be inserted
  LIR_OpList  _ops;
  
  void append_new(int index, int count)  { _index_and_count.append(index); _index_and_count.append(count); }
  void set_index_at(int i, int value)    { _index_and_count.at_put((i << 1),     value); }
  void set_count_at(int i, int value)    { _index_and_count.at_put((i << 1) + 1, value); }

#ifdef ASSERT
  void verify();
#endif
 public:
  LIR_InsertionBuffer() : _lir(NULL), _index_and_count(8), _ops(8) { }

  // must be called before using the insertion buffer
  void init(LIR_List* lir)  { assert(!initialized(), "already initialized"); _lir = lir; _index_and_count.clear(); _ops.clear(); }
  bool initialized() const  { return _lir != NULL; }
  // called automatically when the buffer is appended to the LIR_List
  void finish()             { _lir = NULL; }

  // accessors
  LIR_List*  lir_list() const             { return _lir; }
  int number_of_insertion_points() const  { return _index_and_count.length() >> 1; }
  int index_at(int i) const               { return _index_and_count.at((i << 1));     }
  int count_at(int i) const               { return _index_and_count.at((i << 1) + 1); }

  int number_of_ops() const               { return _ops.length(); }
  LIR_Op* op_at(int i) const              { return _ops.at(i); }

  // append an instruction to the buffer
  void append(int index, LIR_Op* op);

  // instruction
  void move(int index, LIR_Opr src, LIR_Opr dst, CodeEmitInfo* info = NULL) { append(index, new LIR_Op1(lir_move, src, dst, dst->type(), lir_patch_none, info)); }
};


//
// LIR_OpVisitState is used for manipulating LIR_Ops in an abstract way.
// Calling a LIR_Op's visit function with a LIR_OpVisitState causes
// information about the input, output and temporaries used by the
// op to be recorded.  It also records whether the op has call semantics
// and also records all the CodeEmitInfos used by this op.
//


class LIR_OpVisitState: public StackObj {
 public:
  typedef enum { inputMode, firstMode = inputMode, tempMode, outputMode, numModes, invalidMode = -1 } OprMode;

  enum {
    maxNumberOfOperands = 14,
    maxNumberOfInfos = 4
  };

 private:
  LIR_Op*          _op;

  // optimization: the operands and infos are not stored in a variable-length
  //               list, but in a fixed-size array to save time of size checks and resizing
  int              _oprs_len[numModes];
  LIR_Opr*         _oprs_new[numModes][maxNumberOfOperands];
  int _info_len;
  CodeEmitInfo*    _info_new[maxNumberOfInfos];

  bool             _has_call;
  bool             _has_slow_case;


  // only include register operands
  // addresses are decomposed to the base and index registers
  // constants and stack operands are ignored
  void append(LIR_Opr& opr, OprMode mode) {
    assert(opr->is_valid(), "should not call this otherwise");
    assert(mode >= 0 && mode < numModes, "bad mode");

    if (opr->is_register()) {
       assert(_oprs_len[mode] < maxNumberOfOperands, "array overflow");
      _oprs_new[mode][_oprs_len[mode]++] = &opr;

    } else if (opr->is_pointer()) {
      LIR_Address* address = opr->as_address_ptr();
      if (address != NULL) {
        // special handling for addresses: add base and index register of the address
        // both are always input operands!
        if (address->_base->is_valid()) {
          assert(address->_base->is_register(), "must be");
          assert(_oprs_len[inputMode] < maxNumberOfOperands, "array overflow");
          _oprs_new[inputMode][_oprs_len[inputMode]++] = &address->_base;
        }
        if (address->_index->is_valid()) {
          assert(address->_index->is_register(), "must be");
          assert(_oprs_len[inputMode] < maxNumberOfOperands, "array overflow");
          _oprs_new[inputMode][_oprs_len[inputMode]++] = &address->_index;
        }

      } else {
        assert(opr->is_constant(), "constant operands are not processed");
      }
    } else {
      assert(opr->is_stack(), "stack operands are not processed");
    }
  }

  void append(CodeEmitInfo* info) {
    assert(info != NULL, "should not call this otherwise");
    assert(_info_len < maxNumberOfInfos, "array overflow");
    _info_new[_info_len++] = info;
  }

 public:
  LIR_OpVisitState()         { reset(); }

  LIR_Op* op() const         { return _op; }
  void set_op(LIR_Op* op)    { reset(); _op = op; }

  bool has_call() const      { return _has_call; }
  bool has_slow_case() const { return _has_slow_case; }

  void reset() {
    _op = NULL;
    _has_call = false;
    _has_slow_case = false;

    _oprs_len[inputMode] = 0;
    _oprs_len[tempMode] = 0;
    _oprs_len[outputMode] = 0;
    _info_len = 0;
  }


  int opr_count(OprMode mode) const {
    assert(mode >= 0 && mode < numModes, "bad mode");
    return _oprs_len[mode];
  }

  LIR_Opr opr_at(OprMode mode, int index) const {
    assert(mode >= 0 && mode < numModes, "bad mode");
    assert(index >= 0 && index < _oprs_len[mode], "index out of bound");
    return *_oprs_new[mode][index];
  }
                                           
  void set_opr_at(OprMode mode, int index, LIR_Opr opr) const {
    assert(mode >= 0 && mode < numModes, "bad mode");
    assert(index >= 0 && index < _oprs_len[mode], "index out of bound");
    *_oprs_new[mode][index] = opr;
  }

  int info_count() const { 
    return _info_len; 
  }

  CodeEmitInfo* info_at(int index) const {
    assert(index < _info_len, "index out of bounds");
    return _info_new[index];
  }

  XHandlers* all_xhandler();

  // collects all register operands of the instruction
  void visit(LIR_Op* op);

#if ASSERT
  // check that an operation has no operands
  bool no_operands(LIR_Op* op);
#endif

  // LIR_Op visitor functions use these to fill in the state
  void do_input(LIR_Opr& opr)             { append(opr, LIR_OpVisitState::inputMode); }
  void do_output(LIR_Opr& opr)            { append(opr, LIR_OpVisitState::outputMode); }
  void do_temp(LIR_Opr& opr)              { append(opr, LIR_OpVisitState::tempMode); }
  void do_info(CodeEmitInfo* info)        { append(info); }

  void do_stub(CodeStub* stub);
  void do_call()                          { _has_call = true; }
  void do_slow_case()                     { _has_slow_case = true; }
  void do_slow_case(CodeEmitInfo* info) {
    _has_slow_case = true;
    append(info);
  }
};


inline LIR_Opr LIR_OprDesc::illegalOpr()   { return LIR_OprFact::illegalOpr; };

#endif // C1_LIR_HPP
