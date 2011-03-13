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


#include "bitMap.hpp"
#include "sharedRuntime.hpp"
#include "ciEnv.hpp"
#include "ciMethod.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "commonAsm.hpp"
#include "compiledIC.hpp"
#include "disassembler_pd.hpp"
#include "handles.hpp"
#include "jniHandles.hpp"
#include "methodCodeKlass.hpp"
#include "methodCodeOop.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "pcMap.hpp"
#include "register_pd.hpp"
#include "tickProfiler.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "register_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

static CodeBlob dummy_blob(1.0f);

// Normal constructor used by 99% of all cases.
CommonAsm::CommonAsm(Arena *arena, CodeBlob::BlobType type, int id, const char *const name) : _name(name), _type(type), _compile_id(id), _entry_bci(InvocationEntryBci), _oop_indices(arena,0,0,false) {
  _blob = &dummy_blob;          // No real blob allocated yet; wait for code first.
  _pc = _blob->end();           // Arrange for any attempt to add code to trigger a resize/grow
  _code_ends_data_begins = 999999999;  // No data after code
  _movable = true;              // No absolute addresses handed out yet
  _oopmaps = NULL;              // No oopmaps yet
  _debuginfo = NULL;            // No debug info yet
  _NPEinfo = NULL;              // No PC2PC hardware NPE info yet
  _unpatched_branches = true;
  _deopt_sled_relpc = 0;
}

CommonAsm::CommonAsm(Arena *arena, CodeBlob::BlobType type, int id, int bci, const char *const name) : _name(name), _type(type), _compile_id(id), _entry_bci(bci), _oop_indices(arena,0,0,false) {
  _blob = &dummy_blob;          // No real blob allocated yet; wait for code first.
  _pc = _blob->end();           // Arrange for any attempt to add code to trigger a resize/grow
  _code_ends_data_begins = 999999999;  // No data after code
  _movable = true;              // No absolute addresses handed out yet
  _oopmaps = NULL;              // No oopmaps yet
  _debuginfo = NULL;            // No debug info yet
  _NPEinfo = NULL;              // No PC2PC hardware NPE info yet
  _unpatched_branches = true;
  _deopt_sled_relpc = 0;
}

// Constructor used by vtable stubs, which slowly add more code to an existing CodeBlob.
CommonAsm::CommonAsm(CodeBlob *cb, address pc ) : _name(cb->name()), _type((CodeBlob::BlobType) cb->_type), _compile_id(0), _entry_bci(InvocationEntryBci), _oop_indices(0,false) {
_blob=cb;
  assert0( cb->contains(pc) );
  _pc = pc;
  _code_ends_data_begins = 999999999;  // No data after code
  _movable = false;             // Absolute addresses already handed out
  _oopmaps = NULL;              // No oopmaps 
  _debuginfo = NULL;            // No debug info 
  _NPEinfo = NULL;              // No PC2PC hardware NPE info yet
  _unpatched_branches = true;
  _deopt_sled_relpc = 0;
}

// --- reset_branches
void CommonAsm::reset_branches() {
  assert0( _unpatched_branches == false ); // Called patch_branches!
  _unpatched_branches = true;
_bra_pcs.clear();
_bra_idx.clear();
}

// --- pc
// Return an absolute PC for this label.
address CommonAsm::abs_pc( int idx ) const {
  assert0( _unpatched_branches == false ); // Better have grown/shrunk all prior branches
  return (address)_blob + _bra_pcs.at(idx);
}


// --- grow
// Grow Blob by at least size, via realloc
void CommonAsm::grow_impl( int sz ) {
  if( _blob == &dummy_blob ) {  // Initial allocation?
    assert0( _movable );
    _blob = CodeCache::malloc_CodeBlob(_type, sz);
    _pc = _blob->code_begins();
    return;
  }

  // try to extend blob
  if( CodeCache::extend(_blob,_pc-(address)_blob+sz) ) return; // extended in-place?

  // if not, relocate the blob
  if( !_movable ) {
tty->print_cr("Broken: %s",_blob->name());
    guarantee(_movable,"attempt to move a CodeBlob after handing out an absolute pc");
  }
  CodeBlob * tmp_blob = CodeCache::malloc_CodeBlob(_type, _blob->_size_in_bytes+sz);
  int new_size_in_bytes = tmp_blob->_size_in_bytes;
  memcpy(tmp_blob, _blob, _blob->_size_in_bytes);
  tmp_blob->_size_in_bytes = new_size_in_bytes;
  _pc = (address)tmp_blob + rel_pc();
  CodeCache::free_CodeBlob(_blob);
  _blob = tmp_blob;
}

// --- absolute_pc
// Return a PC which never moves, even if more code is generated.
address CommonAsm::pc() { 
  assert0( _unpatched_branches == false || !has_variant_branches() ); // Better have grown/shrunk all prior branches
  if( _pc >= _blob->end() )     // Already full?
    grow(1);          // Make sure we're not in the initial dummy blob
  _movable = false;             // PC can never be moved again
  return _pc; 
}

// --- block_comment
// Record a comment at the current PC, and spit it back out during decode
void CommonAsm::block_comment( const char *str ) {
  //Unimplemented();
}


const ciMethod *CommonAsm::ZeroImplementors = (ciMethod*)-1;
const ciMethod *CommonAsm::NoFinalizers = (ciMethod*)-2;

// --- add_dependent_impl ----------------------------------------------------
// Add this dependency to the general pile, removing duplicates.
// For each ciInstanceKlass, the matching ciMethods we allow are:
// -- ZeroImplementors and nothing else, XOR
// -- NULL and optionally NoFinalizers, XOR
// -- a bunch of ciMethods and optionally NoFinalizers
// All matching ciInstanceKlasses are stacked together; the optional
// NoFinalizer is at the start of the list.
void CommonAsm::add_dependent_impl(const ciInstanceKlass* cik, const ciMethod* cm) {
  // Compute where to insert, if at all
  int idx = add_dependent_impl_at(cik,cm);
  if( idx >= 0 ) {
    _ciks.insert_at(idx,cik);
    _cms .insert_at(idx,cm );
  }
}

// --- add_dependent_impl_at
int CommonAsm::add_dependent_impl_at(const ciInstanceKlass* cik, const ciMethod* cm) {
  // Because we append at the end and because duplicates are common, we hunt
  // for the matching pile of ciInstanceKlass entries from the back first.
  int idx;
  for( idx= _ciks.length()-1; idx>=0; idx-- )
    if( _ciks.at(idx) == cik )
      break;
  for( ; idx>0; idx-- )         // find start of cik collection
    if( _ciks.at(idx-1) != cik )
      break;
  if( idx == -1 ) return _ciks.length(); // Not found?

  // The actual method dependencies can be expressed in as a regular expression
  // Z - Zero Implementors
  // F - No Finalizers
  // N - No subclassing (null)
  // M - Some named methods
  //
  // First cut out the zero-implementor case.
  // cm can be one of: ZFNM
  // Existing array of dependencies: Z | [F]{N|M*}
  const ciMethod *ocm = _cms.at(idx); // old ciMethod
  if( ocm == ZeroImplementors ) 
    return -1;                  // No other dependencies allowed
  if( cm == ZeroImplementors ) { 
    // New cimethod has zero implementors.  Slap down a Z on the first
    // method in _cms associated with cik, and blow away the rest of the
    // methods associated with cik (the deletion is actually done below)
_cms.at_put(idx,ZeroImplementors);
    idx++;
cm=NULL;
    ocm = ZeroImplementors;
  }

  // Now skip/insert the optional finalizer
  // cm can be one of: FNM
  // Existing array of dependencies: [F]{N|M*}
  if( ocm == NoFinalizers ) {   // Already recorded no-finalizer?
    if( cm == NoFinalizers ) return -1; // ignore dups
    // Do Nothing.  Here 'ocm' is the special NoFinalizers flag and 'cm' is
    // some real dependency (or NULL) but not NoFinalizers.  If we just fall
    // out of here we'll end up in the search loop below.
idx++;//Skip the existing no-finalizers
    if( idx < _ciks.length() && _ciks.at(idx) == cik )
      ocm = _cms.at(idx);       // If we still have deps, pre-load
  }
  if( cm == NoFinalizers ) return idx; // Insert a no-finalizers upfront.

  // Now skip/insert the no-subclassing state
  // cm can be one of: NM
  // Existing array of dependencies: N|M*
  if( ocm == NULL )  return -1; // No subklassing allowed?
  if( cm == NULL ) {            // Existing deps: M*
    while( idx < _ciks.length() && _ciks.at(idx) == cik ) {
_ciks.remove_at(idx);
_cms.remove_at(idx);
    }
return idx;//Insert the null
  }

  // Now skip/insert the a not-overriddable method
  // cm can be one of: M
  // Existing array of dependencies: M*
  while( idx < _ciks.length() && _ciks.at(idx) == cik ) {
    ocm = _cms.at(idx++);       // old ciMethod
    if( ocm == cm ) return -1;  // Exact dup; ignore
  }

  // It's a new cik/cm pair dependency
  return idx;
}

