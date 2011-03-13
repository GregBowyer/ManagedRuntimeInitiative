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

#ifndef ARTAOBJECTS_HPP
#define ARTAOBJECTS_HPP


#include "allocation.hpp"
#include "iterator.hpp"
#include "objectRef_pd.hpp"


// ArtaObjectPool class encapsulates a circular buffer structure that
// is used to retain an (id number -> oop) mapping
// that enables us to hand out "object-ref" ids that can
// later be examined

// GC needs to be informed about the oops held in this
// buffer so it can add them to its root set

// a single instance of ArtaObjectPool is managed by ArtaObjects
// ArtaObjects also has utility routines for printing out objects

class ArtaObjectPool:public CHeapObj{
  friend class ArtaObjects;

 private:
  ArtaObjectPool *_next; // chain of all pools
  objectRef     *_object_map;  // buffer containing objectRefs
  unsigned int   _buffer_size;
  // last id returned, range is [0, 0x1000) for bufsize=0x100:
  int            _last_dispensed_id;
  // buffer is full once bufsize ids have been dispensed:
  bool           _buffer_full;

 protected:
ArtaObjectPool*next()const{return _next;}
  void           set_next(ArtaObjectPool* pool)   { _next = pool; }

 public:
  ArtaObjectPool(unsigned int size = 0x1000);
  ~ArtaObjectPool();

  bool is_id_live(int id);
  int  append_oop(const oopDesc *o);
  oop  get_oop(int id);

  // gc support
  void oops_do(OopClosure *f);
void unlink(BoolObjectClosure*is_alive);
  void GPGC_unlink();
};




class ArtaObjects:public AllStatic{

private:
  static ArtaObjectPool *_artaObjectPool;

public:
  static void add   (ArtaObjectPool *pool);
  static void remove(ArtaObjectPool *pool);

  // gc support
  static void oops_do(OopClosure *f);
  static void unlink(BoolObjectClosure* is_alive);
  static void GPGC_unlink();

  // utility routines
  static void oop_print_xml_on(oop o, xmlBuffer *xb, bool ref);
  static void null_print_xml_on(xmlBuffer *xb);
};

#endif // ARTAOBJECTS_HPP
