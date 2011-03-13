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
#ifndef GLOBALDEFINITIONS_HPP
#define GLOBALDEFINITIONS_HPP


// This file holds all globally used constants & types, class (forward)
// declarations and a few frequently used utility functions.

//----------------------------------------------------------------------------------------------------
// Constants

const int LogBytesPerShort   = 1;
const int LogBytesPerInt     = 2;
const int LogBytesPerWord    = 3;
const int LogBytesPerLong    = 3;

const int BytesPerShort      = 1 << LogBytesPerShort;
const int BytesPerInt        = 1 << LogBytesPerInt;
const int BytesPerWord       = 1 << LogBytesPerWord;
const int BytesPerLong       = 1 << LogBytesPerLong;

const int LogBitsPerByte     = 3;
const int LogBitsPerShort    = LogBitsPerByte + LogBytesPerShort;
const int LogBitsPerInt      = LogBitsPerByte + LogBytesPerInt;
const int LogBitsPerWord     = LogBitsPerByte + LogBytesPerWord;
const int LogBitsPerLong     = LogBitsPerByte + LogBytesPerLong;

const int BitsPerByte        = 1 << LogBitsPerByte;
const int BitsPerShort       = 1 << LogBitsPerShort;
const int BitsPerInt         = 1 << LogBitsPerInt;
const int BitsPerWord        = 1 << LogBitsPerWord;
const int BitsPerLong        = 1 << LogBitsPerLong;

const int WordAlignmentMask  = (1 << LogBytesPerWord) - 1;
const int LongAlignmentMask  = (1 << LogBytesPerLong) - 1;

const int WordsPerLong       = 2;	// Number of stack entries for longs


// Includes are here, instead of the top, because the OS specific includes rely on some of 
// the constants defined above.
#include "globalDefinitions_os.hpp"
#include "macros.hpp"


const int oopSize            = sizeof(char*);
const int wordSize           = sizeof(char*); 
const int longSize           = sizeof(jlong);
const int jintSize           = sizeof(jint);
const int size_tSize         = sizeof(size_t);

// Size of a char[] needed to represent a jint as a string in decimal.
const int jintAsStringSize = 12;

const int LogBytesPerOop     = LogBytesPerWord;
const int LogBitsPerOop      = LogBitsPerWord;
const int BytesPerOop        = 1 << LogBytesPerOop;
const int BitsPerOop         = 1 << LogBitsPerOop;
 
const int BitsPerJavaInteger = 32;
const int BitsPerSize_t      = size_tSize * BitsPerByte;


// In fact this should be 
// log2_intptr(sizeof(class JavaThread)) - log2_intptr(64);
// see os::set_memory_serialize_page()
const int SerializePageShiftCount = 4;

// An opaque struct of heap-word width, so that HeapWord* can be a generic
// pointer into the heap.  We require that object sizes be measured in
// units of heap words, so that that 
//   HeapWord* hw;
//   hw += oop(hw)->foo();
// works, where foo is a method (like size or scavenge) that returns the
// object size.
class HeapWord {
private:
  char* i;
};

// HeapWordSize must be 2^LogHeapWordSize.
const int HeapWordSize     = sizeof(HeapWord);
const int LogHeapWordSize  = 3;
const int HeapWordsPerOop  = oopSize      / HeapWordSize;
const int HeapWordsPerLong = BytesPerLong / HeapWordSize;

// The larger HeapWordSize for 64bit requires larger heaps
// for the same application running in 64bit.  See bug 4967770.
// The minimum alignment to a heap word size is done.  Other
// parts of the memory system may required additional alignment
// and are responsible for those alignments.

#define ScaleForWordSize(x) align_size_down_((x) * 13 / 10, HeapWordSize)

// The minimum number of native machine words necessary to contain "byte_size"
// bytes.
inline size_t heap_word_size(size_t byte_size) {
  return (byte_size + (HeapWordSize-1)) >> LogHeapWordSize;
}


const size_t K                  = 1024;
const size_t M                  = K*K;
const size_t G                  = M*K;
const size_t HWperKB            = K / sizeof(HeapWord);

const jint min_jint = (jint)1 << (sizeof(jint)*BitsPerByte-1); // 0x80000000 == smallest jint
const jint max_jint = (juint)min_jint - 1;                     // 0x7FFFFFFF == largest jint

// Constants for converting from a base unit to milli-base units.  For
// example from seconds to milliseconds and microseconds