// --- assert_unique_concrete_method -----------------------------------------
// Assert that this method is not overridden in any subklass of cik.  Note
// that the method does can be defined in some parent of cik; it's only
// important that it not get overridden in some subklass of cik.  E.g. If
// class 'Foo' has 4 children and none override Object.equals, then it's ok to
// make Object.equals a final static all for calls to Foo (or below) objects -
// even if Object.equals is already overridden elsewhere.
void CommonAsm::assert_unique_concrete_method(const ciInstanceKlass *cik, const ciMethod*cm) { 
  add_dependent_impl(cik,cm); 
}

// ------------------------------------------------------------------
// CommonAsm::count_call_targets: returns 0, 1 or 2 for many
int CommonAsm::count_call_targets(instanceKlass* current,
methodOop moop,
                                  int            found) {
  
  methodOop target = current->find_method(moop->name(), moop->signature());
  if( target && !target->is_abstract() ) {
    found++;
    if( found > 1 ) return 2;   // Return if this is the 2nd target
    if( target->is_final_method() || target->is_private() )
      return 1; // This method cannot be overridden, so we do not need to check our subclasses.
  }
  
  for( Klass* s = current->subklass(); s; s = s->next_sibling()) {
    if( s->is_interface() ) continue;
    if( !s->oop_is_instance() ) continue;

    found = count_call_targets((instanceKlass*)s, moop, found);
    if( found > 1 ) return 2; // Return if this is the 2nd target
  }
  return found;
}


// --- check_dependencies
// Verify the current dependencies still hold. 
bool CommonAsm::check_dependencies() const {
  // Since nothing implemented (yet), these (vacuuously) never fail
  assert_lock_strong(Compile_lock);

for(int i=0;i<_ciks.length();i++){
    const ciInstanceKlass *cik = _ciks.at(i);
    const ciMethod        *cm  = _cms .at(i);

if(cm!=NULL&&
        cm != ZeroImplementors &&
        cm != NoFinalizers ) {
      assert0( cm->is_method() );
      if (cm->get_methodOop()->is_obsolete()) return false;
      if (cm->get_methodOop()->number_of_breakpoints() > 0) return false;
    }

if(cik==NULL){
assert(cm!=NULL,"garbage dependency");
      return false;
    }

    // This code needs to match unique_concrete_subklass in some sense: if we
    // sharpen a class based on the current hierarchy there, we need to verify
    // that we can still sharpen it now.

    // Interfaces and abstract classes sharpen if they have 0 or 1 implementors
    if( cm == ZeroImplementors ) { 
      if( cik->nof_implementors_query() != 0 ) return false;  // Going from 0 to 1+ implementors?
    } else if( cm == NoFinalizers ) { 
      if( cik->has_finalizable_subclass_query() ) return false; // Found a finalizer?
}else if(cm==NULL){
      if( cik->nof_implementors_query() != 1 ) return false; // Going from 1 to 2+ implementors?
    } else {                    // Check for overriding
      Thread *current_thread = Thread::current();
      methodOop moop = cm->get_methodOop();
instanceKlass*ik=cik->get_instanceKlass();
      // If ik does not already have a method implemented with the same name/sig as moop, then
      // count_call_targets should assume that some super does (because we're NOT ZeroImplementors,
      // which is checked above).  So, start count_call_targets with a count of 1.
      // If find_method does find something, then we'll just give count_call_targets 0 because
      // he'll find it again.
      bool found = ik->find_method(moop->name(), moop->signature());
      if( count_call_targets(cik->get_instanceKlass(), cm->get_methodOop(), found ? 0 : 1) > 1 )
        return false;
    }
  }

  return true;

}

// --- insert_dependencies ---------------------------------------------------
// Insert a dependency on the CodeBlob we are building into all mentioned
// instanceKlasses.
int CommonAsm::insert_dependencies() const {
  const ciInstanceKlass *ocik = NULL;
for(int i=0;i<_ciks.length();i++){
    const ciInstanceKlass *cik = _ciks.at(i);
    if( cik == ocik ) continue; // Ignore dup CIK's
    EXCEPTION_MARK;
    bool success = cik->get_instanceKlass()->add_dependent_codeblob(_blob, THREAD);
    if( !success ) return 2;
    if( HAS_PENDING_EXCEPTION ) { // out-of-memory?
      CLEAR_PENDING_EXCEPTION;
return 1;
    }
    ocik = cik;
  }
  return 0;
}


// --- print_dependencies ----------------------------------------------------
// Nicely print them
void CommonAsm::print_dependencies() const {
outputStream*st=tty;
st->print("Depends on:");
  const ciInstanceKlass *last_cik=NULL;
for(int i=0;i<_ciks.length();i++){
    const ciInstanceKlass *cik = _ciks.at(i);
    if( last_cik != cik ) {
st->print(last_cik?"} ":" ");
cik->print_name_on(st);
st->print(":{");
      last_cik = cik;
    } else {
st->print(", ");
    }
    const ciMethod *cm = _cms.at(i);
if(cm==ZeroImplementors)st->print("NO_IMPL");
else if(cm==NoFinalizers)st->print("NO_FINALS");
else if(cm==NULL){
st->print("SINGLE_IMPL_IS ");
      ciInstanceKlass *uik = cik->raw_implementor();
      if( uik != (ciInstanceKlass*)-2 ) { // should be cached by now
if(uik==cik)st->print("SELF");
        else             uik->print_name_on(st);
      } else {
st->print("??? (not cached yet)");
      }
    } else {
st->print("NO_OVERRIDING ");
cm->name()->print_symbol_on(st);
    }
    
  }
if(last_cik)st->print("}");
  st->cr();
}

// --- add_oop
// Add an Oop at this rel_pc to the OopMap.  Mostly just sets a bit in a bitvector.
//
// NOTE: If you are using this interface, you are using a default constructed 
// OopMapBuilder(), which limits you to the first 64 oop locations in our encoding. 
void CommonAsm::add_oop( int rel_pc, VOopReg::VR reg ) {
  if( !_oopmaps ) _oopmaps = new OopMapBuilder();
  _oopmaps->add_mapping( rel_pc, reg );
}

// --- add_empty_oopmap
// Record lack-of-oops here, mostly for asserts.
void CommonAsm::add_empty_oopmap( address abs_pc ) {
  if( !_oopmaps ) _oopmaps = new OopMapBuilder();
  _oopmaps->add_empty( abs_pc-(address)blob() );
}

// --- add_empty_oopmap
// Record lack-of-oops here, mostly for asserts.
void CommonAsm::add_empty_oopmap( int rel_pc ) {
  if( !_oopmaps ) _oopmaps = new OopMapBuilder();
  _oopmaps->add_empty( rel_pc );
}

// --- add_oopmap ------------------------------------------------------------
class AddOopClosure:public BitMapClosure{
 private:
   const int _rel_pc;
   CommonAsm *_casm;
 public:
  AddOopClosure(int rel_pc, CommonAsm *casm) : _rel_pc(rel_pc), _casm(casm) {}

  // Callback when bit in map is set
  virtual void do_bit(size_t offset) {
    _casm->add_oop(_rel_pc, (VOopReg::VR)offset);
  }
};

// Add a collection of oops to the OopMap
void CommonAsm::add_oopmap( int rel_pc, const OopMap2 *map ) {
  const BitMap *d = &map->_data;
  if( !_oopmaps ) _oopmaps = new OopMapBuilder(d->size());
if(d->is_empty()){
    _oopmaps->add_empty( rel_pc );
  } else {
    AddOopClosure oop_adder(rel_pc, this);
    d->iterate(&oop_adder);
  }
}

