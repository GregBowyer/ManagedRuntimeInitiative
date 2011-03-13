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


#include "collectedHeap.hpp"
#include "mutexLocker.hpp"
#include "objArrayKlassKlass.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "artaObjects.hpp"
#include "sharedRuntime.hpp"
#include "thread.hpp"
#include "typeArrayKlass.hpp"
#include "typeArrayOop.hpp"
#include "utf8.hpp"
#include "vmSymbols.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"
#include "thread_os.inline.hpp"

bool typeArrayKlass::compute_is_subtype_of(klassOop k) {
  if (!k->klass_part()->oop_is_typeArray()) {
    return arrayKlass::compute_is_subtype_of(k);
  }

  typeArrayKlass* tak = typeArrayKlass::cast(k);
  if (dimension() != tak->dimension()) return false;

  return element_type() == tak->element_type();
}

klassOop typeArrayKlass::create_klass(BasicType type, int scale, TRAPS) {
  typeArrayKlass o;

  symbolHandle sym(symbolOop(NULL));
  // bootstrapping: don't create sym if symbolKlass not created yet
  if (Universe::symbolKlassObj() != NULL) {
    sym = oopFactory::new_symbol_handle(external_name(type), CHECK_NULL);
  }   
  KlassHandle klassklass (THREAD, Universe::typeArrayKlassKlassObj());

  arrayKlassHandle k = base_create_array_klass(o.vtbl_value(), header_size(), klassklass, type/*klassId*/, CHECK_NULL);
  typeArrayKlass* ak = typeArrayKlass::cast(k());
  ak->set_name(sym());
  ak->set_layout_helper(array_layout_helper(type));
  assert(scale == (1 << ak->log2_element_size()), "scale must check out");
  assert(ak->oop_is_javaArray(), "sanity");
  assert(ak->oop_is_typeArray(), "sanity");
  ak->set_max_length(arrayOopDesc::max_array_length(type));
  assert(k()->size() > header_size(), "bad size");

  // Call complete_create_array_klass after all instance variables have been initialized.
  KlassHandle super (THREAD, k->super());
  complete_create_array_klass(k, super, CHECK_NULL);
  
  KlassTable::bindReservedKlassId(k(), (unsigned int)type);  // Azul: type arrays have kid == elt type, May GC!
    
  return k();
}

