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
#ifndef INSTRUCTIONTRACERECORDING_HPP
#define INSTRUCTIONTRACERECORDING_HPP


#include "constants_pd.hpp"
//#include <zlib/zlib.h>
class Assembler;

class InstructionTraceArray {
 public:
  enum Defs {
    _traceSizeInWords=2,  // Must be a power of 2
    _traceSizeInWords_lg=1
  };

  InstructionTraceArray(address base);
  InstructionTraceArray();
  ~InstructionTraceArray();

  int getNumTraces() const    { return _numTraces; }
  long* getTraces() const     { return _traces; }
  // getTracesEnd gives you the address of the **last trace in the array**
  long* getTracesEnd() const  { return _traces+(_numTraces-1)*_traceSizeInWords; }

  void clear();

  void setAssociatedJavaThreadID(int64_t id) { _javaThreadID = id; }
  int64_t getAssociatedJavaThreadID() const { return _javaThreadID; }

  // Must copy in case the thread disappears
  void setAssociatedJavaName(const char* threadName);
  const char* getAssociatedJavaName() const { return _javaName; }

 private:
  int _numTraces;
  long* _traces;
  long* _tracesOrig;

  int64_t _javaThreadID;
  enum Constants { 
    _maxSizeJavaName=256
  };
  char* _javaName;

 // For heuristic use (not exact, not thread safe)
 public:
  
  static intptr_t getNumTraceArrays() { return _numTraceArrays; }

 private:
  static intptr_t _numTraceArrays;
  
 // Linked list functionality
 public:
InstructionTraceArray*next()const{return _next;}
void setNext(InstructionTraceArray*next){_next=next;}

 private:
InstructionTraceArray*_next;
};

class InstructionTraceManager {
 public:
  static void addEmptyTrace(InstructionTraceArray *emptyArray);
  static InstructionTraceArray* getEmptyTrace();

  static void addFullTrace(InstructionTraceArray *fullArray);
  static InstructionTraceArray* getFullTrace();

 private:
  static InstructionTraceArray *_emptyHead;
  static InstructionTraceArray *_fullHead;

 public:
  // Currently there is room for 32 types
  enum Type {
    NULLTYPE=-1,
    LD1 = 0, LD2, LD4, LD8,
    LDU1, LDU2, LDU4, LDC8,
    ST1, ST2, ST4, ST8, STA,
    CAS8, LVB, 
    LADD1, LADD2, LADD4, LADD8,
    FENCE,
    PREF_CLZ, PREF_OTHER,
    SVB,
    ANNOTATION,
    MAXVALIDTYPE=ANNOTATION,
    UNK=30,
    UNK2=31,
    NUMTYPES
  };

  enum FenceType {
    FENCE_LDLD=1, 
    FENCE_LDST=2, 
    FENCE_STLD=4, 
    FENCE_STST=8, 
    FENCE_CLZ=16, 
    FENCE_RSTACK=32,
    MAXFENCE=63
  };

 public:
  static void addIDToThreadName(long id, const char* name);

 private:
  // For recording ID->threadName mapping
  static int IDToThreadNameFD;
   
};

class InstructionTraceRecording:AllStatic{
 public:
  static void record(Assembler* _masm);
  static void record(Assembler* _masm, enum InstructionTraceManager::FenceType fenceTypes);
  static void record(Assembler* _masm, Register rs, enum InstructionTraceManager::Type type, Register offset);
  static void record(Assembler* _masm, Register rs, enum InstructionTraceManager::Type type, int imm15);

  static void deactivate();
  static void activate();

  static void annotate(Assembler* _masm, Register reg, enum ITRAnnotationType type);

  static void insertLD8(int64_t addr);
  static void insertST8(int64_t addr);
  static void insertCAS8(int64_t addr);
  static void insertPrefCLZ(int64_t addr);
  static void insertPrefOther(int64_t addr);

  static bool isActive() { Atomic::membar(); return _active; }
  
 private:
  static volatile bool _active;
};

#endif // INSTRUCTIONTRACERECORDING_HPP