// -- add_dbg ----------------------------------------------------------------
// Add a DebugScope at this PC.  Mostly just record this in a pc->scope mapping.
void CommonAsm::add_dbg( int rel_pc, const DebugScopeBuilder *dbgscope ) {
  if( !_debuginfo ) _debuginfo = new DebugInfoBuilder();
  _debuginfo->add_dbg(rel_pc, dbgscope);
}

// -- add_implicit_exception -------------------------------------------------
// Add a DebugScope at this PC.  Mostly just record this in a pc->scope mapping.
void CommonAsm::add_implicit_exception( int faulting_pc, int handler_pc ) {
  if( !_NPEinfo ) _NPEinfo = new PC2PCMapBuilder();
  _NPEinfo->add_mapping(faulting_pc, handler_pc);
}

// --- record_constant_oop ---------------------------------------------------
// Record constant oops mentioned by this CodeBlob, to keep them alive
// until baking into a methodCode
void CommonAsm::record_constant_oop( int idx ) {
  assert0(idx);
  // have we already recorded the idx?
int len=_oop_indices.length();
  for( int i=0; i<len; i++ ) 
    if (_oop_indices.at(i) == idx) 
      return;
_oop_indices.push(idx);
}

// --- Install Code ----------------------------------------------------------
// Make a methodCodeOop.  Stuff the blob into it.  Compress the bulky
// PCMapBuilder forms and stuff them into it.  Reset all innnards; prevent the
// masm from being used again.  Returns NULL on the rare out-of-memory
// (bleah!).  Lifetime of the CodeBlob is now tied to the lifetime of the 
// methodCodeOop - so better keep it live across any GC points!
methodCodeRef CommonAsm::bake_into_codeOop(methodRef mref, int framesize_bytes, const CodeProfile *profile, bool has_unsafe, TRAPS) {
  if( _unpatched_branches ) patch_branches(); // patch all branch offsets; might grow code

  // Compress and attach the oopmap to the blob
  _blob->_oop_maps = _oopmaps ? _oopmaps->build_regMap() : NULL;
  _blob->_framesize_bytes = framesize_bytes;
  HotspotToGdbSymbolTable.symbolsAddress[_blob->_gdb_idx].frameSize = framesize_bytes;
  _blob->_code_len = _pc - _blob->code_begins();
  if( _code_ends_data_begins < 999999999 )
    _blob->_non_code_tail = rel_pc() - _code_ends_data_begins;
  // Compress and attach other stuff to the codeOop
  methodCodeKlass* mck = methodCodeKlass::cast(Universe::methodCodeKlassObj());
  const DebugMap* dbg = NULL;
  if( _debuginfo ) dbg   = _debuginfo->build_debugMap(this);
  const PC2PCMap *pc2pc = NULL;
  if( _NPEinfo   ) pc2pc = _NPEinfo  ->build_PC2PCMap();
  // Build the list of dependencies
  HandleMark hm;
objArrayHandle dep_klasses;
objArrayHandle dep_methods;
objArrayHandle s_refs;
  methodHandle mh(mref);        // survive GC during allocations below
if(_ciks.length()>0){
    dep_klasses = oopFactory::new_objectArray(_ciks.length(),false, CHECK_(methodCodeRef((uint64_t)0)));
    dep_methods = oopFactory::new_objectArray(_ciks.length(),false, CHECK_(methodCodeRef((uint64_t)0)));
    objArrayOop dk = dep_klasses();
    objArrayOop dm = dep_methods();
for(int i=0;i<_ciks.length();i++){
      const ciMethod *cm = _cms.at(i);
      objectRef mref = (cm == ZeroImplementors) 
        ? methodCodeOopDesc::zeroImplementors() 
        : ((cm == NoFinalizers) ? methodCodeOopDesc::noFinalizers() : (cm ? cm->get_ref() : nullRef));
      dk->ref_at_put(i,_ciks.at(i)->get_ref());
      dm->ref_at_put(i,mref);
    }
  }
if(_oop_indices.length()>0){
    s_refs = oopFactory::new_objectArray(_oop_indices.length() ,false, CHECK_(methodCodeRef((uint64_t)0)));
    objArrayOop sr = s_refs();
    for( int i=0; i<_oop_indices.length(); i++ )
      sr->ref_at_put(i, CodeCacheOopTable::getOopAt(_oop_indices.at(i)));
  }
  methodCodeOop    mco = mck->allocate(_blob, dbg, pc2pc, profile, dep_klasses, dep_methods, _compile_id, _entry_bci, has_unsafe, s_refs, CHECK_(methodCodeRef((uint64_t)0)));
  methodCodeRef    mcr = mco;
  _blob->set_owner(mcr);
  mcr.as_methodCodeOop()->set_method(mh.as_ref());
  mco->_deopt_sled_relpc = _deopt_sled_relpc;

  // GDB frame name support
  const char *n = _blob->methodname();
  if( n ) {
    const char *s = strdup(n);  // LEAK:  but hsgdb table entries are seldom if ever removed.
    HotspotToGdbSymbolTable.symbolsAddress[_blob->_gdb_idx].nameAddress = s;
    HotspotToGdbSymbolTable.symbolsAddress[_blob->_gdb_idx].nameLength = strlen(s);
  }

  // Other PCMapBuilder stuff goes here

  return mcr;
}

// Used only on startup - before Universe is up and we can make CodeOops at
// all.  Basically, used by the stub-gen stuff.
void CommonAsm::bake_into_CodeBlob(int framesize_bytes) {
  patch_branches();             // patch all branch offsets; might grow code
  // Compress and attach the oopmap to the blob
  _blob->_oop_maps = _oopmaps ? _oopmaps->build_regMap() : NULL;
  _blob->_framesize_bytes = framesize_bytes;
  if( _blob->_gdb_idx != 0xffff ) {
    HotspotToGdbSymbolTable.symbolsAddress[_blob->_gdb_idx].frameSize = framesize_bytes;
  }
  _blob->_code_len = _pc - _blob->code_begins();
  // No other stuff can go in a blob
}

// --- set_code_start
// Set the code start location to the current pc; used in case the
// code is aligned past the CodeBlob's earliest starting point.
void CommonAsm::set_code_start() {
  _blob->_code_start_offset = _pc-_blob->_code_begins;
  assert0( _blob->code_begins() == _pc );
}

// --- decode
void CommonAsm::decode(outputStream*st)const{
  Disassembler::decode(_blob->code_begins(), _pc, st); 
}

// --- print
void CommonAsm::print(outputStream*st)const{
decode(st);
  if( _debuginfo ) _debuginfo->print(_blob,st);
  if( _NPEinfo   ) _NPEinfo  ->print(_blob,st);
}


//=============================================================================
// --- add_jmp
// Add a jump to this Label's linked list
void Label::add_jmp( CommonAsm *_asm ) {
  // If the label is bound, then the branch refers to it directly.
  // If the label is not bound, we do a linked-list insert
int idx=_idx;
  if( idx == CommonAsm::EOL || _asm->_bra_idx.at(idx) != CommonAsm::LABEL ) { // unbound label
    // linked list insert
    _idx = _asm->_bra_idx.length(); // new index
  }

  _asm->_bra_pcs.push( _asm->rel_pc() );
  _asm->_bra_idx.push( idx );
}

// --- bind
void Label::bind( CommonAsm *_asm, int rel_pc ) {
  assert0( _idx == CommonAsm::EOL || _asm->_bra_idx.at(_idx) != CommonAsm::LABEL ); // not already bound
  int bidx = _asm->_bra_pcs.length(); // Label will be bound at this index
  // Follow the linked-list, patching all branches
int idx=_idx;
  while( idx != CommonAsm::EOL ) {
    const int link = _asm->_bra_idx.at(idx);
    _asm->_bra_idx.at_put(idx,bidx); // make the jump refer to the label
    idx = link;
  }
  // Bind the label to here
  _idx = bidx;
  _asm->_bra_pcs.push( rel_pc );
  _asm->_bra_idx.push( CommonAsm::LABEL );    // bound-label indicator
}

// --- is_bound
bool Label::is_bound(CommonAsm *_asm) const {
  return _idx != -1 && _asm->_bra_idx.at(_idx) == CommonAsm::LABEL;
}