typeArrayOop typeArrayKlass::allocate(int length, intptr_t sba_hint, TRAPS) {  
  assert(log2_element_size() >= 0, "bad scale");
  if (length >= 0) {
    if (length <= max_length()) {
      size_t size = typeArrayOopDesc::object_size(layout_helper(), length);    
      KlassHandle h_k(THREAD, as_klassOop());
typeArrayOop t=NULL;
      CollectedHeap* ch = Universe::heap();
      if (size < ch->large_typearray_limit()) {
        assert0( THREAD->is_Java_thread() );
        if( UseSBA )
          t = (typeArrayOop)((JavaThread*)THREAD)->sba_area()->allocate( h_k.as_klassRef(), size, length, sba_hint).as_oop();
if(t==NULL)
          t = (typeArrayOop)CollectedHeap::array_allocate(h_k, (int)size, length, CHECK_NULL);
      } else {
        t = (typeArrayOop)CollectedHeap::large_typearray_allocate(h_k, (int)size, length, CHECK_NULL);
      }
      assert(t->is_parsable(), "Don't publish unless parsable");
      return t;
    } else {
      THROW_OOP_0(Universe::out_of_memory_error_array_size());
    }
  } else {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
}

typeArrayOop typeArrayKlass::allocate_permanent(int length, TRAPS) {  
  if (length < 0) THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  int size = typeArrayOopDesc::object_size(layout_helper(), length);
  KlassHandle h_k(THREAD, as_klassOop());  
  typeArrayOop t = (typeArrayOop)
    CollectedHeap::permanent_array_allocate(h_k, size, length, CHECK_NULL);
  assert(t->is_parsable(), "Can't publish until parsable");
  return t;
}

oop typeArrayKlass::multi_allocate(int rank, jint* last_size, intptr_t sba_hint, TRAPS) {  
  // For typeArrays this is only called for the last dimension
  assert(rank == 1, "just checking");
  int length = *last_size;
return allocate(length,true/*SBA*/,THREAD);
}


void typeArrayKlass::copy_array(arrayOop s, int src_pos, arrayOop d, int dst_pos, int length, TRAPS) {
  assert(s->is_typeArray(), "must be type array");

  // Check destination
  if (!d->is_typeArray() || element_type() != typeArrayKlass::cast(d->klass())->element_type()) {
    THROW(vmSymbols::java_lang_ArrayStoreException());
  }

  // Check is all offsets and lengths are non negative
  if (src_pos < 0 || dst_pos < 0 || length < 0) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  // Check if the ranges are valid
  if  ( (((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length())
     || (((unsigned int) length + (unsigned int) dst_pos) > (unsigned int) d->length()) ) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }

int sc=log2_element_size();
#ifndef PRODUCT
  if( PrintStatistics )
    SharedRuntime::collect_arraycopy_stats(length*sc);
#endif
  if( length == 0 ) return;     // Shortcut common fast case
  // This is an attempt to make the copy_array fast.
  // NB: memmove takes care of overlapping memory segments.
  char* src = (char*)s->base(element_type()) + (src_pos << sc);
  char* dst = (char*)d->base(element_type()) + (dst_pos << sc);
  if( s != d ) memcpy (dst, src, length << sc);
  else         memmove(dst, src, length << sc);
}


// create a klass of array holding typeArrays
klassRef typeArrayKlass::array_klass_impl(klassRef thsi,bool or_null,int n,TRAPS){
  typeArrayKlassHandle h_this(thsi);
  return array_klass_impl(h_this, or_null, n, THREAD);
}

klassRef typeArrayKlass::array_klass_impl(typeArrayKlassHandle h_this, bool or_null, int n, TRAPS) {  
  int dimension = h_this->dimension();
  assert(dimension <= n, "check order of chain");
    if (dimension == n) 
      return h_this();

  objArrayKlassHandle  h_ak(THREAD, h_this->higher_dimension());
  if (h_ak.is_null()) {    
    if (or_null)  return klassRef();

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
MutexLockerAllowGC mc(Compile_lock,jt);//for vtables
      // Atomic create higher dimension and link into list
MutexLockerAllowGC mu(MultiArray_lock,jt);
    
      h_ak = objArrayKlassHandle(THREAD, h_this->higher_dimension());
      if (h_ak.is_null()) {
        klassOop oak = objArrayKlassKlass::cast(
Universe::objArrayKlassKlassObj())->allocate_objArray_klass(dimension+1,h_this,CHECK_(klassRef()));
        h_ak = objArrayKlassHandle(THREAD, oak);
        h_ak->set_lower_dimension(h_this());
        h_this->set_higher_dimension(h_ak());
        assert(h_ak->oop_is_objArray(), "incorrect initialization of objArrayKlass");    
      }       
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());    
  }
  
  if (or_null) {
    return h_ak->array_klass_or_null(n); 
  }
  return Klass::array_klass(h_ak.as_klassRef(), n, CHECK_(klassRef()));
}

klassRef typeArrayKlass::array_klass_impl(klassRef thsi, bool or_null, TRAPS) {
  return array_klass_impl(thsi, or_null, dimension() +  1, THREAD);
}

int typeArrayKlass::oop_size(oop obj) const { 
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  return t->object_size();
}

int typeArrayKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_typeArray()), obj->_klass may not be available.
  typeArrayOop t = typeArrayOop(obj);
return t->GC_object_size(this);
}


int typeArrayKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::typeArrayKlass never moves.
  return t->object_size();
}

int typeArrayKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  if(blk->do_header())
t->oop_iterate_header(blk);
  return t->object_size();
}

int typeArrayKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert(obj->is_typeArray(),"must be a type array");
  typeArrayOop t = typeArrayOop(obj);
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::typeArrayKlass never moves.
  return t->object_size();
}

void typeArrayKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
}

void typeArrayKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
}

int
typeArrayKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_typeArray(),"must be a type array");
  return typeArrayOop(obj)->object_size();
}

int
typeArrayKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				    HeapWord* beg_addr, HeapWord* end_addr) {
  assert(obj->is_typeArray(),"must be a type array");
  return typeArrayOop(obj)->object_size();
}