const int MILLIUNITS	= 1000;		// milli units per base unit
const int MICROUNITS	= 1000000;	// micro units per base unit
const int NANOUNITS	= 1000000000;	// nano units per base unit

inline const char* proper_unit_for_byte_size(size_t s) {
  if (s >= 10*M) {
    return "M";
  } else if (s >= 10*K) {
    return "K";
  } else {
    return "B";
  }
}

inline size_t byte_size_in_proper_unit(size_t s) {
  if (s >= 10*M) {
    return s/M;
  } else if (s >= 10*K) {
    return s/K;
  } else {
    return s;
  }
}


//----------------------------------------------------------------------------------------------------
// VM type definitions

// intx and uintx are the 'extended' int and 'extended' unsigned int types;
// they are 32bit wide on a 32-bit platform, and 64bit wide on a 64bit platform.

typedef intptr_t  intx;
typedef uintptr_t uintx;

const intx  min_intx  = (intx)1 << (sizeof(intx)*BitsPerByte-1);
const intx  max_intx  = (uintx)min_intx - 1;
const uintx max_uintx = (uintx)-1;

// Table of values:
// 	sizeof intx	    4		    8
// min_intx		0x80000000	0x8000000000000000
// max_intx		0x7FFFFFFF	0x7FFFFFFFFFFFFFFF
// max_uintx		0xFFFFFFFF	0xFFFFFFFFFFFFFFFF

typedef unsigned int uint;   NEEDS_CLEANUP


//----------------------------------------------------------------------------------------------------
// Java type definitions

// All kinds of 'plain' byte addresses
typedef   signed char s_char;
typedef unsigned char u_char;
typedef u_char*       address;
typedef uintptr_t     address_word; // unsigned integer which will hold a pointer
				    // except for some implementations of a C++
				    // linkage pointer to function. Should never
				    // need one of those to be placed in this
				    // type anyway.

//  Utility functions to "portably" (?) bit twiddle pointers
//  Where portable means keep ANSI C++ compilers quiet

inline address       set_address_bits(address x, int m)       { return address(intptr_t(x) | m); }
inline address       clear_address_bits(address x, int m)     { return address(intptr_t(x) & ~m); }

//  Utility functions to "portably" make cast to/from function pointers.

inline address_word  mask_address_bits(address x, int m)      { return address_word(x) & m; }
inline address_word  castable_address(address x)              { return address_word(x) ; }
inline address_word  castable_address(void* x)                { return address_word(x) ; }

// Pointer subtraction.
// The idea here is to avoid ptrdiff_t, which is signed and so doesn't have 
// the range we might need to find differences from one end of the heap 
// to the other.
// A typical use might be:
//     if (pointer_delta(end(), top()) >= size) {
//       // enough room for an object of size
//       ...
// and then additions like 
//       ... top() + size ...
// are safe because we know that top() is at least size below end().
inline size_t pointer_delta(const void* left,
			    const void* right,
			    size_t element_size) {
  return (((uintptr_t) left) - ((uintptr_t) right)) / element_size;
}
// A version specialized for HeapWord*'s.
inline size_t pointer_delta(const HeapWord* left, const HeapWord* right) {
  return pointer_delta(left, right, sizeof(HeapWord));
}

//
// ANSI C++ does not allow casting from one pointer type to a function pointer
// directly without at best a warning. This macro accomplishes it silently
// In every case that is present at this point the value be cast is a pointer
// to a C linkage function. In somecase the type used for the cast reflects
// that linkage and a picky compiler would not complain. In other cases because
// there is no convenient place to place a typedef with extern C linkage (i.e
// a platform dependent header file) it doesn't. At this point no compiler seems
// picky enough to catch these instances (which are few). It is possible that
// using templates could fix these for all cases. This use of templates is likely
// so far from the middle of the road that it is likely to be problematic in
// many C++ compilers.
//
#define CAST_TO_FN_PTR(func_type, value) ((func_type)(castable_address(value)))
#define CAST_FROM_FN_PTR(new_type, func_ptr) ((new_type)((address_word)(func_ptr)))

// Unsigned byte types for os and stream.hpp

// Unsigned one, two, four and eigth byte quantities used for describing
// the .class file format. See JVM book chapter 4.

typedef jubyte  u1;
typedef jushort u2;
typedef juint   u4;
typedef julong  u8;