//=============================================================================
// --- add_mapping
// Add a pc-to-stuff mapping.  Dup's not allowed.
void PCMapBuilder::add_mapping( int rel_pc, intptr_t stuff ) {
  assert0( rel_pc == InvocationEntryBci || (rel_pc >=0 && rel_pc <= 1024*1024) ); // sane relative-pc
  assert0( _words_per_stuff == 1 || stuff == 0 );
  if( !_rel_pcs ) {
    _rel_pcs = new GrowableArray<intptr_t>();
    _stuff   = new GrowableArray<intptr_t>();
  }

#ifdef ASSERT
  // Sanity check: no dup PC's
for(int i=0;i<_rel_pcs->length();i++)
    assert0( _rel_pcs->at(i) != rel_pc );
#endif // ASSERT
  _rel_pcs->push(rel_pc);
  _stuff  ->push(stuff );
}


// --- get_mapping
// Load from the mapping (used during compilation; no failures allowed).
intptr_t PCMapBuilder::get_mapping( int rel_pc ) const {
  return _stuff->at(_rel_pcs->find(rel_pc));
}

// --- add_bit
// Add a pc-to-bit mapping.  Dup PC's ARE allowed & expected.  Dup bits are not allowed.
void PCMapBuilder::add_bit( int rel_pc, int reg ) { 
  assert0( rel_pc == InvocationEntryBci || (rel_pc >=0 && rel_pc <= 2*1024*1024) ); // sane relative-pc
  if( !_rel_pcs ) {
    _rel_pcs = new GrowableArray<intptr_t>();
    _stuff   = new GrowableArray<intptr_t>();
  }

  assert( reg >= 0 && reg < (_words_per_stuff * BitsPerWord), "out of bounds");

  // Find (or make) the rel_pc entry.
  if( _cached_idx >= _rel_pcs->length() || _rel_pcs->at(_cached_idx) != rel_pc ) {
    int idx;
    for( idx=0; idx<_rel_pcs->length(); idx++ )
      if( _rel_pcs->at(idx) == rel_pc ) // found it?
        break;
    if( idx >= _rel_pcs->length() ) { // Not found, so enter it
      _rel_pcs->push(rel_pc);
      _stuff  ->push(     0);
    }
    _cached_idx = idx;          // Record cache for next time
  }

  // Insert bit into bitmap
  if (_words_per_stuff == 1) {
    intptr_t bits = _stuff->at(_cached_idx);
    intptr_t mask = 1L << reg;
    assert0( (bits & mask)==0 );  // no dup bits
    _stuff->at_put(_cached_idx,bits|mask);
  } else {
    intptr_t* words = (intptr_t*)_stuff->at(_cached_idx);
    if (words == 0) {
      words = NEW_RESOURCE_ARRAY(intptr_t, _words_per_stuff);
      memset(words, 0, _words_per_stuff << LogBytesPerWord);
      _stuff->at_put(_cached_idx,(intptr_t)words);
    }
    intptr_t  mask  = 1L << (reg % BitsPerWord);
    assert0( (words[reg >> LogBitsPerWord] & mask)==0 );  // no dup bits
    words[reg >> LogBitsPerWord] |= mask;
  }
}

// --- bits_per_stuff
// A little math on the bitvectors - how big a span do they cover?
int PCMapBuilder::bits_per_stuff() const {
  if (_words_per_stuff == 1) {
    uint64_t maxbits = 0;
for(int i=0;i<_stuff->length();i++)
      if( (uint64_t)(_stuff->at(i)) > maxbits )
        maxbits = (uint64_t)(_stuff->at(i));
    return log2_long(maxbits)+1;
  } 

  assert0(sizeof(uint64_t) == sizeof(intptr_t));
  uint64_t maxbits = 0;
  uint64_t maxword = 0;
for(int i=0;i<_stuff->length();i++){
    uint64_t *words = (uint64_t*)_stuff->at(i);
    if( !words ) continue;
for(int j=maxword+1;j<_words_per_stuff;j++){
if(words[j]!=0){
        maxbits = words[j];
maxword=j;
      }
    }
    if( words[maxword] > maxbits )
      maxbits = words[maxword];
  }
  return (maxword*BitsPerWord) + log2_long(maxbits)+1;
}

// --- build_pcMap
// Build a pcMap.  Empties the contents of this PCMapBuilder.
// The built structure needs to be fairly compact - we'll be making loads of these.
// It also needs to support fast lookup!
pcMap *PCMapBuilder::build_pcMap() {
  GrowableArray<intptr_t> *relpcs = _rel_pcs;
  GrowableArray<intptr_t> *stuffs = _stuff;

if(relpcs==NULL)return NULL;

  // A little math on the (relative) PC's - how big a span do they cover?
  // We already know they are between 0 <= rel_pc <= 200000 (asserted for)
  // In the case they are holding BCIs they are > InvocationEntryBci and
  // < 63336 in which case this math is redundant but still correct.
  int maxpc = 0;
for(int i=0;i<relpcs->length();i++){
int relpc=relpcs->at(i);
    if( relpc > maxpc ) maxpc = relpc;
  }

  // Go to the specific child classes and get a bits-per-stuff report.
  int bps = bits_per_stuff();

  // Clean out guts so we don't call build_pcMap twice
  assert0( !_rel_pcs->allocated_on_C_heap() ); // no leaks!
  _rel_pcs = NULL;      // wipe out so we don't call build_pcMap twice
_stuff=NULL;

  // Decide if we would like a 16-key-bits and 16-stuff-bits hashtable.
  if( maxpc < (1<<16) && bps <= 16 ) return pcMapHash16x16::make(relpcs,stuffs,_words_per_stuff);

  // Decide if we would like a 16-key-bits and 32-stuff-bits hashtable.
  if( maxpc < (1<<16) && bps <= 32 ) return pcMapHash16x32::make(relpcs,stuffs,_words_per_stuff);

  // Decide if we would like a 16-key-bits and 64-stuff-bits hashtable.
  if( maxpc < (1<<16) && bps <= 64 ) return pcMapHash16x64::make(relpcs,stuffs,_words_per_stuff);

  // Use variable length encoding
  return pcMapHashBig::make(relpcs,stuffs,_words_per_stuff);
}

// --- print
void PCMapBuilder::print( const CodeBlob *blob, outputStream *st ) const {
  if( !st ) st = tty;
  if( !_rel_pcs ) return;
st->print_cr("%s: {",_name);
for(int i=0;i<_rel_pcs->length();i++){
    if( blob ) st->print(     "  %p - ",_rel_pcs->at(i)+(address)blob);
    else       st->print("  +%4.4ld - ",_rel_pcs->at(i));
    stuff_print_on(st,_stuff->at(i));
st->cr();
  }
st->print_cr("}");
}

// --- get_sole_lock
// Get the unique sole lock in this oopmap
VOopReg::VR RegMap::get_sole_oop( int rel_pc ) const {
  intptr_t mask = _regs->get(rel_pc);
for(int i=0;i<64;i++)
    if( (1LL<<i) == mask )      // explode unless there is exactly 1 lock (and we find it)
      return VOopReg::VR(i);
  ShouldNotReachHere();
return VOopReg::Bad;
}

// --- do_closure
void RegMap::do_closure( int rel_pc, frame fr, OopClosure *f ) const {
  objectRef* sp = (objectRef*)fr.sp();
  // Oopmaps are stored with a relative-to-Blob pc offset
  intptr_t oopmap = _regs->get(rel_pc);
  assert0( oopmap != NO_MAPPING );
  char buf[pcMap::MaxBytes];
  intptr_t *w= (intptr_t*)buf;
  w[0] = 0;                     // zero 1st word always, so no need to check len for registers

  int len = _regs->fill(buf,rel_pc);
  assert0( len != NO_MAPPING );

  if( w[0] != 0 ) {
for(int i=0;i<REG_OOP_COUNT;i++)
      if( (1LL<<i) & w[0] ) 
        f->do_oop((objectRef*)fr.pd_reg_to_addr(VReg::VR(i)));

    for( int i=REG_OOP_COUNT; i<64; i++ )
      if( (1LL<<i) & w[0] )
        f->do_oop(&sp[i-REG_OOP_COUNT]);
  }
  
  assert0( len < 8 || (len>>3)<<3 == len );
  for( int j = 1; j*8 < len; j++ ) {
    intptr_t oopmap = w[j];
    if( oopmap ) 
for(int i=0;i<64;i++)
        if( (1LL<<i) & oopmap )
          f->do_oop(&sp[i-REG_OOP_COUNT+j*64]);
  }
}