void typeArrayKlass::initialize(TRAPS) {
  // Nothing to do. Having this function is handy since objArrayKlasses can be
  // initialized by calling initialize on their bottom_klass, see objArrayKlass::initialize
}

const char* typeArrayKlass::external_name(BasicType type) {
  switch (type) {
    case T_BOOLEAN: return "[Z";
    case T_CHAR:    return "[C";
    case T_FLOAT:   return "[F";
    case T_DOUBLE:  return "[D";
    case T_BYTE:    return "[B";
    case T_SHORT:   return "[S";
    case T_INT:     return "[I";
    case T_LONG:    return "[J";
    default: ShouldNotReachHere();
  }
  return NULL;
}

// Printing

static void print_boolean_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %s", index, (ta->bool_at(index) == 0) ? "false" : "true");
  }
}


static void print_char_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jchar c = ta->char_at(index);
    st->print_cr(" - %3d: %x %c", index, c, isprint(c) ? c : ' ');
  }
}


static void print_float_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %g", index, ta->float_at(index));
  }
}


static void print_double_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    st->print_cr(" - %3d: %g", index, ta->double_at(index));
  }
}


static void print_byte_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jbyte c = ta->byte_at(index);
    st->print_cr(" - %3d: %x %c", index, c, isprint(c) ? c : ' ');
  }
}


static void print_short_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    int v = ta->ushort_at(index);
    st->print_cr(" - %3d: 0x%x\t %d", index, v, v);
  }
}


static void print_int_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jint v = ta->int_at(index);
    st->print_cr(" - %3d: 0x%x %d", index, v, v);
  }
}


static void print_long_array(typeArrayOop ta, int print_len, outputStream* st) {
  for (int index = 0; index < print_len; index++) {
    jlong v = ta->long_at(index);
    st->print_cr(" - %3d: 0x%x 0x%x", index, high(v), low(v));
  }
}


static void print_xml_boolean_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    xb->name_value_item("val", (ta->bool_at(index) == 0) ? "false" : "true");
  }
}


static void print_xml_char_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    jchar c = ta->char_at(index);
    // if not xml friendly
    if ((c < 32) || (c == 34) || (c == 38) || (c == 39) ||
        (c == 60) || (c == 62) || (c >= 127)) {
      sprintf(val, "0x%x %d", c, c);
    } else {
      sprintf(val, "0x%x %d '%c'", c, c, c);
    }
    xb->name_value_item("val", val);
  }
}


static void print_xml_float_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    sprintf(val, "%g", ta->float_at(index));
    xb->name_value_item("val", val);
  }
}


static void print_xml_double_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    sprintf(val, "%g", ta->double_at(index));
    xb->name_value_item("val", val);
  }
}


static void print_xml_byte_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    jbyte c = ta->byte_at(index);
    // if not xml friendly
    if ((c < 32) || (c == 34) || (c == 38) || (c == 39) ||
        (c == 60) || (c == 62) || (c >= 127)) {
      sprintf(val, "0x%x %d", c, c);
    } else {
      sprintf(val, "0x%x %d '%c'", c, c, c);
    }
    xb->name_value_item("val", val);
  }
}


static void print_xml_short_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    int v = ta->ushort_at(index);
sprintf(val,"0x%x %d",v,v);
    xb->name_value_item("val", val);
  }
}


static void print_xml_int_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    jint v = ta->int_at(index);
sprintf(val,"0x%x %d",v,v);
    xb->name_value_item("val", val);
  }
}


static void print_xml_long_array(typeArrayOop ta,int print_len,xmlBuffer*xb){
  char val[64];
  xmlElement a(xb, "elements");
  for (int index = 0; index < print_len; index++) {
    xmlElement el(xb, "elem");
    xb->name_value_item("idx", index);
    jlong v = ta->long_at(index);
sprintf(val,"0x%llx %lld",v,v);
    xb->name_value_item("val", val);
  }
}

#ifndef PRODUCT