const jubyte  max_jubyte  = (jubyte)-1;  // 0xFF       largest jubyte
const jushort max_jushort = (jushort)-1; // 0xFFFF     largest jushort
const juint   max_juint   = (juint)-1;   // 0xFFFFFFFF largest juint
const julong  max_julong  = (julong)-1;  // 0xFF....FF largest julong

#undef bool
#undef true
#undef false
//----------------------------------------------------------------------------------------------------
// JVM spec restrictions

const int max_method_code_size = 64*K - 1;  // JVM spec, 2nd ed. section 4.8.1 (p.134)


//----------------------------------------------------------------------------------------------------
// Object alignment, in units of HeapWords.
//
// Minimum is max(BytesPerLong, BytesPerDouble, BytesPerOop) / HeapWordSize, so jlong, jdouble and
// reference fields can be naturally aligned.

const int MinObjAlignment            = HeapWordsPerLong;
const int MinObjAlignmentInBytes     = MinObjAlignment * HeapWordSize;
const int MinObjAlignmentInBytesMask = MinObjAlignmentInBytes - 1;

//----------------------------------------------------------------------------------------------------
// Machine dependent stuff

#include "globalDefinitions_pd.hpp"

// Stride for prefetch and size allowance for running off the end of the TLAB.
const size_t HeapWordsPerCacheLine = BytesPerCacheLine / sizeof(HeapWord);


// The byte alignment to be used by Arena::Amalloc.  See bugid 4169348.
// Note: this value must be a power of 2

#define ARENA_AMALLOC_ALIGNMENT (BytesPerWord)

// Signed variants of alignment helpers.  There are two versions of each, a macro
// for use in places like enum definitions that require compile-time constant
// expressions and a function for all other places so as to get type checking.

#define align_size_up_(size, alignment) (((size) + ((alignment) - 1)) & ~((alignment) - 1))

inline intptr_t align_size_up(intptr_t size, intptr_t alignment) { 
  return align_size_up_(size, alignment);
}

#define align_size_down_(size, alignment) ((size) & ~((alignment) - 1))

inline intptr_t align_size_down(intptr_t size, intptr_t alignment) { 
  return align_size_down_(size, alignment);
}

// Align objects by rounding up their size, in HeapWord units.

#define align_object_size_(size) align_size_up_(size, MinObjAlignment)

inline intptr_t align_object_size(intptr_t size) { 
  return align_size_up(size, MinObjAlignment);
}

// Pad out certain offsets to jlong alignment, in HeapWord units.

#define align_object_offset_(offset) align_size_up_(offset, HeapWordsPerLong)

inline intptr_t align_object_offset(intptr_t offset) {
  return align_size_up(offset, HeapWordsPerLong);
}

inline bool is_object_aligned(intptr_t offset) {
  return offset == align_object_offset(offset);
}


//----------------------------------------------------------------------------------------------------
// Utility macros for compilers
// used to silence compiler warnings

#define Unused_Variable(var) var

//----------------------------------------------------------------------------------------------------
// Inlining failure messages
enum InliningFailureID {
  IF_NULL=0,
  IF_NOFAILURE,
  IF_INLINEDINTRINSIC,
  IF_GENERALFAILURE,
  IF_POLYMORPHIC,
  IF_POLYMORPHICNOWINNER,
  IF_INLINEHOT,
  IF_TOOCOLDTOINLINE,
  IF_FAILEDINITIALCHECKS,
  IF_SIZEGTDESIREDMETHODLIMIT,
  IF_NODECOUNTINLININGCUTOFF,
  IF_NOTANACCESSOR,
  IF_INLININGTOODEEP,
  IF_RECURSIVELYINLININGTOODEEP,
  IF_ALREADYCOMPILEDMEDIUMMETHOD,
  IF_ALREADYCOMPILEDBIGMETHOD,
  IF_HOTMETHODTOOBIG,
  IF_TOOBIG,
  IF_ABSTRACTMETHOD,
  IF_METHODHOLDERNOTINIT,
  IF_NATIVEMETHOD,
  IF_EXCEPTIONMETHOD,
  IF_NEVEREXECUTED,
  IF_EXECUTEDLTMININLININGTHRESHOLD,
  IF_UNLOADEDSIGNATURECLASS,
  IF_COMPILERORACLEREQUEST,

  IF_NUMMSGS
};

extern const char* InliningFailureID2Name[IF_NUMMSGS];

//----------------------------------------------------------------------------------------------------
// Miscellaneous