// --- is_oop 
// Test a single register for being an oop
bool RegMap::is_oop( int rel_pc, VOopReg::VR vreg ) const {
  intptr_t oopmap = _regs->get(rel_pc);
  assert0( oopmap != NO_MAPPING );
  char buf[pcMap::MaxBytes];
  intptr_t *w= (intptr_t*)buf;
  w[0] = 0;                     // zero 1st word always, so no need to check len for registers
  const int len = _regs->fill(buf,rel_pc);
  assert0( len != NO_MAPPING );
  assert0( (int)vreg < len*8 );
  return w[vreg>>6] & (1LL<<(vreg&63));
}

// --- print
// Print all register maps
void RegMap::print(const CodeBlob *blob, outputStream *st) const {
  if( !st ) st = tty;
st->print_cr("{");
  for( int i=0; i<_regs->_maxidx; i=_regs->next_idx(i) ) {
    st->print("  ");
    int pc = _regs->get_relpc(i);
    if( pc == NO_MAPPING ) continue;
    if( blob ) st->print(  "  %p: ", (address)blob+pc);
else st->print("  +%4d: ",pc);
print(pc,st);
st->print(",");
  }
st->print_cr("}");
}

// --- print
// Print as a collection of registers
void RegMap::print(int rel_pc, outputStream *st) const {
  char oopmask[pcMap::MaxBytes];
  int bytes = _regs->fill(oopmask, rel_pc);
  assert0 (bytes <= pcMap::MaxBytes);
  if( bytes != NO_MAPPING ) {
    int bit=0;
    if( !st ) st = tty;
st->print(_name);
st->print("< ");
for(int i=0;i<bytes;i++){
for(int j=0;j<8;j++){
        if( (1<<j) & oopmask[i] ) {
          if (bit < REG_OOP_COUNT) {
            st->print("%s ",raw_reg_name((Register)bit));
          } else {
            st->print("[SP+%d] ",(bit-REG_OOP_COUNT)<<3);
          }
        }
        bit++;
      }
    }
st->print_cr(">");
  }
}

//=============================================================================
// --- stuff_print_on
void RegMapBuilder::stuff_print_on( outputStream *st, intptr_t stuff ) const {
  if( st==NULL ) st = tty;
  if( stuff == 0 ) { st->print("nothing"); return; }
for(int i=0;i<64;i++)
    if( ((1LL<<i) & stuff) != 0 )
      st->print(raw_reg_name((Register)i));
}

//=============================================================================
// --- print -----------------------------------------------------------------
void OopMap2::print(outputStream*st){
#ifndef PRODUCT
_data.print_on(st);
  if( _base_derived_pairs.is_nonempty() ) 
    Unimplemented();
  if( _callee_save_pairs.is_nonempty() ) 
    Unimplemented();
#endif
}

// --- add_derived_oop -------------------------------------------------------
void OopMap2::add_derived_oop( VReg::VR base, VReg::VR derived ) {
  assert0( base != derived );   // whats the point???
  _base_derived_pairs.push(base);
  _base_derived_pairs.push(derived);
}

// --- add_callee_save_pair --------------------------------------------------
void OopMap2::add_callee_save_pair( VReg::VR src, VReg::VR dst ) {
  assert0( src != dst );   // whats the point???
  _callee_save_pairs.push(src);
  _callee_save_pairs.push(dst);
}


//=============================================================================
// --- count locks
// Count instances of this object
int DebugScope::count_locks( const DebugMap *dm, const frame fr, const oop o ) const {
  int cnt = 0;
  if( _caller ) cnt += _caller->count_locks(dm,fr,o);
  if( !numlocks() ) return cnt;
  // count_locks is often called remotely; from one thread trying to count the
  // locks on another thread as part of a bias-revoke.  Hence the thread being
  // counted is not "JavaThread::current"; instead we grab the thread from the
  // remote frame being counted.
  assert0( Thread::stack_ptr_to_Thread(fr.sp())->is_Java_thread() );
for(uint i=0;i<numlocks();i++)
    if( o == objectRef(dm->get_value(get_lock(i),fr)).as_oop() )
      cnt++;
  return cnt;
}

//=============================================================================
// --- get_local
// Get the named local in this debug scope
DebugScopeValue::Name DebugScope::get_local( unsigned int num ) const {
  assert0( num < (unsigned int)method().as_methodOop()->max_locals() );
return _regs[num];
}

// --- get_expr
// Get the named local in this debug scope
DebugScopeValue::Name DebugScope::get_expr( unsigned int num ) const {
  methodOop moop = method().as_methodOop();
int mlocals=moop->max_locals();
  assert0( num < (unsigned int)_stk );
  int j = mlocals+num;
  return _regs[j];
}

// --- get_lock
// Get the named lock in this debug scope
DebugScopeValue::Name DebugScope::get_lock( unsigned int num ) const {
  assert0( num < (unsigned int)_maxlocks );
  methodOop moop = method().as_methodOop();
int mlocals=moop->max_locals();
  int j = mlocals+_stk+num;
  assert0( DebugScopeValue::is_valid(_regs[j]) );
  return _regs[j];
}

// --- gc_base_derived -------------------------------------------------------
// GC any base/derived pairs here
void DebugScope::gc_base_derived( frame fr, OopClosure *f ) const {
  if( UseGenPauselessGC ) return;
  if( _base_derived_pairs == 0 ) return;
  MutexLocker dptgc(DerivedPointerTableGC_lock);

  // See if this address corresponds to any debug info.  Concatenate the
  // debug info, in case there's more than one use of the same word.
  methodOop moop = method().as_methodOop();
  uint mlocals = moop->max_locals();
  uint len = mlocals + _stk + _maxlocks + (_callee_save_pairs<<1);

  for( uint i=len; i<len+(_base_derived_pairs); i+=2 ) {
    VReg::VR base = DebugScopeValue::to_vreg(_regs[i+0]);
    VReg::VR deri = DebugScopeValue::to_vreg(_regs[i+1]);
    objectRef *ptr0 = (objectRef*)fr.reg_to_addr(base);
    objectRef *ptr1 = (objectRef*)fr.reg_to_addr(deri);
DerivedPointerTable::add(ptr0,ptr1);
  }
}

// --- restore_callee_saves --------------------------------------------------
// Restore callee-save registers using the debug info from a compiled C1 or C2
// frame and store them into a call_VM_compiled frame.  When the runtime stub
// frame unwinds the callee-save registers will get restored.
void DebugScope::restore_callee_saves(frame fr, frame stub) const {
  assert0( CodeCache::find_blob(stub.pc())->framesize_bytes() == frame::runtime_stub_frame_size );
  methodOop moop = method().as_methodOop();
  uint idx = moop->max_locals() + _stk + _maxlocks;
  intptr_t saved[REG_COUNT];

for(uint i=0;i<_callee_save_pairs;i++){
    VReg::VR reg = DebugScopeValue::to_vreg(_regs[idx+(i<<1)+0]); // the callee-save register
    assert0( reg < REG_COUNT );
    VReg::VR stk = DebugScopeValue::to_vreg(_regs[idx+(i<<1)+1]); // where it was saved on the stack
    intptr_t *ptr0 = fr.reg_to_addr(stk);                         // read from JIT'd frame & save
    saved[reg] = *ptr0;
  }
for(uint i=0;i<_callee_save_pairs;i++){
    VReg::VR reg = DebugScopeValue::to_vreg(_regs[idx+(i<<1)+0]); // the callee-save register
    intptr_t *ptr1 = fr.reg_to_addr(reg);
    *ptr1 = saved[reg]; // read from saved, write to the stub frame
  }
}


// --- find_handler
// find an exception-handler rel_pc for this bci
int DebugScope::find_handler( int bci ) const {
  if( !_bci2pc_map ) return NO_MAPPING;
  return _bci2pc_map->get(bci);
}