void typeArrayKlass::oop_print_on(oop obj, outputStream* st) {
  arrayKlass::oop_print_on(obj, st);
  typeArrayOop ta = typeArrayOop(obj);
  int print_len = MIN2((intx) ta->length(), MaxElementPrintSize);
  switch (element_type()) {
    case T_BOOLEAN: print_boolean_array(ta, print_len, st); break;
    case T_CHAR:    print_char_array(ta, print_len, st);    break;
    case T_FLOAT:   print_float_array(ta, print_len, st);   break;
    case T_DOUBLE:  print_double_array(ta, print_len, st);  break;
    case T_BYTE:    print_byte_array(ta, print_len, st);    break;
    case T_SHORT:   print_short_array(ta, print_len, st);   break;
    case T_INT:     print_int_array(ta, print_len, st);     break;
    case T_LONG:    print_long_array(ta, print_len, st);    break;
    default: ShouldNotReachHere();
  }
  int remaining = ta->length() - print_len;
  if (remaining > 0) {
    tty->print_cr(" - <%d more elements, increase MaxElementPrintSize to print>", remaining);
  }
}

#endif // PRODUCT

const char* typeArrayKlass::internal_name() const {
  return Klass::external_name();
}


void typeArrayKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
const char*elem_type=NULL;
    switch (element_type()) {
case T_BOOLEAN:elem_type="boolean";break;
      case T_FLOAT:   elem_type = "float";   break;
      case T_DOUBLE:  elem_type = "double";  break;
      case T_BYTE:    elem_type = "byte";    break;
      case T_SHORT:   elem_type = "short";   break;
      case T_INT:     elem_type = "int";     break;
      case T_LONG:    elem_type = "long";    break;
      case T_CHAR: {
        elem_type = "char";
        int length = ((typeArrayOop)obj)->length();
        if (length > 0) {
if(length>ARTAStringPreviewLength){
length=ARTAStringPreviewLength;
          }
          jchar* position = typeArrayOop(obj)->char_at_addr(0);

          // Note: This is needed because string may contain non-friendly
          // xml characters.
          const char * string_val = UNICODE::as_utf8(position, length);
          int templen = (int)strlen(string_val) + 1;
char*tempstr=new char[templen];
          strcpy(tempstr, string_val);
          for (int index = 0; tempstr[index] != (char)0; index++) {
char c=tempstr[index];
              if ((c < 32) || (c == 34) || (c == 38) || (c == 39) ||
                  (c == 60) || (c == 62) || (c >= 127)) {
tempstr[index]='?';
              }
          }
          xb->name_value_item("string_value", tempstr);
          delete [] tempstr; 
        }
        break;
      }
      default: ShouldNotReachHere();
    }
    { xmlElement xn(xb, "name", xmlElement::no_LF);
      xb->print("%s[%d]", elem_type, ((arrayOop)obj)->length());
    }
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}


void typeArrayKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  typeArrayOop ta = typeArrayOop(obj);

  xmlElement o(xb, "array");
  { xmlElement xe(xb, "class");
    { xmlElement se(xb, "name");
      xb->print(Klass::external_name());
    }
  }
  { xmlElement e(xb, "element_type");
const char*elem_type=NULL;
    switch (element_type()) {
case T_BOOLEAN:elem_type="boolean";break;
      case T_FLOAT:   elem_type = "float";   break;
      case T_DOUBLE:  elem_type = "double";  break;
      case T_BYTE:    elem_type = "byte";    break;
      case T_SHORT:   elem_type = "short";   break;
      case T_INT:     elem_type = "int";     break;
      case T_LONG:    elem_type = "long";    break;
      case T_CHAR:    elem_type = "char";    break;
      default: ShouldNotReachHere();
    }
    xb->name_value_item("name", elem_type);
  }
  { xmlElement l(xb, "length");
    xb->print("%d", ta->length());
  }
  {
    int print_len = ta->length();
    switch (element_type()) {
case T_BOOLEAN:print_xml_boolean_array(ta,print_len,xb);break;
case T_CHAR:print_xml_char_array(ta,print_len,xb);break;
case T_FLOAT:print_xml_float_array(ta,print_len,xb);break;
case T_DOUBLE:print_xml_double_array(ta,print_len,xb);break;
case T_BYTE:print_xml_byte_array(ta,print_len,xb);break;
case T_SHORT:print_xml_short_array(ta,print_len,xb);break;
case T_INT:print_xml_int_array(ta,print_len,xb);break;
case T_LONG:print_xml_long_array(ta,print_len,xb);break;
      default: ShouldNotReachHere();
    }
  }
}