// 6302670 Eliminate Hotspot __fabsf dependency
// All fabs() callers should call this function instead, which will implicitly
// convert the operand to double, avoiding a dependency on __fabsf which
// doesn't exist in early versions of Solaris 8.
inline double fabsd(double value) {
  return fabs(value);
}

inline jint low (jlong value)                    { return jint(value); }
inline jint high(jlong value)                    { return jint(value >> 32); }

// the fancy casts are a hopefully portable way 
// to do unsigned 32 to 64 bit type conversion
inline void set_low (jlong* value, jint low )    { *value &= (jlong)0xffffffff << 32;
                                                   *value |= (jlong)(julong)(juint)low; }

inline void set_high(jlong* value, jint high)    { *value &= (jlong)(julong)(juint)0xffffffff;
                                                   *value |= (jlong)high       << 32; }

inline jlong jlong_from(jint h, jint l) {
  jlong result = 0; // initialization to avoid warning
  set_high(&result, h);
  set_low(&result,  l);
  return result;
}

union jlong_accessor {
  jint  words[2];
  jlong long_value;
};

void check_basic_types(); // cannot define here; uses assert


// NOTE: replicated in SA in vm/agent/sun/jvm/hotspot/runtime/BasicType.java
enum BasicType {
  T_BOOLEAN  =  4,
  T_CHAR     =  5,
  T_FLOAT    =  6,
  T_DOUBLE   =  7,
  T_BYTE     =  8,
  T_SHORT    =  9,
  T_INT      = 10,
  T_LONG     = 11,
  T_OBJECT   = 12,
  T_ARRAY    = 13,
  T_VOID     = 14,
  T_ADDRESS  = 15,
  T_CONFLICT = 16, // for stack value type with conflicting contents
  T_ILLEGAL  = 99
};

// Convert a char from a classfile signature to a BasicType
inline BasicType char2type(char c) {
  switch( c ) {
  case 'B': return T_BYTE;    
  case 'C': return T_CHAR;    
  case 'D': return T_DOUBLE;  
  case 'F': return T_FLOAT;   
  case 'I': return T_INT;     
  case 'J': return T_LONG;    
  case 'S': return T_SHORT;   
  case 'Z': return T_BOOLEAN; 
  case 'V': return T_VOID;    
  case 'L': return T_OBJECT;  
  case '[': return T_ARRAY;   
  }
  return T_ILLEGAL;
}

extern char type2char_tab[T_CONFLICT+1];     // Map a BasicType to a jchar
inline char type2char(BasicType t) { return (uint)t < T_CONFLICT+1 ? type2char_tab[t] : 0; }
extern int type2size[T_CONFLICT+1];	    // Map BasicType to result stack elements
extern const char* type2name_tab[T_CONFLICT+1];     // Map a BasicType to a jchar
inline const char* type2name(BasicType t) { return (uint)t < T_CONFLICT+1 ? type2name_tab[t] : NULL; }
extern BasicType name2type(const char* name);

// Auxilary math routines
// least common multiple
extern size_t lcm(size_t a, size_t b);


// NOTE: replicated in SA in vm/agent/sun/jvm/hotspot/runtime/BasicType.java
enum BasicTypeSize {
  T_BOOLEAN_size = 1,
  T_CHAR_size    = 1,
  T_FLOAT_size   = 1,
  T_DOUBLE_size  = 2,
  T_BYTE_size    = 1,
  T_SHORT_size   = 1,
  T_INT_size     = 1,
  T_LONG_size    = 2,
  T_OBJECT_size  = 1,
  T_ARRAY_size   = 1,
  T_VOID_size    = 0
};


// maps a BasicType to its instance field storage type:
// all sub-word integral types are widened to T_INT
extern BasicType type2field[T_CONFLICT+1];
extern BasicType type2wfield[T_CONFLICT+1];


// size in bytes
enum ArrayElementSize {
  T_BOOLEAN_aelem_bytes = 1,
  T_CHAR_aelem_bytes    = 2,
  T_FLOAT_aelem_bytes   = 4,
  T_DOUBLE_aelem_bytes  = 8,
  T_BYTE_aelem_bytes    = 1,
  T_SHORT_aelem_bytes   = 2,
  T_INT_aelem_bytes     = 4,
  T_LONG_aelem_bytes    = 8,
  T_OBJECT_aelem_bytes  = 8,
  T_ARRAY_aelem_bytes   = 8,
  T_VOID_aelem_bytes    = 0
};

extern int type2aelembytes[T_CONFLICT+1]; // maps a BasicType to nof bytes used by its array element

