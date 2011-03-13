/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ostream.hpp"
#include "symbolOop.hpp"
#include "utf8.hpp"

#include "allocation.inline.hpp"

bool symbolOopDesc::equals(const char* str, int len) const {
  int l = utf8_length();
  if (l != len) return false;
  while (l-- > 0) {
    if (str[l] != (char) byte_at(l)) 
      return false;
  }
  assert(l == -1, "we should be at the beginning");
  return true;
}

char* symbolOopDesc::as_C_string(char* buf, int size) const {
  if (size > 0) {
    int len = MIN2(size - 1, utf8_length());
    memcpy(buf, base(), len);
    buf[len] = '\0';
  }
  return buf;
}

char* symbolOopDesc::as_C_string() const {
  int len = utf8_length();
  char* str = NEW_RESOURCE_ARRAY(char, len + 1);
  return as_C_string(str, len + 1);
}

char* symbolOopDesc::as_C_string_flexible_buffer(Thread* t,
                                                 char* buf, int size) const {
  char* str;
  int len = utf8_length();
  int buf_len = len + 1;
  if (size < buf_len) {
    str = NEW_RESOURCE_ARRAY(char, buf_len);
  } else {
    str = buf;
  }
  return as_C_string(str, buf_len);
}

void symbolOopDesc::print_symbol_on(outputStream* st) {
  st = st ? st : tty;
  for (int index = 0; index < utf8_length(); index++)
    st->put((char)byte_at(index));
}

jchar* symbolOopDesc::as_unicode(int& length) const {
  symbolOopDesc* this_ptr = (symbolOopDesc*)this;
  length = UTF8::unicode_length((char*)this_ptr->bytes(), utf8_length());
  jchar* result = NEW_RESOURCE_ARRAY(jchar, length);
  if (length > 0) {
    UTF8::convert_to_unicode((char*)this_ptr->bytes(), result, length);
  }
  return result;
}

const char* symbolOopDesc::as_klass_external_name(char* buf, int size) const {
  if (size > 0) {
    char* str    = as_C_string(buf, size);
    int   length = (int)strlen(str);
    // Turn all '/'s into '.'s (also for array klasses)
    for (int index = 0; index < length; index++) {
      if (str[index] == '/') {
        str[index] = '.';
      }
    }
    return str;
  } else {
    return buf;
  }
}

const char* symbolOopDesc::as_klass_external_name() const {
  char* str    = as_C_string();
  int   length = (int)strlen(str);
  // Turn all '/'s into '.'s (also for array klasses)
  for (int index = 0; index < length; index++) {
    if (str[index] == '/') {
      str[index] = '.';
    }
  }
  return str;
}

const char*symbolOopDesc::as_klass_pretty_name()const{
char*buf=as_C_string();

  // Count array dimensions and strip leading '['.
  size_t n = 0;
  while (*buf == '[') {
    ++n;
    ++buf;
  }

  // Replace '/' with '.'.
char*ptr=buf;
  while (*ptr != '\0') {
    if (*ptr == '/') *ptr = '.';
    ++ptr;
  }

  // Demangle array element type names.
  const char *name = NULL;
  if (n > 0) {
    switch (*buf) {
    case 'B': name = "byte";    break;
    case 'C': name = "char";    break;
    case 'D': name = "double";  break;
    case 'F': name = "float";   break;
    case 'I': name = "int";     break;
    case 'J': name = "long";    break;
    case 'S': name = "short";   break;
    case 'Z': name = "boolean"; break;
    case 'L': {
      name = ++buf;
char*ptr=buf;
      while (*ptr != '\0') {
if(*ptr==';'){
          *ptr = '\0';
          break;
        } else {
          ++ptr;
        }
      }
      assert(*ptr != ';', "bad array type signature");
      break;
    }
    default:
assert(false,"bad array type signature");
name=buf;
    }
  } else {
name=buf;
  }

  // Append array dimensions as '[]'.
  size_t len = strlen(name);
  char* str = NEW_RESOURCE_ARRAY(char, len + (2*n) + 1);
  memcpy(str, name, len);
  for (size_t k = 0; k < n; k++) {
    size_t i = len + (2*k);
str[i]='[';
    str[i+1] = ']';
  }
  str[len + (2*n)] = '\0';

  return str;
}
