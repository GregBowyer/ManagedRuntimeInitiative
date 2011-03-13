/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


// Portions of code courtesy of Clifford Click

// Optimization - Graph Style

#include "assembler_pd.hpp"
#include "c2_globals.hpp"
#include "ciArray.hpp"
#include "ciKlassKlass.hpp"
#include "ciInstance.hpp"
#include "ciObjArrayKlass.hpp"
#include "ciTypeArrayKlass.hpp"
#include "ciTypeFlow.hpp"
#include "matcher.hpp"
#include "mutexLocker.hpp"
#include "opcodes.hpp"
#include "ostream.hpp"
#include "type.hpp"
#include "objArrayOop.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

// Dictionary of types shared among compilations.
Dict* Type::_shared_type_dict = NULL;

// Array which maps compiler types to Basic Types
const BasicType Type::_basic_type[Type::lastype] = {
  T_ILLEGAL,    // Bad
  T_ILLEGAL,    // Control
  T_VOID,       // Top
  T_INT,        // Int
  T_LONG,       // Long
  T_VOID,       // Half

  T_ILLEGAL,    // Tuple
  T_ARRAY,      // Array

  T_ADDRESS,    // AnyPtr   // shows up in factory methods for NULL_PTR
  T_ADDRESS,    // RawPtr
  T_OBJECT,     // OopPtr
  T_OBJECT,     // InstPtr
  T_OBJECT,     // AryPtr
  T_OBJECT,     // KlassPtr

  T_OBJECT,     // Function
  T_ILLEGAL,    // Abio
  T_ADDRESS,    // Return_Address
  T_ILLEGAL,    // Memory
  T_FLOAT,      // FloatTop
  T_FLOAT,      // FloatCon
  T_FLOAT,      // FloatBot
  T_DOUBLE,     // DoubleTop
  T_DOUBLE,     // DoubleCon
  T_DOUBLE,     // DoubleBot
  T_ILLEGAL,    // Bottom
};

// Map ideal registers (machine types) to ideal types
const Type *Type::mreg2type[_last_machine_leaf];

// Map basic types to canonical Type* pointers.
const Type* Type::     _const_basic_type[T_CONFLICT+1];

// Map basic types to constant-zero Types.
const Type* Type::            _zero_type[T_CONFLICT+1];

// Map basic types to array-body alias types.
const TypeAryPtr* TypeAryPtr::_array_body_type[T_CONFLICT+1];

//=============================================================================
// Convenience common pre-built types.
const Type *Type::ABIO;         // State-of-machine only
const Type *Type::BOTTOM;       // All values
const Type *Type::CONTROL;      // Control only
const Type *Type::DOUBLE;       // All doubles
const Type *Type::FLOAT;        // All floats
const Type *Type::HALF;         // Placeholder half of doublewide type
const Type *Type::MEMORY;       // Abstract store only
const Type *Type::RETURN_ADDRESS;
const Type *Type::TOP;          // No values in set

//------------------------------get_const_type---------------------------
const Type* Type::get_const_type(ciType* type) {
  if (type == NULL) {
    return NULL;
  } else if (type->is_primitive_type()) {
    return get_const_basic_type(type->basic_type());
  } else {
return TypeOopPtr::make_from_klass_unique(type->as_klass());
  }
}

// Ported from 1.6
//---------------------------array_element_basic_type---------------------------------
// Mapping to the array element's basic type.
BasicType Type::array_element_basic_type() const {
  BasicType bt = basic_type();
  if (bt == T_INT) {
    if (this == TypeInt::INT)   return T_INT;
    if (this == TypeInt::CHAR)  return T_CHAR;
    if (this == TypeInt::BYTE)  return T_BYTE;
    if (this == TypeInt::BOOL)  return T_BOOLEAN;
    if (this == TypeInt::SHORT) return T_SHORT;
    return T_VOID;
  }
  return bt;
}

//---------------------------get_typeflow_type---------------------------------
// Import a type produced by ciTypeFlow.
const Type* Type::get_typeflow_type(ciType* type) {
  switch (type->basic_type()) {

  case ciTypeFlow::StateVector::T_BOTTOM:
    assert(type == ciTypeFlow::StateVector::bottom_type(), "");
    return Type::BOTTOM;

  case ciTypeFlow::StateVector::T_TOP:
    assert(type == ciTypeFlow::StateVector::top_type(), "");
    return Type::TOP;

  case ciTypeFlow::StateVector::T_NULL:
    assert(type == ciTypeFlow::StateVector::null_type(), "");
    return TypePtr::NULL_PTR;

  case ciTypeFlow::StateVector::T_LONG2:
    // The ciTypeFlow pass pushes a long, then the half.
    // We do the same.
    assert(type == ciTypeFlow::StateVector::long2_type(), "");
    return TypeInt::TOP;

  case ciTypeFlow::StateVector::T_DOUBLE2:
    // The ciTypeFlow pass pushes double, then the half.
    // Our convention is the same.
    assert(type == ciTypeFlow::StateVector::double2_type(), "");
    return Type::TOP;

  case T_ADDRESS:
    assert(type->is_return_address(), "");
    return TypeRawPtr::make((address)(intptr_t)type->as_return_address()->bci());

  default:
    // make sure we did not mix up the cases:
    assert(type != ciTypeFlow::StateVector::bottom_type(), "");
    assert(type != ciTypeFlow::StateVector::top_type(), "");
    assert(type != ciTypeFlow::StateVector::null_type(), "");
    assert(type != ciTypeFlow::StateVector::long2_type(), "");
    assert(type != ciTypeFlow::StateVector::double2_type(), "");
    assert(!type->is_return_address(), "");

    return Type::get_const_type(type);
  }
}


//------------------------------make-------------------------------------------
// Create a simple Type, with default empty symbol sets.  Then hashcons it
// and look for an existing copy in the type dictionary.
const Type *Type::make( enum TYPES t ) {
  return (new Type(t))->hashcons();
}

//------------------------------cmp--------------------------------------------
int Type::cmp( const Type *const t1, const Type *const t2 ) {
  if( t1->_base != t2->_base ) 
    return 1;                   // Missed badly
  assert(t1 != t2 || t1->eq(t2), "eq must be reflexive");
  return !t1->eq(t2);           // Return ZERO if equal
}

//------------------------------hash-------------------------------------------
int Type::uhash( const Type *const t ) {
  return t->hash();
}

//--------------------------Initialize_shared----------------------------------
void Type::Initialize_shared(Compile* current) {
  if( _shared_type_dict ) {     // Already initialized?
    Atomic::read_barrier();     // Must fence, to avoid double-checked locking bug
    return;
  }
    
  MutexLockerAllowGC mu(Compile_lock, CompilerThread::current());
  if( _shared_type_dict )       // Already initialized?
    return;

  Arena* save = current->type_arena();
  Arena* shared_type_arena = new Arena();

  current->set_type_arena(shared_type_arena);
Dict*shared_type_dict=new(shared_type_arena)Dict((CmpKey)Type::cmp,(Hash)Type::uhash,shared_type_arena,128);
  current->set_type_dict(shared_type_dict);

  // Make shared pre-built types.
  CONTROL = make(Control);      // Control only
  TOP     = make(Top);          // No values in set
  MEMORY  = make(Memory);       // Abstract store only
  ABIO    = make(Abio);         // State-of-machine only
  RETURN_ADDRESS=make(Return_Address);
  FLOAT   = make(FloatBot);     // All floats
  DOUBLE  = make(DoubleBot);    // All doubles                          
  BOTTOM  = make(Bottom);       // Everything
  HALF    = make(Half);         // Placeholder half of doublewide type

  TypeF::ZERO = TypeF::make(0.0); // Float 0 (positive zero)
  TypeF::ONE  = TypeF::make(1.0); // Float 1

  TypeD::ZERO = TypeD::make(0.0); // Double 0 (positive zero)
  TypeD::ONE  = TypeD::make(1.0); // Double 1

  TypeInt::MINUS_1 = TypeInt::make(-1);  // -1
  TypeInt::ZERO    = TypeInt::make( 0);  //  0
  TypeInt::ONE     = TypeInt::make( 1);  //  1
  TypeInt::BOOL    = TypeInt::make(0,1,   WidenMin);  // 0 or 1, FALSE or TRUE.
  TypeInt::CC      = TypeInt::make(-1, 1, WidenMin);  // -1, 0 or 1, condition codes
  TypeInt::CC_LT   = TypeInt::make(-1,-1, WidenMin);  // == TypeInt::MINUS_1
  TypeInt::CC_GT   = TypeInt::make( 1, 1, WidenMin);  // == TypeInt::ONE
  TypeInt::CC_EQ   = TypeInt::make( 0, 0, WidenMin);  // == TypeInt::ZERO
  TypeInt::CC_LE   = TypeInt::make(-1, 0, WidenMin);
  TypeInt::CC_GE   = TypeInt::make( 0, 1, WidenMin);  // == TypeInt::BOOL
  TypeInt::BYTE    = TypeInt::make(-128,127,     WidenMin); // Bytes
  TypeInt::CHAR    = TypeInt::make(0,65535,      WidenMin); // Java chars
  TypeInt::SHORT   = TypeInt::make(-32768,32767, WidenMin); // Java shorts
  TypeInt::POS     = TypeInt::make(0,max_jint,   WidenMin); // Non-neg values
  TypeInt::POS1    = TypeInt::make(1,max_jint,   WidenMin); // Positive values
  TypeInt::INT     = TypeInt::make(min_jint,max_jint, WidenMax); // 32-bit integers
  TypeInt::SYMINT  = TypeInt::make(-max_jint,max_jint,WidenMin); // symmetric range
  // CmpL is overloaded both as the bytecode computation returning
  // a trinary (-1,0,+1) integer result AND as an efficient long
  // compare returning optimizer ideal-type flags.
  assert( TypeInt::CC_LT == TypeInt::MINUS_1, "types must match for CmpL to work" );
  assert( TypeInt::CC_GT == TypeInt::ONE,     "types must match for CmpL to work" );
  assert( TypeInt::CC_EQ == TypeInt::ZERO,    "types must match for CmpL to work" );
  assert( TypeInt::CC_LE == TypeInt::make(-1,0, WidenMin), "types must match for CmpL to work" );
  assert( TypeInt::CC_GE == TypeInt::BOOL,    "types must match for CmpL to work" );

  TypeLong::MINUS_1 = TypeLong::make(-1);        // -1
  TypeLong::ZERO    = TypeLong::make( 0);        //  0
  TypeLong::ONE     = TypeLong::make( 1);        //  1
  TypeLong::POS     = TypeLong::make(0,max_jlong, WidenMin); // Non-neg values
  TypeLong::LONG    = TypeLong::make(min_jlong,max_jlong,WidenMax); // 64-bit integers
  TypeLong::INT     = TypeLong::make((jlong)min_jint,(jlong)max_jint,WidenMin);
  TypeLong::UINT    = TypeLong::make(0,(jlong)max_juint,WidenMin);

  const Type **fboth =(const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  fboth[0] = Type::CONTROL;
  fboth[1] = Type::CONTROL;
  TypeTuple::IFBOTH = TypeTuple::make( 2, fboth );

  const Type **ffalse =(const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  ffalse[0] = Type::CONTROL;
  ffalse[1] = Type::TOP;
  TypeTuple::IFFALSE = TypeTuple::make( 2, ffalse );

  const Type **fneither =(const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  fneither[0] = Type::TOP;
  fneither[1] = Type::TOP;
  TypeTuple::IFNEITHER = TypeTuple::make( 2, fneither );

  const Type **ftrue =(const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  ftrue[0] = Type::TOP;
  ftrue[1] = Type::CONTROL;
  TypeTuple::IFTRUE = TypeTuple::make( 2, ftrue );

  const Type **floop =(const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  floop[0] = Type::CONTROL;
  floop[1] = TypeInt::INT;
  TypeTuple::LOOPBODY = TypeTuple::make( 2, floop );

  TypePtr::NULL_PTR= TypePtr::make( AnyPtr, TypePtr::Null, 0 );
  TypePtr::NOTNULL = TypePtr::make( AnyPtr, TypePtr::NotNull, OffsetBot );
  TypePtr::BOTTOM  = TypePtr::make( AnyPtr, TypePtr::BotPTR, OffsetBot );

TypeRawPtr::BOTTOM=TypeRawPtr::make(TypePtr::BotPTR,OffsetBot);
  TypeRawPtr::NOTNULL= TypeRawPtr::make( TypePtr::NotNull, OffsetBot );

  mreg2type[Op_Node] = Type::BOTTOM;
  mreg2type[Op_Set ] = 0;
  mreg2type[Op_RegI] = TypeInt::INT;
  mreg2type[Op_RegP] = TypePtr::BOTTOM;
  mreg2type[Op_RegF] = Type::FLOAT;
  mreg2type[Op_RegD] = Type::DOUBLE;
  mreg2type[Op_RegL] = TypeLong::LONG;
  mreg2type[Op_RegFlags] = TypeInt::CC;

  const Type **fmembar = TypeTuple::fields(0);
  TypeTuple::MEMBAR = TypeTuple::make(TypeFunc::Parms+0, fmembar);

  const Type **fsc = (const Type**)shared_type_arena->Amalloc_4(2*sizeof(Type*));
  fsc[0] = TypeInt::CC;
  fsc[1] = Type::MEMORY;
  TypeTuple::STORECONDITIONAL = TypeTuple::make(2, fsc);

  TypeInstPtr::NOTNULL = TypeInstPtr::make(TypePtr::NotNull, current->env()->Object_klass());
TypeInstPtr::OBJECT=TypeInstPtr::make(TypePtr::BotPTR,current->env()->Object_klass());
TypeInstPtr::BOTTOM=TypeInstPtr::make(TypePtr::BotPTR,current->env()->Object_klass(),
					   false, 0, OffsetBot);
  TypeInstPtr::MIRROR  = TypeInstPtr::make(TypePtr::NotNull, current->env()->Class_klass());
  TypeInstPtr::MARK    = TypeInstPtr::make(TypePtr::BotPTR,  current->env()->Object_klass(),
                                           false, 0, oopDesc::mark_offset_in_bytes());

  TypeAryPtr::RANGE   = TypeAryPtr::make( TypePtr::BotPTR, TypeAry::make(Type::BOTTOM,TypeInt::POS), current->env()->Object_klass(), arrayOopDesc::length_offset_in_bytes());
  // There is no shared klass for Object[].  See note in TypeAryPtr::klass().
TypeAry::OOPS=TypeAry::make(TypeInstPtr::OBJECT,TypeInt::POS);
TypeAry::PTRS=TypeAry::make(TypeOopPtr::make(TypePtr::BotPTR,Type::OffsetBot),TypeInt::POS);
TypeAryPtr::OOPS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::OOPS,NULL/*ciArrayKlass::make(o)*/,Type::OffsetBot);
TypeAryPtr::PTRS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::PTRS,NULL/*ciArrayKlass::make(o)*/,Type::OffsetBot);
TypeAryPtr::EKID=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::OOPS,NULL,objArrayOopDesc::ekid_offset_in_bytes());
TypeAryPtr::BYTES=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(TypeInt::BYTE,TypeInt::POS),ciTypeArrayKlass::make(T_BYTE),Type::OffsetBot);
TypeAryPtr::SHORTS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(TypeInt::SHORT,TypeInt::POS),ciTypeArrayKlass::make(T_SHORT),Type::OffsetBot);
TypeAryPtr::CHARS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(TypeInt::CHAR,TypeInt::POS),ciTypeArrayKlass::make(T_CHAR),Type::OffsetBot);
TypeAryPtr::INTS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(TypeInt::INT,TypeInt::POS),ciTypeArrayKlass::make(T_INT),Type::OffsetBot);
TypeAryPtr::LONGS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(TypeLong::LONG,TypeInt::POS),ciTypeArrayKlass::make(T_LONG),Type::OffsetBot);
TypeAryPtr::FLOATS=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(Type::FLOAT,TypeInt::POS),ciTypeArrayKlass::make(T_FLOAT),Type::OffsetBot);
TypeAryPtr::DOUBLES=TypeAryPtr::make(TypePtr::BotPTR,TypeAry::make(Type::DOUBLE,TypeInt::POS),ciTypeArrayKlass::make(T_DOUBLE),Type::OffsetBot);

  TypeAryPtr::_array_body_type[T_OBJECT]  = TypeAryPtr::OOPS;
  TypeAryPtr::_array_body_type[T_ARRAY]   = TypeAryPtr::OOPS;   // arrays are stored in oop arrays
  TypeAryPtr::_array_body_type[T_BYTE]    = TypeAryPtr::BYTES;
  TypeAryPtr::_array_body_type[T_BOOLEAN] = TypeAryPtr::BYTES;  // boolean[] is a byte array
  TypeAryPtr::_array_body_type[T_SHORT]   = TypeAryPtr::SHORTS;
  TypeAryPtr::_array_body_type[T_CHAR]    = TypeAryPtr::CHARS;
  TypeAryPtr::_array_body_type[T_INT]     = TypeAryPtr::INTS;
  TypeAryPtr::_array_body_type[T_LONG]    = TypeAryPtr::LONGS;
  TypeAryPtr::_array_body_type[T_FLOAT]   = TypeAryPtr::FLOATS;
  TypeAryPtr::_array_body_type[T_DOUBLE]  = TypeAryPtr::DOUBLES;

  TypeKlassPtr::OBJECT         = TypeKlassPtr::make( TypePtr::NotNull,current->env()->Object_klass(), 0 );
  TypeKlassPtr::   KID         = TypeKlassPtr::make( TypePtr::NotNull,current->env()->Object_klass(), 0, true );
  TypeKlassPtr::OBJECT_OR_NULL = TypeKlassPtr::make( TypePtr::BotPTR, current->env()->Object_klass(), 0 );
  TypeKlassPtr::   KID_OR_NULL = TypeKlassPtr::make( TypePtr::BotPTR, current->env()->Object_klass(), 0, true);

const Type**fnew=TypeTuple::fields(5);
fnew[TypeFunc::Parms+0]=TypeRawPtr::BOTTOM;//Thread
  fnew[TypeFunc::Parms+1] = TypeInt::INT; // KID of object to allocate
  fnew[TypeFunc::Parms+2] = TypeLong::LONG; // bytes to allocate
  fnew[TypeFunc::Parms+3] = Type::HALF; // other half of long
  fnew[TypeFunc::Parms+4] = TypeInt::BOOL; // extra slow-path test
  // fnew[TypeFunc::Parms+5] = TypeInt::INT; // SBA frame hint is a secret argument supplied by template
TypeTuple::NEW_OBJ_DOMAIN=TypeTuple::make(TypeFunc::Parms+5,fnew);
const Type**anew=TypeTuple::fields(7);
anew[TypeFunc::Parms+0]=TypeRawPtr::BOTTOM;//Thread
  anew[TypeFunc::Parms+1] = TypeInt::INT; // KID of object to allocate
  anew[TypeFunc::Parms+2] = TypeLong::LONG; // bytes to allocate
  anew[TypeFunc::Parms+3] = Type::HALF; // other half of long
  anew[TypeFunc::Parms+4] = TypeInt::BOOL; // extra slow-path test
  anew[TypeFunc::Parms+5] = TypeInt::POS; // array length
  anew[TypeFunc::Parms+6] = TypeInt::POS; // element KID