extern int64_t type2logaelembytes[T_CONFLICT+1]; // maps a BasicType to log 2 nof bytes used by its array element

// JavaValue serves as a container for arbitrary Java values.

class JavaValue {

 public:
  typedef union JavaCallValue {
    jfloat   f;
    jdouble  d;
    jint     i;
    jlong    l;
    jobject  h;
  } JavaCallValue;
 
 private:
  BasicType _type;
  JavaCallValue _value;
 
 public:
  JavaValue(BasicType t = T_ILLEGAL) { _type = t; }

  JavaValue(jfloat value) {
    _type    = T_FLOAT;
    _value.f = value;
  }

  JavaValue(jdouble value) {
    _type    = T_DOUBLE;
    _value.d = value;
  }

 jfloat get_jfloat() const { return _value.f; }
 jdouble get_jdouble() const { return _value.d; }
 jint get_jint() const { return _value.i; }
 jlong get_jlong() const { return _value.l; }
 jobject get_jobject() const { return _value.h; }
 JavaCallValue* get_value_addr() { return &_value; }
 BasicType get_type() const { return _type; }
 
 void set_jfloat(jfloat f) { _value.f = f;}
 void set_jdouble(jdouble d) { _value.d = d;}
 void set_jint(jint i) { _value.i = i;}
 void set_jlong(jlong l) { _value.l = l;}
 void set_jobject(jobject h) { _value.h = h;}
 void set_type(BasicType t) { _type = t; }
 
 jboolean get_jboolean() const { return (jboolean) (_value.i);}
 jbyte get_jbyte() const { return (jbyte) (_value.i);}
 jchar get_jchar() const { return (jchar) (_value.i);}
 jshort get_jshort() const { return (jshort) (_value.i);}

};


#define STACK_BIAS	0
// V9 Sparc CPU's running in 64 Bit mode use a stack bias of 7ff
// in order to extend the reach of the stack pointer.
#if defined(SPARC)
#undef STACK_BIAS
#define STACK_BIAS	0x7ff
#endif


// TosState describes the top-of-stack state before and after the execution of
// a bytecode or method. The top-of-stack value may be cached in one or more CPU
// registers. The TosState corresponds to the 'machine represention' of this cached
// value. There's 4 states corresponding to the JAVA types int, long, float & double
// as well as a 5th state in case the top-of-stack value is actually on the top
// of stack (in memory) and thus not cached. The atos state corresponds to the itos
// state when it comes to machine representation but is used separately for (oop)
// type specific operations (e.g. verification code).

enum TosState {         // describes the tos cache contents
#if defined(AZUL)
  tos = 0,
  btos = 0, ctos = 0, stos = 0, itos = 0,
  ltos = 0, ftos = 0, dtos = 0, atos = 0,
  vtos = 0,
#else // defined(AZUL)
  btos = 0, 		// byte, bool tos cached
  ctos = 1,		// short, char tos cached
  stos = 2,		// short, char tos cached
  itos = 3,             // int tos cached
  ltos = 4,             // long tos cached
  ftos = 5,             // float tos cached
  dtos = 6,             // double tos cached
  atos = 7, 		// object cached
  vtos = 8,             // tos not cached
#endif // !defined(AZUL)
  number_of_states,
  ilgl                  // illegal state: should not occur
};

// NEEDS_CLEANUP
inline TosState as_TosState(BasicType type) {
  switch (type) {
    case T_BYTE   : return btos;
    case T_BOOLEAN: return btos;
    case T_CHAR   : return ctos;
    case T_SHORT  : return stos;
    case T_INT    : return itos;
    case T_LONG   : return ltos;
    case T_FLOAT  : return ftos;
    case T_DOUBLE : return dtos;
    case T_VOID   : return vtos;
    case T_ARRAY  : // fall through
    case T_OBJECT : return atos;
    default       : break;
  }
  return ilgl;
}


// Helper function to convert BasicType info into TosState
// Note: Cannot define here as it uses global constant at the time being.
TosState as_TosState(BasicType type);

// ReferenceType is used to distinguish between java/lang/ref/Reference subclasses
enum ReferenceType {
REF_SOFT=0,//Subclass of java/lang/ref/SoftReference
REF_WEAK=1,//Subclass of java/lang/ref/WeakReference
REF_FINAL=2,//Subclass of java/lang/ref/FinalReference
  REF_PHANTOM = 3,    // Subclass of java/lang/ref/PhantomReference
  REF_OTHER   = 4,    // Subclass of java/lang/ref/Reference, but not subclass of one of the classes above
  REF_NONE    = 5,    // Regular class
};

