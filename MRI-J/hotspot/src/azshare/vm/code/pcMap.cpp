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


#include "ostream.hpp"
#include "pcMap.hpp"

#include "allocation.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

// --- unstuff
// Little helper.
// If words_per_stuff is 1, then 'stuff' is the actual data.
// Else stuff is a pointer to the data, or NULL.
static intptr_t unstuff(int wps, intptr_t d, int idx) {
  if( wps == 1   ) return d; // only 1 word of data
  if( d == 0     ) return 0; // no data == 0
  if( idx >= wps ) return 0; // asking for data beyond the stuffs
  return ((intptr_t*)d)[idx]; // return appropriate word of stuff
}

// --- print_on
void pcMap::print( const CodeBlob *blob, outputStream *st ) const {
st->print_cr("{");
for(int i=0;i<_maxidx;i++){
    int pc = get_relpc(i);
    if( pc == NO_MAPPING ) continue;
    if( blob ) st->print_cr(  "  %p -> %lx", (address)blob+pc,get_stuff(i));
    else       st->print_cr("  +%4d -> %lx", pc,get_stuff(i));
  }
  st->print_cr("}");
}

// --- ~pcMapHash16x16
pcMapHash16x16::~pcMapHash16x16() { 
  FREE_C_HEAP_ARRAY(short,_table); 
}

// --- get_relpc
int pcMapHash16x16::get_relpc(int idx) const { 
  int relpc = _table[(idx<<1)+0]; 
  return ((relpc<<16 | _table[(idx<<1)+1]) == NO_MAPPING) ? NO_MAPPING : relpc;
}

// --- find_slot
// Find the proper slot for 'rel_pc'.  Returns an index, where the key at that
// index is either: -1 (NO_MAPPING), or 'rel_pc' (a hit) or some other pc
// (also miss but due to running the table out).
int pcMapHash16x16::find_slot( int rel_pc ) const {
  int h = rel_pc;
  h += (h <<  15) ^ 0xffffcd7d;
  h ^= ((unsigned)h >> 10);
  h += (h <<   3);
  h &= (_maxidx-1);
  int cnt=0;                    
  while( true ) {               // reprobe loop
    unsigned short key = _table[(h<<1)+0];
    if( key == (unsigned short)rel_pc ) return h; // hit
    if( ((key<<16) | _table[(h<<1)+1]) == (int)0xffffffff ) return h; // miss
    if( cnt++ > _maxidx ) return h; // table full
    h = (h+1)&(_maxidx-1);      // miss, so stride-1 reprobe
  }
}

// --- get
// Table lookup.  Should be fast!
intptr_t pcMapHash16x16::get( int rel_pc ) const {
  int idx = find_slot(rel_pc);
  return (_table[(idx<<1)+0] == (unsigned short)rel_pc) ? _table[(idx<<1)+1] : NO_MAPPING;
}

// --- fill
int pcMapHash16x16::fill( char *buf, int rel_pc ) const {
  int idx = find_slot(rel_pc);
  if ( _table[(idx<<1)+0] == rel_pc ) {
    *((int16*)buf) = get_stuff (idx);
    return 2;
  } else {
    return NO_MAPPING;
  }
}

// --- make
// Make a hashtable with 16-bit PCs and 16-bit int stuff.
pcMapHash16x16 *pcMapHash16x16::make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff ) {
  int logtbl = log2_long(rel_pcs->length());
  if( (1<<logtbl) <= rel_pcs->length() ) logtbl++;
  int maxidx = 1<<logtbl;       // power of 2 table, just barely big enuf
  unsigned short *tbl = NEW_C_HEAP_ARRAY(unsigned short,2*maxidx);
  memset(tbl,NO_MAPPING,2*maxidx*sizeof(short));
  pcMapHash16x16 *map = new pcMapHash16x16(maxidx,tbl);

  // now fill in the hash table
for(int i=0;i<rel_pcs->length();i++){
int relpc=rel_pcs->at(i);
    intptr_t datum = unstuff(words_per_stuff,stuff->at(i),0);
#ifdef ASSERT
for(int j=1;j<words_per_stuff;j++){
      assert0( unstuff(words_per_stuff,stuff->at(i),j) == 0 );
    }
#endif
    assert0( 0 <= datum && datum < (1L<<16) );
    if( (relpc<<16 | datum) == NO_MAPPING )
Unimplemented();//confusion in the ranks, can't tell this combo from a miss
    int idx = map->find_slot(relpc);
    unsigned short key = tbl[(idx<<1)+0];
    assert0( key != relpc );   // dup keys???
    assert0( (key<<16 | tbl[(idx<<1)+1]) == NO_MAPPING ); // table full?  made it too small?
    tbl[(idx<<1)+0] = relpc;
    tbl[(idx<<1)+1] = datum;
  }
  assert0( map->get(rel_pcs->at(0)) == unstuff(words_per_stuff,stuff->at(0),0) );
  return map;
}

