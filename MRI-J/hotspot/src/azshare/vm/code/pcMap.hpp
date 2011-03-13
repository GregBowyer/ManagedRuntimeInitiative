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
#ifndef PCMAP_HPP
#define PCMAP_HPP

#include "allocation.hpp"
class CodeBlob;
class PCMapBuilder;
class outputStream;

// A class supporting a mapping from relative-pc's to 'stuff' generally via
// hash table lookup.  The mapping is fixed at creation time - so doesn't
// support modification.  Lots of these are made, so the mapping is also very
// dense.  

class pcMap:public CHeapObj{
  friend class PCMapBuilder;
  friend class RegMap;
  friend class DebugMap;
  virtual intptr_t get_stuff(int idx) const=0; // get stuff at index
protected:
  pcMap( int max ) : _maxidx(max) { }
  virtual int  get_relpc(int idx) const=0; // get PC at index of hashtable
public:
  static const int MaxBytes = 88; // max number of bytes that can be filled by fill
  const int _maxidx;              // max number of indices
  int next_idx(int idx) const { return idx+1; } // next indice
#define NO_MAPPING ((intptr_t)-1)
  // Do a lookup.  Return NO_MAPPING if we missed, otherwise return the data.
  virtual intptr_t get( int rel_pc ) const = 0;
  virtual int find_slot( int rel_pc ) const = 0;
  // Do a lookup and fill buf. Return NO_MAPPING if we missed, otherwise return the size of the data.
  virtual int  fill( char *buf, int rel_pc ) const = 0;
  virtual int  size( ) const = 0; // size in bytes, including self
  virtual void print( const CodeBlob *blob, outputStream * ) const;
};

// Lookup via hash table - 16 bits of relative-pc and 16 bits of payload.
class pcMapHash16x16 : public pcMap {
  const unsigned short *const _table;
  virtual int      get_relpc(int idx) const;
  virtual intptr_t get_stuff(int idx) const { return _table[(idx<<1)+1]; }
  virtual intptr_t get( int rel_pc ) const;  // get stuff for given PC
  virtual int fill ( char *buf, int rel_pc ) const;
  virtual int size( ) const { return sizeof(this)+_maxidx*sizeof(short)*2; }
  virtual int find_slot( int rel_pc ) const;
  pcMapHash16x16( int max, unsigned short *table ) : pcMap(max), _table(table) { }
  ~pcMapHash16x16();
public:
  static pcMapHash16x16 *make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff );
};

// Lookup via hash table - 16 bits of relative-pc and 32 bits of payload.
class pcMapHash16x32 : public pcMap {
  const unsigned short *const _relpcs;
  const uint32_t       *const _stuffs;
  virtual int      get_relpc(int idx) const { return _relpcs[idx]==0xffff ? NO_MAPPING : _relpcs[idx]; }
  virtual intptr_t get_stuff(int idx) const { return _stuffs[idx]; }
  virtual intptr_t get( int rel_pc ) const;  // get stuff for given PC
  virtual int fill( char *buf, int rel_pc ) const;
  virtual int size( ) const { return sizeof(this)+_maxidx*(sizeof(uint32_t)+sizeof(short)); }
  virtual int find_slot( int rel_pc ) const;
  pcMapHash16x32( int max, unsigned short *relpcs, uint32_t* stuffs ) : pcMap(max), _relpcs(relpcs), _stuffs(stuffs) {}
  ~pcMapHash16x32();
public:
  static pcMapHash16x32 *make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff );
};

// Lookup via hash table - 16 bits of relative-pc and 64 bits of payload.
class pcMapHash16x64 : public pcMap {
  const unsigned short *const _relpcs;
  const uint64_t       *const _stuffs;
  virtual int      get_relpc(int idx) const { return _relpcs[idx]==0xffff ? NO_MAPPING : _relpcs[idx]; }
  virtual intptr_t get_stuff(int idx) const { return _stuffs[idx]; }
  virtual intptr_t get( int rel_pc ) const;  // get stuff for given PC
  virtual int fill( char *buf, int rel_pc ) const;
  virtual int size( ) const { return sizeof(this)+_maxidx*(sizeof(uint64_t)+sizeof(short)) ; }
  virtual int find_slot( int rel_pc ) const;
  pcMapHash16x64( int max, unsigned short *relpcs, uint64_t* stuffs ) : pcMap(max), _relpcs(relpcs), _stuffs(stuffs) {}
  ~pcMapHash16x64();
public:
  static pcMapHash16x64 *make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff );
};

// encode size and relpc into 32bit quantity
struct relPCandSize {
  uint8_t size  : 8;
  int     relpc : 24;
};

// Hash table that can handle 24bit PCs and 256*8 worth of stuff
class pcMapHashBig : public pcMap {
  const struct relPCandSize *const _relpcs_and_sizes;
  const uint16_t            *const _offsets;
  const char                *const _stuff;
  const int                        _stuff_size;
  const char *get_stuff_addr(int idx) const { return &_stuff[_offsets[idx]]; }
  uint8_t     get_stuff_size(int idx) const { return _relpcs_and_sizes[idx].size; }
  virtual int      get_relpc(int idx) const { return _relpcs_and_sizes[idx].relpc==NO_MAPPING ? NO_MAPPING : _relpcs_and_sizes[idx].relpc; }
  virtual intptr_t get_stuff(int idx) const;
  virtual intptr_t get( int rel_pc ) const;  // get stuff for given PC
  virtual int fill( char *buf, int rel_pc ) const;
  virtual int size( ) const { return sizeof(this)+_maxidx*(sizeof(struct relPCandSize)+sizeof(short)) + sizeof(char)*_stuff_size; }
  virtual int find_slot( int rel_pc ) const;
  pcMapHashBig( int max, relPCandSize *r_and_s, uint16_t* offsets, char *stuff, int stuff_size ) :
    pcMap(max), _relpcs_and_sizes(r_and_s), _offsets(offsets), _stuff(stuff), _stuff_size(stuff_size) {}
  ~pcMapHashBig();
public:
  static pcMapHashBig *make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff );
};

#endif // PCMAP_HPP