const int SubclassesOfJavaLangRefReference = 4;


// This is the initial Bci
const int InvocationEntryBci = -2;

// Enumeration to distinguish tiers of compilation
enum CompLevel {
  CompLevel_none              = 0,
  CompLevel_fast_compile      = 1,
  CompLevel_full_optimization = 2,

  CompLevel_highest_tier      = CompLevel_full_optimization,
  CompLevel_initial_compile   = CompLevel_full_optimization
};

// Basic support for errors (general debug facilities not defined at this point fo the include phase)

extern void basic_fatal(const char* msg);


//----------------------------------------------------------------------------------------------------
// Special constants for debugging

const jint     badInt           = -3;                       // generic "bad int" value
const long     badAddressVal    = -2;                       // generic "bad address" value
const long     badOopVal        = -1;                       // generic "bad oop" value
const intptr_t badHeapOopVal    = (intptr_t) CONST64(0x2BAD4B0BBAADBABE); // value used to zap heap after GC
const int      badHandleValue   = 0xBC;                     // value used to zap vm handle area
const int      badResourceValue = 0xAB;                     // value used to zap resource area
const int      badHeapValue     = 0x51;                     // value used to zap c-heap area
const int      freeBlockPad     = 0xBA;                     // value used to pad freed blocks.
const int      uninitBlockPad   = 0xF1;                     // value used to zap newly malloc'd blocks.
const uint64_t badJNIHandleVal=(uint64_t)CONST64(0xFEFEFEFEFEFEFEFA);//value used to zap jni handle area
const julong badHeapWordVal=CONST64(0xBAADBABEBAADBABE);//value used to zap heap after GC
const int      badCodeHeapNewVal= 0xCC;                     // value used to zap Code heap at allocation
const int      badCodeHeapFreeVal = 0xDD;                   // value used to zap Code heap at deallocation


// (These must be implemented as #defines because C++ compilers are
// not obligated to inline non-integral constants!)
#define       badAddress        ((address)::badAddressVal)
#define       badOop            ((oop)::badOopVal)
#define       badHeapWord       (::badHeapWordVal)
#define       deadHeapWord      (::deadHeapWordVal)
#define       badJNIHandle      ((oop)::badJNIHandleVal)
#define       PSLabFillWord     (::PSLabFillWordVal)
#define       PSResizeFillWord  (::PSResizeFillWordVal)


//----------------------------------------------------------------------------------------------------
// Utility functions for bitfield manipulations

const intptr_t AllBits    = ~0; // all bits set in a word
const intptr_t NoBits     =  0; // no bits set in a word
const jlong    NoLongBits =  0; // no bits set in a long
const intptr_t OneBit     =  1; // only right_most bit set in a word

// get a word with the n.th or the right-most or left-most n bits set
// (note: #define used only so that they can be used in enum constant definitions)
#define nth_bit(n)        (n >= BitsPerWord ? 0 : OneBit << (n))
#define right_n_bits(n)   (nth_bit(n) - 1)
#define left_n_bits(n)    (right_n_bits(n) << (n >= BitsPerWord ? 0 : (BitsPerWord - n)))

// bit-operations using a mask m
inline void   set_bits    (intptr_t& x, intptr_t m) { x |= m; }
inline void clear_bits    (intptr_t& x, intptr_t m) { x &= ~m; }
inline intptr_t mask_bits      (intptr_t  x, intptr_t m) { return x & m; }
inline jlong    mask_long_bits (jlong     x, jlong    m) { return x & m; }
inline bool mask_bits_are_true (intptr_t flags, intptr_t mask) { return (flags & mask) == mask; }

// bit-operations using the n.th bit
inline void    set_nth_bit(intptr_t& x, int n) { set_bits  (x, nth_bit(n)); }
inline void  clear_nth_bit(intptr_t& x, int n) { clear_bits(x, nth_bit(n)); }
inline bool is_set_nth_bit(intptr_t  x, int n) { return mask_bits (x, nth_bit(n)) != NoBits; }

// returns the bitfield of x starting at start_bit_no with length field_length (no sign-extension!)
inline intptr_t bitfield(intptr_t x, int start_bit_no, int field_length) {
  return mask_bits(x >> start_bit_no, right_n_bits(field_length));
}


//----------------------------------------------------------------------------------------------------
// Utility functions for integers