TypeTuple::NEW_ARY_DOMAIN=TypeTuple::make(TypeFunc::Parms+7,anew);

  const Type **intpair = TypeTuple::fields(2);
  intpair[0] = TypeInt::INT;
  intpair[1] = TypeInt::INT;
  TypeTuple::INT_PAIR = TypeTuple::make(2, intpair);

  const Type **longpair = TypeTuple::fields(2);
  longpair[0] = TypeLong::LONG;
  longpair[1] = TypeLong::LONG;
  TypeTuple::LONG_PAIR = TypeTuple::make(2, longpair);

  _const_basic_type[T_BOOLEAN] = TypeInt::BOOL;
  _const_basic_type[T_CHAR]    = TypeInt::CHAR;
  _const_basic_type[T_BYTE]    = TypeInt::BYTE;
  _const_basic_type[T_SHORT]   = TypeInt::SHORT;
  _const_basic_type[T_INT]     = TypeInt::INT;
  _const_basic_type[T_LONG]    = TypeLong::LONG;
  _const_basic_type[T_FLOAT]   = Type::FLOAT; 
  _const_basic_type[T_DOUBLE]  = Type::DOUBLE; 
_const_basic_type[T_OBJECT]=TypeInstPtr::OBJECT;
_const_basic_type[T_ARRAY]=TypeInstPtr::OBJECT;//there is no separate bottom for arrays
  _const_basic_type[T_VOID]    = TypePtr::NULL_PTR;   // reflection represents void this way
  _const_basic_type[T_ADDRESS] = TypeRawPtr::BOTTOM;  // both interpreter return addresses & random raw ptrs
  _const_basic_type[T_CONFLICT]= Type::BOTTOM;        // why not?

  _zero_type[T_BOOLEAN] = TypeInt::ZERO;     // false == 0
  _zero_type[T_CHAR]    = TypeInt::ZERO;     // '\0' == 0
  _zero_type[T_BYTE]    = TypeInt::ZERO;     // 0x00 == 0
  _zero_type[T_SHORT]   = TypeInt::ZERO;     // 0x0000 == 0
  _zero_type[T_INT]     = TypeInt::ZERO;
  _zero_type[T_LONG]    = TypeLong::ZERO;
  _zero_type[T_FLOAT]   = TypeF::ZERO; 
  _zero_type[T_DOUBLE]  = TypeD::ZERO; 
  _zero_type[T_OBJECT]  = TypePtr::NULL_PTR;
  _zero_type[T_ARRAY]   = TypePtr::NULL_PTR; // null array is null oop
  _zero_type[T_ADDRESS] = TypePtr::NULL_PTR; // raw pointers use the same null
  _zero_type[T_VOID]    = Type::TOP;         // the only void value is no value at all

  // get_zero_type() should not happen for T_CONFLICT
  _zero_type[T_CONFLICT]= NULL;

#if 0
  //#ifdef ASSERT
  // Validate correctness of the Type Lattice. 
  Type_Array validate(save);  int validx=0;
  validate.map(validx++,ABIO                );
  validate.map(validx++,BOTTOM              );
  validate.map(validx++,CONTROL             );
  validate.map(validx++,DOUBLE              );
  validate.map(validx++,FLOAT               );
  validate.map(validx++,HALF                );
  validate.map(validx++,MEMORY              );
  validate.map(validx++,RETURN_ADDRESS      );
  validate.map(validx++,TOP                 );
  validate.map(validx++,TypeAryPtr::BYTES   );
  validate.map(validx++,TypeAryPtr::CHARS   );
  validate.map(validx++,TypeAryPtr::DOUBLES );
  validate.map(validx++,TypeAryPtr::FLOATS  );
  validate.map(validx++,TypeAryPtr::INTS    );
  validate.map(validx++,TypeAryPtr::LONGS   );
  validate.map(validx++,TypeAryPtr::OOPS    );
  validate.map(validx++,TypeAryPtr::RANGE   );
  validate.map(validx++,TypeAryPtr::SHORTS  );
  validate.map(validx++,TypeD::ONE          );
  validate.map(validx++,TypeD::ZERO         );
  validate.map(validx++,TypeF::ONE          );
  validate.map(validx++,TypeF::ZERO         );
  validate.map(validx++,TypeInstPtr::OBJECT );
  validate.map(validx++,TypeInstPtr::BOTTOM );
  validate.map(validx++,TypeInstPtr::MARK   );
  validate.map(validx++,TypeInstPtr::NOTNULL);
  validate.map(validx++,TypeInt::BOOL       );
  validate.map(validx++,TypeInt::BYTE       );
  validate.map(validx++,TypeInt::CC         );
  validate.map(validx++,TypeInt::CC_EQ      );
  validate.map(validx++,TypeInt::CC_GE      );
  validate.map(validx++,TypeInt::CC_GT      );
  validate.map(validx++,TypeInt::CC_LE      );
  validate.map(validx++,TypeInt::CC_LT      );
  validate.map(validx++,TypeInt::CHAR       );
  validate.map(validx++,TypeInt::INT        );
  validate.map(validx++,TypeInt::MINUS_1    );
  validate.map(validx++,TypeInt::ONE        );
  validate.map(validx++,TypeInt::POS        );
  validate.map(validx++,TypeInt::SHORT      );
  validate.map(validx++,TypeInt::ZERO       );
  validate.map(validx++,TypeKlassPtr::OBJECT);
  validate.map(validx++,TypeLong::INT       );
  validate.map(validx++,TypeLong::LONG      );
  validate.map(validx++,TypeLong::MINUS_1   );
  validate.map(validx++,TypeLong::ONE       );
  validate.map(validx++,TypeLong::ZERO      );
  validate.map(validx++,TypePtr::BOTTOM     );
  validate.map(validx++,TypePtr::NOTNULL    );
  validate.map(validx++,TypePtr::NULL_PTR   );
  validate.map(validx++,TypeRawPtr::BOTTOM  );
  validate.map(validx++,TypeRawPtr::NOTNULL );
  validate.map(validx++,TypeTuple::IFBOTH          );
  validate.map(validx++,TypeTuple::IFFALSE         );
  validate.map(validx++,TypeTuple::IFNEITHER       );
  validate.map(validx++,TypeTuple::IFTRUE          );
  validate.map(validx++,TypeTuple::LOOPBODY        );
  validate.map(validx++,TypeTuple::MEMBAR          );
  validate.map(validx++,TypeTuple::NEW_ARY_DOMAIN  );
  validate.map(validx++,TypeTuple::NEW_OBJ_DOMAIN  );
  validate.map(validx++,TypeTuple::STORECONDITIONAL);
  // Some more types, just to validate things better
ciEnv*E=ciEnv::current();
  // A not-null oop array
  const TypeAryPtr *t1 = TypeAryPtr::make(TypePtr::NotNull, TypeAry::OOPS, NULL, Type::OffsetBot);
  t1->klass();
  validate.map(validx++,t1);
  // An interface
  ciInstanceKlass *iface = E->array_ifaces()[0];
  const TypePtr *t2 = TypeInstPtr::make_from_klass_unique(iface);
  validate.map(validx++,t2);
  const Type *t2top = t2->dual();
  validate.map(validx++,t2top);
  const Type *t2any = t2->cast_to_ptr_type(TypePtr::AnyNull);
  validate.map(validx++,t2any);
  // An array of interfaces
const TypeAry*t3ary=TypeAry::make(t2,TypeInt::POS);
  validate.map(validx++,t3ary);  
  const Type *t3    = TypeAryPtr::make(TypePtr::NotNull, t3ary, ciObjArrayKlass::make(iface), Type::OffsetBot);
  validate.map(validx++,t3);
  const Type *t3any = TypeAryPtr::make(TypePtr::AnyNull, t3ary, ciObjArrayKlass::make(iface), Type::OffsetBot);
  validate.map(validx++,t3any);
  validate.map(validx++,t3->dual());
  // A String
  const TypeInstPtr *t4_string = TypeInstPtr::make_from_klass_unique(E->String_klass())->is_instptr();
  validate.map(validx++,t4_string);
  // A constant String.
  ciObject* string = E->test_string();
  assert0(string->is_instance());
  assert0(string->has_encoding());
  const Type *t5 = TypeOopPtr::make_from_constant(string);
  validate.map(validx++,t5);
  // Some unloaded classes
  ciInstanceKlass *unload = E->unloaded_ciinstance_klass();
  const Type *t6 = TypeInstPtr::make_from_klass_unique(unload);
  validate.map(validx++,t6);
  const Type *t6any = t6->dual();
  validate.map(validx++,t6any);
  // Another concrete type with subclasses, not String
  const Type *t7 = TypeInstPtr::make_from_klass_unique(E->Throwable_klass());
  validate.map(validx++,t7);
  const Type *t7any = t7->dual();
  validate.map(validx++,t7any);
  // An exact class that would otherwise have subklasses
  const Type *t8 = t7->is_instptr()->cast_to_exactness(true);
  validate.map(validx++,t8);
  const Type *t8any = t8->dual();
  validate.map(validx++,t8any);
  // An exact class Object
  const Type *t9 = TypeInstPtr::NOTNULL->cast_to_exactness(true);
  validate.map(validx++,t9);
  // A funny NULL+offset
  const Type *t10 = TypePtr::NULL_PTR->add_offset(24);
  validate.map(validx++,t10);
  // An array of String
const TypeAry*t11ary=TypeAry::make(t4_string,TypeInt::POS);
  validate.map(validx++,t11ary);
  const TypeAryPtr *t11 = TypeAryPtr::make(TypePtr::NotNull, t11ary, NULL, Type::OffsetBot);
  t11->klass();
  validate.map(validx++,t11);
  const Type *t11top = t11->dual();
  validate.map(validx++,t11top);
  const Type *t11any = t11->cast_to_ptr_type(TypePtr::AnyNull);
  validate.map(validx++,t11any);
  // Another interface
  ciInstanceKlass *iface2 = t4_string->ifaces()[1];
  const TypePtr *t12 = TypeInstPtr::make_from_klass_unique(iface2);
  validate.map(validx++,t12);
  const Type *t12top = t12->dual();
  validate.map(validx++,t12top);
  const Type *t12any = t12->cast_to_ptr_type(TypePtr::AnyNull);
  validate.map(validx++,t12any);

  // Create java/lang/String:exact:TopPTR{Interfaces:java/io/Serializable,java/lang/Comparable,java/lang/CharSequence,} *[int:2147483647..0]:AnyNull:exact*+top
  const TypeInstPtr *t13_string = (TypeInstPtr*)t4_string->cast_to_ptr_type(TypePtr::TopPTR);
const TypeAry*t13ary=TypeAry::make(t13_string,TypeInt::POS);
  const TypeAryPtr *t13aryptr = TypeAryPtr::make(TypePtr::AnyNull, t13ary, NULL, 0);
  validate.map(validx++,t13aryptr);

  // Create java/lang/Object:TopPTR *[int+]:AnyNull:exact*
  const TypeInstPtr *t14_object = TypeInstPtr::make_from_klass_unique(E->Object_klass())->is_instptr()->cast_to_exactness(false)->cast_to_ptr_type(TypePtr::TopPTR)->is_instptr();
const TypeAry*t14ary=TypeAry::make(t14_object,TypeInt::POS);
  const TypeAryPtr *t14aryptr = (TypeAryPtr*)TypeAryPtr::make(TypePtr::AnyNull, t14ary, NULL, 0)->cast_to_exactness(true);
  validate.map(validx++,t14aryptr);

  // Create Thread klass array
  const TypeAry *t15ary = TypeAry::make(TypeInstPtr::make_from_klass_unique(E->Thread_klass()),TypeInt::POS);
  const TypeAryPtr *t15aryptr = (TypeAryPtr*)TypeAryPtr::make(TypePtr::NotNull, t15ary, ciObjArrayKlass::make(E->Thread_klass()), Type::OffsetBot)->cast_to_exactness(false);
  validate.map(validx++,t15aryptr);

  // Create array of interface iface
  const TypeAry *t16ary = TypeAry::make(TypeInstPtr::make_from_klass_unique(iface),TypeInt::POS);
  const TypeAryPtr *t16aryptr = (TypeAryPtr*)TypeAryPtr::make(TypePtr::BotPTR, t16ary, ciObjArrayKlass::make(iface), 0)->cast_to_exactness(false);
  validate.map(validx++,t16aryptr);

  // [Thread vs [iface, where Thread is not constrained by iface nor final, should join to 
  // be [thread with iface as an added constraint.
  const TypeAryPtr* mytype2 = (TypeAryPtr*)t16aryptr->join(t15aryptr); 
  assert0(mytype2->elem()->base() != Top);

  // The internal asserts of 'meet' do the job.
for(int i=0;i<validx;i++){
    const Type *ti = validate.fast_lookup(i);
for(int j=i;j<validx;j++){
      ti->meet(validate.fast_lookup(j));
      ti->join(validate.fast_lookup(j));
    }
  }
  int fail_assoc_crossing_centerline = 0;
  int fail_assoc_same_centerline = 0;
for(int i=0;i<validx;i++){
    const Type *ti = validate.fast_lookup(i);
for(int j=i;j<validx;j++){
      const Type *tj = validate.fast_lookup(j);
      const Type *ij = ti->meet(tj);
for(int k=0;k<validx;k++){
        const Type *tk = validate.fast_lookup(k);
        const Type *ijk1 = ij->meet(tk);
        const Type *jk = tj->meet(tk);
        const Type *ijk2 = ti->meet(jk);
        if( ijk1 != ijk2 ) {
const TypePtr*tip=ti->isa_ptr();
const TypePtr*tjp=tj->isa_ptr();
const TypePtr*tkp=tk->isa_ptr();
          if( !tip || !tjp || !tkp || 
              (!tip->above_centerline() && !tjp->above_centerline() && !tkp->above_centerline()) ||
              (!tip->below_centerline() && !tjp->below_centerline() && !tkp->below_centerline()) ) {
C2OUT->print_cr("=== Meet Not Associative ===");
C2OUT->print("I     = ");ti->dump();C2OUT->cr();
C2OUT->print("J     = ");tj->dump();C2OUT->cr();
C2OUT->print("K     = ");tk->dump();C2OUT->cr();
C2OUT->print("(IJ)  = ");ij->dump();C2OUT->cr();
C2OUT->print(" (JK) = ");jk->dump();C2OUT->cr();
C2OUT->print("(IJ)K = ");ijk1->dump();C2OUT->cr();
C2OUT->print("I(JK) = ");ijk2->dump();C2OUT->cr();
            fail_assoc_same_centerline++;
            //assert0( ijk1 == ijk2 ); // Associativity
          } else {
            if( fail_assoc_crossing_centerline == 0 ) {
C2OUT->print_cr("=== Meet Not Associative Crossing Centerline, 1st Sample Failure ===");
C2OUT->print("I     = ");ti->dump();C2OUT->cr();
C2OUT->print("J     = ");tj->dump();C2OUT->cr();
C2OUT->print("K     = ");tk->dump();C2OUT->cr();
C2OUT->print("(IJ)  = ");ij->dump();C2OUT->cr();
C2OUT->print(" (JK) = ");jk->dump();C2OUT->cr();
C2OUT->print("(IJ)K = ");ijk1->dump();C2OUT->cr();
C2OUT->print("I(JK) = ");ijk2->dump();C2OUT->cr();
            }
            fail_assoc_crossing_centerline++;
          }
        }
      }
    }
  }
  if( fail_assoc_crossing_centerline ) 
    C2OUT->print_cr("=== Meet Not Associative crossing the centerline: %d times",fail_assoc_crossing_centerline);
  if( fail_assoc_same_centerline ) 
    C2OUT->print_cr("=== Meet Not Associative same side of centerline: %d times",fail_assoc_same_centerline);
#endif // !ASSERT

  Atomic::write_barrier();      // Fence out dict contents
  _shared_type_dict = shared_type_dict; // Publish global pointer

  // Restore working type arena.
  current->set_type_arena(save);
  current->set_type_dict(NULL);
}

//------------------------------Initialize-------------------------------------
void Type::Initialize(Compile* current) {
  assert(current->type_arena() != NULL, "must have created type arena");

  Initialize_shared(current);

  Arena* type_arena = current->type_arena();

  // Create the hash-cons'ing dictionary with top-level storage allocation
  Dict *tdic = new (type_arena) Dict( (CmpKey)Type::cmp,(Hash)Type::uhash, type_arena, 128 );
  current->set_type_dict(tdic);

  // Transfer the shared types.
  DictI i(_shared_type_dict);
  for( ; i.test(); ++i ) {
    Type* t = (Type*)i._value;
    tdic->Insert(t,t);  // New Type, insert into Type table
  }
}

