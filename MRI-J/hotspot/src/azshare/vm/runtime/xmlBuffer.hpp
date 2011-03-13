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

#ifndef XMLBUFFER_HPP
#define XMLBUFFER_HPP


#include "ostream.hpp"

// an xmlBuffer is a buffer for data that is marked up in xml.  This
// class facilitates making tags, and also escaping characters like <,
// \, &, etc., which have special meaning to xml

// DO NOT use this class directly to write xml tags; use the
// xmlElement class

// This is needed here to break an include cycle.
class ArtaObjectPool;

class xmlBuffer:public outputStream{

private:
  azprof::Response *_response;
  ArtaObjectPool* _object_pool; // All object IDs should be coming out of this pool.
  int _flags;

public:
  enum Flag {
    DATA_ACCESS = 0x1,
    ADDR_ACCESS = 0x2,
    SYM_ACCESS = 0x4
  };

  xmlBuffer(azprof::Response*, ArtaObjectPool*, int flags);
  ~xmlBuffer();

  azprof::Request* request() {
#ifdef AZ_PROFILER
    return _response->request();
#else // !AZ_PROFILER
    return NULL;
#endif // !AZ_PROFILER
  }
  azprof::Response* response() {
#ifdef AZ_PROFILER
    return _response;
#else // !AZ_PROFILER
    return NULL;
#endif // !AZ_PROFILER
  }
  ArtaObjectPool* object_pool() {return _object_pool;}
  int flags() {return _flags;}
  bool can_show_data() const {return _flags & DATA_ACCESS;}
  bool can_show_addrs() const {return _flags & ADDR_ACCESS;}
  bool can_show_syms() const {return _flags & SYM_ACCESS;}
  void set_non_blocking(bool x) {
#ifdef AZ_PROFILER
    response()->set_non_blocking(x);
#endif // AZ_PROFILER
  }

  // This methods escapes special xml chars.
  // It is invoked from the outputStream based formatters.
virtual void write(const char*str,size_t len);

  // does not escape; used for printing tags, etc.
  void print_raw(const char *str);
  void print_raw(const char *str, size_t len);

  // convenience methods for printing java primitives in the proper
  // encoding
  void print_jboolean(jboolean);
  void print_jbyte   (jbyte);
  void print_jint    (jint);
  void print_jchar   (jchar);
  void print_jshort  (jshort);
  void print_jlong   (jlong);
  void print_jfloat  (jfloat);
  void print_jdouble (jdouble);
  void print_jstring (jstring);
  void print_address (void*);

  // Name/Value pair:
  // <name>value</name>
  void name_value_item(const char *name, const char *format, ...);
  // Same thing for ints
  void name_value_item(const char *name, intptr_t value);
  // <blah> 0x12341234 </blah>
  void name_ptr_item(const char *name, const void *ptr);
};



// This is a convenience class used to print an XML element (a subtree of an
// XML document, beginning with an opening tag and ending with the closing of
// the same tag)
class xmlElement:public ResourceObj{
 public:
  enum LineStyle {
    no_LF = 0,
    delayed_LF = 2,
    standard = 3
  };
 private:
  xmlBuffer  *_xb;
  char *_tagName;
  LineStyle  _style;
 public:
  xmlElement(xmlBuffer *xb, const char *tagName, LineStyle style = standard);
  ~xmlElement();
};

#endif // XMLBUFFER_HPP