// --- print_compiler_info
void DebugScope::print_compiler_info( VReg::VR vreg, char *buf ) const {
  // See if this address corresponds to any debug info.  Concatenate the
  // debug info, in case there's more than one use of the same word.
  methodOop moop = method().as_methodOop();
  uint mlocals = moop->max_locals();
  uint len = mlocals + _stk + _maxlocks;

  for( uint i=0; i<len; i++ ) {
    DebugScopeValue::Name v = _regs[i];
    if( DebugScopeValue::is_vreg(v) && DebugScopeValue::to_vreg(v) == vreg ) {
      ResourceMark rm;
      buf += sprintf(buf,"{%s@%d ", moop->name()->as_C_string(), _bci);
      if( i<mlocals ) buf += sprintf(buf,"JL%d} ",(i-0)); 
      else if( i<mlocals+_stk ) buf += sprintf(buf,"JES%d} ",(i-mlocals)); 
      else buf += sprintf(buf,"LCK%d}",(i-mlocals-_stk));
    }
  }

  for( uint i=len; i<len+(_callee_save_pairs<<1); i+=2 ) {
    DebugScopeValue::Name v = _regs[i+1];
    if( DebugScopeValue::is_vreg(v) && DebugScopeValue::to_vreg(v) == vreg ) {
      buf += sprintf(buf,"{saved_%s}",raw_reg_name((Register)DebugScopeValue::to_vreg(_regs[i+0])));
    }
  }
  len += (_callee_save_pairs<<1);

  for( uint i=len; i<len+(_base_derived_pairs); i+=2 ) {
    DebugScopeValue::Name deri = _regs[i+1];
    assert0( DebugScopeValue::is_vreg(deri) );
    if( DebugScopeValue::to_vreg(deri) == vreg ) {
      buf += sprintf(buf,"{derived_from_");
      VReg::VR base = DebugScopeValue::to_vreg(_regs[i+0]);
      if( VReg::is_stack(base) ) buf += sprintf(buf,"[rsp+%d]",VReg::reg2stk(base));
      else buf += sprintf(buf,"%s",raw_reg_name(reg2gpr(base)));
      buf += sprintf(buf,"}");
    }
  }
  len += (_base_derived_pairs<<1);

  if( _caller ) _caller->print_compiler_info(vreg,buf);
}

// --- print_value
void DebugScope::print_value( DebugScopeValue::Name rname, const DebugMap *dm, outputStream *st ) const {
  assert0( DebugScopeValue::is_valid(rname) );
  if( DebugScopeValue::is_vreg(rname) ) {
    VReg::print_on(DebugScopeValue::to_vreg(rname),st);
    st->print(DebugScopeValue::is_vreg_8byte(rname) ? ".8 " : ".4 ");
  } else {
    int index = DebugScopeValue::to_index(rname);
if(dm==NULL){
st->print("const_table[%d] ",index);
    } else {
      intptr_t val = dm->get_value(rname, frame(NULL,NULL));
      if( DebugScopeValue::is_const_oop(rname) ) {
st->print("oop[");
objectRef ref=objectRef(val);
        ref.as_oop()->print_value_on(st);
st->print("] ");
      } else {
st->print("int["INTPTR_FORMAT"] ",val);
      }
    }
  }
}

// --- print
void DebugScope::print( const DebugMap *dm, outputStream *st ) const {
  if( st==NULL ) st = tty;
  if( _caller ) _caller->print(dm,st);
  st->print("{");
  ResetNoHandleMark rm;
  HandleMark hm;
  methodOop moop = method().as_methodOop();
moop->print_name(st);

st->print(":%d",_bci);
if(_reexecute)st->print(" reexecute_bci");
if(_extra_lock)st->print(" extra_lock");
if(_inline_cache)st->print(" IC");
  const uint mlocals = moop->max_locals();
  int j = 0;

for(uint i=0;i<mlocals;i++){
    DebugScopeValue::Name rname = _regs[j+i];
st->print(" JL%d=",i);
    if( DebugScopeValue::is_valid(rname) ) {
      print_value(rname, dm, st);
    } else {
st->print("_ ");
    }
  }
  j += mlocals;

for(uint i=0;i<_stk;i++){
    DebugScopeValue::Name rname = _regs[j+i];
st->print(" JSTK%d=",i);
    if( DebugScopeValue::is_valid(rname) ) {
      print_value(rname, dm, st);
    } else {
st->print("_ ");
    }
  }
  j += _stk;

for(uint i=0;i<_maxlocks;i++){
    DebugScopeValue::Name rname = _regs[j+i];
st->print(" LCK%d=",i);
    if( DebugScopeValue::is_valid(rname) ) {
      print_value(rname, dm, st);
    } else {
st->print("_ ");
    }
  }
  j += _maxlocks;

for(uint i=0;i<_callee_save_pairs;i++){
    if( !DebugScopeValue::is_valid(_regs[j+(i<<1)]) ) continue;
    VReg::VR src = DebugScopeValue::to_vreg(_regs[j+(i<<1)+0]);
    VReg::VR dst = DebugScopeValue::to_vreg(_regs[j+(i<<1)+1]);
    st->print(" ");
    VReg::print_on(src,st);
st->print("_saved@_");
    VReg::print_on(dst,st);
  }
j+=(_callee_save_pairs<<1);

for(uint i=0;i<_base_derived_pairs;i+=2){
    VReg::VR base = DebugScopeValue::to_vreg(_regs[j+i+0]);
    VReg::VR deri = DebugScopeValue::to_vreg(_regs[j+i+1]);
    st->print(" ");
    VReg::print_on(base,st);
st->print("_base_for_derived@_");
    VReg::print_on(deri,st);
  }
  j += (_base_derived_pairs);

  if( _bci2pc_map ) {           // Any exception handler mappings?
st->cr();
st->print(" exception_handler_bci to rel_pc:");
    _bci2pc_map->print(NULL,st);
  }
st->print("} ");
}

// --- print_inline_tree    
// short printout in tree format
void DebugScope::print_inline_tree(outputStream*st)const{
  methodOop moop = method().as_methodOop();
moop->print_name(st);
st->print(":%d",_bci);
if(_caller)st->print(" called by");
  st->cr();
  if( _caller ) _caller->print_inline_tree(st);
}

//=============================================================================
// --- clean_inline_caches ---------------------------------------------------
void DebugMap::clean_inline_caches(const CodeBlob *cb) const {
  for( int i=0; i<_info->_maxidx; i= _info->next_idx(i) ) {
    int pc = _info->get_relpc(i);
    if( pc == NO_MAPPING ) continue;
    if( !NativeCall::is_call_before((address)cb+pc) ) continue;
    NativeCall *call = nativeCall_before((address)cb+pc);
    if( call->destination() == StubRoutines::resolve_and_patch_call_entry() ) 
      continue;
    CodeBlob *dst = CodeCache::find_blob(call->destination());
    if( !dst ) continue;        // clean ICs have no destination 
    if( dst->owner().is_null() ) continue;
    // Clean up the call to not call the target: allow dead targets to be GCd
    const DebugScope *ds = get(pc);
    if( ds->is_inline_cache() ) {
      CompiledIC::make_before((address)cb+pc)->set_clean();
    } else {
      // Standard static calls can just get re-resolved
      call->set_destination_mt_safe( StubRoutines::resolve_and_patch_call_entry() );
      cb->owner().as_methodCodeOop()->patched_a_call_to((address)cb+pc,NULL);
    }
  }
}


// --- get_value -------------------------------------------------------------
// Support for constants in debug info.  This routine does a generic load of a
// generic value referred to by DebugScopeValue::name (probably pulled out from
// some DebugScope in this CodeBlob).  If the DebugScopeValue::name can refer to
// a debug constant which will be returned instead.
intptr_t DebugMap::get_value( const DebugScopeValue::Name reg, const frame fr ) const {
  // Common case of getting a value known to be dead by the JIT
  intptr_t result;
  if( !DebugScopeValue::is_valid(reg) ) {
    result = 0;
  } else if( DebugScopeValue::is_vreg(reg) ) {
    result = *fr.reg_to_addr(DebugScopeValue::to_vreg(reg));
    if (!DebugScopeValue::is_vreg_8byte(reg)) {
      // clear top 4 bytes if not an 8byte value (to avoid bad oop injection)
      result &= 0x00000000FFFFFFFF;
    }
  } else {
    int index = DebugScopeValue::to_index(reg);
assert(index>=0&&index<_cons_len,"range check");
    if( DebugScopeValue::is_const_oop(reg) ) { // oop constant
      int oop_index = _cons[index];
      assert0( CodeCacheOopTable::is_valid_index(oop_index) );
      objectRef ref = CodeCacheOopTable::getOopAt(oop_index);
      result = ref.raw_value();
    } else {                                 // int constant
      result = _cons[index];
    }
  }
  return result;
}