//------------------------------hashcons---------------------------------------
// Do the hash-cons trick.  If the Type already exists in the type table,
// delete the current Type and return the existing Type.  Otherwise stick the
// current Type in the Type table.
const Type *Type::hashcons(void) {
  debug_only(base());           // Check the assertion in Type::base().
  // Look up the Type in the Type dictionary
  Dict *tdic = type_dict();
  Type* old = (Type*)(tdic->Insert(this, this, false));
  if( old ) {                   // Pre-existing Type?
    assert0( old->_dual );
    // If these are oopptrs, make sure they have matching _klass fields.
    // Capture the cached value for the old guy, if he does not have one and
    // the new guy does.
const TypeOopPtr*oldoop=old->isa_oopptr();
    if( oldoop ) {              // Got an oopptr, verify _klass match
      ciKlass *newklass = ((TypeOopPtr*)this)->_klass;
      if( !oldoop->_klass && newklass ) { // No old _klass and a newklass
#ifdef ASSERT
ciKlass*kkk=oldoop->klass();
        assert0( kkk == newklass || !kkk );
#endif
const TypeAryPtr*tap=oldoop->isa_aryptr();
        if( !tap || (tap->ary() != TypeAry::OOPS && tap->ary() != TypeAry::OOPS->dual()) )  // No capture for OOPS
          ((TypeOopPtr*)oldoop)->_klass = newklass; // Capture _klass field to avoid computing it later
      }
    }
    if( old != this )           // Yes, this guy is not the pre-existing?
      delete this;              // Yes, Nuke this guy
    return old;                 // Return pre-existing
  }

  // Record a dependency for newly discovered EXACT classes
  const TypeOopPtr *newoop = this->isa_oopptr();
  if( newoop && newoop->klass_is_exact() ) {
    ciKlass *k = newoop->_klass;
    if( k ) {
      while( k->is_obj_array_klass() ) // Dependence if can subklass inner element
        k = k->as_obj_array_klass()->element_klass();
if(k->is_instance_klass()){
ciInstanceKlass*cik=k->as_instance_klass();
        if( cik->is_loaded() && !cik->is_final() )
          cik->nof_implementors(&Compile::current()->_masm);
      }
    }
  }

  // Every type has a dual (to make my lattice symmetric).
  // Since we just discovered a new Type, compute its dual right now.
  assert( !_dual, "" );         // No dual yet
  _dual = xdual();              // Compute the dual
  if( cmp(this,_dual)==0 ) {    // Handle self-symmetric
    _dual = this;
    return this; 
  }
  assert( !_dual->_dual, "" );  // No reverse dual yet
  assert( !(*tdic)[_dual], "" ); // Dual not in type system either
  // New Type, insert into Type table
  tdic->Insert((void*)_dual,(void*)_dual);
  ((Type*)_dual)->_dual = this; // Finish up being symmetric
#ifdef ASSERT
  Type *dual_dual = (Type*)_dual->xdual();
  assert( eq(dual_dual), "xdual(xdual()) should be identity" );
  delete dual_dual;
#endif
  return this;                  // Return new Type
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool Type::eq( const Type * ) const {
  return true;                  // Nothing else can go wrong
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int Type::hash(void) const {
  return _base;
}

//------------------------------is_finite--------------------------------------
// Has a finite value
bool Type::is_finite() const {
  return false;
}
  
//------------------------------is_nan-----------------------------------------
// Is not a number (NaN)
bool Type::is_nan()    const {
  return false;
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  NOT virtual.  It enforces that meet is
// commutative and the lattice is symmetric.
const Type *Type::meet( const Type *t ) const {
  const Type *mt = xmeet(t);
#ifdef ASSERT
  if (!FAM) {
    if( mt != t->xmeet(this) ) {
C2OUT->print_cr("=== Meet Not Commutative ===");
C2OUT->print("t   =                   ");t->dump();C2OUT->cr();
C2OUT->print("this=                   ");dump();C2OUT->cr();
C2OUT->print("mt=(t meet this)=       ");mt->dump();C2OUT->cr();
      C2OUT->print("   (this meet t)=       "); t->xmeet(this)->dump(); C2OUT->cr();
    }
    assert( mt == t->xmeet(this), "meet not commutative" );
    const Type* dual_join = mt->_dual;
    const Type *t2t    = dual_join->xmeet(t->_dual);
    const Type *t2this = dual_join->xmeet(   _dual);
  
    // Interface meet Oop is Not Symmetric:
    // Interface:AnyNull meet Oop:AnyNull == Interface:AnyNull
    // Interface:NotNull meet Oop:NotNull == java/lang/Object:NotNull
    const TypeInstPtr* this_inst = this->isa_instptr();
    const TypeInstPtr*    t_inst =    t->isa_instptr();
    bool interface_vs_oop = false;
    if( this_inst && this_inst->is_loaded() && t_inst && t_inst->is_loaded() ) {
      bool this_interface = this_inst->klass()->is_interface();
      bool    t_interface =    t_inst->klass()->is_interface();
      interface_vs_oop = this_interface ^ t_interface;
    }
    const Type *tdual = t->_dual;
    const Type *thisdual = _dual;
    // strip out instances
    if (t2t->isa_oopptr() != NULL) {
      t2t = t2t->isa_oopptr()->cast_to_instance(TypeOopPtr::UNKNOWN_INSTANCE);
    }
    if (t2this->isa_oopptr() != NULL) {
      t2this = t2this->isa_oopptr()->cast_to_instance(TypeOopPtr::UNKNOWN_INSTANCE);
    }
    if (tdual->isa_oopptr() != NULL) {
      tdual = tdual->isa_oopptr()->cast_to_instance(TypeOopPtr::UNKNOWN_INSTANCE);
    }
    if (thisdual->isa_oopptr() != NULL) {
      thisdual = thisdual->isa_oopptr()->cast_to_instance(TypeOopPtr::UNKNOWN_INSTANCE);
    }
    
    if( !interface_vs_oop && (t2t != tdual || t2this != thisdual) ) {
C2OUT->print_cr("=== Meet Not Symmetric ===");
C2OUT->print("t   =                   ");t->dump();C2OUT->cr();
C2OUT->print("this=                   ");dump();C2OUT->cr();
C2OUT->print("mt=(t meet this)=       ");mt->dump();C2OUT->cr();
  
C2OUT->print("t_dual=                 ");t->_dual->dump();C2OUT->cr();
C2OUT->print("this_dual=              ");_dual->dump();C2OUT->cr();
C2OUT->print("mt_dual=                ");mt->_dual->dump();C2OUT->cr();

C2OUT->print("mt_dual meet t_dual=    ");t2t->dump();C2OUT->cr();
C2OUT->print("mt_dual meet this_dual= ");t2this->dump();C2OUT->cr();

      fatal("meet not symmetric" );
    }
  }
#endif
  return mt;
}

//------------------------------xmeet------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *Type::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Meeting TOP with anything?
  if( _base == Top ) return t;

  // Meeting BOTTOM with anything?
  if( _base == Bottom ) return BOTTOM;

  // Current "this->_base" is one of: Bad, Multi, Control, Top, 
  // Abio, Abstore, Floatxxx, Doublexxx, Bottom, lastype.
  switch (t->base()) {  // Switch on original type

  // Cut in half the number of cases I must handle.  Only need cases for when
  // the given enum "t->type" is less than or equal to the local enum "type".
  case FloatCon:
  case DoubleCon:
  case Int:
  case Long:
  case AnyPtr:
  case OopPtr:
  case InstPtr:
  case AryPtr:
  case KlassPtr:
    return t->xmeet(this);

  default:                      // Bogus type not in lattice
    return Type::BOTTOM;

  case Bottom:                  // Ye Olde Default
    return t;

  case FloatTop:
    if( _base == FloatTop ) return this;
  case FloatBot:                // Float
    if( _base == FloatBot || _base == FloatTop ) return FLOAT;
    if( _base == DoubleTop || _base == DoubleBot ) return Type::BOTTOM;
    return Type::BOTTOM;

  case DoubleTop:
    if( _base == DoubleTop ) return this;
  case DoubleBot:               // Double
    if( _base == DoubleBot || _base == DoubleTop ) return DOUBLE;
    if( _base == FloatTop || _base == FloatBot ) return Type::BOTTOM;
    return Type::BOTTOM;

  // These next few cases must match exactly or it is a compile-time error.
  case Control:                 // Control of code 
  case Abio:                    // State of world outside of program
  case Memory:
    if( _base == t->_base )  return this;
    return Type::BOTTOM;

  case Top:                     // Top of the lattice
    return this;
  }

  // The type is unchanged
  return this;
}

//-----------------------------filter------------------------------------------
const Type *Type::filter( const Type *kills ) const {
  const Type* ft = join(kills);
  if (ft->empty())
    return Type::TOP;           // Canonical empty value
  return ft;
}

//------------------------------xdual------------------------------------------
// Compute dual right now.
const Type::TYPES Type::dual_type[Type::lastype] = {
  Bad,          // Bad
  Control,      // Control
  Bottom,       // Top
  Bad,          // Int - handled in v-call
  Bad,          // Long - handled in v-call
  Half,         // Half
  
  Bad,          // Tuple - handled in v-call
  Bad,          // Array - handled in v-call

  Bad,          // AnyPtr - handled in v-call
  Bad,          // RawPtr - handled in v-call
  Bad,          // OopPtr - handled in v-call
  Bad,          // InstPtr - handled in v-call
  Bad,          // AryPtr - handled in v-call
  Bad,          // KlassPtr - handled in v-call

  Bad,          // Function - handled in v-call
  Abio,         // Abio
  Return_Address,// Return_Address
  Memory,       // Memory
  FloatBot,     // FloatTop
  FloatCon,     // FloatCon
  FloatTop,     // FloatBot
  DoubleBot,    // DoubleTop
  DoubleCon,    // DoubleCon
  DoubleTop,    // DoubleBot
  Top           // Bottom
};

const Type *Type::xdual() const {
  // Note: the base() accessor asserts the sanity of _base.
  return new Type(dual_type[_base]);
}

//------------------------------has_memory-------------------------------------
bool Type::has_memory() const {
  Type::TYPES tx = base();
  if (tx == Memory) return true;
  if (tx == Tuple) {
    const TypeTuple *t = is_tuple();
    for (uint i=0; i < t->cnt(); i++) {
      tx = t->field_at(i)->base();
      if (tx == Memory)  return true;
    }
  }
  return false;
}

//------------------------------dump2------------------------------------------
void Type::dump2( Dict &d, uint depth, outputStream *st ) const {
  st->print(msg[_base]);
}

//------------------------------dump-------------------------------------------
void Type::dump_on( outputStream *st ) const {
  ResourceMark rm;
  Dict d(cmpkey,hashkey);       // Stop recursive type dumping
  dump2(d,1,st);
}

//------------------------------data-------------------------------------------
const char * const Type::msg[Type::lastype] = {
  "bad","control","top","int:","long:","half", 
  "tuple:", "aryptr", 
  "anyptr:", "rawptr:", "java:", "inst:", "ary:", "klass:", 
  "func", "abIO", "return_address", "memory", 
  "float_top", "ftcon:", "float",
  "double_top", "dblcon:", "double",
  "bottom"
};

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants.
bool Type::singleton(void) const {
  return _base == Top || _base == Half;
}

//------------------------------empty------------------------------------------
// TRUE if Type is a type with no values, FALSE otherwise.
bool Type::empty(void) const {
  switch (_base) {
  case DoubleTop:
  case FloatTop:
  case Top:
    return true;

  case Half:
  case Abio:
  case Return_Address:
  case Memory:
  case Bottom:
  case FloatBot:
  case DoubleBot:
    return false;  // never a singleton, therefore never empty
  }

  ShouldNotReachHere();
  return false;
}

//------------------------------dump_stats-------------------------------------
// Dump collected statistics to stderr
#ifndef PRODUCT
void Type::dump_stats() {
C2OUT->print("Types made: %d\n",type_dict()->Size());
}
#endif

//------------------------------isa_oop_ptr------------------------------------
// Return true if type is an oop pointer type.  False for raw pointers.
static char isa_oop_ptr_tbl[Type::lastype] = {
  0,0,0,0,0,0,0/*tuple*/, 0/*ary*/,
  0/*anyptr*/,0/*rawptr*/,1/*OopPtr*/,1/*InstPtr*/,1/*AryPtr*/,1/*KlassPtr*/,
  0/*func*/,0,0/*return_address*/,0,
  /*floats*/0,0,0, /*doubles*/0,0,0,
  0
};
bool Type::isa_oop_ptr() const {
  return isa_oop_ptr_tbl[_base] != 0;
}

//------------------------------dump_stats-------------------------------------
// // Check that arrays match type enum
#ifndef PRODUCT
void Type::verify_lastype() {
  // Check that arrays match enumeration
  assert( Type::dual_type  [Type::lastype - 1] == Type::Top, "did not update array");
  assert( strcmp(Type::msg [Type::lastype - 1],"bottom") == 0, "did not update array");
  // assert( PhiNode::tbl     [Type::lastype - 1] == NULL,    "did not update array");
  //assert( Matcher::base2reg[Type::lastype - 1] == 0,      "did not update array");
  assert( isa_oop_ptr_tbl  [Type::lastype - 1] == (char)0,  "did not update array");
}
#endif

//=============================================================================
// Convenience common pre-built types.
const TypeF *TypeF::ZERO;       // Floating point zero
const TypeF *TypeF::ONE;        // Floating point one

//------------------------------make-------------------------------------------
// Create a float constant
const TypeF *TypeF::make(float f) {
  return (TypeF*)(new TypeF(f))->hashcons();
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeF::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is FloatCon
  switch (t->base()) {          // Switch on original type
  case AnyPtr:                  // Mixing with oops happens when javac
case OopPtr://reuses local variables
  case InstPtr:
  case AryPtr:
  case Int:
  case Long:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;

  case FloatBot:
    return t;

  case FloatCon:                // Float-constant vs Float-constant?
    if( jint_cast(_f) != jint_cast(t->getf()) )         // unequal constants?
                                // must compare bitwise as positive zero, negative zero and NaN have 
                                // all the same representation in C++
      return FLOAT;             // Return generic float
                                // Equal constants 
  case Top:
  case FloatTop:
    break;                      // Return the float constant
  }
  return this;                  // Return the float constant
}

//------------------------------xdual------------------------------------------
// Dual: symmetric
const Type *TypeF::xdual() const {
  return this;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeF::eq( const Type *t ) const {
  if( g_isnan(_f) || 
      g_isnan(t->getf()) ) {
    // One or both are NANs.  If both are NANs return true, else false.
    return (g_isnan(_f) && g_isnan(t->getf()));
  }
  if (_f == t->getf()) {
    // (NaN is impossible at this point, since it is not equal even to itself)
    if (_f == 0.0) {
      // difference between positive and negative zero
      if (jint_cast(_f) != jint_cast(t->getf()))  return false;
    }
    return true;
  }
  return false;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeF::hash(void) const {
  return *(int*)(&_f);
}

//------------------------------is_finite--------------------------------------
// Has a finite value
bool TypeF::is_finite() const {
  return g_isfinite(getf()) != 0;
}
  
//------------------------------is_nan-----------------------------------------
// Is not a number (NaN)
bool TypeF::is_nan()    const {
  return g_isnan(getf()) != 0;
}

//------------------------------dump2------------------------------------------
// Dump float constant Type
void TypeF::dump2( Dict &d, uint depth, outputStream *st ) const {
  Type::dump2(d,depth,st);
  st->print("%f", _f);
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants
// or a single symbol.
bool TypeF::singleton(void) const {
  return true;                  // Always a singleton
}

bool TypeF::empty(void) const {
  return false;                 // always exactly a singleton
}

//=============================================================================
// Convenience common pre-built types.
const TypeD *TypeD::ZERO;       // Floating point zero
const TypeD *TypeD::ONE;        // Floating point one

//------------------------------make-------------------------------------------
const TypeD *TypeD::make(double d) {
  return (TypeD*)(new TypeD(d))->hashcons();
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeD::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is DoubleCon
  switch (t->base()) {          // Switch on original type
  case AnyPtr:                  // Mixing with oops happens when javac
case OopPtr://reuses local variables
  case InstPtr:
  case AryPtr:
  case Int:
  case Long:
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;

  case DoubleBot:
    return t;

  case DoubleCon:               // Double-constant vs Double-constant?
    if( jlong_cast(_d) != jlong_cast(t->getd()) )       // unequal constants? (see comment in TypeF::xmeet)
      return DOUBLE;            // Return generic double
  case Top:
  case DoubleTop:
    break;
  }
  return this;                  // Return the double constant
}

//------------------------------xdual------------------------------------------
// Dual: symmetric
const Type *TypeD::xdual() const {
  return this;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeD::eq( const Type *t ) const {
  if( g_isnan(_d) || 
      g_isnan(t->getd()) ) {
    // One or both are NANs.  If both are NANs return true, else false.
    return (g_isnan(_d) && g_isnan(t->getd()));
  }
  if (_d == t->getd()) {
    // (NaN is impossible at this point, since it is not equal even to itself)
    if (_d == 0.0) {
      // difference between positive and negative zero
      if (jlong_cast(_d) != jlong_cast(t->getd()))  return false;
    }
    return true;
  }
  return false;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeD::hash(void) const {
  return *(int*)(&_d);
}

//------------------------------is_finite--------------------------------------
// Has a finite value
bool TypeD::is_finite() const {
  return g_isfinite(getd()) != 0;
}
  
//------------------------------is_nan-----------------------------------------
// Is not a number (NaN)
bool TypeD::is_nan()    const {
  return g_isnan(getd()) != 0;
}

//------------------------------dump2------------------------------------------
// Dump double constant Type
void TypeD::dump2( Dict &d, uint depth, outputStream *st ) const {
  Type::dump2(d,depth,st);
  st->print("%f", _d);
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants
// or a single symbol.
bool TypeD::singleton(void) const {
  return true;                  // Always a singleton
}

bool TypeD::empty(void) const {
  return false;                 // always exactly a singleton
}

//=============================================================================
// Convience common pre-built types.
const TypeInt *TypeInt::MINUS_1;// -1
const TypeInt *TypeInt::ZERO;   // 0
const TypeInt *TypeInt::ONE;    // 1
const TypeInt *TypeInt::BOOL;   // 0 or 1, FALSE or TRUE.
const TypeInt *TypeInt::CC;     // -1,0 or 1, condition codes
const TypeInt *TypeInt::CC_LT;  // [-1]  == MINUS_1
const TypeInt *TypeInt::CC_GT;  // [1]   == ONE
const TypeInt *TypeInt::CC_EQ;  // [0]   == ZERO
const TypeInt *TypeInt::CC_LE;  // [-1,0]
const TypeInt *TypeInt::CC_GE;  // [0,1] == BOOL (!)
const TypeInt *TypeInt::BYTE;   // Bytes, -128 to 127
const TypeInt *TypeInt::CHAR;   // Java chars, 0-65535
const TypeInt *TypeInt::SHORT;  // Java shorts, -32768-32767
const TypeInt *TypeInt::POS;    // Positive 32-bit integers or zero
const TypeInt *TypeInt::POS1;   // Positive 32-bit integers
const TypeInt *TypeInt::INT;    // 32-bit integers
const TypeInt *TypeInt::SYMINT; // symmetric range [-max_jint..max_jint]

//------------------------------TypeInt----------------------------------------
TypeInt::TypeInt( jint lo, jint hi, int w ) : Type(Int), _lo(lo), _hi(hi), _widen(w) {
}

//------------------------------make-------------------------------------------
const TypeInt *TypeInt::make( jint lo ) {
  return (TypeInt*)(new TypeInt(lo,lo,WidenMin))->hashcons();
}

#define SMALLINT ((juint)3)  // a value too insignificant to consider widening

const TypeInt *TypeInt::make( jint lo, jint hi, int w ) {
  // Certain normalizations keep us sane when comparing types.
  // The 'SMALLINT' covers constants and also CC and its relatives.
  assert(CC == NULL || (juint)(CC->_hi - CC->_lo) <= SMALLINT, "CC is truly small");
  if (lo <= hi) {
    if ((juint)(hi - lo) <= SMALLINT)   w = Type::WidenMin;
    if ((juint)(hi - lo) >= max_juint)  w = Type::WidenMax; // plain int
  }
  return (TypeInt*)(new TypeInt(lo,hi,w))->hashcons();
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type representation object
// with reference count equal to the number of Types pointing at it.
// Caller should wrap a Types around it.
const Type *TypeInt::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type?

  // Currently "this->_base" is a TypeInt
  switch (t->base()) {          // Switch on original type
  case AnyPtr:                  // Mixing with oops happens when javac
case OopPtr://reuses local variables
  case InstPtr:
  case AryPtr:
  case Long:
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:                     // No change
    return this;
  case Int:                     // Int vs Int?
    break;
  }

  // Expand covered set
  const TypeInt *r = t->is_int();
  return (new TypeInt( MIN2(_lo,r->_lo), MAX2(_hi,r->_hi), MAX2(_widen,r->_widen) ))->hashcons();
}

//------------------------------xdual------------------------------------------
// Dual: reverse hi & lo; flip widen
const Type *TypeInt::xdual() const {
  return new TypeInt(_hi,_lo,WidenMax-_widen);
}

//------------------------------widen------------------------------------------
// Only happens for optimistic top-down optimizations.
const Type *TypeInt::widen( const Type *old ) const {
  // Coming from TOP or such; no widening
  if( old->base() != Int ) return this;
  const TypeInt *ot = old->is_int();

  // If new guy is equal to old guy, no widening
  if( _lo == ot->_lo && _hi == ot->_hi ) 
    return old;

  // If new guy contains old, then we widened
  if( _lo <= ot->_lo && _hi >= ot->_hi ) {
    // New contains old
    // If new guy is already wider than old, no widening
    if( _widen > ot->_widen ) return this;
    // If old guy was a constant, do not bother
    if (ot->_lo == ot->_hi)  return this;
    // Now widen new guy.
    // Check for widening too far
    if (_widen == WidenMax) {
      if (min_jint < _lo && _hi < max_jint) {
        // If neither endpoint is extremal yet, push out the endpoint
        // which is closer to its respective limit.
        if (_lo >= 0 ||                 // easy common case
            (juint)(_lo - min_jint) >= (juint)(max_jint - _hi)) {
          // Try to widen to an unsigned range type of 31 bits:
          return make(_lo, max_jint, WidenMax);
        } else {
          return make(min_jint, _hi, WidenMax);
        }
      }
      return TypeInt::INT;
    }
    // Returned widened new guy
    return make(_lo,_hi,_widen+1);
  }

  // If old guy contains new, then we probably widened too far & dropped to
  // bottom.  Return the wider fellow.
  if ( ot->_lo <= _lo && ot->_hi >= _hi ) 
    return old;

  //fatal("Integer value range is not subset");
  //return this;
  return TypeInt::INT;
}

//------------------------------narrow---------------------------------------
// Only happens for pessimistic optimizations.
const Type *TypeInt::narrow( const Type *old ) const {
  if (_lo >= _hi)  return this;   // already narrow enough
  if (old == NULL)  return this;
  const TypeInt* ot = old->isa_int();
  if (ot == NULL)  return this;
  jint olo = ot->_lo;
  jint ohi = ot->_hi;

  // If new guy is equal to old guy, no narrowing
  if (_lo == olo && _hi == ohi)  return old;

  // If old guy was maximum range, allow the narrowing
  if (olo == min_jint && ohi == max_jint)  return this;

  if (_lo < olo || _hi > ohi)
    return this;                // doesn't narrow; pretty wierd

  // The new type narrows the old type, so look for a "death march".
  // See comments on PhaseTransform::saturate.
  juint nrange = _hi - _lo;
  juint orange = ohi - olo;
  if (nrange < max_juint - 1 && nrange > (orange >> 1) + (SMALLINT*2)) {
    // Use the new type only if the range shrinks a lot.
    // We do not want the optimizer computing 2^31 point by point.
    return old;
  }

  return this;
}

//-----------------------------filter------------------------------------------
const Type *TypeInt::filter( const Type *kills ) const {
  const TypeInt* ft = join(kills)->isa_int();
  if (ft == NULL || ft->_lo > ft->_hi)
    return Type::TOP;           // Canonical empty value
  if (ft->_widen < this->_widen) {
    // Do not allow the value of kill->_widen to affect the outcome.
    // The widen bits must be allowed to run freely through the graph.
    ft = TypeInt::make(ft->_lo, ft->_hi, this->_widen);
  }
  return ft;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeInt::eq( const Type *t ) const {
  const TypeInt *r = t->is_int(); // Handy access
  return r->_lo == _lo && r->_hi == _hi && r->_widen == _widen;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeInt::hash(void) const {
  return _lo+_hi+_widen+(int)Type::Int;
}

//------------------------------is_finite--------------------------------------
// Has a finite value
bool TypeInt::is_finite() const {
  return true;
}
  
//------------------------------dump2------------------------------------------
// Dump TypeInt
static const char* intname(char* buf, jint n) {
  if (n == min_jint)
    return "min";
  else if (n < min_jint + 10000)
    sprintf(buf, "min+" INT32_FORMAT, n - min_jint);
  else if (n == max_jint)
    return "max";
  else if (n > max_jint - 10000)
    sprintf(buf, "max-" INT32_FORMAT, max_jint - n);
  else
    sprintf(buf, INT32_FORMAT, n);
  return buf;
}

void TypeInt::dump2( Dict &d, uint depth, outputStream *st ) const {
  char buf[40], buf2[40];
  if (_lo == min_jint && _hi == max_jint)
    st->print("int");
  else if (is_con()) 
    st->print("int:%s", intname(buf, get_con()));
  else if (_lo == BOOL->_lo && _hi == BOOL->_hi) 
    st->print("bool");
  else if (_lo == BYTE->_lo && _hi == BYTE->_hi)
    st->print("byte");
  else if (_lo == CHAR->_lo && _hi == CHAR->_hi) 
    st->print("char");
  else if (_lo == SHORT->_lo && _hi == SHORT->_hi) 
    st->print("short");
  else if (_hi == max_jint)
    st->print("int:>=%s", intname(buf, _lo));
  else if (_lo == min_jint)
    st->print("int:<=%s", intname(buf, _hi));
  else
    st->print("int:%s..%s", intname(buf, _lo), intname(buf2, _hi));

  if (_widen != 0 && this != TypeInt::INT)
    st->print(":%.*s", _widen, "wwww");
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants.
bool TypeInt::singleton(void) const {
  return _lo >= _hi;
}

bool TypeInt::empty(void) const {
  return _lo > _hi;
}

//=============================================================================
// Convenience common pre-built types.
const TypeLong *TypeLong::MINUS_1;// -1
const TypeLong *TypeLong::ZERO; // 0
const TypeLong *TypeLong::ONE;  // 1
const TypeLong *TypeLong::POS;  // >=0
const TypeLong *TypeLong::LONG; // 64-bit integers
const TypeLong *TypeLong::INT;  // 32-bit subrange
const TypeLong *TypeLong::UINT; // 32-bit unsigned subrange

//------------------------------TypeLong---------------------------------------
TypeLong::TypeLong( jlong lo, jlong hi, int w ) : Type(Long), _lo(lo), _hi(hi), _widen(w) {
}

//------------------------------make-------------------------------------------
const TypeLong *TypeLong::make( jlong lo ) {
  return (TypeLong*)(new TypeLong(lo,lo,WidenMin))->hashcons();
}

const TypeLong *TypeLong::make( jlong lo, jlong hi, int w ) {
  // Certain normalizations keep us sane when comparing types.
  // The '1' covers constants.
  if (lo <= hi) {
    if ((julong)(hi - lo) <= SMALLINT)    w = Type::WidenMin;
    if ((julong)(hi - lo) >= max_julong)  w = Type::WidenMax; // plain long
  }
  return (TypeLong*)(new TypeLong(lo,hi,w))->hashcons();
}


//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type representation object
// with reference count equal to the number of Types pointing at it.
// Caller should wrap a Types around it.
const Type *TypeLong::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type?

  // Currently "this->_base" is a TypeLong
  switch (t->base()) {          // Switch on original type
  case AnyPtr:                  // Mixing with oops happens when javac
case OopPtr://reuses local variables
  case InstPtr:
  case AryPtr:
  case Int:
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:                     // No change
    return this;
  case Long:                    // Long vs Long?
    break;
  }

  // Expand covered set
  const TypeLong *r = t->is_long(); // Turn into a TypeLong
  // (Avoid TypeLong::make, to avoid the argument normalizations it enforces.)
  return (new TypeLong( MIN2(_lo,r->_lo), MAX2(_hi,r->_hi), MAX2(_widen,r->_widen) ))->hashcons();
}

//------------------------------xdual------------------------------------------
// Dual: reverse hi & lo; flip widen
const Type *TypeLong::xdual() const {
  return new TypeLong(_hi,_lo,WidenMax-_widen);
}

//------------------------------widen------------------------------------------
// Only happens for optimistic top-down optimizations.
const Type *TypeLong::widen( const Type *old ) const {
  // Coming from TOP or such; no widening
  if( old->base() != Long ) return this;
  const TypeLong *ot = old->is_long();

  // If new guy is equal to old guy, no widening
  if( _lo == ot->_lo && _hi == ot->_hi ) 
    return old;

  // If new guy contains old, then we widened
  if( _lo <= ot->_lo && _hi >= ot->_hi ) {
    // New contains old
    // If new guy is already wider than old, no widening
    if( _widen > ot->_widen ) return this;
    // If old guy was a constant, do not bother
    if (ot->_lo == ot->_hi)  return this;
    // Now widen new guy.
    // Check for widening too far
    if (_widen == WidenMax) {
      if (min_jlong < _lo && _hi < max_jlong) {
        // If neither endpoint is extremal yet, push out the endpoint
        // which is closer to its respective limit.
        if (_lo >= 0 ||                 // easy common case
            (julong)(_lo - min_jlong) >= (julong)(max_jlong - _hi)) {
          // Try to widen to an unsigned range type of 32/63 bits:
          if (_hi < max_juint)
            return make(_lo, max_juint, WidenMax);
          else
            return make(_lo, max_jlong, WidenMax);
        } else {
          return make(min_jlong, _hi, WidenMax);
        }
      }
      return TypeLong::LONG;
    }
    // Returned widened new guy
    return make(_lo,_hi,_widen+1);
  }

  // If old guy contains new, then we probably widened too far & dropped to
  // bottom.  Return the wider fellow.
  if ( ot->_lo <= _lo && ot->_hi >= _hi ) 
    return old;

  //  fatal("Long value range is not subset");
  // return this;
  return TypeLong::LONG;
}

//------------------------------narrow----------------------------------------
// Only happens for pessimistic optimizations.
const Type *TypeLong::narrow( const Type *old ) const {
  if (_lo >= _hi)  return this;   // already narrow enough
  if (old == NULL)  return this;
  const TypeLong* ot = old->isa_long();
  if (ot == NULL)  return this;
  jlong olo = ot->_lo;
  jlong ohi = ot->_hi;

  // If new guy is equal to old guy, no narrowing
  if (_lo == olo && _hi == ohi)  return old;

  // If old guy was maximum range, allow the narrowing
  if (olo == min_jlong && ohi == max_jlong)  return this;

  if (_lo < olo || _hi > ohi)
    return this;                // doesn't narrow; pretty wierd

  // The new type narrows the old type, so look for a "death march".
  // See comments on PhaseTransform::saturate.
  julong nrange = _hi - _lo;
  julong orange = ohi - olo;
  if (nrange < max_julong - 1 && nrange > (orange >> 1) + (SMALLINT*2)) {
    // Use the new type only if the range shrinks a lot.
    // We do not want the optimizer computing 2^31 point by point.
    return old;
  }

  return this;
}

//-----------------------------filter------------------------------------------
const Type *TypeLong::filter( const Type *kills ) const {
  const TypeLong* ft = join(kills)->isa_long();
  if (ft == NULL || ft->_lo > ft->_hi)
    return Type::TOP;           // Canonical empty value
  if (ft->_widen < this->_widen) {
    // Do not allow the value of kill->_widen to affect the outcome.
    // The widen bits must be allowed to run freely through the graph.
    ft = TypeLong::make(ft->_lo, ft->_hi, this->_widen);
  }
  return ft;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeLong::eq( const Type *t ) const {
  const TypeLong *r = t->is_long(); // Handy access
  return r->_lo == _lo &&  r->_hi == _hi  && r->_widen == _widen;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeLong::hash(void) const {
  return (int)(_lo+_hi+_widen+(int)Type::Long);
}

//------------------------------is_finite--------------------------------------
// Has a finite value
bool TypeLong::is_finite() const {
  return true;
}
  
//------------------------------dump2------------------------------------------
// Dump TypeLong
static const char* longnamenear(jlong x, const char* xname, char* buf, jlong n) {
  if (n > x) {
    if (n >= x + 10000)  return NULL;
sprintf(buf,"%s+%lld",xname,n-x);
  } else if (n < x) {
    if (n <= x - 10000)  return NULL;
sprintf(buf,"%s-%lld",xname,x-n);
  } else {
    return xname;
  }
  return buf;
}

static const char* longname(char* buf, jlong n) {
  const char* str;
  if (n == min_jlong)
    return "min";
  else if (n < min_jlong + 10000)
sprintf(buf,"min+%lld",n-min_jlong);
  else if (n == max_jlong)
    return "max";
  else if (n > max_jlong - 10000)
sprintf(buf,"max-%lld",max_jlong-n);
  else if ((str = longnamenear(max_juint, "maxuint", buf, n)) != NULL)
    return str;
  else if ((str = longnamenear(max_jint, "maxint", buf, n)) != NULL)
    return str;
  else if ((str = longnamenear(min_jint, "minint", buf, n)) != NULL)
    return str;
  else
sprintf(buf,"%lld",n);
  return buf;
}

void TypeLong::dump2( Dict &d, uint depth, outputStream *st ) const {
  char buf[80], buf2[80];
  if (_lo == min_jlong && _hi == max_jlong)
    st->print("long");
  else if (is_con()) 
    st->print("long:%s", longname(buf, get_con()));
  else if (_hi == max_jlong)
    st->print("long:>=%s", longname(buf, _lo));
  else if (_lo == min_jlong)
    st->print("long:<=%s", longname(buf, _hi));
  else
    st->print("long:%s..%s", longname(buf, _lo), longname(buf2, _hi));

  if (_widen != 0 && this != TypeLong::LONG)
    st->print(":%.*s", _widen, "wwww");
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants 
bool TypeLong::singleton(void) const {
  return _lo >= _hi;
}

bool TypeLong::empty(void) const {
  return _lo > _hi;
}

//=============================================================================
// Convenience common pre-built types.
const TypeTuple *TypeTuple::IFBOTH;     // Return both arms of IF as reachable
const TypeTuple *TypeTuple::IFFALSE;
const TypeTuple *TypeTuple::IFTRUE;
const TypeTuple *TypeTuple::IFNEITHER;
const TypeTuple *TypeTuple::LOOPBODY;
const TypeTuple *TypeTuple::MEMBAR;
const TypeTuple *TypeTuple::STORECONDITIONAL;
const TypeTuple *TypeTuple::INT_PAIR;
const TypeTuple *TypeTuple::LONG_PAIR;
const TypeTuple*TypeTuple::NEW_ARY_DOMAIN;
const TypeTuple*TypeTuple::NEW_OBJ_DOMAIN;


//------------------------------make-------------------------------------------
// Make a TypeTuple from the range of a method signature
const TypeTuple *TypeTuple::make_range(ciSignature* sig) {
  ciType* return_type = sig->return_type();
  uint total_fields = TypeFunc::Parms + return_type->size();
  const Type **field_array = fields(total_fields);
  switch (return_type->basic_type()) {
  case T_LONG:
    field_array[TypeFunc::Parms]   = TypeLong::LONG;
    field_array[TypeFunc::Parms+1] = Type::HALF;
    break;
  case T_DOUBLE:
    field_array[TypeFunc::Parms]   = Type::DOUBLE;
    field_array[TypeFunc::Parms+1] = Type::HALF;      
    break;
  case T_OBJECT:
  case T_ARRAY:
  case T_BOOLEAN:
  case T_CHAR:
  case T_FLOAT:
  case T_BYTE:
  case T_SHORT:
  case T_INT:
    field_array[TypeFunc::Parms] = get_const_type(return_type);
    break;
  case T_VOID:
    break;
  default:
    ShouldNotReachHere();
  }
  return (TypeTuple*)(new TypeTuple(total_fields,field_array))->hashcons();
}

// Make a TypeTuple from the domain of a method signature
const TypeTuple *TypeTuple::make_domain(ciInstanceKlass* recv, ciSignature* sig) {
  uint total_fields = TypeFunc::Parms + sig->size();

  uint pos = TypeFunc::Parms;
  const Type **field_array;
  if (recv != NULL) {
    total_fields++;
    field_array = fields(total_fields);
    // Use get_const_type here because it respects UseUniqueSubclasses:
    field_array[pos++] = get_const_type(recv)->join(TypePtr::NOTNULL);
  } else {
    field_array = fields(total_fields);
  }

  int i = 0;
  while (pos < total_fields) {
    ciType* type = sig->type_at(i);

    switch (type->basic_type()) {
    case T_LONG:
      field_array[pos++] = TypeLong::LONG;
      field_array[pos++] = Type::HALF;
      break;
    case T_DOUBLE:
      field_array[pos++] = Type::DOUBLE;
      field_array[pos++] = Type::HALF;      
      break;
    case T_OBJECT:
    case T_ARRAY:
    case T_BOOLEAN:
    case T_CHAR:
    case T_FLOAT:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
      field_array[pos++] = get_const_type(type);
      break;   
    default:
      ShouldNotReachHere();
    }
    i++;
  }
  return (TypeTuple*)(new TypeTuple(total_fields,field_array))->hashcons();
}

const TypeTuple *TypeTuple::make( uint cnt, const Type **fields ) {
  return (TypeTuple*)(new TypeTuple(cnt,fields))->hashcons();  
}

//------------------------------fields-----------------------------------------
// Subroutine call type with space allocated for argument types
const Type **TypeTuple::fields( uint arg_cnt ) {
  const Type **flds = (const Type **)(Compile::current()->type_arena()->Amalloc_4((TypeFunc::Parms+arg_cnt)*sizeof(Type*) ));
  flds[TypeFunc::Control  ] = Type::CONTROL;
  flds[TypeFunc::I_O      ] = Type::ABIO;
  flds[TypeFunc::Memory   ] = Type::MEMORY;
  flds[TypeFunc::FramePtr ] = TypeRawPtr::BOTTOM;
  flds[TypeFunc::ReturnAdr] = Type::RETURN_ADDRESS;

  return flds;
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeTuple::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is Tuple
  switch (t->base()) {          // switch on original type

  case Bottom:                  // Ye Olde Default
    return t;

  default:                      // All else is a mistake
    return Type::BOTTOM;

  case Tuple: {                 // Meeting 2 signatures?
    const TypeTuple *x = t->is_tuple();
    if( _cnt != x->_cnt ) return Type::BOTTOM;
    const Type **fields = (const Type **)(Compile::current()->type_arena()->Amalloc_4( _cnt*sizeof(Type*) ));
    for( uint i=0; i<_cnt; i++ )
      fields[i] = field_at(i)->xmeet( x->field_at(i) );
    return TypeTuple::make(_cnt,fields);
  }           
  case Top:    
    break;
  }
  return this;                  // Return the double constant
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type *TypeTuple::xdual() const {
  const Type **fields = (const Type **)(Compile::current()->type_arena()->Amalloc_4( _cnt*sizeof(Type*) ));
  for( uint i=0; i<_cnt; i++ )
    fields[i] = _fields[i]->dual();
  return new TypeTuple(_cnt,fields);
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeTuple::eq( const Type *t ) const {
  const TypeTuple *s = (const TypeTuple *)t;
  if (_cnt != s->_cnt)  return false;  // Unequal field counts
  for (uint i = 0; i < _cnt; i++)
    if (field_at(i) != s->field_at(i)) // POINTER COMPARE!  NO RECURSION!
      return false;             // Missed
  return true;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeTuple::hash(void) const {
  intptr_t sum = _cnt;
  for( uint i=0; i<_cnt; i++ )
    sum += (intptr_t)_fields[i];     // Hash on pointers directly
  return sum;
}

//------------------------------dump2------------------------------------------
// Dump signature Type
void TypeTuple::dump2( Dict &d, uint depth, outputStream *st ) const {
  st->print("{");
  if( !depth || d[this] ) {     // Check for recursive print
    st->print("...}");
    return;
  }
  d.Insert((void*)this, (void*)this);   // Stop recursion
  if( _cnt ) {
    uint i;
    for( i=0; i<_cnt-1; i++ ) {
      st->print("%d:", i);
      _fields[i]->dump2(d, depth-1,st);
      st->print(", ");
    }
    st->print("%d:", i);
    _fields[i]->dump2(d, depth-1,st);
  }
  st->print("}");
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants
// or a single symbol.
bool TypeTuple::singleton(void) const {
  return false;                 // Never a singleton
}

bool TypeTuple::empty(void) const {
  for( uint i=0; i<_cnt; i++ ) {
    if (_fields[i]->empty())  return true;
  }
  return false;
}

//=============================================================================
// Convenience common pre-built types.
const TypeAry *TypeAry::OOPS;
const TypeAry *TypeAry::PTRS;

//=============================================================================
// Convenience common pre-built types.

inline const TypeInt* normalize_array_size(const TypeInt* size) {
  // Certain normalizations keep us sane when comparing types.
  // We do not want arrayOop variables to differ only by the wideness
  // of their index types.  Pick minimum wideness, since that is the
  // forced wideness of small ranges anyway.
  if (size->_widen != Type::WidenMin)
    return TypeInt::make(size->_lo, size->_hi, Type::WidenMin);
  else
    return size;
}

//------------------------------make-------------------------------------------
const TypeAry *TypeAry::make( const Type *elem, const TypeInt *size) {
  size = normalize_array_size(size);
  return (TypeAry*)(new TypeAry(elem,size))->hashcons();
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeAry::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is Ary
  switch (t->base()) {          // switch on original type

  case Bottom:                  // Ye Olde Default
    return t;

  default:                      // All else is a mistake
    return Type::BOTTOM;

  case Array: {                 // Meeting 2 arrays?
    const TypeAry *a = t->is_ary();
const TypeInt*sz=_size->xmeet(a->_size)->is_int();
    if( sz->_lo == 0 && sz->_hi == max_jint )
      sz = TypeInt::POS;        // Blow off 'widen' for array widths
    return TypeAry::make(_elem->meet(a->_elem), sz);
  }
  case Top:
    break;
  }
  return this;                  // Return the double constant
}

//------------------------------klass_is_exact---------------------------------
bool TypeAry::klass_is_exact()const{
const TypePtr*tp=_elem->isa_ptr();
  return tp ? tp->klass_is_exact() : true;
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type *TypeAry::xdual() const {
  const TypeInt* size_dual = _size->dual()->is_int();
  size_dual = normalize_array_size(size_dual);
  return new TypeAry( _elem->dual(), size_dual);
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeAry::eq( const Type *t ) const {
  const TypeAry *a = (const TypeAry*)t;
  return _elem == a->_elem &&
    _size == a->_size;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeAry::hash(void) const {
  return (intptr_t)_elem + (intptr_t)_size;
}

//------------------------------dump2------------------------------------------
void TypeAry::dump2( Dict &d, uint depth, outputStream *st ) const {
  _elem->dump2(d, depth,st);
  st->print("[");
  _size->dump2(d, depth,st);
  st->print("]");
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants
// or a single symbol.
bool TypeAry::singleton(void) const {
  return false;                 // Never a singleton
}

bool TypeAry::empty(void) const {
  return _elem->empty() || _size->empty();
}

//--------------------------ary_must_be_exact----------------------------------
bool TypeAry::ary_must_be_exact() const {
  if (!UseExactTypes)       return false;
  // This logic looks at the element type of an array, and returns true
  // if the element type is either a primitive or a final instance class.
  // In such cases, an array built on this ary must have no subclasses.
  if (_elem == BOTTOM)      return false;  // general array not exact
  if (_elem == TOP   )      return false;  // inverted general array not exact
  const TypeOopPtr*  toop = _elem->isa_oopptr();
  if (!toop)                return true;   // a primitive type, like int
  ciKlass* tklass = toop->klass();
  if (tklass == NULL)       return false;  // unloaded class
  if (!tklass->is_loaded()) return false;  // unloaded class
  const TypeInstPtr* tinst = _elem->isa_instptr();
  if (tinst)                return tklass->as_instance_klass()->is_final();
  const TypeAryPtr*  tap = _elem->isa_aryptr();
  if (tap)                  return tap->ary()->ary_must_be_exact();
  return false;
}

//=============================================================================
// Convenience common pre-built types.
const TypePtr *TypePtr::NULL_PTR;
const TypePtr *TypePtr::NOTNULL;
const TypePtr *TypePtr::BOTTOM;

//------------------------------meet-------------------------------------------
// Meet over the PTR enum
const TypePtr::PTR TypePtr::ptr_meet[TypePtr::lastPTR][TypePtr::lastPTR] = {
  //              TopPTR,    AnyNull,   Constant, Null,   NotNull, BotPTR,
  { /* Top     */ TopPTR,    AnyNull,   Constant, Null,   NotNull, BotPTR,},
  { /* AnyNull */ AnyNull,   AnyNull,   Constant, BotPTR, NotNull, BotPTR,},
  { /* Constant*/ Constant,  Constant,  Constant, BotPTR, NotNull, BotPTR,},
  { /* Null    */ Null,      BotPTR,    BotPTR,   Null,   BotPTR,  BotPTR,},
  { /* NotNull */ NotNull,   NotNull,   NotNull,  BotPTR, NotNull, BotPTR,},
  { /* BotPTR  */ BotPTR,    BotPTR,    BotPTR,   BotPTR, BotPTR,  BotPTR,}
};

//------------------------------make-------------------------------------------
const TypePtr *TypePtr::make( TYPES t, enum PTR ptr, int offset ) {
  return (TypePtr*)(new TypePtr(t,ptr,offset))->hashcons();
}

//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypePtr::cast_to_ptr_type(PTR ptr)const{
  assert(_base == AnyPtr, "subclass must override cast_to_ptr_type");
  if( ptr == _ptr ) return this;
  return make(_base, ptr, _offset);
}
const TypePtr *TypePtr::meet_with_ptr(const TypePtr *tp) const {
  return make(AnyPtr, ptr_meet[tp->ptr()][_ptr], meet_offset(tp->offset()));
}

//------------------------------get_con----------------------------------------
intptr_t TypePtr::get_con() const {
  assert( _ptr == Null, "" );
  return _offset;
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypePtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is AnyPtr
  switch (t->base()) {          // switch on original type
  case Int:                     // Mixing ints & oops happens when javac
  case Long:                    // reuses local variables
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:
    return this;

  case RawPtr:
  case OopPtr:
  case InstPtr:
  case KlassPtr:
  case AryPtr:
  case AnyPtr: {                // Meeting to AnyPtrs
    const TypePtr *tp = t->is_ptr();
    return (ptr_meet[_ptr][tp->ptr()] == Null || below_centerline(_ptr)) ? meet_with_ptr(tp) : tp->meet_with_ptr(this);
  }
  }
  return this;                  
}

//------------------------------meet_offset------------------------------------
int TypePtr::meet_offset( int offset ) const {
  // Either is 'TOP' offset?  Return the other offset!
  if( _offset == OffsetTop ) return offset;
  if( offset == OffsetTop ) return _offset;
  // If either is different, return 'BOTTOM' offset
  if( _offset != offset ) return OffsetBot;
  return _offset;
}

//------------------------------dual_offset------------------------------------
int TypePtr::dual_offset( ) const {
  if( _offset == OffsetTop ) return OffsetBot;// Map 'TOP' into 'BOTTOM'
  if( _offset == OffsetBot ) return OffsetTop;// Map 'BOTTOM' into 'TOP'
  return _offset;               // Map everything else into self
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const TypePtr::PTR TypePtr::ptr_dual[TypePtr::lastPTR] = {
  BotPTR, NotNull, Constant, Null, AnyNull, TopPTR
};
const Type *TypePtr::xdual() const {
  return new TypePtr( AnyPtr, dual_ptr(), dual_offset() );
}

//------------------------------add_offset-------------------------------------
const TypePtr *TypePtr::add_offset( int offset ) const {
  if( offset == 0 ) return this; // No change
  if( _offset == OffsetBot ) return this;
  if(  offset == OffsetBot ) offset = OffsetBot;
  else if( _offset == OffsetTop || offset == OffsetTop ) offset = OffsetTop;
  else offset += _offset;
  return make( AnyPtr, _ptr, offset );
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypePtr::eq( const Type *t ) const {
  const TypePtr *a = (const TypePtr*)t;
  return _ptr == a->ptr() && _offset == a->offset();
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypePtr::hash(void) const {
  return _ptr + _offset;
}

//------------------------------dump2------------------------------------------
const char *const TypePtr::ptr_msg[TypePtr::lastPTR] = {
  "TopPTR","AnyNull","Constant","NULL","NotNull","BotPTR"
};

void TypePtr::dump2( Dict &d, uint depth, outputStream *st ) const {
  if( _ptr == Null ) st->print("NULL");
  else st->print("%s *", ptr_msg[_ptr]);
  if( _offset == OffsetTop ) st->print("+top");
  else if( _offset == OffsetBot ) st->print("+bot");
  else if( _offset ) st->print("+%d", _offset);
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants 
bool TypePtr::singleton(void) const {
  // TopPTR, Null, AnyNull, Constant are all singletons
  return (_offset != OffsetBot) && !below_centerline(_ptr);
}

bool TypePtr::empty(void) const {
  return (_offset == OffsetTop) || above_centerline(_ptr);
}

//=============================================================================
// Convenience common pre-built types.
const TypeRawPtr *TypeRawPtr::BOTTOM;
const TypeRawPtr *TypeRawPtr::NOTNULL;

//------------------------------make-------------------------------------------
const TypeRawPtr*TypeRawPtr::make(enum PTR ptr,int offset){
  assert( ptr != Constant, "what is the constant?" );
  assert( ptr != Null, "Use TypePtr for NULL" );
return(TypeRawPtr*)(new TypeRawPtr(ptr,0,offset))->hashcons();
}

const TypeRawPtr *TypeRawPtr::make( address bits ) {
  assert( bits, "Use TypePtr for NULL" );
return(TypeRawPtr*)(new TypeRawPtr(Constant,bits,0))->hashcons();
}

//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypeRawPtr::cast_to_ptr_type(PTR ptr)const{
  assert( ptr != Constant, "what is the constant?" );
  assert( ptr != Null, "Use TypePtr for NULL" );
  assert( _bits==0, "Why cast a constant address?");
  if( ptr == _ptr ) return this;
  return make(ptr,_offset);
}
const TypePtr *TypeRawPtr::meet_with_ptr(const TypePtr *tp) const {
  return make( ptr_meet[tp->ptr()][_ptr], meet_offset(tp->offset()));
}

//------------------------------get_con----------------------------------------
intptr_t TypeRawPtr::get_con() const {
  assert( _ptr == Null || _ptr == Constant, "" );
  return (intptr_t)_bits;
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeRawPtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is RawPtr
  switch( t->base() ) {         // switch on original type
  case Bottom:                  // Ye Olde Default
    return t; 
  case Top:
    return this;
  case RawPtr: {                // might be top, bot, any/not or constant
    enum PTR tptr = t->is_ptr()->ptr();
    enum PTR ptr = meet_ptr( tptr );
    if( ptr == Constant ) {     // Cannot be equal constants, so...
      if( tptr == Constant && _ptr != Constant)  return t; 
      if( _ptr == Constant && tptr != Constant)  return this; 
      ptr = NotNull;            // Fall down in lattice
    }
    return make( ptr, t->is_ptr()->meet_offset(_offset) );
  }
  case AnyPtr:                  // Meeting to AnyPtrs
    return t->xmeet(this);
  case KlassPtr:
  case AryPtr:
  case InstPtr:
  case OopPtr:
    return TypePtr::make(AnyPtr, BotPTR, meet_offset(t->is_ptr()->offset()));
  default:                      // All else is a mistake
    return Type::BOTTOM;
  }

  return this;                  
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type *TypeRawPtr::xdual() const {
return new TypeRawPtr(dual_ptr(),_bits,dual_offset());
}

//------------------------------add_offset-------------------------------------
const TypePtr *TypeRawPtr::add_offset( int offset ) const {
  if( offset == OffsetTop ) return BOTTOM; // Undefined offset-> undefined pointer
  if( offset == OffsetBot ) return BOTTOM; // Unknown offset-> unknown pointer
  if( offset == 0 ) return this; // No change
  switch (_ptr) {
  case TypePtr::TopPTR:
  case TypePtr::BotPTR:
  case TypePtr::NotNull:
    return this;
  case TypePtr::Null:
  case TypePtr::Constant:
    return make( _bits+offset );
  default:  ShouldNotReachHere();
  }
  return NULL;                  // Lint noise
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeRawPtr::eq( const Type *t ) const {
  const TypeRawPtr *a = (const TypeRawPtr*)t;
  return _bits == a->_bits && TypePtr::eq(t);
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeRawPtr::hash(void) const {
  return (intptr_t)_bits + TypePtr::hash();
}

//------------------------------dump2------------------------------------------
void TypeRawPtr::dump2( Dict &d, uint depth, outputStream *st ) const {
  if( _ptr == Constant ) 
st->print("%p",_bits);
  else
    st->print("rawptr:%s", ptr_msg[_ptr]);
}

//=============================================================================

static bool subsets( ciInstanceKlass *const*bigger, ciInstanceKlass *const*small ) {
  if( bigger == small ) return true;
  // Simple subset check: is 'small' a subset of 'bigger'
  for( ; *small; small++ ) {    // For all 'small'
    ciInstanceKlass *iface = *small;
    ciInstanceKlass *const*p;
    for( p = bigger; *p; p++ )
      if( *p == iface )
        break;
    if( !*p ) return false;     // Hit end of list but did not see it
  }
  return true;
}

//------------------------------TypeOopPtr-------------------------------------
static ciInstanceKlass *ciik = NULL; // The empty interface list
TypeOopPtr::TypeOopPtr( TYPES t, PTR ptr, ciKlass* k, bool xk, ciObject* o, int offset, ciInstanceKlass*const* ifaces ) 
  : TypePtr(t, ptr, offset), _const_oop(o), _klass(k), _klass_is_exact(xk), _ifaces(ifaces?ifaces:&ciik) { 
  assert0( _ifaces && (((intptr_t)(_ifaces[0]))&3) == 0 );
  // Either not an instance klass (unloaded or method) OR
  assert( FAM || !k || !k->is_loaded() || !k->is_instance_klass() || _base==KlassPtr ||
          // it is an instance klass, and therefore NOT an interface 
          (!((ciInstanceKlass*)k)->is_interface() &&
           // AND also we list all known implemented interfaces
           subsets(ifaces, ((ciInstanceKlass*)k)->transitive_interfaces()) &&
           // AND if exact we have no extra interfaces
           (!xk || subsets(((ciInstanceKlass*)k)->transitive_interfaces(), ifaces)) ),
"no direct interface types");
}

//------------------------------make-------------------------------------------
const TypeOopPtr*TypeOopPtr::make(PTR ptr,int offset){
  assert(ptr != Constant, "no constant generic pointers");
  ciKlass*  k = ciKlassKlass::make();
  bool      xk = false;
  ciObject* o = NULL;
return(TypeOopPtr*)(new TypeOopPtr(OopPtr,ptr,k,xk,o,offset,&ciik))->hashcons();
}


//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypeOopPtr::cast_to_ptr_type(PTR ptr)const{
  assert(_base == OopPtr, "subclass must override cast_to_ptr_type");
  if( ptr == _ptr ) return this;
  return make(ptr, _offset);
}
const TypePtr *TypeOopPtr::meet_with_ptr(const TypePtr *tp) const {
  return make( ptr_meet[tp->ptr()][_ptr], meet_offset(tp->offset()));
}

//-----------------------------cast_to_instance-------------------------------
const TypeOopPtr *TypeOopPtr::cast_to_instance(int instance_id) const {
  // There are no instances of a general oop. 
  // Return self unchanged.
  return this;
}

//-----------------------------cast_to_exactness-------------------------------
const TypePtr*TypeOopPtr::cast_to_exactness(bool klass_is_exact)const{
  // There is no such thing as an exact general oop. 
  // Return self unchanged.
  return this;
}


//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeOopPtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?
const TypePtr*tp=t->isa_ptr();

  // Current "this->_base" is OopPtr
  switch (t->base()) {          // switch on original type

  case Int:                     // Mixing ints & oops happens when javac
  case Long:                    // reuses local variables
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:
    return this;
  case AnyPtr:
    return t->xmeet(this);
  case RawPtr:
    return TypePtr::meet_with_ptr(t->is_ptr());
  case KlassPtr:
  case AryPtr:
  case InstPtr:
  case OopPtr:
    return above_centerline(_ptr) 
      ? tp->meet_with_ptr(this) 
      : this->meet_with_ptr(tp);
  } // End of switch
  return this;                  // Return the double constant
}


//------------------------------xdual------------------------------------------
// Dual of a pure heap pointer.  No relevant klass or oop information.
const Type *TypeOopPtr::xdual() const {
  assert(klass() == ciKlassKlass::make(), "no klasses here");
  assert(const_oop() == NULL,             "no constants here");
return new TypeOopPtr(_base,dual_ptr(),klass(),klass_is_exact(),const_oop(),dual_offset(),_ifaces);
}

//--------------------------make_from_klass_common-----------------------------
// Computes the element-type given a klass.
const TypePtr*TypeOopPtr::make_from_klass_common(ciKlass*klass,bool klass_change){
  assert(klass->is_java_klass(), "must be java language klass");
  if (klass->is_instance_klass()) {
    Compile* C = Compile::current();
    // Element is an instance
    bool klass_is_exact = false;
    if (klass->is_loaded()) {
      // Try to set klass_is_exact.
      ciInstanceKlass* ik = klass->as_instance_klass();
      klass_is_exact = ik->is_final();
      if (!klass_is_exact && klass_change
&&UseUniqueSubclasses){
ciInstanceKlass*sub=ik->unique_concrete_subklass(&C->_masm);
        if (sub != NULL) {
          klass = ik = sub;
          klass_is_exact = sub->is_final();
        }
      }
if(!klass_is_exact&&UseExactTypes){
	if (!ik->is_interface() && !ik->has_subklass()) {
          // Dependencies on no-more-subklasses installed in the type constructor
          klass_is_exact = true;
        }
      }
      if( ik->nof_implementors(&C->_masm)==0 ) // No implementors?
        return TypePtr::NULL_PTR; // Must be NULL!
      if( ik->is_interface() ) { // Making an interface type?
        // Not directly allowed; instead it is a j/l/Object known to implement
        // that interface.  Copy any existing implemented interfaces and self
        // to the list, to get a new larger list.
        int len=0;
        for( ciInstanceKlass** ifx = ik->transitive_interfaces(); *ifx; ifx++ ) len++;
        ciInstanceKlass** ifaces = (ciInstanceKlass**)C->type_arena()->Amalloc((len+2)*sizeof(ciInstanceKlass*));
        ciInstanceKlass** ifp = ifaces;
        for( ciInstanceKlass** ifx = ik->transitive_interfaces(); *ifx; ifx++ ) *ifp++ = *ifx;
        *ifp++ = ik;            // Append self to list
        *ifp++ = NULL;
        return TypeInstPtr::make(TypePtr::BotPTR, C->env()->Object_klass(), false, NULL, 0, ifaces );
      }
    } else if( klass == C->env()->unloaded_ciinstance_klass() ) {
      // No dependency added here; it was added when we made the original 
      // array-of-null (which is where we got the unloaded_ciobjarray from).
      return TypePtr::NULL_PTR;
    }
    return TypeInstPtr::make(TypePtr::BotPTR, klass, klass_is_exact, NULL, 0);
  } else if (klass->is_obj_array_klass()) {
    // Element is an object array.  Recursively call ourself - BUT we canNOT
    // sharpen the base class!  An array-of-interface cannot sharpen to an
    // array-of-concrete, lest the array itself fail a CheckCast, even though
    // we can only pull concrete objects out of the array.  Same goes for
    // array-of-array-of-interface - when we load the subarray out we canNOT
    // sharpen it then, only when loading the base element.  
ciKlass*eklass=klass->as_obj_array_klass()->element_klass();
    const TypePtr *etype = TypeOopPtr::make_from_klass_common(eklass, false);
    const TypeAry* arr0 = TypeAry::make(etype, TypeInt::POS);
    // We used to pass NotNull in here, asserting that the sub-arrays
    // are all not-null.  This is not true in generally, as code can 
    // slam NULLs down in the subarrays.
const TypeAryPtr*arr=TypeAryPtr::make(TypePtr::BotPTR,arr0,klass,0);
    return arr;
  } else if (klass->is_type_array_klass()) {
    // Element is an typeArray
    const Type* etype = get_const_basic_type(klass->as_type_array_klass()->element_type());
    const TypeAry* arr0 = TypeAry::make(etype, TypeInt::POS);
    // We used to pass NotNull in here, asserting that the array pointer
    // is not-null. That was not true in general.
const TypeAryPtr*arr=TypeAryPtr::make(TypePtr::BotPTR,arr0,klass,0);
    return arr;
  } else {
    ShouldNotReachHere();
    return NULL;
  }
}

//------------------------------make_from_constant-----------------------------
// Make a java pointer from an oop constant
const TypeOopPtr* TypeOopPtr::make_from_constant(ciObject* o) {
  bool oop_in_code = o->has_encoding() ;
    
if(o->is_method()){
    // Treat much like a typeArray of bytes, like below, but fake the type...
assert(oop_in_code,"must be a perm space object");
    const Type* etype = (Type*)get_const_basic_type(T_BYTE);
    const TypeAry* arr0 = TypeAry::make(etype, TypeInt::POS);
    ciKlass *klass = ciTypeArrayKlass::make((BasicType) T_BYTE);
const TypeAryPtr*arr=TypeAryPtr::make(TypePtr::Constant,o,arr0,klass,0);
    return arr;
  } else if( o->is_klass() ) {
    return TypeKlassPtr::make( TypePtr::Constant, o->as_klass(), 0 );
  } else {
    assert(o->is_java_object(), "must be java language object");
    assert(!o->is_null_object(), "null object not yet handled here.");
    ciKlass *klass = o->klass();
    if (klass->is_instance_klass()) {       
      // Element is an instance
if(!oop_in_code){//not a perm-space constant
        // %%% remove this restriction by rewriting non-perm ConPNodes in a later phase
        return TypeInstPtr::make(TypePtr::NotNull, klass, true, NULL, 0);
      }
      return TypeInstPtr::make(o);    
    } else if (klass->is_obj_array_klass()) {
      // Element is an object array. Recursively call ourself.
ciKlass*eklass=klass->as_obj_array_klass()->element_klass();
      const TypePtr* etype = TypeOopPtr::make_from_klass_common(eklass, false);
      const TypeAry* arr0  = TypeAry::make(etype, TypeInt::make(o->as_array()->length()));
if(!oop_in_code){//not a perm-space constant
        // %%% remove this restriction by rewriting non-perm ConPNodes in a later phase
return TypeAryPtr::make(TypePtr::NotNull,arr0,klass,0);
      }
const TypeAryPtr*arr=TypeAryPtr::make(TypePtr::Constant,o,arr0,klass,0);
      return arr;
    } else if (klass->is_type_array_klass()) {
      // Element is an typeArray
const Type*etype=(Type*)get_const_basic_type(klass->as_type_array_klass()->element_type());
      const TypeAry* arr0 = TypeAry::make(etype, TypeInt::make(o->as_array()->length()));
      // We used to pass NotNull in here, asserting that the array pointer
      // is not-null. That was not true in general.
if(!oop_in_code){//not a perm-space constant
        // %%% remove this restriction by rewriting non-perm ConPNodes in a later phase
return TypeAryPtr::make(TypePtr::NotNull,arr0,klass,0);
      }
const TypeAryPtr*arr=TypeAryPtr::make(TypePtr::Constant,o,arr0,klass,0);
      return arr;
    }
  } 
    
  ShouldNotReachHere();
  return NULL;
}

//------------------------------get_con----------------------------------------
intptr_t TypeOopPtr::get_con() const {
  assert( _ptr == Null || _ptr == Constant, "" );
  assert( _offset >= 0, "" );
  
  if (_offset != 0) {
    // After being ported to the compiler interface, the compiler no longer
    // directly manipulates the addresses of oops.  Rather, it only has a pointer
    // to a handle at compile time.  This handle is embedded in the generated
    // code and dereferenced at the time the nmethod is made.  Until that time,
    // it is not reasonable to do arithmetic with the addresses of oops (we don't
    // have access to the addresses!).  This does not seem to currently happen,
    // but this assertion here is to help prevent its occurrance.
C2OUT->print_cr("Found oop constant with non-zero offset");
    ShouldNotReachHere();
  }
  
  return (intptr_t)const_oop()->encoding();
}


//-----------------------------filter------------------------------------------
// Do not allow interface-vs.-noninterface joins to collapse to top.
const Type *TypeOopPtr::filter( const Type *kills ) const {

  const Type* ft = join(kills);

  // Azul's implementation of interfaces and classes is exact, and meets and
  // joins will never "forget" an interface.  This obfuscation is not needed.

  //const TypeInstPtr* ftip = ft->isa_instptr();
  //const TypeInstPtr* ktip = kills->isa_instptr();
  //
  //if (ft->empty()) {
  //  // Check for evil case of 'this' being a class and 'kills' expecting an
  //  // interface.  This can happen because the bytecodes do not contain
  //  // enough type info to distinguish a Java-level interface variable
  //  // from a Java-level object variable.  If we meet 2 classes which
  //  // both implement interface I, but their meet is at 'j/l/O' which
  //  // doesn't implement I, we have no way to tell if the result should
  //  // be 'I' or 'j/l/O'.  Thus we'll pick 'j/l/O'.  If this then flows
  //  // into a Phi which "knows" it's an Interface type we'll have to
  //  // uplift the type.
  //  if (!empty() && ktip != NULL && ktip->is_loaded() && ktip->klass()->is_interface())
  //    return kills;             // Uplift to interface
  //
  //  return Type::TOP;           // Canonical empty value
  //}
  //
  //// If we have an interface-typed Phi or cast and we narrow to a class type,
  //// the join should report back the class.  However, if we have a J/L/Object
  //// class-typed Phi and an interface flows in, it's possible that the meet &
  //// join report an interface back out.  This isn't possible but happens
  //// because the type system doesn't interact well with interfaces.
  //if (ftip != NULL && ktip != NULL &&
  //    ftip->is_loaded() &&  ftip->klass()->is_interface() && 
  //    ktip->is_loaded() && !ktip->klass()->is_interface()) {
  //  // Happens in a CTW of rt.jar, 320-341, no extra flags
  //  return ktip->cast_to_ptr_type(ftip->ptr());
  //}

  return ft;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeOopPtr::eq( const Type *t ) const {
  const TypeOopPtr *a = (const TypeOopPtr*)t;
if(_klass_is_exact!=a->_klass_is_exact){
return false;
  }
  ciObject* one = const_oop();
  ciObject* two = a->const_oop();
  if (one == NULL || two == NULL) {
if(one!=two)return false;
  } else {
if(!one->equals(two))return false;
  }
  // Check interface lists for equality
  if( _ifaces != a->_ifaces ) { // trivially equal cutout test
    if( !_ifaces || !a->_ifaces ) return false;
    // Hard O(n^2) search because lists are short
    int len2=0;
    for( ciInstanceKlass *const*p2 = a->_ifaces; *p2; p2++ )
      len2++;
    int len1=0;
    for( ciInstanceKlass *const*p1 =    _ifaces; *p1; p1++ ) {
      len1++;
      ciInstanceKlass *const*p2;
      for( p2 = a->_ifaces; *p2; p2++ )
        if( *p1 == *p2 )
          break;
      if( !*p2 ) return false;
    }
    if( len1 != len2 ) return false;
  }
  return TypePtr::eq(t);
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeOopPtr::hash(void) const {
int hash=(const_oop()?const_oop()->hash():0)+
_klass_is_exact;
if(_ifaces)
    for( ciInstanceKlass *const*iptr = _ifaces; *iptr; iptr++ )
      hash += (*iptr)->hash();
  return hash + TypePtr::hash();
}

//------------------------------dump2------------------------------------------
void TypeOopPtr::dump2( Dict &d, uint depth, outputStream *st ) const {
  st->print("oopptr:%s", ptr_msg[_ptr]);
  if( _klass_is_exact ) st->print(":exact");
if(const_oop())st->print("%p",const_oop());
  switch( _offset ) {
  case OffsetTop: st->print("+top"); break;
case OffsetBot:st->print("+bot");break;
  case         0: break;
  default:        st->print("+%d",_offset); break;
  }
  if( _ifaces && *_ifaces ) {
st->print("{Interfaces:");
    for( ciInstanceKlass *const*iptr = _ifaces; *iptr; iptr++ ) {
      (*iptr)->print_name_on(st);
st->print(",");
    }
    st->print("}");
  }
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants 
bool TypeOopPtr::singleton(void) const {
  // detune optimizer to not generate constant oop + constant offset as a constant!
  // TopPTR, Null, AnyNull, Constant are all singletons
  return (_offset == 0) && !below_centerline(_ptr);
}

//------------------------------xadd_offset------------------------------------
int TypeOopPtr::xadd_offset( int offset ) const {
  // Adding to 'TOP' offset?  Return 'TOP'!
  if( _offset == OffsetTop || offset == OffsetTop ) return OffsetTop;
  // Adding to 'BOTTOM' offset?  Return 'BOTTOM'!
  if( _offset == OffsetBot || offset == OffsetBot ) return OffsetBot;

  // assert( _offset >= 0 && _offset+offset >= 0, "" );
  // It is possible to construct a negative offset during PhaseCCP

  return _offset+offset;        // Sum valid offsets
}

//------------------------------add_offset-------------------------------------
const TypePtr *TypeOopPtr::add_offset( int offset ) const {
  return make( _ptr, xadd_offset(offset) );
}

//------------------------------make_new_alloc_sig-----------------------------
// Create a "new object-allocation call" signature using this type as the
// return type.
const TypeFunc *TypeOopPtr::make_new_alloc_sig(bool is_ary) const {
  const TypeTuple *domain = is_ary ? TypeTuple::NEW_ARY_DOMAIN : TypeTuple::NEW_OBJ_DOMAIN;

const Type**field_array=TypeTuple::fields(1);
  field_array[TypeFunc::Parms] = this;
const TypeTuple*range=TypeTuple::make(TypeFunc::Parms+1,field_array);

const TypeFunc*tf=TypeFunc::make(domain,range);
  return tf;
}


//=============================================================================
// Convenience common pre-built types.
const TypeInstPtr *TypeInstPtr::NOTNULL;
const TypeInstPtr *TypeInstPtr::BOTTOM;
const TypeInstPtr *TypeInstPtr::MIRROR;
const TypeInstPtr*TypeInstPtr::OBJECT;
const TypeInstPtr *TypeInstPtr::MARK;

//------------------------------TypeInstPtr-------------------------------------
TypeInstPtr::TypeInstPtr(PTR ptr,ciKlass*k,bool xk,ciObject*o,int off,ciInstanceKlass*const*ifaces)
:TypeOopPtr(InstPtr,ptr,k,xk,o,off,ifaces),_name(k->name()){
assert(k->is_loaded()||o==NULL,
          "cannot have constants with non-loaded klass");
  // Either const_oop() is NULL or else ptr is Constant
  assert0( (!o && ptr != Constant) || (o && ptr == Constant && xk == true) );
}

//------------------------------make-------------------------------------------
const TypeInstPtr *TypeInstPtr::make(PTR ptr, 
                                     ciKlass* k,
                                     bool xk,
                                     ciObject* o,
                                     int offset,
                                     ciInstanceKlass *const*ifaces ) {
  assert( !k->is_loaded() || k->is_instance_klass() ||
          k->is_method_klass(), "Must be for instance or method");
  // Ptr is never Null
  assert( ptr != Null, "NULL pointers are not typed" );

  if (!UseExactTypes)  xk = false;
if(ptr==Constant)xk=true;//Note:  This case includes meta-object constants, such as methods.
else o=NULL;
if(k->is_loaded()&&k->is_instance_klass()){
    ciInstanceKlass* ik = k->as_instance_klass();
    if (!xk && ik->is_final()) xk = true; // no inexact final klass
    if( !ifaces ) ifaces = ik->transitive_interfaces();
  }

  // Now hash this baby
  TypeInstPtr *result =
(TypeInstPtr*)(new TypeInstPtr(ptr,k,xk,o,offset,ifaces))->hashcons();

  return result;
}


//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypeInstPtr::cast_to_ptr_type(PTR ptr)const{
  if( ptr == _ptr ) return this;
  // Reconstruct _sig info here since not a problem with later lazy
  // construction, _sig will show up on demand.
return make(ptr,klass(),klass_is_exact(),(ptr==Constant?const_oop():NULL),_offset,_ifaces);
}
const TypePtr *TypeInstPtr::meet_with_ptr(const TypePtr *tp) const {
  PTR ptr = ptr_meet[tp->ptr()][_ptr];
  return make( ptr, klass(), klass_is_exact(), (ptr == Constant ? const_oop() : NULL), meet_offset(tp->offset()), _ifaces);
}


//-----------------------------iface_union------------------------------------
// Compute the union (or intersection) of interface sets.  Usually, both lists
// are very short (0 or 1 length) AND equal so fast-path these common cases.
static ciInstanceKlass *const*iface_union(ciInstanceKlass *const*p1, ciInstanceKlass *const*p2, bool do_union) {
  if( p1 == p2 ) return p1;     // Equal lists

  int len1=0, len2=0;
  for( ciInstanceKlass *const*ptr = p1; *ptr; ptr++ )
    len1++;
  for( ciInstanceKlass *const*ptr = p2; *ptr; ptr++ )
    len2++;
    
  if( len1 < len2 ) {           // Get smaller in p2/len2
    ciInstanceKlass *const*tmp = p1;   int tlen = len1;
    p1 = p2;                           len1 = len2;
    p2 = tmp;                          len2 = tlen;
  }
  if( subsets(p1,p2) ) {        // True subset?
    // Yes, return the larger set (or if equal, the smaller-valued ptr)
    if( len1 != len2 )          // Unequal lengths, return larger (smaller for intersection)
      return do_union ? p1 : p2;
    // Completely equal sets.  Select smaller ptr value list from 2 equal
    // lists, to enable more fast "list ptrs are equal" cutouts.
    return (intptr_t)p1 < (intptr_t)p2 ? p1 : p2;
  }

  // Compute complete union/intersection solution
  ciInstanceKlass **pnew = (ciInstanceKlass**)Compile::current()->type_arena()->Amalloc((len1+len2+1)*sizeof(ciInstanceKlass*));
  ciInstanceKlass **ps = pnew;
  ciInstanceKlass *const*pt;    // Scanner for larger set
  if( do_union )
    for( pt = p1; *pt; pt++ )   // Copy larger set for union
      *ps++ = *pt; 
  for( ; *p2; p2++ ) {          // Forall smaller set
    for( pt = p1; *pt; pt++ )   // Scan larger set
      if( *pt == *p2 )          // Found?
        break;
    // Found in larger set, so found in both sets.
    // If doing intersection & found, append
    // If doing union    & NOT found, append
    if( do_union ^ (*pt!=NULL) ) *ps++ = *p2;
  }

  *ps++ = NULL;                 // NULL-terminate list
  return pnew;
}


//-----------------------------cast_to_exactness-------------------------------
const TypePtr*TypeInstPtr::cast_to_exactness(bool klass_is_exact)const{
  if( klass_is_exact == _klass_is_exact ) return this;
  if (!UseExactTypes)  return this;
  if (!_klass->is_loaded())  return this;
  ciInstanceKlass* ik = _klass->as_instance_klass();
  if( (ik->is_final() || _const_oop) )  return this;  // cannot clear xk
//Exactness means we can no longer implement extra interfaces, only those
//that the exact class directly implements.  i.e., we cannot rely on child
//classes to implement exciting new interfaces.
return make(ptr(),klass(),klass_is_exact,const_oop(),_offset,
              iface_union(ik->transitive_interfaces(),_ifaces,false));
}

//-----------------------------cast_to_instance-------------------------------
const TypeOopPtr *TypeInstPtr::cast_to_instance(int instance_id) const {
  return this;         // NOT IMPLEMENTED ON AZUL
  //if( instance_id == _instance_id) return this;
  //bool exact = (instance_id == UNKNOWN_INSTANCE) ? _klass_is_exact : true;
  //
  //return make(ptr(), klass(), exact, const_oop(), _offset, _ifaces, instance_id);
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeInstPtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is Pointer
  switch (t->base()) {          // switch on original type

  case Int:                     // Mixing ints & oops happens when javac
  case Long:                    // reuses local variables
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:
    return this;
  case RawPtr:
case AryPtr://Reverse; do this in TypeAryPtr::xmeet
  case AnyPtr:
  case KlassPtr:
  case OopPtr:
    return t->xmeet(this);

  case InstPtr: {                // Meeting 2 Oops?
    // Found an InstPtr sub-type vs self-InstPtr type
const TypeInstPtr*thip=t->is_instptr();
    int m_off = this->meet_offset( thip->offset() );
    PTR m_ptr = this->meet_ptr( thip->ptr() );
    ciKlass *this_klass = this->klass();
ciKlass*thip_klass=thip->klass();

    // Check for subtyping in the Java hierarchy.  Klasses nearer Object are
    // larger (they include the subklasses) and we want the intersection.
    // So if the classes subtype, we want the more refined type.
    const TypeInstPtr *subtype = NULL;
    const TypeInstPtr *suptype = NULL;
    if( this_klass->is_subtype_of( thip_klass ) ) { subtype = this; suptype = thip; } 
    if( thip_klass->is_subtype_of( this_klass ) ) { subtype = thip; suptype = this; }
    bool sub_xk=0, sup_xk=0;        // Only valid if we have a subtype
    PTR sub_ptr = TopPTR, sup_ptr = TopPTR;
    if( subtype ) {             // Have a subtype?
      sub_xk = subtype->klass_is_exact();
      sup_xk = suptype->klass_is_exact();
      sub_ptr = subtype->ptr();
      sup_ptr = suptype->ptr();
    } else {
      // Check for both are 'top' (which allows NULL) and otherwise
      // incompatible.  Instead of a falling hard, just pick NULL.
      if( m_ptr == TypePtr::TopPTR ) return TypePtr::NULL_PTR;
      if( this->ptr() == TopPTR ) return thip->meet_with_ptr( TypePtr::NULL_PTR );
      if( thip->ptr() == TopPTR ) return this->meet_with_ptr( TypePtr::NULL_PTR );
      m_ptr = ptr_meet[m_ptr][NotNull]; // Will fall hard
    }

    // Both Types are Up: means both represent infinite sets of acceptable
    // answers (acceptable answer = pick any member of the set; it is a valid
    // semantics-preserving answer).  We need to intersect those infinite sets
    // of acceptable answers.
    if( above_centerline(m_ptr) ) {
      assert0( subtype );       // They subtype!
      // 'Exactness' prevents proper subtyping
      if( subtype->klass() == suptype->klass() || !sup_xk ) { // Super is not exact (or equal classes)
        bool m_xk = sub_xk | sup_xk; // Result is exact?
        // Interface lists are already an intersection of interfaces; just
        // union the iface-lists to make a bigger intersection, or intersect
        // the lists to get a smaller intersection.
        ciInstanceKlass *const* m_ifaces = iface_union(this->_ifaces,thip->_ifaces,true);
        // If one is exact and the other not-exact, we must check the
        // interface lists.  The exact type has an exact interface list, the
        // in-exact type can have extra interfaces (assumed implemented in
        // some child).  In other words, the union-interface-list better be
        // equal to the exact type's list.
        if( !m_xk || subsets( (sup_xk ? suptype->_ifaces : subtype->_ifaces), m_ifaces) ) {
          return make( m_ptr, subtype->klass(), m_xk, NULL, m_off, m_ifaces );
        } // Else exactness excludes required interface
      } // Else exactness prevents subtyping
      // Check for both are 'top' (which allows NULL) and otherwise
      // incompatible.  Instead of a falling hard, just pick NULL.
      if( m_ptr == TypePtr::TopPTR ) return TypePtr::NULL_PTR;
      // It is the program, and not the compiler, that chooses.  
      // We must fall below the centerline.
      sup_ptr = sub_ptr = NotNull; // Act "as if" it is below centerline
m_ptr=NotNull;
    }

    // Here either there is no subtype, OR the subtype is below the
    // centerline.
    ciInstanceKlass *const* m_ifaces = iface_union(this->_ifaces,thip->_ifaces,false);

    // Result is constant or down.  Do we have an up/down split?  If so, and
    // the up Type has any member which is also in the down Type then we can
    // keep the down Type.  Otherwise we end up dragging in an extra element
    // from the up Type which means the meet is more general than just the
    // down Type.
    if( suptype && above_centerline(sup_ptr) ) {
      if( sup_ptr == TypePtr::TopPTR && sub_ptr == TypePtr::BotPTR ) // Allow the use of NULL from a Top set
        return make(BotPTR, subtype->klass(), sub_xk, NULL, m_off, subtype->_ifaces );
      // The supertype is high; if it is not exact (or equal classes) then it
      // may implement all interfaces needed by the down subtype.
      if( subtype->klass() == suptype->klass() || !sup_xk ) {
        // If the subtype is not exact, it may implement all interfaces
        // including any required by the supertype.  If the subtype is exact
        // it's interface list is exclusive; the supertype better not require
        // any extra interfaces.
        if( !sub_xk || subsets(subtype->_ifaces, suptype->_ifaces) )
          return make( m_ptr, subtype->klass(), sub_xk, subtype->_const_oop, m_off, sup_xk ? m_ifaces : subtype->_ifaces );
        // Incompatible interfaces; lose exactness
        if( sub_ptr == TypePtr::Constant ) sub_ptr = TypePtr::NotNull;
        sub_xk = false;
      }
      if( subtype->klass() != suptype->klass() && sup_ptr == TypePtr::TopPTR ) // Allow the use of NULL from a Top set
        return make(ptr_meet[m_ptr][TypePtr::Null], subtype->klass(), sub_xk, subtype->_const_oop, m_off, subtype->_ifaces );
      // Supertype was high, but exactness prevents there being any element in
      // the supertype set which is also in the subtype set; so we must let
      // the program choose which supertype element.
sup_ptr=NotNull;
      m_ptr = ptr_meet[sub_ptr][sup_ptr];
    }

    // Subtype is high (and falling) and inexact - meaning we can pick
    // children from it that implement all the down (supertype) interfaces.
    if( subtype && above_centerline(sub_ptr) ) {
      if( sub_ptr == TypePtr::TopPTR && sup_ptr == TypePtr::BotPTR ) // Allow the use of NULL from a Top set
        return make(BotPTR, suptype->klass(), sup_xk, NULL, m_off, suptype->_ifaces );
      // The subtype is high; if it is not exact then it may implement all
      // interfaces needed by the down supertype.
      if( !sub_xk || subsets(subtype->_ifaces, suptype->_ifaces) )
        if( !sup_xk || subsets(suptype->_ifaces,subtype->_ifaces) )
          // If the subtype is not exact, it may implement all interfaces
          // including any required by the supertype.  If the subtype is exact
          // it's interface list is exclusive; the supertype better not require
          // any extra interfaces.
          return make( m_ptr, suptype->klass(), sup_xk, suptype->_const_oop, m_off, suptype->_ifaces );
      if( subtype->klass() != suptype->klass() && sub_ptr == TypePtr::TopPTR ) // Allow the use of NULL from a Top set
        return make(ptr_meet[m_ptr][TypePtr::Null], suptype->klass(), sup_xk, suptype->_const_oop, m_off, suptype->_ifaces );
      if( !sub_xk ) 
        m_ifaces = suptype->_ifaces;
    }

    if( subtype && suptype && subtype->klass() == suptype->klass() && m_ptr == TypePtr::Constant && suptype->_const_oop==subtype->_const_oop) {
      return make(TypePtr::Constant, suptype->klass(), sup_xk, suptype->_const_oop, m_off, suptype->_ifaces );
    }

    // Woohoo!  Fall hard - 
    if( m_ptr == Constant ) m_ptr = NotNull; // No chance of constants here
ciKlass*m_k=this_klass->least_common_ancestor(thip_klass);
    // Exact only if both are same klass and exact
    bool m_xk = subtype && subtype->klass() == suptype->klass() && sub_xk && sup_xk;
    return make( m_ptr, m_k, m_xk, NULL, m_off, m_ifaces );
  } // End of case InstPtr

  } // End of switch
  return this;                  // Return the double constant
}


//---------------------------java_mirror_type----------------------------------------
ciType* TypeInstPtr::java_mirror_type() const {
  // must be a singleton type
  if( const_oop() == NULL )  return NULL;

  // must be of type java.lang.Class
  if( klass() != ciEnv::current()->Class_klass() )  return NULL;

  return const_oop()->as_instance()->java_mirror_type();
}


//------------------------------xdual------------------------------------------
// Dual: do NOT dual on klasses.  This means I do NOT understand the Java
// inheritence mechanism.
const Type *TypeInstPtr::xdual() const {
return new TypeInstPtr(dual_ptr(),klass(),klass_is_exact(),const_oop(),dual_offset(),_ifaces);
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeInstPtr::eq( const Type *t ) const {
  return 
klass()->equals(t->is_instptr()->klass())&&
TypeOopPtr::eq(t);//Check sub-type stuff
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeInstPtr::hash(void) const {
  return klass()->hash() + TypeOopPtr::hash();
}

//------------------------------dump2------------------------------------------
// Dump oop Type
void TypeInstPtr::dump2( Dict &d, uint depth, outputStream *st ) const {
  // Print the name of the klass.
  klass()->print_name_on(st);
  if( klass()->is_instance_klass() && !((ciInstanceKlass*)klass())->uses_default_loader() ) 
    klass()->print(st);           // Very noisy, but see custom loader
  if( _klass_is_exact ) st->print(":exact");
  if( _ptr != BotPTR )
    st->print(":%s", ptr_msg[_ptr]);
if(const_oop())st->print(":%p",const_oop());
  if( _offset ) {               // Dump offset, if any
if(_offset==OffsetBot)st->print("+bot");
else if(_offset==OffsetTop)st->print("+top");
    else st->print("+%d", _offset);
  }
  if( _ifaces && *_ifaces ) {
st->print("{Interfaces:");
    for( ciInstanceKlass *const*iptr = _ifaces; *iptr; iptr++ ) {
      (*iptr)->print_name_on(st);
st->print(",");
    }
    st->print("}");
  }
  st->print(" *");
}

//------------------------------add_offset-------------------------------------
const TypePtr *TypeInstPtr::add_offset( int offset ) const {
return make(_ptr,klass(),klass_is_exact(),const_oop(),xadd_offset(offset),_ifaces);
}

//=============================================================================
// Convenience common pre-built types.
const TypeAryPtr *TypeAryPtr::RANGE;
const TypeAryPtr*TypeAryPtr::EKID;
const TypeAryPtr *TypeAryPtr::OOPS;
const TypeAryPtr*TypeAryPtr::PTRS;
const TypeAryPtr *TypeAryPtr::BYTES;
const TypeAryPtr *TypeAryPtr::SHORTS;
const TypeAryPtr *TypeAryPtr::CHARS;
const TypeAryPtr *TypeAryPtr::INTS;
const TypeAryPtr *TypeAryPtr::LONGS;
const TypeAryPtr *TypeAryPtr::FLOATS;
const TypeAryPtr *TypeAryPtr::DOUBLES;


static bool xact_check_for_array( ciKlass *k, const Type *elem ) {
  if( k == ciEnv::current()->Object_klass() ) return false;
if(!k){//The rare no-klass-but-ary-is-exact case
    const TypeInstPtr *tinst = elem->isa_instptr();
    if( !tinst ) return false;
k=ciObjArrayKlass::make(tinst->klass());
  }
  // Abstract and interface arrays are not exact, even with no (obvious)
  // implementors.  Suppose the element type has some abstract implementors
  // but no concrete ones.  Then the array must be full of NULLs only and so
  // it appears to be an exact array.  But really it might be an array-of-interface 
  // (and full of NULLs) or an array-of-abstract (and still full of NULLs).
  // The types are different and a checkcast can tell them apart.
  if( !k->is_obj_array_klass() ) return true;
ciKlass*eklass=k->as_obj_array_klass()->element_klass();
  return !eklass->is_abstract();
}

TypeAryPtr::TypeAryPtr( PTR ptr, ciObject* o, const TypeAry *ary, ciKlass* k, bool xk, int offset, ciInstanceKlass*const* ifaces ) : TypeOopPtr(AryPtr,ptr,k,xk,o,offset, ifaces), _ary(ary) {
  if( !xk && ary->ary_must_be_exact() ) xk = true;
  assert( (ptr==Constant && o) || (ptr!=Constant && !o), "" );
  assert(!(k == NULL && ary->_elem->isa_int()), "integral arrays must be pre-equipped with a class");
}

//------------------------------make-------------------------------------------
const TypeAryPtr*TypeAryPtr::make(PTR ptr,ciObject*o,const TypeAry*ary,ciKlass*k,bool xk,int offset,ciInstanceKlass*const*ifaces){
  if( ary->_elem == Type::BOTTOM ) {
    k = ciEnv::current()->Object_klass();
    ifaces = ciEnv::current()->array_ifaces();
    xk = false;
  }
return(TypeAryPtr*)(new TypeAryPtr(ptr,o,ary,k,xk,offset,ifaces))->hashcons();
}

//------------------------------make-------------------------------------------
const TypeAryPtr*TypeAryPtr::make(PTR ptr,ciObject*o,const TypeAry*ary,ciKlass*k,int offset,ciInstanceKlass*const*ifaces){
  return make(ptr, o, ary, k, ary->klass_is_exact() && xact_check_for_array(k,ary->_elem), offset, ifaces);
}

//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypeAryPtr::cast_to_ptr_type(PTR ptr)const{
  if( ptr == _ptr ) return this;
return make(ptr,const_oop(),_ary,klass(),_offset);
}
const TypePtr *TypeAryPtr::meet_with_ptr(const TypePtr *tp) const {
  TypePtr::PTR ptr = ptr_meet[tp->ptr()][_ptr];
ciObject*con=NULL;
  if( ptr == TypePtr::Constant ) { // Result is a  Constant?
    if( tp->ptr() == TypePtr::Constant ) { // Either one is a Constant and the other is either Constant or AnyNull or TOP
      con = tp->is_oopptr()->const_oop();
      Unimplemented();
      if( _ptr == TypePtr::Constant &&
          !con->equals(_const_oop) ) {
ptr=TypePtr::NotNull;
con=NULL;
      }
    } else {
      assert0( tp->ptr() == TypePtr::AnyNull || tp->ptr() == TypePtr::TopPTR );
con=_const_oop;
    }
  }
  return make(ptr, con, _ary, klass(), _klass_is_exact, meet_offset(tp->offset()), _ifaces);
}


//-----------------------------cast_to_exactness-------------------------------
const TypePtr*TypeAryPtr::cast_to_exactness(bool xk)const{
if(xk==_klass_is_exact)return this;
  if( !xk && _ary->ary_must_be_exact() ) return this; // Cannot clear exactness
return make(ptr(),const_oop(),_ary,klass(),xk,_offset,_ifaces);
}

//-----------------------------cast_to_instance-------------------------------
const TypeOopPtr *TypeAryPtr::cast_to_instance(int instance_id) const {
  return this;         // NOT IMPLEMENTED ON AZUL
  //if( instance_id == _instance_id) return this;
  //bool exact = (instance_id == UNKNOWN_INSTANCE) ? _klass_is_exact : true;
  //return make(ptr(), const_oop(), _ary, klass(), exact, _offset, _ifaces, instance_id);
}

//-----------------------------narrow_size_type-------------------------------
// Local cache for arrayOopDesc::max_array_length(etype),
// which is kind of slow (and cached elsewhere by other users).
static jint max_array_length_cache[T_CONFLICT+1];
static jint max_array_length(BasicType etype) {
  jint& cache = max_array_length_cache[etype];
  jint res = cache;
  if (res == 0) {
    switch (etype) {
    case T_CONFLICT:
    case T_ILLEGAL:
    case T_VOID:
      etype = T_BYTE;           // will produce conservatively high value
    }
    cache = res = arrayOopDesc::max_array_length(etype);
  }
  return res;
}

// Narrow the given size type to the index range for the given array base type.
// Return NULL if the resulting int type becomes empty.
const TypeInt* TypeAryPtr::narrow_size_type(const TypeInt* size, BasicType elem) {
  jint hi = size->_hi;
  jint lo = size->_lo;
  jint min_lo = 0;
  jint max_hi = max_array_length(elem);
  //if (index_not_size)  --max_hi;     // type of a valid array index, FTR
  bool chg = false;
  if (lo < min_lo) { lo = min_lo; chg = true; }
  if (hi > max_hi) { hi = max_hi; chg = true; }
  if (lo > hi)
    return NULL;
  if (!chg)
    return size;
  return TypeInt::make(lo, hi, Type::WidenMin);
}

//-------------------------------cast_to_size----------------------------------
const TypeAryPtr* TypeAryPtr::cast_to_size(const TypeInt* new_size) const {
  assert(new_size != NULL, "");
  new_size = narrow_size_type(new_size, elem()->basic_type());
  if (new_size == NULL)       // Negative length arrays will produce weird
    new_size = TypeInt::ZERO; // intermediate dead fast-path goo
  if (new_size == size())  return this;
  const TypeAry* new_ary = TypeAry::make(elem(), new_size);
return make(ptr(),const_oop(),new_ary,klass(),klass_is_exact(),_offset,_ifaces);
}


//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeAryPtr::eq( const Type *t ) const {
  const TypeAryPtr *p = t->is_aryptr();
  return 
    _ary == p->_ary &&  // Check array
    TypeOopPtr::eq(p);  // Check sub-parts
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeAryPtr::hash(void) const {
  return (intptr_t)_ary + TypeOopPtr::hash();
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeAryPtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?
  // Current "this->_base" is Pointer
  switch (t->base()) {          // switch on original type

  // Mixing ints & oops happens when javac reuses local variables
  case Int:
  case Long:
  case FloatTop:
  case FloatCon:
  case FloatBot:
  case DoubleTop:
  case DoubleCon:
  case DoubleBot:
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:
    return this;
  case KlassPtr:
    return TypeOopPtr::make(BotPTR,meet_offset(t->is_ptr()->offset()));

  case RawPtr:
  case AnyPtr:
  case OopPtr:
    return t->xmeet(this);

case AryPtr:{//Meeting 2 array pointers?
    const TypeAryPtr *tap = t->is_aryptr();
    int off = meet_offset(tap->offset());
    const TypeAry *tary = _ary->meet(tap->_ary)->is_ary();
    PTR ptr = meet_ptr(tap->ptr());
    ciKlass* lazy_klass = NULL;
    bool xk = false;
    bool this_xk = this->klass_is_exact();
    bool that_xk = tap ->klass_is_exact();
    bool sub1 = klass()->is_subtype_of(tap->klass());
    bool sub2 = tap->klass()->is_subtype_of(klass());
    if( tary==     _ary ) {     // Falling to one or the other?
      lazy_klass =      klass(); 
      xk = this_xk && !that_xk;
    }
    if( tary==tap->_ary ) {     // Falling to one or the other?
      lazy_klass = tap->klass();
      xk = that_xk && !this_xk;
    }
    if( tap->klass() == klass() ) {
      lazy_klass = klass();     // Equal _klasses?  Take one.
      xk = above_centerline(ptr) ? (this_xk|that_xk) : (this_xk&that_xk);
    }
    if( !sub1 && !sub2 && (this_xk || that_xk) ) { // Plain old incompatible arrays; e.g. array-of-Cloneable vs array-of-String
      // Something like String[int+] meets Cloneable[int+]
      // Something like byte[int+] meets char[int+], or byte[] meets float[].
      // Class is j/l/Object, not any array klass.
      // Result is known typed as an array though.
      const TypeAry *tx = TypeAry::make(Type::BOTTOM,TypeInt::POS);
      return make( ptr_meet[ptr][NotNull], NULL, tx, ciEnv::current()->Object_klass(), false, off, _ifaces );
    }
    // Still no klass?  (both input arrays are lazy-klasses - hence not
    // arrays-of-interfaces?)  But result is array-of-Object?  Array-of-Object
    // can be confused with array-of-interface, so declare the ciKlass now.
    if( !lazy_klass && 
        tary->_elem->isa_oopptr() &&
        ((TypeOopPtr*)tary->_elem)->klass() == ciEnv::current()->Object_klass() ) {
      if( _klass && tap->_klass && tap->_klass != _klass ) {
        if( ptr == TopPTR  ) ptr = BotPTR; // Incompatible interfaces must fall
        if( ptr == AnyNull ) ptr = NotNull;
      }
      return make( ptr, NULL, tary, ciObjArrayKlass::make(ciEnv::current()->Object_klass()), xk, off, _ifaces );
    }

    switch (ptr) {
    case AnyNull: 
    case TopPTR:  
      return make( ptr, const_oop(), tary, lazy_klass, xk, off, _ifaces );
    case Constant: 
      if( _ptr != Constant )
        return make( Constant, tap->const_oop(), tary, tap->_klass, off, _ifaces );
      if( tap->_ptr != Constant || const_oop()->equals(tap->const_oop()) )
        return make( Constant,      const_oop(), tary,      _klass, off, _ifaces );
ptr=NotNull;//Incompatible constants, must fall
      // Fall into next case
    case NotNull: 
case BotPTR:{//Compute new klass on demand, do not use tap->_klass
if(above_centerline(this->ptr()))xk=tap->klass_is_exact();
if(above_centerline(tap->ptr()))xk=this->klass_is_exact();
return make(ptr,NULL,tary,lazy_klass,xk,off,_ifaces);
    }
    default: ShouldNotReachHere();
    }
  }

  // All arrays inherit from Object class
  case InstPtr: {
ciEnv*E=ciEnv::current();
    const TypeInstPtr *tp = t->is_instptr();
    int offset = meet_offset(tp->offset());
    PTR ptr = meet_ptr(tp->ptr());
bool tp_is_Object=tp->klass()->equals(E->Object_klass());
bool allows_array_ifaces=above_centerline(tp->ptr())&&!tp->klass_is_exact();
//If 'tp' is above the centerline and it is Object class then we can
    // subclass in the Java class hierarchy.
    if( tp_is_Object && allows_array_ifaces && subsets(E->array_ifaces(), tp->ifaces()) )
      return make( ptr, NULL, _ary, _klass, _klass_is_exact, offset, E->array_ifaces() );
    if( above_centerline(ptr) ) ptr = NotNull;

    // If the instance klass includes NULL, use it as an instance of a valid
    // array-ptr-like-thing.
    if( tp->ptr() == TypePtr::TopPTR )
      return make( ptr_meet[ptr][TypePtr::Null], _ary, _klass, offset );
    // Compute interface intersection if needed
    ciInstanceKlass *const* ifaces = allows_array_ifaces
      ? E->array_ifaces()     // Inexact up class implements all interfaces
      : iface_union(_ifaces,tp->ifaces(),false);
    // Same in reverse; allow NULL to not pollute an instance klass with an array
    // (unless the meet is already compatible without the NULL).
    if( _ptr == TypePtr::TopPTR &&
        (!tp_is_Object || tp->ifaces() != ifaces) )
      return TypeInstPtr::make( ptr_meet[ptr][TypePtr::Null], tp->klass(), tp->klass_is_exact(), NULL, offset, tp->ifaces() );
    // The other case cannot happen, since t cannot be a subtype of an array.
    // The meet falls down to Object class below centerline.
    if( ptr == Constant )
      ptr = NotNull;
return TypeInstPtr::make(ptr,E->Object_klass(),false,NULL,offset,ifaces);
  }

  }
  return this;                  // Lint noise
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type *TypeAryPtr::xdual() const {
return new TypeAryPtr(dual_ptr(),_const_oop,_ary->dual()->is_ary(),_klass,_klass_is_exact,dual_offset(),_ifaces);
}

//------------------------------dump2------------------------------------------
void TypeAryPtr::dump2( Dict &d, uint depth, outputStream *st ) const {
  _ary->dump2(d,depth,st);
  switch( _ptr ) {
  case Constant:
    const_oop()->print(st);
    break;
  case BotPTR:
    if( _klass_is_exact ) st->print(":exact");
    break;
  case TopPTR:
  case AnyNull:
  case NotNull:
    st->print(":%s", ptr_msg[_ptr]);
    if( _klass_is_exact ) st->print(":exact");
    break;
  }

  st->print("*");
if(_offset){
if(_offset==OffsetTop)st->print("+top");
else if(_offset==OffsetBot)st->print("+bot");
else if(_offset==arrayOopDesc::length_offset_in_bytes())st->print("._len");
    else if( _offset == objArrayOopDesc::ekid_offset_in_bytes() && _ary->_elem->isa_oopptr() ) st->print(".ekid");
    else {
BasicType bt=T_OBJECT;
      if( 0 ) ;
      else if( this == TypeAryPtr::BYTES  ) bt = T_BYTE;
      else if( this == TypeAryPtr::CHARS  ) bt = T_CHAR;
      else if( this == TypeAryPtr::SHORTS ) bt = T_SHORT;
      else if( this == TypeAryPtr::INTS   ) bt = T_INT;
      else if( this == TypeAryPtr::FLOATS ) bt = T_FLOAT;
      else if( this == TypeAryPtr::LONGS  ) bt = T_LONG;
      else if( this == TypeAryPtr::DOUBLES) bt = T_DOUBLE;
      int index = (_offset - arrayOopDesc::base_offset_in_bytes(bt)) >> type2logaelembytes[bt];
st->print("[%d]",index);
    }
  }
  if( _ifaces && _ifaces != ciEnv::current()->array_ifaces() ) {
st->print("{Interfaces:");
    for( ciInstanceKlass *const*iptr = _ifaces; *iptr; iptr++ ) {
      (*iptr)->print_name_on(st);
st->print(",");
    }
    st->print("}");
  }
}

bool TypeAryPtr::empty(void) const {
  if (_ary->empty())       return true;
  return TypeOopPtr::empty();
}

//------------------------------add_offset-------------------------------------
const TypePtr *TypeAryPtr::add_offset( int offset ) const {
return make(_ptr,_const_oop,_ary,_klass,_klass_is_exact,xadd_offset(offset),ciEnv::current()->array_ifaces());
}


//=============================================================================
// Convenience common pre-built types.

// Not-null object klass or below
const TypeKlassPtr *TypeKlassPtr::OBJECT;
const TypeKlassPtr*TypeKlassPtr::KID;
const TypeKlassPtr *TypeKlassPtr::OBJECT_OR_NULL;
const TypeKlassPtr*TypeKlassPtr::KID_OR_NULL;

//------------------------------TypeKlasPtr------------------------------------
TypeKlassPtr::TypeKlassPtr(PTR ptr,ciKlass*klass,int offset,char is_kid)
:TypeOopPtr(KlassPtr,ptr,klass,(ptr==Constant),(ptr==Constant?klass:NULL),offset,&ciik),_is_kid(is_kid){
  assert0( !is_kid || !offset );
}

//------------------------------make-------------------------------------------
// ptr to klass 'k', if Constant, or possibly to a sub-klass if not a Constant
const TypeKlassPtr *TypeKlassPtr::make( PTR ptr, ciKlass* k, int offset ) {
  assert( k != NULL, "Expect a non-NULL klass");
  assert(k->is_instance_klass() || k->is_array_klass() ||
         k->is_method_klass(), "Incorrect type of klass oop");
  TypeKlassPtr *r =
(TypeKlassPtr*)(new TypeKlassPtr(ptr,k,offset,false))->hashcons();

  return r;
}

//------------------------------make-------------------------------------------
// ptr to klass 'k', if Constant, or possibly to a sub-klass if not a Constant
const TypeKlassPtr *TypeKlassPtr::make( PTR ptr, ciKlass* k, int offset, char is_kid ) {
  assert( k != NULL, "Expect a non-NULL klass");
  assert(k->is_instance_klass() || k->is_array_klass() ||
         k->is_method_klass(), "Incorrect type of klass oop");
  TypeKlassPtr *r =
    (TypeKlassPtr*)(new TypeKlassPtr(ptr, k, offset, is_kid))->hashcons();

  return r;
}

//------------------------------make_kid----------------------------------------
// ptr to constant klass 'k', treated as a KID reference at code-gen time.
const TypeKlassPtr *TypeKlassPtr::make_kid( ciKlass* k, bool is_exact ) {
  assert( k != NULL, "Expect a non-NULL klass");
  assert(k->is_instance_klass() || k->is_array_klass() ||
         k->is_method_klass(), "Incorrect type of klass oop");
  TypePtr::PTR ptr = k ? (is_exact ? Constant : NotNull) : Null;
  TypeKlassPtr *r =
    (TypeKlassPtr*)(new TypeKlassPtr(ptr, k, 0, true))->hashcons();

  return r;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeKlassPtr::eq( const Type *t ) const {
  const TypeKlassPtr *p = t->is_klassptr();
  return 
    _is_kid == p->_is_kid &&
    klass()->equals(p->klass()) && 
    TypeOopPtr::eq(p);
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeKlassPtr::hash(void) const {
return(klass()->hash()+TypeOopPtr::hash())<<_is_kid;
}


//------------------------------klass------------------------------------------
// Return the defining klass for this class
ciKlass* TypeAryPtr::klass() const {
  if( _klass ) return _klass;   // Return cached value, if possible

  // Oops, need to compute _klass and cache it
  ciKlass* k_ary = NULL;
  const TypeInstPtr *tinst;
  const TypeAryPtr *tary;
  // Get element klass
  if ((tinst = elem()->isa_instptr()) != NULL) {
    // Compute array klass from element klass
    assert( !tinst->ifaces()[0] || tinst->klass() != ciEnv::current()->Object_klass(), 
"no lazy array klasses for interfaces");
    assert0( (((intptr_t)(tinst->ifaces()[0]))&3) == 0 );
    k_ary = ciObjArrayKlass::make(tinst->klass());
  } else if ((tary = elem()->isa_aryptr()) != NULL) {
    // Compute array klass from element klass
    ciKlass* k_elem = tary->klass();
    // If element type is something like bottom[], k_elem will be null.
    if (k_elem != NULL)
      k_ary = ciObjArrayKlass::make(k_elem);
  } else if ((elem()->base() == Type::Top) || 
             (elem()->base() == Type::Bottom)) {
    // element type of Bottom occurs from meet of basic type
    // and object; Top occurs when doing join on Bottom.
    k_ary = ciEnv::current()->Object_klass();
  } else if( elem() == TypePtr::NULL_PTR ) {
    // Array of some unloaded klass - must be NULL always
    k_ary = ciEnv::current()->unloaded_ciobjarrayklass();
  } else if( elem()->base() == Type::OopPtr ) {
return NULL;//Not representable as a Java array?
  } else {
    // Cannot compute array klass directly from basic type,
    // since subtypes of TypeInt all have basic type T_INT.
    assert(!elem()->isa_int(),
           "integral arrays must be pre-equipped with a class");
    // Compute array klass directly from basic type
    k_ary = ciTypeArrayKlass::make(elem()->basic_type());
  }
  
  if( this->_ary != TypeAry::OOPS &&
      this->_ary != TypeAry::OOPS->dual() )
    // The _klass field acts as a cache of the underlying
    // ciKlass for this array type.  In order to set the field,
    // we need to cast away const-ness.
    //
    // IMPORTANT NOTE: we *never* set the _klass field for the
    // type TypeAryPtr::OOPS.  This Type is shared between all
    // active compilations.  However, the ciKlass which represents
    // this Type is *not* shared between compilations, so caching
    // this value would result in fetching a dangling pointer.
    //
    // Recomputing the underlying ciKlass for each request is
    // a bit less efficient than caching, but calls to
    // TypeAryPtr::OOPS->klass() are not common enough to matter.
    ((TypeAryPtr*)this)->_klass = k_ary;
  return k_ary;
}


//------------------------------add_offset-------------------------------------
// Access internals of klass object
const TypePtr *TypeKlassPtr::add_offset( int offset ) const {
  return make( _ptr, klass(), xadd_offset(offset) );
}

//------------------------------cast_to_ptr_type-------------------------------
const TypePtr*TypeKlassPtr::cast_to_ptr_type(PTR ptr)const{
  if( ptr == _ptr ) return this;
return make(ptr,_klass,_offset,_is_kid);
}
const TypePtr *TypeKlassPtr::meet_with_ptr(const TypePtr *tp) const {
  assert0( !_is_kid );
  return make(ptr_meet[tp->ptr()][_ptr], klass(), meet_offset(tp->offset()));
}


//-----------------------------cast_to_exactness-------------------------------
const TypePtr*TypeKlassPtr::cast_to_exactness(bool klass_is_exact)const{
  if( klass_is_exact == _klass_is_exact ) return this;
return make(klass_is_exact?Constant:NotNull,_klass,_offset,_is_kid);
}

//-----------------------------cast_to_kid-------------------------------------
const TypeKlassPtr *TypeKlassPtr::cast_to_kid(bool is_kid) const {
  if( _is_kid == is_kid ) return this;
  return make(_ptr, _klass, _offset, is_kid);
}

//-----------------------------as_instance_type--------------------------------
// Corresponding type for an instance of the given class.
// It will be NotNull, and exact if and only if the klass type is exact.
const TypeOopPtr* TypeKlassPtr::as_instance_type() const {
  ciKlass* k = klass();
  bool    xk = klass_is_exact();
  //return TypeInstPtr::make(TypePtr::NotNull, k, xk, NULL, 0);
const TypeOopPtr*toop=TypeOopPtr::make_from_klass_raw(k)->is_oopptr();
  toop = toop->cast_to_ptr_type(TypePtr::NotNull)->is_oopptr();
  return toop->cast_to_exactness(xk)->is_oopptr();
}


//------------------------------xmeet------------------------------------------
// Compute the MEET of two types, return a new Type object.
const Type    *TypeKlassPtr::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is Pointer
  switch (t->base()) {          // switch on original type
  case Bottom:                  // Ye Olde Default
  default:                      // All else is a mistake
    return Type::BOTTOM;
  case Top:
    return this;
  case AryPtr:
  case InstPtr:
    assert0( !_is_kid );
    return TypeOopPtr::make(BotPTR,meet_offset(t->is_ptr()->offset()));
  case RawPtr:
  case AnyPtr:
  case OopPtr:
    assert0( !_is_kid );
    return t->xmeet(this);

  //  
  //             A-top         }
  //           /   |   \       }  Tops
  //       B-top A-any C-top   }
  //          | /  |  \ |      }  Any-nulls
  //       B-any   |   C-any   }
  //          |    |    |
  //       B-con A-con C-con   } constants; not comparable across classes
  //          |    |    |
  //       B-not   |   C-not   }
  //          | \  |  / |      }  not-nulls
  //       B-bot A-not C-bot   }
  //           \   |   /       }  Bottoms
  //             A-bot         }
  //
  
  case KlassPtr: {  // Meet two KlassPtr types
    const TypeKlassPtr *tkls = t->is_klassptr();
    assert0( _is_kid == tkls->_is_kid );
    int  off     = meet_offset(tkls->offset());
    PTR  ptr     = meet_ptr(tkls->ptr());

    // Check for easy case; klasses are equal (and perhaps not loaded!)
    // If we have constants, then we created oops so classes are loaded
    // and we can handle the constants further down.  This case handles
    // not-loaded classes
    if( ptr != Constant && tkls->klass()->equals(klass()) ) {
return make(ptr,klass(),off,_is_kid);
    }

    // Classes require inspection in the Java klass hierarchy.  Must be loaded.
    ciKlass* tkls_klass = tkls->klass();
    ciKlass* this_klass = this->klass();
    assert( tkls_klass->is_loaded(), "This class should have been loaded.");
    assert( this_klass->is_loaded(), "This class should have been loaded.");

    // If 'this' type is above the centerline and is a superclass of the
    // other, we can treat 'this' as having the same type as the other.
    if ((above_centerline(this->ptr())) &&
        tkls_klass->is_subtype_of(this_klass)) {
      this_klass = tkls_klass;
    }
    // If 'tinst' type is above the centerline and is a superclass of the
    // other, we can treat 'tinst' as having the same type as the other.
    if ((above_centerline(tkls->ptr())) &&
        this_klass->is_subtype_of(tkls_klass)) {
      tkls_klass = this_klass;
    }

    // Check for classes now being equal
    if (tkls_klass->equals(this_klass)) {
      // If the klasses are equal, the constants may still differ.  Fall to
      // NotNull if they do (neither constant is NULL; that is a special case
      // handled elsewhere).
      ciObject* o = NULL;             // Assume not constant when done
      ciObject* this_oop = const_oop();
      ciObject* tkls_oop = tkls->const_oop();
      if( ptr == Constant ) {
        if (this_oop != NULL && tkls_oop != NULL &&
            this_oop->equals(tkls_oop) )
          o = this_oop;
        else if (above_centerline(this->ptr()))
          o = tkls_oop;
        else if (above_centerline(tkls->ptr()))
          o = this_oop;
        else
          ptr = NotNull;
      }
return make(ptr,this_klass,off,_is_kid);
    } // Else classes are not equal
               
    // Since klasses are different, we require the LCA in the Java
    // class hierarchy - which means we have to fall to at least NotNull.
    if( ptr == TopPTR || ptr == AnyNull || ptr == Constant )
      ptr = NotNull;
    // Now we find the LCA of Java classes
    ciKlass* k = this_klass->least_common_ancestor(tkls_klass);
return make(ptr,k,off,_is_kid);
  } // End of case KlassPtr

  } // End of switch
  return this;                  // Return the double constant
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type    *TypeKlassPtr::xdual() const {
return new TypeKlassPtr(dual_ptr(),klass(),dual_offset(),_is_kid);
}

//------------------------------dump2------------------------------------------
// Dump Klass Type
void TypeKlassPtr::dump2( Dict & d, uint depth, outputStream *st ) const {
  const char *name = klass()->name()->as_utf8();
  st->print("klass %s", name ? name : "NO_NAME");
  if( _ptr != BotPTR )
    st->print(":%s", ptr_msg[_ptr]);
  if( _klass_is_exact ) st->print(":exact");
if(_is_kid)st->print(":kid");
  if( _offset ) {               // Dump offset, if any
if(_offset==OffsetBot){st->print("+bot");}
else if(_offset==OffsetTop){st->print("+top");}
    else                            { st->print("+%d", _offset); }
  }
  st->print(" *");
}



//=============================================================================
// Convenience common pre-built types.

//------------------------------make-------------------------------------------
const TypeFunc *TypeFunc::make( const TypeTuple *domain, const TypeTuple *range ) {
  return (TypeFunc*)(new TypeFunc(domain,range))->hashcons();
}

//------------------------------make-------------------------------------------
const TypeFunc *TypeFunc::make(ciMethod* method) {
  Compile* C = Compile::current();
  const TypeFunc* tf = C->last_tf(method); // check cache
  if (tf != NULL)  return tf;  // The hit rate here is almost 50%.
  const TypeTuple *domain;
  if (method->flags().is_static()) {
    domain = TypeTuple::make_domain(NULL, method->signature());
  } else {
    domain = TypeTuple::make_domain(method->holder(), method->signature());
  }
  const TypeTuple *range  = TypeTuple::make_range(method->signature());
  tf = TypeFunc::make(domain, range);
  C->set_last_tf(method, tf);  // fill cache
  return tf;
}

//------------------------------meet-------------------------------------------
// Compute the MEET of two types.  It returns a new Type object.
const Type *TypeFunc::xmeet( const Type *t ) const {
  // Perform a fast test for common case; meeting the same types together.
  if( this == t ) return this;  // Meeting same type-rep?

  // Current "this->_base" is Func
  switch (t->base()) {          // switch on original type

  case Bottom:                  // Ye Olde Default
    return t;

  default:                      // All else is a mistake
    return Type::BOTTOM;

  case Top:
    break;
  }
  return this;                  // Return the double constant
}

//------------------------------xdual------------------------------------------
// Dual: compute field-by-field dual
const Type *TypeFunc::xdual() const {
  return this;
}

//------------------------------eq---------------------------------------------
// Structural equality check for Type representations
bool TypeFunc::eq( const Type *t ) const {
  const TypeFunc *a = (const TypeFunc*)t;
  return _domain == a->_domain &&
    _range == a->_range;
}

//------------------------------hash-------------------------------------------
// Type-specific hashing function.
int TypeFunc::hash(void) const {
  return (intptr_t)_domain + (intptr_t)_range;
}

//------------------------------dump2------------------------------------------
// Dump Function Type
void TypeFunc::dump2( Dict &d, uint depth, outputStream *st ) const {
  if( _range->_cnt <= Parms )
    st->print("void");
  else {
    uint i;
    for (i = Parms; i < _range->_cnt-1; i++) {
      _range->field_at(i)->dump2(d,depth,st);
      st->print("/");
    }
    _range->field_at(i)->dump2(d,depth,st);
  }
  st->print(" ");
  st->print("( ");
  if( !depth || d[this] ) {     // Check for recursive dump
    st->print("...)");
    return;
  }
  d.Insert((void*)this,(void*)this);    // Stop recursion
  if (Parms < _domain->_cnt)
    _domain->field_at(Parms)->dump2(d,depth-1,st);
  for (uint i = Parms+1; i < _domain->_cnt; i++) {
    st->print(", ");
    _domain->field_at(i)->dump2(d,depth-1,st);
  }
  st->print(" )");
}

//------------------------------print_flattened--------------------------------
// Print a 'flattened' signature
static const char * const flat_type_msg[Type::lastype] = {
  "bad","control","top","int","long","_", 
  "tuple:", "array:", 
  "ptr", "rawptr", "ptr", "ptr", "ptr", "ptr", 
  "func", "abIO", "return_address", "mem", 
  "float_top", "ftcon:", "flt",
  "double_top", "dblcon:", "dbl",
  "bottom"
};

void TypeFunc::print_flattened() const {
  if( _range->_cnt <= Parms )
C2OUT->print("void");
  else {
    uint i;
    for (i = Parms; i < _range->_cnt-1; i++)
C2OUT->print("%s/",flat_type_msg[_range->field_at(i)->base()]);
C2OUT->print("%s",flat_type_msg[_range->field_at(i)->base()]);
  }
C2OUT->print(" ( ");
  if (Parms < _domain->_cnt)
C2OUT->print("%s",flat_type_msg[_domain->field_at(Parms)->base()]);
  for (uint i = Parms+1; i < _domain->_cnt; i++)
C2OUT->print(", %s",flat_type_msg[_domain->field_at(i)->base()]);
C2OUT->print(" )");
}

//------------------------------singleton--------------------------------------
// TRUE if Type is a singleton type, FALSE otherwise.   Singletons are simple
// constants (Ldi nodes).  Singletons are integer, float or double constants
// or a single symbol.
bool TypeFunc::singleton(void) const {
  return false;                 // Never a singleton
}

bool TypeFunc::empty(void) const {
  return false;                 // Never empty
}

BasicType TypeFunc::return_type() const{
  if (range()->cnt() == TypeFunc::Parms) {
    return T_VOID;
  }
  return range()->field_at(TypeFunc::Parms)->basic_type();
}