// --- ~pcMapHash16x32
pcMapHash16x32::~pcMapHash16x32() { 
  FREE_C_HEAP_ARRAY(short   ,_relpcs); 
  FREE_C_HEAP_ARRAY(uint32_t,_stuffs); 
}

// --- find_slot
// Find the proper slot for 'rel_pc'.  Returns an index, where the key at that
// index is either: -1 (NO_MAPPING), or 'rel_pc' (a hit) or some other pc
// (also miss but due to running the table out).
int pcMapHash16x32::find_slot( int rel_pc ) const {
  int h = rel_pc;
  h += (h <<  15) ^ 0xffffcd7d;
  h ^= ((unsigned)h >> 10);
  h += (h <<   3);
  h &= (_maxidx-1);
  int cnt=0;                    
  while( true ) {               // reprobe loop
    unsigned short key = _relpcs[h];
    if( key == (unsigned short)rel_pc ) return h; // hit
    if( key == 0xffff ) return h; // miss
    if( cnt++ > _maxidx ) return h; // table full
    h = (h+1)&(_maxidx-1);      // miss, so stride-1 reprobe
  }
}

// --- get
// Table lookup.  Should be fast!
intptr_t pcMapHash16x32::get( int rel_pc ) const {
  int idx = find_slot(rel_pc);
  return (_relpcs[idx] == (unsigned short)rel_pc) ? _stuffs[idx] : NO_MAPPING;
}

// --- fill
int pcMapHash16x32::fill( char *buf, int rel_pc ) const {
  int idx = find_slot(rel_pc);
  if ( _relpcs[idx] == rel_pc ) {
    *((uint32_t*)buf) = get_stuff (idx);
return 4;
  } else {
    return NO_MAPPING;
  }
}

// --- make
// Make a hashtable with 16-bit PCs and 32-bit int stuff.
pcMapHash16x32 *pcMapHash16x32::make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff ) {
  int logtbl = log2_long(rel_pcs->length());
  if( (1<<logtbl) <= rel_pcs->length() ) logtbl++;
  int maxidx = 1<<logtbl;       // power of 2 table, just barely big enuf
  unsigned short *tbl_pc = NEW_C_HEAP_ARRAY(unsigned short,maxidx);
  uint32_t       *tbl_da = NEW_C_HEAP_ARRAY(uint32_t,      maxidx);
  memset(tbl_pc,NO_MAPPING,maxidx*sizeof(short));
  pcMapHash16x32 *map = new pcMapHash16x32(maxidx,tbl_pc,tbl_da);

  // now fill in the hash table
for(int i=0;i<rel_pcs->length();i++){
int relpc=rel_pcs->at(i);
    intptr_t datum = unstuff(words_per_stuff,stuff->at(i),0);
#ifdef ASSERT
for(int j=1;j<words_per_stuff;j++){
      assert0( unstuff(words_per_stuff,stuff->at(i),j) == 0 );
    }
#endif
    assert0( 0 <= datum && datum < (1L<<32) );
    int idx = map->find_slot(relpc);
    unsigned short key = tbl_pc[idx];
    assert0( key != relpc );   // dup keys???
    assert0( key == (unsigned short)NO_MAPPING ); // table full?  made it too small?
    tbl_pc[idx] = relpc;
    tbl_da[idx] = datum;
  }
  assert0( map->get(rel_pcs->at(0)) == unstuff(words_per_stuff,stuff->at(0),0) );
  return map;
}


// --- ~pcMapHash16x64
pcMapHash16x64::~pcMapHash16x64() { 
  FREE_C_HEAP_ARRAY(short   ,_relpcs); 
  FREE_C_HEAP_ARRAY(uint64_t,_stuffs); 
}

// --- find_slot
// Find the proper slot for 'rel_pc'.  Returns an index, where the key at that
// index is either: -1 (NO_MAPPING), or 'rel_pc' (a hit) or some other pc
// (also miss but due to running the table out).
int pcMapHash16x64::find_slot( int rel_pc ) const {
  int h = rel_pc;
  h += (h <<  15) ^ 0xffffcd7d;
  h ^= ((unsigned)h >> 10);
  h += (h <<   3);
  h &= (_maxidx-1);
  int cnt=0;                    
  while( true ) {               // reprobe loop
    unsigned short key = _relpcs[h];
    if( key == (unsigned short)rel_pc ) return h; // hit
    if( key == 0xffff ) return h; // miss
    if( cnt++ > _maxidx ) return h; // table full
    h = (h+1)&(_maxidx-1);      // miss, so stride-1 reprobe
  }
}

