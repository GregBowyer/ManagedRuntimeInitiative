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

#ifndef FREEZEANDMELT_HPP
#define FREEZEANDMELT_HPP

#include "allocation.hpp"

typedef uintptr_t FAMPtr;

class Dict;

class FreezeAndMelt:public CHeapObj{
 public:
  FreezeAndMelt(const char* freezeAndMeltInFilename, const char* freezeAndMeltOutFilename);
  ~FreezeAndMelt();

  int getInt(const char* data, ...);
  long getLong(const char* data, ...);
  FAMPtr getOldPtr(const char* data, ...);
  char* getString(const char* data, ...);
  u_char* getUCharArray(const char* data, ...);

  int getIntFromGDBCmd(const char* cmd, ...);
  long getLongFromGDBCmd(const char* cmd, ...);
  char* getStringFromGDBCmd(const char* cmd, ...);

  // New <-> Old pointer conversion
  FAMPtr getOldFromNewPtr(const void* new_addr) const;
  void*  getNewFromOldPtr(FAMPtr old_addr) const;
  void mapNewToOldPtr(void* new_addr, FAMPtr old_addr);

  void issueGDBCmdNoRPC(const char* cmd);

 private:
FILE*_faminfile;
FILE*_famoutfile;

  Dict* _newToOldPtr;
  Dict* _oldToNewPtr;

  char* getNumber(const char* data, va_list args);
  char* getStringFromGDBCmd(const char* cmd, va_list args);
  char* getStringFromGDBHelper(const char* cmd);

 private:
  FAMPtr _thread;

 public:
  FAMPtr thread() const { return _thread; }
};

#endif // FREEZEANDMELT_HPP
