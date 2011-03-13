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
#ifndef FLATHASHSET_HPP
#define FLATHASHSET_HPP



// FlatHashSet
// Simple flat hash table without the ability to remove entries or resize
template<class T> class FlatHashSet {
 private:
  volatile T* _entries; 
  unsigned int _maxEntries;

 public:
  FlatHashSet() {
    initialize(10);
  }

  FlatHashSet(unsigned int lgMaxEntries) {
initialize(lgMaxEntries);
  }

  ~FlatHashSet() {
    delete[] _entries;
  }

  bool add(const void* entry) {
if(entry==NULL)return 0;
    unsigned int hash = (((int)(int64)entry)&(_maxEntries-1)) | 0x01;
    unsigned offset = hash;
for(unsigned int i=0;i<_maxEntries;i++){
      if (_entries[offset] == NULL) {
        // Cas me down
        if (Atomic::cmpxchg_ptr((void*)entry, (void*)&_entries[offset], NULL) == NULL) {
          // We won!
          return 1;
        }
      }
      offset = (hash+offset) & (_maxEntries-1);
    }

    // Not found
    return 0;
  }

  bool contains(T entry) const {
if(entry==NULL)return 0;
    unsigned int hash = (((int)(int64)entry)&(_maxEntries-1)) | 0x01;
    unsigned offset = hash;
for(unsigned int i=0;i<_maxEntries;i++){
      if (_entries[offset] == entry) {
        return 1;
      }
      if (_entries[offset] == NULL) {
        return 0;
      }
      offset = (hash+offset) & (_maxEntries-1);
    }

    // Not found
    return 0;
  }

 private:
  void initialize(unsigned int lgMaxEntries) {
    _maxEntries = 1<<lgMaxEntries;
    _entries = new T[_maxEntries];
    guarantee(_entries, "Null??");
for(unsigned int i=0;i<_maxEntries;i++){
_entries[i]=NULL;
    }
  }
};

#endif // FLATHASHSET_HPP