// Avoid use of global min/max macros which may cause unwanted double
// evaluation of arguments.
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#define max(a,b) Do_not_use_max_use_MAX2_instead
#define min(a,b) Do_not_use_min_use_MIN2_instead

// It is necessary to use templates here. Having normal overloaded
// functions does not work because it is necessary to provide both 32-
// and 64-bit overloaded functions, which does not work, and having
// explicitly-typed versions of these routines (i.e., MAX2I, MAX2L)
// will be even more error-prone than macros.
template<class T> inline T MAX2(T a, T b)           { return (a > b) ? a : b; }
template<class T> inline T MIN2(T a, T b)           { return (a < b) ? a : b; }
template<class T> inline T MAX3(T a, T b, T c)      { return MAX2(MAX2(a, b), c); }
template<class T> inline T MIN3(T a, T b, T c)      { return MIN2(MIN2(a, b), c); }
template<class T> inline T MAX4(T a, T b, T c, T d) { return MAX2(MAX3(a, b, c), d); }
template<class T> inline T MIN4(T a, T b, T c, T d) { return MIN2(MIN3(a, b, c), d); }

template<class T> inline T ABS(T x)                 { return (x > 0) ? x : -x; }

// true if value fits into a uint8
inline bool is_uint8(int64_t value) {
  return (((uint64_t)value << 56) >> 56) == (uint64_t)value;
}

// true if value fits into an int8
inline bool is_int8(int64_t value) {
  return ((value << 56) >> 56) == value;
}

// true if value fits into a uint16
inline bool is_uint16(int64_t value) {
  return (((uint64_t)value << 48) >> 48) == (uint64_t)value;
}

// true if value fits into an int16
inline bool is_int16(int64_t value) {
  return ((value << 48) >> 48) == value;
}

inline bool is_uint32(int64_t value) {
  return (((uint64_t)value << 32) >> 32) == (uint64_t)value;
}

inline bool is_int32(int64_t value) {
  return ((value << 32) >> 32) == value;
}

// true if x is a power of 2, false otherwise
inline bool is_power_of_2(intptr_t x) {
  return ((x != NoBits) && (mask_bits(x, x - 1) == NoBits));
}

// long version of is_power_of_2
inline bool is_power_of_2_long(jlong x) {
  return ((x != NoLongBits) && (mask_long_bits(x, x - 1) == NoLongBits));
}

//* largest i such that 2^i <= x
//  A negative value of 'x' will return '31'
inline int log2_intptr(intptr_t x) {
  int i = -1;
  uintptr_t p =  1;
  while (p != 0 && p <= (uintptr_t)x) {
    // p = 2^(i+1) && p <= x (i.e., 2^(i+1) <= x)
    i++; p *= 2;
  }
  // p = 2^(i+1) && x < p (i.e., 2^i <= x < 2^(i+1))
  // (if p = 0 then overflow occured and i = 31)
  return i;
}

//* largest i such that 2^i <= x
//  A negative value of 'x' will return '63'
inline int log2_long(jlong x) {
  int i = -1;
  julong p =  1;
  while (p != 0 && p <= (julong)x) {
    // p = 2^(i+1) && p <= x (i.e., 2^(i+1) <= x)
    i++; p *= 2;
  }
  // p = 2^(i+1) && x < p (i.e., 2^i <= x < 2^(i+1))
  // (if p = 0 then overflow occured and i = 31)
  return i;
}

//* the argument must be exactly a power of 2
inline int exact_log2(intptr_t x) {
  #ifdef ASSERT
    if (!is_power_of_2(x)) basic_fatal("x must be a power of 2");
  #endif
  return log2_intptr(x);
}


// returns integer round-up to the nearest multiple of s (s must be a power of two)
inline intptr_t round_to(intptr_t x, uintx s) {
  #ifdef ASSERT
    if (!is_power_of_2(s)) basic_fatal("s must be a power of 2");
  #endif
  const uintx m = s - 1;
  return mask_bits(x + m, ~m);
}

// returns integer round-down to the nearest multiple of s (s must be a power of two)
inline intptr_t round_down(intptr_t x, uintx s) {
  #ifdef ASSERT
    if (!is_power_of_2(s)) basic_fatal("s must be a power of 2");
  #endif
  const uintx m = s - 1;
  return mask_bits(x, ~m);
}


inline bool is_odd (intx x) { return x & 1;      }
inline bool is_even(intx x) { return !is_odd(x); }

