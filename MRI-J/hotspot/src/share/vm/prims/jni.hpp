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
#ifndef JNI_HPP
#define JNI_HPP

#include "atomic.hpp"
#include "mutexLocker.hpp"
#include "thread.hpp"

//   jfieldIDCache
//
// Maintains a list of fieldids for which a GetFieldID call has been
// made from the proxy

class jfieldIDEntry:public CHeapObj{
  public:
    jfieldIDEntry()                               { _offset = 0; _next = NULL; }
    void init(int offset, jfieldID fieldid, BasicType type, jfieldIDEntry *next) {
      _offset = offset;
      _fieldid = fieldid;
_fieldtype=type;
      _next = next;
    }
    int offset()                                  { return _offset; }
    jfieldID fieldid()                            { return _fieldid; }
    BasicType fieldtype()                         { return _fieldtype; }
jfieldIDEntry*next(){return _next;}

  private:
    jfieldIDEntry *_next;                         // Next fieldid entry
int _offset;//offset of fieldid
    jfieldID _fieldid;                            // fieldid
    BasicType _fieldtype;                         // field type
};

class jfieldIDCache:public CHeapObj{
  public:
    jfieldIDCache() {
_head=NULL;
      _count = 0;
    }
    // NOTE: This destructor is called from release_C_heap_structures which is executed
    //       on class unloading, no other mutator thread would be accessing the same
    //       klass and hence it is not necessary to lock this code
    ~jfieldIDCache() {
      jfieldIDEntry *iter, *temp;
      iter = (jfieldIDEntry *)_head;
      _count = 0;
_head=NULL;
      while (iter != NULL) {                      // Delete the fieldid list
        temp = iter;
        iter = iter->next();
        delete temp;
      }
    }

    inline int get_fieldids(jfieldIDEntry **head) __attribute__((always_inline));
    inline bool lookup_fieldid(jfieldID fieldid) __attribute__((always_inline));
    inline void add_fieldid(int offset, jfieldID fieldid, BasicType type) __attribute__((always_inline));

  private:
    volatile jfieldIDEntry *_head;                // Next fieldid entry
    volatile int _count;                          // number of fieldid entries
};

// NOTE: we are looking at only a snap shot of the fieldid list, the list will not be deleted
//       while the readers are traversing it, new entries might be added to the top of the list
//       concurrently but the readers will not see it (the snap shot read by the readers will
//       be consistent)
int jfieldIDCache::get_fieldids(jfieldIDEntry **head)
{
  *head = (jfieldIDEntry *)_head;
  Atomic::read_barrier();
  return _count;
}

// Check if the fieldid already exists in a snapshot of the list
bool jfieldIDCache::lookup_fieldid(jfieldID fieldid)
{
  jfieldIDEntry *iter;

  iter = (jfieldIDEntry *)_head;
  Atomic::read_barrier();
while(iter!=NULL){
    if (iter->fieldid() == fieldid) {
      return true;                                // fieldid exists so return
    }
    iter = iter->next();
  }
  return false;
}

void jfieldIDCache::add_fieldid(int offset, jfieldID fieldid, BasicType type)
{
  // Fieldid does not already exist add it to the list
  jfieldIDEntry *entry = new jfieldIDEntry();
  if (entry != NULL) {
MutexLockerAllowGC ml(JfieldIdCreation_lock,JavaThread::current());
    jfieldIDEntry *iter;

    iter = (jfieldIDEntry *)_head;
while(iter!=NULL){
      if (iter->fieldid() == fieldid) {
        delete entry;
        return;
      }
      iter = iter->next();
    }
    entry->init(offset, fieldid, type, (jfieldIDEntry *)_head);
    _count += 1;
    Atomic::write_barrier();
_head=entry;
  }
}

#endif // JNI_HPP