// --- get
// Table lookup.  Should be fast!
intptr_t pcMapHash16x64::get( int rel_pc ) const {
  int idx = find_slot(rel_pc);
  return (_relpcs[idx] == (unsigned short)rel_pc) ? _stuffs[idx] : NO_MAPPING;
}

// --- fill
int pcMapHash16x64::fill( char *buf, int rel_pc ) const {
  int idx = find_slot(rel_pc);
  if ( _relpcs[idx] == rel_pc ) {
    *((uint64_t*)buf) = get_stuff (idx);
return 8;
  } else {
    return NO_MAPPING;
  }
}

// --- make
// Make a hashtable with 16-bit PCs and 64-bit int stuff.
pcMapHash16x64 *pcMapHash16x64::make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff ) {
  int logtbl = log2_long(rel_pcs->length());
  if( (1<<logtbl) <= rel_pcs->length() ) logtbl++;
  int maxidx = 1<<logtbl;       // power of 2 table, just barely big enuf
  unsigned short *tbl_pc = NEW_C_HEAP_ARRAY(unsigned short,maxidx);
  uint64_t       *tbl_da = NEW_C_HEAP_ARRAY(uint64_t,      maxidx);
  memset(tbl_pc,NO_MAPPING,maxidx*sizeof(short));
  pcMapHash16x64 *map = new pcMapHash16x64(maxidx,tbl_pc,tbl_da);

  // now fill in the hash table
for(int i=0;i<rel_pcs->length();i++){
int relpc=rel_pcs->at(i);
    intptr_t datum = unstuff(words_per_stuff,stuff->at(i),0);
#ifdef ASSERT
for(int j=1;j<words_per_stuff;j++){
      assert0( unstuff(words_per_stuff,stuff->at(i),j) == 0 );
    }
#endif
    int idx = map->find_slot(relpc);
    unsigned short key = tbl_pc[idx];
    assert0( key != relpc );   // dup keys???
    assert0( key == (unsigned short)NO_MAPPING ); // table full?  made it too small?
    tbl_pc[idx] = relpc;
    tbl_da[idx] = datum;
  }
  assert0( map->get(rel_pcs->at(0)) == unstuff(words_per_stuff,stuff->at(0),0) );
  return map;
}

// --- ~pcMapHashBig
pcMapHashBig::~pcMapHashBig() {
  FREE_C_HEAP_ARRAY(struct relPCandSize, _relpcs_and_sizes);
  FREE_C_HEAP_ARRAY(uint16_t, _offsets);
FREE_C_HEAP_ARRAY(char,_stuff);
}

// --- find_slot2
// local version of find_slot for use before map is constructed
static int find_slot2( int rel_pc, int maxidx, const struct relPCandSize *const relpcs_and_sizes) {
  int h = rel_pc;
  h += (h <<  15) ^ 0xffffcd7d;
  h ^= ((unsigned)h >> 10);
  h += (h <<   3);
  h &= (maxidx-1);
  int cnt=0;
  while( true ) {               // reprobe loop
    int key = relpcs_and_sizes[h].relpc;
    if( key == rel_pc ) return h; // hit
    if( key == NO_MAPPING ) return h; // miss
    if( cnt++ > maxidx ) return h; // table full
    h = (h+1)&(maxidx-1);      // miss, so stride-1 reprobe
  }
}

// --- find_slot
// Find the proper slot for 'rel_pc'.  Returns an index, where the key at that
// index is either: -1 (NO_MAPPING), or 'rel_pc' (a hit) or some other pc
// (also miss but due to running the table out).
int pcMapHashBig::find_slot( int rel_pc ) const {
  return find_slot2(rel_pc, _maxidx, _relpcs_and_sizes);
}

// --- get
// Table lookup.  Should be fast!
intptr_t pcMapHashBig::get( int rel_pc ) const {
  int idx = find_slot(rel_pc);
  return (rel_pc == _relpcs_and_sizes[idx].relpc) ? get_stuff(idx) : NO_MAPPING;
}

// --- get_stuff
intptr_t pcMapHashBig::get_stuff( int idx ) const {
  int size = get_stuff_size(idx);
  if( size == 0 ) {
    return 0;
  } else {
    const char *adr = get_stuff_addr(idx);
    if (size >= 8) {
      return *((uint64_t*)adr);
    } else {
      intptr_t result = 0;
      memcpy(&result, adr, size);
      return result;
    }
  }
}

// --- fill
int pcMapHashBig::fill( char *buf, int rel_pc ) const {
  int idx = find_slot(rel_pc);
  if ( get_relpc(idx) == rel_pc ) {
    int  size = get_stuff_size(idx);       // size of stuff
    if( size == 0 ) {
      return 0;
    } else {
      const char *adr = get_stuff_addr(idx); // address of stuff
      int size_in_words = ((size+7)>>3)<<3;  // size of stuff rounding upto word sizes
      int adjust = size_in_words - size;     // amount of bytes missing after stuff
memcpy(buf,adr,size);
      memset(buf+size, 0, adjust );
      return size_in_words;
    }
  } else {
    return NO_MAPPING;
  }
}

