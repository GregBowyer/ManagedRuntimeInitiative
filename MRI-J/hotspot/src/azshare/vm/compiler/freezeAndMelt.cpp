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

#include "freezeAndMelt.hpp"

#include "dict.hpp"
#include "ostream.hpp"

FreezeAndMelt::FreezeAndMelt(const char* freezeAndMeltInFilename, const char* freezeAndMeltOutFilename) {
  _faminfile = fopen(freezeAndMeltInFilename, "r");
  guarantee(_faminfile, "Could not open freeze and melt 'in' file");
  _famoutfile = fopen(freezeAndMeltOutFilename, "w");
  guarantee(_famoutfile, "Could not open freeze and melt 'out' file");

  int alive = getInt("12345");
  if (alive == 12345) {
tty->print_cr("FAM link up!");
  } else {
tty->print_cr("FAM link dooowwnn..");
    ShouldNotReachHere();
  }

  { char buf[32];
    guarantee(FreezeAndMeltThreadNumber != -1, "You must specify -XX:FreezeAndMeltThreadNumber");
    snprintf(buf, 32, "t %ld", FreezeAndMeltThreadNumber);
getIntFromGDBCmd(buf);
  }

  _thread = getOldPtr("$r29");
    
  _newToOldPtr = new Dict(cmpkey,hashkey);
  _oldToNewPtr = new Dict(cmpkey,hashkey);
}

FreezeAndMelt::~FreezeAndMelt() {
  // TODO: Destroy all strings and the dictionary
fclose(_faminfile);
fclose(_famoutfile);
}

FAMPtr FreezeAndMelt::getOldFromNewPtr(const void* new_addr) const {
if(new_addr==NULL)return NULL;//0x0 maps to 0x0 in the core always
guarantee(_newToOldPtr->contains(new_addr),"Did not find address in new->old pointer dictionary");
  FAMPtr old_addr = (FAMPtr)((*_newToOldPtr)[new_addr]);
  return old_addr;
}

void* FreezeAndMelt::getNewFromOldPtr(FAMPtr old_addr) const {
  if (old_addr==0) return NULL; // 0x0 in the core maps to 0x0 always
  guarantee(_oldToNewPtr->contains((void*)old_addr), "Did not find address in old->new pointer dictionary");
  void* new_addr = (*_oldToNewPtr)[(void*)old_addr];
  return new_addr;
}

void FreezeAndMelt::mapNewToOldPtr(void* new_addr, FAMPtr old_addr) {
  _newToOldPtr->Insert(new_addr, (void*)old_addr);
  _oldToNewPtr->Insert((void*)old_addr, new_addr);
}

int FreezeAndMelt::getIntFromGDBCmd(const char* cmd, ...) {
  va_list args;
va_start(args,cmd);
  int ret = atoi(getStringFromGDBCmd(cmd, args));
  va_end(args);
  return ret;
}

long FreezeAndMelt::getLongFromGDBCmd(const char* cmd, ...) {
  va_list args;
va_start(args,cmd);
  long ret = atol(getStringFromGDBCmd(cmd, args));
  va_end(args);
  return ret;
}

char* FreezeAndMelt::getStringFromGDBCmd(const char* cmd, ...) {
  va_list args;
va_start(args,cmd);
  char* ret = getStringFromGDBCmd(cmd, args);
  va_end(args);
  return ret;
}

// p 12345
char* FreezeAndMelt::getStringFromGDBCmd(const char* cmd, va_list args) {
  char tcmd[512];
  vsnprintf(tcmd, 512, cmd, args);

  return getStringFromGDBHelper(tcmd);
}

void FreezeAndMelt::issueGDBCmdNoRPC(const char* cmd) {
  fputs(cmd, _famoutfile);
  fputc('\n', _famoutfile);
fflush(_famoutfile);
}

char* FreezeAndMelt::getStringFromGDBHelper(const char* cmd) {
  issueGDBCmdNoRPC(cmd); 

  int i=0;
  char* buf = NEW_RESOURCE_ARRAY(char, 512);
  for(; i<50; i++) {
    fgets(buf, 512, _faminfile);
if(strncmp(buf,"(azgdb) ",8)==0){
      break;
    }
tty->print("[FAM GDB Debug] %s",buf);
if(strncmp(buf,"Attempt to extract a component ",31)==0){
      ShouldNotReachHere();
    }
  }
int len=strlen(buf);
  if (buf[len-1] == '\n') {
    buf[len-1] = '\0'; // Chop off \n
  }
  return buf+8; // Skip past '(azgdb) '
}

int FreezeAndMelt::getInt(const char* data, ...) {
  va_list args;
va_start(args,data);
  int ret = atoi(getNumber(data, args));
  va_end(args);
  return ret;
}

long FreezeAndMelt::getLong(const char* data, ...) {
  va_list args;
va_start(args,data);
  long ret = atol(getNumber(data, args));
  va_end(args);
  return ret;
}

FAMPtr FreezeAndMelt::getOldPtr(const char* data, ...) {
  va_list args;
va_start(args,data);
  FAMPtr ret = (FAMPtr)atol(getNumber(data, args));
  va_end(args);
  return ret;
}

u_char* FreezeAndMelt::getUCharArray(const char* data, ...) {
  va_list args;
va_start(args,data);
char buf[512],buf2[512];
  vsnprintf(buf, 512, data, args);
  va_end(args);

  snprintf(buf2, 512, "printf \"%%d\\n\", %s", buf);
  if (atol(getStringFromGDBHelper(buf2)) == 0) {
    return NULL;
  }
  snprintf(buf2, 512, "printf \"%%s\\n\", %s", buf);
  return (u_char*)getStringFromGDBHelper(buf2);
}

char* FreezeAndMelt::getString(const char* data, ...) {
  va_list args;
va_start(args,data);
char buf[512],buf2[512];
  vsnprintf(buf, 512, data, args);
  va_end(args);

  snprintf(buf2, 512, "printf \"%%s\\n\", %s", buf);
  char* str = getStringFromGDBHelper(buf2);
  if (strcmp(str,"")==0) {
    snprintf(buf2, 512, "printf \"%%d\\n\", %s", buf);
    if (atol(getStringFromGDBHelper(buf2)) == 0) {
      return NULL;
    }
  }
  return str;
}

char* FreezeAndMelt::getNumber(const char* data, va_list args) {
  char buf[512];
  vsnprintf(buf, 512, data, args);

  char* cmd = NEW_RESOURCE_ARRAY(char, 512);
  snprintf(cmd, 512, "printf \"%%lld\\n\", %s", buf);
  return getStringFromGDBHelper(cmd);
}