// "to" should be greater than "from."
inline intx byte_size(void* from, void* to) {
  return (address)to - (address)from;
}

// Portable routines to go the other way:

inline void explode_short_to( u2 x, u1& c1, u1& c2 ) {
  c1 = u1(x >> 8);
  c2 = u1(x);
}

inline void explode_short_to( u2 x, u1* p ) {
  explode_short_to( x, p[0], p[1]);
}

inline void explode_int_to( u4 x, u1& c1, u1& c2, u1& c3, u1& c4 ) {
  c1 = u1(x >> 24);
  c2 = u1(x >> 16);
  c3 = u1(x >>  8);
  c4 = u1(x);
}

inline void explode_int_to( u4 x, u1* p ) {
  explode_int_to( x, p[0], p[1], p[2], p[3]);
}


// Pack and extract shorts to/from ints:

inline int extract_low_short_from_int(jint x) {
  return x & 0xffff;
}

inline int extract_high_short_from_int(jint x) {
  return (x >> 16) & 0xffff;
}

inline int build_int_from_shorts( jushort low, jushort high ) {
  return ((int)((unsigned int)high << 16) | (unsigned int)low);
}


//----------------------------------------------------------------------------------------------------
// Azul: KlassId constants

enum {
  null_kid                   = 0,
  unresolved_kid             = 1,
  // <unused>                = 2-3
  booleanArrayKlass_kid      = 4,  // == T_BOOLEAN
  charArrayKlass_kid         = 5,  // == T_CHAR
  floatArrayKlass_kid        = 6,  // == T_FLOAT
  doubleArrayKlass_kid       = 7,  // == T_DOUBLE
  byteArrayKlass_kid         = 8,  // == T_BYTE
  shortArrayKlass_kid        = 9,  // == T_SHORT
  intArrayKlass_kid          = 10, // == T_INT
  longArrayKlass_kid         = 11, // == T_LONG
  arrayKlass_kid             = 12,
  constMethodKlass_kid       = 13,
  constantPoolKlass_kid      = 14,
  constantPoolCacheKlass_kid = 15,
  instanceKlass_kid          = 16,
  klassKlass_kid             = 17,
  methodKlass_kid            = 18,
  objArrayKlass_kid          = 19,
  symbolKlass_kid            = 20,
  typeArrayKlass_kid         = 21,
  codeKlass_kid              = 22,
  methodCodeKlass_kid        = 23,
  dependencyKlass_kid        = 24,
  // <unused>                = 25-31
  max_reserved_kid           = 31,
  systemObjArrayKlass_kid    = 32,
  java_lang_Object_kid       = 33,
  // <java classes>          = 33-524287
  max_kid                    = 0x7FFFF
};


// Printf-style formatters for fixed- and variable-width types as pointers and
// integers.
// 
// Format 32-bit quantities.
#define INT32_FORMAT  "%d"
#define UINT32_FORMAT "%u"
#define INT32_FORMAT_W(width)	"%" #width "d"
#define UINT32_FORMAT_W(width)	"%" #width "u"

#define PTR32_FORMAT  "0x%08x"

// Format 64-bit quantities.
#define INT64_FORMAT "%ld"
#define UINT64_FORMAT "%lu"
#define PTR64_FORMAT "%p"

#define INT64_FORMAT_W(width)  "%" #width FORMAT64_MODIFIER "d"
#define UINT64_FORMAT_W(width) "%" #width FORMAT64_MODIFIER "u"

// Format macros that allow the field width to be specified.  The width must be
// a string literal (e.g., "8") or a macro that evaluates to one.

#define SSIZE_FORMAT_W(width)	INT64_FORMAT_W(width)
#define SIZE_FORMAT_W(width)	UINT64_FORMAT_W(width)

// Format pointers and size_t (or size_t-like integer types) which change size
// between 32- and 64-bit.
#define PTR_FORMAT    PTR64_FORMAT
#define UINTX_FORMAT  UINT64_FORMAT
#define INTX_FORMAT   INT64_FORMAT
#define SIZE_FORMAT   UINT64_FORMAT
#define SSIZE_FORMAT  INT64_FORMAT

#define INTPTR_FORMAT "0x%016lx"

#define ARRAY_SIZE(array) (sizeof(array)/sizeof((array)[0]))

// Logging
//
// Default tag for an individual logging construct.
#define NOTAG (__FILE__ ":" XSTR(__LINE__))

#endif // GLOBALDEFINITIONS_HPP