#ifdef ASSERT
// --- verify_value
void DebugMap::verify_value( DebugScopeValue::Name vreg ) const {
  // Verify an individual vreg in the debug map
  if( !DebugScopeValue::is_valid(vreg) || DebugScopeValue::is_vreg(vreg) ) {
    // do nothing for dead, stack or register values (they need a live frame)
  } else {                   // Compile-time constant
    int index = DebugScopeValue::to_index(vreg);
assert(index>=0&&index<_cons_len,"range check");
    if( !DebugScopeValue::is_const_oop(vreg) ) {  // int constant - no check
    } else {                // oop constant - check
      int oop_index = _cons[index];
      assert0( CodeCacheOopTable::is_valid_index(oop_index) );
      objectRef ref = CodeCacheOopTable::getOopAt(oop_index);
      assert0( !ref.is_null() );
      SharedRuntime::verify_oop(ref);
    }
  }
}

// --- verify
void DebugMap::verify()const{
  int num_ds = tablesize();
  for( int cur_ds=0; cur_ds < num_ds; cur_ds=_info->next_idx(cur_ds) ) {
    int relpc =  _info->get_relpc(cur_ds);
    if (relpc == NO_MAPPING) continue;
    const DebugScope *ds = get(relpc);
if(ds==NULL)continue;
    int numlocals = ds->numlocals();
for(int i=0;i<numlocals;i++){
      DebugScopeValue::Name vreg = ds->get_local(i);
      verify_value(vreg);
    }
    int numstack = ds->numstk();
for(int i=0;i<numstack;i++){
      DebugScopeValue::Name vreg = ds->get_expr(i);
      verify_value(vreg);
    }
    int numlocks = ds->numlocks();
for(int i=0;i<numlocks;i++){
      DebugScopeValue::Name vreg = ds->get_lock(i);
      verify_value(vreg);
    }
  }
}
#endif

// --- print
void DebugMap::print( int rel_pc, outputStream *st ) const {
  const DebugScope *scope = get(rel_pc);
  if( (intptr_t)scope == NO_MAPPING ) return;
  if( !st ) st = tty;
st->print("DebugMap< ");
  scope->print(this, st);
st->print_cr(">");
}

//=============================================================================
// --- build_debugMap
// Build a compressed-form of this DebugInfoBuilder.  Empties this struct; all
// DebugScopeBuilders "die" after this (but they are reclaimed using the
// ResourceObj mechanism).
const DebugMap *DebugInfoBuilder::build_debugMap(CommonAsm *_asm) {
  // Compress each of the DebugScopeBuilders into a DebugScope.  Gather them
  // into a new temp PCMapBuilder, then get a PCMap out of that.
  class Tmp : public PCMapBuilder {
  public:
    void stuff_print_on( outputStream *st, intptr_t stuff ) const { ShouldNotReachHere(); }
    Tmp() : PCMapBuilder("debug tmp map") {}
    void add_mapping( int rel_pc, void *stuff ) { PCMapBuilder::add_mapping(rel_pc, stuff); }
    pcMap *build_pcMap( ) { return PCMapBuilder::build_pcMap(); }
  };
  Tmp pcm;
  GrowableArray<intptr_t> *shared_cons = new GrowableArray<intptr_t>();
for(int i=0;i<_rel_pcs->length();i++){
    DebugScopeBuilder *dsb = (DebugScopeBuilder*)_stuff->at(i);
    const DebugScope *ds = dsb->get_compressed(_asm, shared_cons);
    pcm.add_mapping(_rel_pcs->at(i),(void*)ds);
  }

  int baked_cons_len = shared_cons->length();
  intptr_t *baked_cons = baked_cons_len > 0 ? NEW_C_HEAP_ARRAY(intptr_t, baked_cons_len) : NULL;
for(int i=0;i<baked_cons_len;i++){
    baked_cons[i] = shared_cons->at(i);
  }
  // A new C-heap-allocated DebugMap using a C-heap-allocated PCMap built from
  // the temp PCMapBuilder.
  DebugMap *dbgMap = new DebugMap(pcm.build_pcMap(), baked_cons_len, baked_cons);
#ifdef ASSERT
dbgMap->verify();
#endif
  return dbgMap;
}

// --- stuff_print_on
void DebugInfoBuilder::stuff_print_on( outputStream *st, intptr_t stuff ) const {
  ((DebugScopeBuilder *)stuff)->print(st);
}

//=============================================================================
// --- DebugScopeBuilder
DebugScopeBuilder::DebugScopeBuilder( DebugScopeBuilder *caller, 
                                      const int objectId,
                                      const int max_locals,
                                      const int max_stack,
                                      const int maxlocks,
				      int bci ) : 
  _compressed(NULL), _caller(caller), _objectId(objectId), _max_locals(max_locals), _max_stack(max_stack), _bci(bci), _maxlocks(maxlocks), _callee_save_pairs(0), _extra_lock(false), _reexecute(false), _inline_cache(false),
  _regs(NEW_RESOURCE_ARRAY(VReg::VR,max_locals+max_stack+maxlocks+REG_CALLEE_SAVE_COUNT*2)),
  _cons(NEW_RESOURCE_ARRAY(intptr_t,max_locals+max_stack+maxlocks)),
  _8byte(max_locals+max_stack+maxlocks+REG_CALLEE_SAVE_COUNT*2),
_base_derived_pairs(NULL),
  _maxstks(0), _ex_bcis(NULL), _ex_labels(NULL)
{ 
  // Fill with VReg::Bad
  memset( _regs, -1, sizeof(VReg::VR)*(max_locals+max_stack+maxlocks+REG_CALLEE_SAVE_COUNT*2) );
_8byte.clear();
}

// --- add_empty
void DebugScopeBuilder::add_empty( DebugInfoBuilder::JVM_Part part, uint idx ) {
  if( part == DebugInfoBuilder::JStack ) {
    if( idx+1 > _maxstks ) _maxstks = idx+1;
  } else {
    assert0( part == DebugInfoBuilder::JLock || part == DebugInfoBuilder::JLocal );
  }
}

// --- add_vreg
void DebugScopeBuilder::add_vreg( DebugInfoBuilder::JVM_Part part, uint idx, VReg::VR vreg, bool is8bytes ) {
  if( part == DebugInfoBuilder::JStack ) {
    if( idx+1 > _maxstks ) _maxstks = idx+1;
    idx += size_jlocals();
  } else if( part == DebugInfoBuilder::JLock ) {
    idx += size_jlocals();
    idx += _max_stack;
  } else {
    assert0( part == DebugInfoBuilder::JLocal );
  }
  assert0( _regs[idx] == VReg::Bad ); // No double setting
  assert ( (idx >= 0) && (idx < (uint)(size_jlocals()+_max_stack+size_locks())), "range check" );
  _regs[idx] = vreg;
  if( is8bytes ) _8byte.set_bit(idx);
}

// --- add_const_int
void DebugScopeBuilder::add_const_int( DebugInfoBuilder::JVM_Part part, uint idx, intptr_t val, bool is_oop ) {
  if( part == DebugInfoBuilder::JStack ) {
    if( idx+1 > _maxstks ) _maxstks = idx+1;
    idx += size_jlocals();
  } else if( part == DebugInfoBuilder::JLock ) {
    idx += size_jlocals();
    idx += _max_stack;
  } else {
    assert0( part == DebugInfoBuilder::JLocal );
  }
  assert0( _regs[idx] == VReg::Bad ); // No double setting
  assert ( (idx >= 0) && (idx < (uint)(size_jlocals()+_max_stack+size_locks())), "range check" );
  _regs[idx] = is_oop? (VReg::VR)-3 : (VReg::VR)-2;
  _cons[idx] = val;
  if( is_oop ) _8byte.set_bit(idx);
}

// --- add_callee_save_pair
void DebugScopeBuilder::add_callee_save_pair( VReg::VR src, VReg::VR dst ) {
  int idx = size_jlocals() + _max_stack + size_locks();
  idx += _callee_save_pairs<<1;
  assert ( (idx >= size_jlocals() + _max_stack + size_locks()) && (idx < size_jlocals()+_max_stack+size_locks()+REG_CALLEE_SAVE_COUNT*2), "range check" );
  _regs[idx+0] = src;
  _8byte.set_bit(idx+0);
  _regs[idx+1] = dst;
  _8byte.set_bit(idx+1);
  _callee_save_pairs++;
}

// --- size_jlocals
int DebugScopeBuilder::size_jlocals() const {
  return _max_locals;
}

// --- size_jstack
int DebugScopeBuilder::size_jstack() const {
  return _maxstks;
}

// --- size_locks
int DebugScopeBuilder::size_locks() const {
  return _maxlocks;
}

// --- equals
bool DebugScopeBuilder::equals( const DebugScopeBuilder *dbg ) const {
  //Unimplemented();
  return true;
}