// --- bits_in_stuff
static int bits_in_stuff(intptr_t stuff, int words_per_stuff) {
  if (words_per_stuff == 1) {
    return log2_long(stuff)+1;
  }
  uint64_t *words = (uint64_t*)stuff;
  if( !words ) return 0;
  int maxword      = 0;
  uint64_t maxbits = 0;
for(int j=0;j<words_per_stuff;j++){
if(words[j]!=0){
      maxbits = words[j];
maxword=j;
    }
  }
  return (maxword*BitsPerWord) + log2_long(maxbits)+1;
}

// --- make
// Make a hashtable with 24-bit PCs and upto 256bytes of stuff
pcMapHashBig *pcMapHashBig::make( GrowableArray<intptr_t> *rel_pcs, GrowableArray<intptr_t> *stuff, int words_per_stuff ) {
  int logtbl = log2_long(rel_pcs->length());
  if( (1<<logtbl) <= rel_pcs->length() ) logtbl++;
  int maxidx = 1<<logtbl;       // power of 2 table, just barely big enuf
  struct relPCandSize *tbl_relpc_size = NEW_C_HEAP_ARRAY(struct relPCandSize,maxidx);
  uint16_t               *tbl_offsets = NEW_C_HEAP_ARRAY(unsigned short,maxidx);
  const int max_stuff_size = maxidx*words_per_stuff*BitsPerWord;
  char                     *tbl_stuff = NEW_C_HEAP_ARRAY(char, max_stuff_size);
  int tbl_stuff_size = 0; // no entries in stuff table yet
  memset(tbl_relpc_size,NO_MAPPING,maxidx*sizeof(struct relPCandSize));

  // fill in tables, we need to know the eventual stuff size
for(int i=0;i<rel_pcs->length();i++){
int relpc=rel_pcs->at(i);
    int idx = find_slot2(relpc, maxidx, tbl_relpc_size);
    int key = tbl_relpc_size[idx].relpc;
    assert0( key != relpc );   // dup keys???
    assert0( key == NO_MAPPING ); // table full?  made it too small?
    intptr_t cur_stuff = stuff->at(i);
    int size = (bits_in_stuff(cur_stuff, words_per_stuff)+7) >> 3;
int offset=0;
    if( size > 0 ) {
      // TODO: use an efficient string in string search
      char *stuff_adr = (words_per_stuff == 1) ? (char*)(&cur_stuff) : (char*)cur_stuff;
      offset = -1;
      for( int i=0; i<=(tbl_stuff_size - size); i++ ) {
        if( bcmp(stuff_adr, &tbl_stuff[i], size) == 0 ) {
          // found duplicate in tbl_stuff
offset=i;
          break;
        }
      }
      if( offset == -1 ) {
offset=tbl_stuff_size;
        memcpy(&tbl_stuff[offset], stuff_adr, size);
tbl_stuff_size+=size;
      }
    }
    assert0( (idx    >= 0) && (idx    < maxidx)         );
    assert0( (size   >= 0) && (size   < MaxBytes)       );
    assert0( (relpc  >= 0) && (relpc  < (1<<24))        );
    assert0(((offset >= 0) && (offset < tbl_stuff_size)) ||
            ((offset == 0) && (tbl_stuff_size == 0)));
    tbl_relpc_size[idx].size  = size;
    tbl_relpc_size[idx].relpc = relpc;
    tbl_offsets[idx]          = offset;
  }
  if( tbl_stuff_size < max_stuff_size ) { // shrink table
    tbl_stuff = REALLOC_C_HEAP_ARRAY(char, tbl_stuff, tbl_stuff_size);
  }
  pcMapHashBig *map = new pcMapHashBig(maxidx,tbl_relpc_size,tbl_offsets, tbl_stuff, tbl_stuff_size);
#ifdef ASSERT
  assert0( map->get(rel_pcs->at(0)) == unstuff(words_per_stuff,stuff->at(0),0) );
#if 0
for(int i=0;i<rel_pcs->length();i++){
char buf[MaxBytes];
int relpc=rel_pcs->at(i);
    int size = map->fill(buf,relpc);
    if (size > 0) {
      intptr_t tmp = stuff->at(i);
      if (words_per_stuff == 1) {
        assert0( tmp == *((intptr_t*)buf) );
      } else {
        assert0( bcmp(buf, (char*)tmp, ((size+7)>>3)<<3) == 0 );
      }
    }
  }
#endif
#endif
  return map;
}
