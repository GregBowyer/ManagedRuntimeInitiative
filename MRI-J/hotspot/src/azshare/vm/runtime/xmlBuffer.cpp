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


#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "os_os.inline.hpp"

xmlBuffer::xmlBuffer(azprof::Response *res, ArtaObjectPool *pool, int flags) :
  _response(res), _object_pool(pool), _flags(flags) {}

xmlBuffer::~xmlBuffer() {}

void xmlBuffer::write(const char*str,size_t len){
#ifdef AZ_PROFILER
  // escape special chars
  assert(len == strlen(str), "inconsistent parameters");
  response()->xwrite(str, len);
#endif // AZ_PROFILER
}

void xmlBuffer::print_raw(const char *str, size_t len) {
#ifdef AZ_PROFILER
  int wlen = response()->write(str, len);
  assert0( (size_t)wlen == len );
#endif // AZ_PROFILER
}

void xmlBuffer::print_raw(const char *str) {
size_t len=strlen(str);
print_raw(str,len);
}



// FIXME: these must agree with the SOAP encoding standard

void xmlBuffer::print_jboolean(jboolean v) {
  xmlElement j(this, "jboolean", xmlElement::delayed_LF);
  print(v? "true" : "false");
}

void xmlBuffer::print_jbyte   (jbyte v) {
  xmlElement j(this, "jbyte", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%d",v);
print(buf);
}

void xmlBuffer::print_jint    (jint v) {
  xmlElement j(this, "jint", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%d",v);
print(buf);
}

// prints character number
void xmlBuffer::print_jchar   (jchar v) {
  xmlElement j(this, "jchar", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%d",v);
print(buf);
}

void xmlBuffer::print_jshort  (jshort v) {
  xmlElement j(this, "jshort", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%d",v);
print_cr(buf);
}

void xmlBuffer::print_jlong   (jlong v) {
  xmlElement j(this, "jlong", xmlElement::delayed_LF);
char buf[25];
  snprintf(buf, sizeof(buf), os::jlong_format_specifier(), v);
print(buf);
}

void xmlBuffer::print_jfloat  (jfloat v) {
  xmlElement j(this, "jfloat", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%g",v);
print(buf);
}

void xmlBuffer::print_jdouble (jdouble v) {
  xmlElement j(this, "jdouble", xmlElement::delayed_LF);
char buf[25];
snprintf(buf,sizeof(buf),"%lg",v);
print(buf);
}

// Name/Value pair item:
// <name>value</name>
void xmlBuffer::name_value_item( const char *name, const char *format, ... ) {
  xmlElement xe(this,name, xmlElement::delayed_LF);
  va_list ap;
  va_start(ap, format);
vprint(format,ap);
  va_end(ap);
}

void xmlBuffer::name_value_item( const char *name, intptr_t value ) {
  char buf[100];
  snprintf(buf, sizeof(buf), os::jlong_format_specifier(), value);
  name_value_item(name, buf);
}

void xmlBuffer::name_ptr_item( const char *name, const void *ptr ) {
  char buf[100];
  snprintf(buf, sizeof(buf), PTR_FORMAT, ptr);
  name_value_item(name, buf);
}

// -------------  xmlElement ------------------
xmlElement::xmlElement(xmlBuffer *xb, const char *tagName, LineStyle style)
  : _xb(xb), _style(style)
{
assert(xb,"xmlElement::xmlElement() should only be used with arpm, not tty output.");
  assert(strchr(tagName, ' ') == NULL, "malformed tag");
  _tagName = strdup(tagName); // Make sure we are using a surviving copy in the closing tag.
                              // _tagName free'd in destructor.
  if (_style == delayed_LF) {
_xb->print_raw("  ");
  }
_xb->print_raw("<");
  _xb->print(_tagName);
_xb->print_raw(">");
  if (_style & 1) {
_xb->cr();
  }
}

xmlElement::~xmlElement() {
  if( !_xb ) return;
_xb->print_raw("</");
  _xb->print(_tagName);
free(_tagName);
_tagName=NULL;
_xb->print_raw(">");
  if (_style & 2) {
_xb->cr();
  }
}