// --- add_exception
// Add a bci->rel_pc mapping.  Add exception-lookup-time, the core VM will
// produce a handler_bci for the current exception (& inlined method), and
// this mapping will give us the code offset for the compiled handler.
void DebugScopeBuilder::add_exception(CommonAsm *_asm, int bci, const Label *lbl) {
  if( !_ex_bcis ) {
    _ex_bcis   = new GrowableArray<int   >();
    _ex_labels = new GrowableArray<const Label*>();
  }
  // Dup mappings are tolerated (and the dup is ignored)
  int idx = _ex_bcis->find(bci);
  if( idx != -1 ) {
    assert0( lbl == _ex_labels->at(idx) );
    return;
  }
  _ex_bcis  ->push(bci);
  _ex_labels->push(lbl);
}

// -- compress_reg
// Produce the compressed encoding of the VReg or constant value
DebugScopeValue::Name DebugScopeBuilder::compress_reg(int index, GrowableArray<intptr_t> *shared_cons) {
  DebugScopeValue::Name creg;
  VReg::VR ureg = _regs[index];
  if( ureg == VReg::Bad ) {        // bad register
    creg = DebugScopeValue::Bad;
  } else if ( ureg > VReg::Bad ) { // register or stack encoding
    creg = DebugScopeValue::from_vreg(ureg, _8byte.at(index));
  } else {                          // oop or int constant
    assert0( ureg == -3 || ureg == -2);
    intptr_t c = _cons[index];
    // check validity of oop index if oop
    assert0( ureg == -2 || CodeCacheOopTable::is_valid_index(c) );
    shared_cons->append_if_missing(c);
    int c_idx = shared_cons->find(c);
    creg = DebugScopeValue::from_index(c_idx, ureg == -3);
  }
  return creg;
}
// --- make_compressed
// Make a DebugScope for this DebugScopeBuilder.  Kill the DebugScopeBuilder
// so it is not accidentally changed or re-used.
void DebugScopeBuilder::make_compressed( CommonAsm *_asm, GrowableArray<intptr_t> *shared_cons ) {
  assert0( !_compressed );      // Do not make twice
  int len = size_jlocals()+size_jstack()+_maxlocks+_callee_save_pairs*2;
  if( _base_derived_pairs )     // Make space for base/derived pairs
    len += _base_derived_pairs->length();

  DebugScopeValue::Name *regs = NEW_C_HEAP_ARRAY(DebugScopeValue::Name,len);
  // Encode regs avoiding unused stack slots
  const int x = size_jlocals()+size_jstack();
  const int y = size_jlocals()+_max_stack;
for(int i=0;i<x;i++)
    regs[0+i] = compress_reg(0+i, shared_cons);
  for( uint i=0; i<_maxlocks+_callee_save_pairs*2; i++ )
    regs[x+i] = compress_reg(y+i, shared_cons);
  // Now copy the derived-ptr pairs
  if( _base_derived_pairs ) {
    int z = x+_maxlocks+_callee_save_pairs*2;
for(int i=0;i<_base_derived_pairs->length();i+=2){
      regs[z+i+0] = DebugScopeValue::from_vreg(_base_derived_pairs->at(i+0), true); // base
      regs[z+i+1] = DebugScopeValue::from_vreg(_base_derived_pairs->at(i+1), true); // derived
    }
  }

  const PC2PCMap *bci2pc_map = NULL;
  if( _ex_bcis ) {              // Have exception handlers here?
    // Build a compressed bci->rel_pc mapping
    PC2PCMapBuilder bci2pc_bld;
for(int i=0;i<_ex_bcis->length();i++)
      bci2pc_bld.add_mapping( _ex_bcis->at(i)/*a bci not really a rel_pc*/, _ex_labels->at(i)->rel_pc(_asm) );
    bci2pc_map = bci2pc_bld.build_PC2PCMap();
  }
  _compressed = new DebugScope( _caller ? _caller->get_compressed(_asm, shared_cons) : NULL, _objectId, _bci,
                                size_jstack(), _maxlocks, _callee_save_pairs, 
                                _base_derived_pairs ? _base_derived_pairs->length() : 0, 
                                _extra_lock, _reexecute, _inline_cache, 
                                regs, bci2pc_map );

  // Callee-save is only recorded on the leaf DS.
  assert0( !_caller || _caller-> _callee_save_pairs == 0 );
  assert0( !_caller || _caller->_base_derived_pairs == 0 );
  // Crush this guy
  *(int*)&_maxlocks = 0;
_regs=NULL;
_cons=NULL;
  _ex_bcis = NULL;        // no free of storage; ResourceMark reclaims
  _ex_labels = NULL;      // no free of storage; ResourceMark reclaims
}

// --- print
void DebugScopeBuilder::print(outputStream*st)const{
  if( _compressed ) return;     // Already compressed; nothing to print here
  if( st==NULL ) st = tty;
  st->print("[");
  method().as_methodOop()->print_name(st);
st->print(":%d",_bci);
if(_inline_cache)st->print(" IC");
  int j = 0;

for(int i=0;i<size_jlocals();i++){
st->print(" JL%d=",i);
    if( _regs[j+i] == VReg::Bad ) {
      st->print("_");
    } else if( _regs[j+i] >= 0 ) {
      VReg::print_on(_regs[j+i],st);
    } else if (_regs[j+i] == -2) { // int constant
      st->print(INTPTR_FORMAT, _cons[j+i]);
    } else {
      assert0 (_regs[j+i] == -3);  // oop constant
      objectRef ref = CodeCacheOopTable::getOopAt(_cons[j+i]);
      ref.as_oop()->print_value_on(st);
    }
  }
  j += size_jlocals();

for(int i=0;i<size_jstack();i++){
st->print(" JSTK%d=",i);
    if( _regs[j+i] == VReg::Bad ) {
      st->print("_");
    } else if( _regs[j+i] >= 0 ) {
      VReg::print_on(_regs[j+i],st);
    } else if (_regs[j+i] == -2) { // int constant
      st->print(INTPTR_FORMAT, _cons[j+i]);
    } else {
      assert0 (_regs[j+i] == -3);  // oop constant
      objectRef ref = CodeCacheOopTable::getOopAt(_cons[j+i]);
      ref.as_oop()->print_value_on(st);
    }
  }
  j += _max_stack;

for(uint i=0;i<_maxlocks;i++){
st->print(" LCK%d=",i);
    if( _regs[j+i] == VReg::Bad ) {
      st->print("_");
    } else if( _regs[j+i] >= 0 ) {
      VReg::print_on(_regs[j+i],st);
    } else {
      assert0 (_regs[j+i] == -3);  // oop constant
      objectRef ref = CodeCacheOopTable::getOopAt(_cons[j+i]);
      ref.as_oop()->print_value_on(st);
    }
  }
  j += _maxlocks;

for(uint i=0;i<_callee_save_pairs;i++){
    if( _regs[j+(i<<1)] == VReg::Bad ) continue;
    VReg::VR src = _regs[j+(i<<1)+0];
    VReg::VR dst = _regs[j+(i<<1)+1];
    st->print(" ");
    VReg::print_on(src,st);
st->print("_saved@_");
    VReg::print_on(dst,st);
  }
j+=(_callee_save_pairs<<1);

  if( _base_derived_pairs ) {
for(int i=0;i<_base_derived_pairs->length();i+=2){
      VReg::VR base = _base_derived_pairs->at(i+0);
      VReg::VR deri = _base_derived_pairs->at(i+1);
      st->print(" ");
      VReg::print_on(base,st);
st->print("_base_for_derived_");
      VReg::print_on(deri,st);
    }
    j += _base_derived_pairs->length();
  }

  if( _ex_bcis ) {              // Any exception handlers?
st->print(" ex_handlers{");
for(int i=0;i<_ex_bcis->length();i++)
st->print("bci:%d->Label, ",_ex_bcis->at(i));
st->print("}");
  }
st->print("]");
}

//=============================================================================
// --- print
void PC2PCMap::print( int faulting_pc, const CodeBlob *blob, outputStream *st ) const {
  intptr_t handler_pc = get(faulting_pc);
  if( (intptr_t)handler_pc == NO_MAPPING ) return;
  if( !st ) st = tty;
  st->print("  // NPE-> %p", (address)blob+handler_pc);
}

void PC2PCMapBuilder::stuff_print_on( outputStream *st, intptr_t stuff ) const {
  Unimplemented(); 
st->print(INTPTR_FORMAT,stuff);
}
