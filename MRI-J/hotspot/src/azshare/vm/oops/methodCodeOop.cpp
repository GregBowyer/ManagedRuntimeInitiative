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

 
#include "assembler_pd.hpp"
#include "codeBlob.hpp"
#include "codeProfile.hpp"
#include "copy.hpp"
#include "deoptimization.hpp"
#include "jvmtiExport.hpp"
#include "methodCodeOop.hpp"
#include "methodOop.hpp"
#include "oopFactory.hpp"
#include "oopsHierarchy.hpp"
#include "orderAccess.hpp"
#include "sharedRuntime.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "copy.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "markSweep.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

void methodCodeOopDesc::print_value_on(outputStream*st)const{
  st->print("mco: %p  blob: %p",this,_blob);
}

void methodCodeOopDesc::print_xml_on( xmlBuffer *xb, bool ref ) const {
  _blob->print_xml_on(xb,ref);
}

uint methodCodeOopDesc::_deopt_list_len = 0;
const CodeBlob *methodCodeOopDesc::_deopt_list[512];

// --- adr_blob_owner --------------------------------------------------------
heapRef *methodCodeOopDesc::adr_blob_owner() const { 
  return _blob ? _blob->adr_owner() : (heapRef*)&_blob; 
}

// --- post_compiled_method_load_event ---------------------------------------
// post_compiled_method_load_event
// new method for install_code() path
// Transfer information from compilation to jvmti
void methodCodeOopDesc::post_compiled_method_load_event(){
  if (JvmtiExport::should_post_compiled_method_load()) {
    HandleMark hm;
    JvmtiExport::post_compiled_method_load(this);
  }
}

// --- add_static_ref --------------------------------------------------------
// Add this oop to the static refs used by the CodeBlob (hence must be kept
// alive).  Used by C1 late code-patching.  Allocating in this array is racey
// with other threads also patching the same method in different places.
void methodCodeOopDesc::add_static_ref( int kid, TRAPS ) {
  HandleMark hm;
  // Big Spin: try to find (and CAS) a spare slot in the static_ref array,
  // growing the array as needed - and retrying in the case of conflicts.
  // Allocation happens once per outer-loop trip, so things need to be
  // handlized if they survive the loop backedge.
  methodCodeHandle thsi(this);
  while( true ) {
    // CAS-update requires we read the static_refs field ONCE per base-array
    // update attempt, which happens ONCE per trip through this outer loop.  
    // But we might need to allocate which triggers a GC, so we need to
    // handlize the ONE read attempt we are allowed.
    objArrayHandle oah(thsi->static_refs()); // Read _static_refs once per CAS
    objArrayOop srs = oah();
    int srs_len = srs ? srs->length() : 0;
    objectRef sref = CodeCacheOopTable::getOopAt(kid);
for(int i=0;srs&&i<srs_len;i++){
      objectRef ref_from_array = srs->ref_at(i);
      if( sref == ref_from_array ) return; // Dup!  Already done!
      if( ref_from_array.is_null() ) { // scan for a null
        // Card-mark, etc
        sref = ref_check_without_store(srs,srs->obj_at_addr(i),sref);
        // Inner-loop one-shot CAS from NULL to not-NULL.
        if( Atomic::cmpxchg_ptr(ALWAYS_POISON_OBJECTREF(sref).raw_value(),
                                (volatile intptr_t*)srs->obj_at_addr(i), 
                                NULL) == 0 ) {
          return;               // Was NULL, so CAS worked!
        } else {
          srs = oah(); // re-read srs in case of SBA escape during ref_check
        }
      }
    }
    // Array is full already (or NULL).
    // Need to allocate a larger array here, and copy from the old to the new.
    // GC point: old array has to be handlized here.  Also the 'this' pointer moves.
objArrayOop newsrs=NULL;
    if( !srs ) {
      newsrs = oopFactory::new_objectArray(2,false,CHECK);
    } else {
      int newlen = srs_len + (srs_len>>1)+1;
      newsrs = oopFactory::new_objectArray(newlen,false,CHECK);
      Copy::conjoint_objectRefs_atomic(oah.as_ref().as_objArrayOop()->base(),newsrs->base(),srs_len);
    }
    objectRef newref = newsrs;
    newref = ref_check_without_store(thsi(),thsi->adr_static_refs(),newref);
    // One-shot attempt update, then repeat the whole loop.
    // If the update works then the new array has some space in it.
    Atomic::cmpxchg_ptr_without_result(ALWAYS_POISON_OBJECTREF(newref).raw_value(),
                                       (volatile intptr_t*)thsi->adr_static_refs(),
                                       ALWAYS_POISON_OBJECTREF(oah.as_ref()).raw_value());
  }
}

static int ivTableStubSizeLmt = 256;
// --- get_vtable_stub -------------------------------------------------------
// Collection of vtable-stubs for this method.  These are cloned per-method in
// an effort to let hardware BTB's have unique addresses to predict from.  We
// store a bunch of them in each CodeBlob and chain the CodeBlobs together
// using a pointer stored at the start of the code. The lifetime of a vtable
// stub matches the lifetime of this oop.
address methodCodeOopDesc::get_vtable_stub_impl( int idx,
                                                 bool is_itable ) {
  CodeBlob* activeVtablesBlob = _vtables;
  if( !activeVtablesBlob ) {
    activeVtablesBlob =
    _vtables = CodeCache::malloc_CodeBlob(CodeBlob::vtablestub,
                                          BytesPerSmallPage  ); 
    // create next code blob ptr
    MacroAssembler masm(activeVtablesBlob,_vtables->code_begins());
    masm.emit8((int64_t)0);
    _vtable_pc = masm.pc(); 
    // Replicate the owner MethodCodeOop from the calling inline cache into this
    // blob.  They both have the same lifetime.
    activeVtablesBlob->set_owner(this);
    // vTable Frame only has return address of its caller.
    _vtables->_framesize_bytes = 1 * wordSize;
    HotspotToGdbSymbolTable.symbolsAddress[_vtables->gdb_idx()].frameSize =
      _vtables->_framesize_bytes;
  }
  const address next_entry = _vtable_pc;

  // Search for duplicate vtable requests
  const int match_idx = is_itable ? -idx : idx;
  address entry = activeVtablesBlob->code_begins() + 8; // 8 byte hole for next blob ptr
  while( entry != next_entry ) {
    int vstublen = ((int16_t*)entry)[0];
    assert0( vstublen > 0 );
    int vstubidx = ((int16_t*)entry)[1];
    if( match_idx == vstubidx )
      // Stub starts past the 4 bytes of control data, and is aligned
      return (address)round_to((intptr_t)(entry+4),CodeEntryAlignment); 
    entry += vstublen;          // Skip to next stub

    // If the space left behind in CodeBlob is less than i/vTableStubSize
    // then we expect to have address of the start of next CodeBlob's
    // code_begin.
    if( (activeVtablesBlob->end() - entry) < ivTableStubSizeLmt ) {
      activeVtablesBlob = activeVtablesBlob->next_vtable_blob();
      entry = activeVtablesBlob->code_begins() + 8; // 8 byte hole for next blob ptr
    }
  }

  // "Placed" MacroAssembler: extend the CodeBlob in-place.
  MacroAssembler masm(activeVtablesBlob,next_entry);
  assert0( is_int16(idx) );
  masm.emit2(0);                // length of stub
  masm.emit2(match_idx);        // Yea Olde Index
  masm.align(CodeEntryAlignment);
  if( is_itable ) pd_create_itable_stub(&masm,idx);
  else            pd_create_vtable_stub(&masm,idx);
masm.align(8);
masm.patch_branches();
  _vtable_pc = masm.pc();       // Record end of this stub for next time
  activeVtablesBlob->set_code_len(_vtable_pc - activeVtablesBlob->code_begins());

  uint len = _vtable_pc - next_entry;
  assert( (2 * len) <
          (activeVtablesBlob->end()-activeVtablesBlob->code_begins()),
"VTable Stub length has grown too much relative to Page Size.");
  *(uint16_t*)next_entry = len;

  // At this time, we see a very limited space left behing in CodeBlob
  // and we may not be able to fit in next i/vTableStub, for that matter
  // create a new CodeBlob and chain it with the the exisiting Blob.
  if( (activeVtablesBlob->end() - _vtable_pc) < ivTableStubSizeLmt ) {
    CodeBlob* nextVtables = CodeCache::malloc_CodeBlob(CodeBlob::vtablestub,
                                                       BytesPerSmallPage );
    // create next code blob ptr
    MacroAssembler next_masm(nextVtables,nextVtables->code_begins());
    next_masm.emit8((int64_t)0);
    _vtable_pc = next_masm.pc();
    // Replicate the owner MethodCodeOop from the calling inline cache into this
    // blob.  They both have the same lifetime.
    nextVtables->set_owner(this);
    // vTable Frame only has return address of its caller.
    nextVtables->_framesize_bytes = 1 * wordSize;
    HotspotToGdbSymbolTable.symbolsAddress[nextVtables->gdb_idx()].frameSize =
      nextVtables->_framesize_bytes;
    activeVtablesBlob->set_next_vtable_blob(nextVtables);
  }

  return (address)(((intptr_t)next_entry+2/*stub len*/+2/*idx*/+CodeEntryAlignment-1) & -CodeEntryAlignment);
}


// --- patched_a_call_to -----------------------------------------------------
void methodCodeOopDesc::patched_a_call_to( address after_call_pc, address target_pc ) const {
  // Requires atomic update of both the call-target and this array.  Or at
  // least cannot have 2 threads both patching the same site to 2 different
  // locations (one getting stomped), and then have their updates to the
  // mco_call_targets also race - and having the other one get stomped such
  // that the mco_call_targets and the patched call site are out of sync.
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(after_call_pc));
  int idx = _debuginfo->find_slot(after_call_pc - (address)_blob);
  assert0( idx != NO_MAPPING );
  methodCodeRef target_mcr(target_pc ? CodeCache::find_blob(target_pc)->owner().raw_value() : 0);
  objArrayOopDesc::ref_at_put(mco_call_targets(),idx,target_mcr);
}

// --- print_dependencies ----------------------------------------------------
void methodCodeOopDesc::print_dependencies(outputStream*st)const{
  ResourceMark rm;
  objArrayOop dk = dep_klasses().as_objArrayOop();
  objArrayOop dm = dep_methods().as_objArrayOop();
  if ( dk == 0 ) return;
int len=dk->length();
  if( len == 0 ) return;
st->print("Depends on:");
  const instanceKlass *last_ik=NULL;
  for( int i=0; i<len; i++ ) {
    const instanceKlass *ik = instanceKlass::cast((klassOop)dk->obj_at(i));
    if( last_ik != ik ) {
if(last_ik)st->print("}");
st->print(" %s:{",ik->internal_name());
last_ik=ik;
    } else {
      st->print(", ");
    }
    const methodOop m = (methodOop)dm->obj_at(i);
    if( m == zeroImplementors().as_oop() ) st->print("NO_IMPL");
    else if( m == noFinalizers().as_oop() ) st->print("NO_FINALS");
else if(m==NULL)st->print("SINGLE_IMPL");
    else {
st->print("NO_OVERRIDING ");
m->name()->print_symbol_on(st);
    }
    
  }
if(last_ik)st->print("}");
  st->cr();
}

// --- zeroImplementors ------------------------------------------------------
methodRef methodCodeOopDesc::zeroImplementors() {
  // A random bizarre method used as a sentinel
  return methodRef(Universe::loader_addClass_method());
}

// --- noFinalizers ----------------------------------------------------------
methodRef methodCodeOopDesc::noFinalizers() {
  // A random bizarre method used as a sentinel
  return methodRef(Universe::finalizer_register_method());
}

#ifndef PRODUCT
// --- verify_dependencies ---------------------------------------------------
void methodCodeOopDesc::verify_dependencies( const instanceKlass *ik ) const {
  objArrayOop dk = dep_klasses().as_objArrayOop();
  objArrayOop dm = dep_methods().as_objArrayOop();
int len=dk->length();
  int i=0;
  while( ik != instanceKlass::cast((klassOop)dk->obj_at(i)) )
    i++;
methodOop m=(methodOop)dm->obj_at(i);
  if( m == zeroImplementors().as_oop() ) { // zero implementors test
    assert0( ik->nof_implementors() == 0 );
    return;
  }
  if( m == noFinalizers().as_oop() ) { // no finalizers test
    assert0( ik->find_finalizable_subclass() == NULL );
    i++;
    if( i >= len || ik != instanceKlass::cast((klassOop)dk->obj_at(i)) )
      return;
    m = (methodOop)dm->obj_at(i);
  }
  if( !m ) {                    // Single implementor test
    assert0( ik->nof_implementors() == 1 );
    return;
  }
  // Check all the individual no-override methods
  while( i<len && ik == instanceKlass::cast((klassOop)dk->obj_at(i)) ) {
    m = (methodOop)dm->obj_at(i);
    // Verify 'm' no child of ik has the same name+sig
    // this test could be improved to recursively check all children
    for( Klass *child = ik->subklass(); child; child = child->next_sibling() ) {
instanceKlass*ik2=(instanceKlass*)child;
      if( ik2->nof_implementors() == 0 ) continue; // This child does not have any implementors.. try his siblings
      while(ik2->nof_implementors() == 1 ) {
        instanceKlass *ik3 = instanceKlass::cast(ik2->implementor().as_klassOop());
        if( ik3 == ik2 ) break;
        ik2 = ik3;
      }
methodOop lookedup=ik2->lookup_method(m->name(),m->signature());
      assert0( lookedup==m );
    }
    i++;
  }

}
#endif // !PRODUCT


// --- not_entrant -----------------------------------------------------------
// This wad of compiled code should no longer be called (possibly because a
// phase-change has started executing some previously never-JIT'd path) but
// existing callers may correctly complete.  Make the code Not-Entrant: future
// callers will trip on the 1st instruction.  
void methodCodeOopDesc::not_entrant() {
  // Remove from list of all of methods: future lookups should not find this
  // method.
  methodOop moop = method().as_methodOop();
  moop->remove_methodCode(this);
  _blob->make_not_entrant();    // No point in letting fresh callers in

  // Remove self methodCode from method: future lookups should not find this method
  assert_locked_or_safepoint(Compile_lock);
  if( moop->codeRef().as_methodCodeOop() == this ) {
    HandleMark hm;
    moop->clear_codeRef();
    moop->invocation_counter()->reset();    
    moop->backedge_counter()->reset();
  }  
}

// --- deoptimize ------------------------------------------------------------
// This wad of compiled code is no longer valid (probably because some class
// loading has invalidated some JIT decision or because a phase-change has
// started executing some previously never-JIT'd path).  Make the code Not
// Entrant: future callers will trip on the 1st instruction.  Make the code
// full of no-ops/deopt-groo so that existing callers, when they return to the
// method, fall out the bottom (and in the deopt-sled) without executing any
// more instructions in the method.
void methodCodeOopDesc::deoptimize(int/*Deoptimization::DeoptInfo*/ cause) {
  assert_locked_or_safepoint(Compile_lock);
assert(!_blob->is_native_method(),"no reason to deopt a native!");
  if( _patched_for_deopt ) return; // Already done it
  // _patched_for_deopt will be set later by the VM_Deoptimization operation
  // at a full safepoint.

  // Add self to the list of CodeBlobs needing patching with deopt groo
  // instructions.  A VM operation forces the full Safepoint we require.
  // Adding to the list is race-safe because we hold the Compile_lock.
  Deoptimization::increment_deoptimization_count((Deoptimization::DeoptReason)cause);
  guarantee( _deopt_list_len < sizeof(_deopt_list)/sizeof(_deopt_list[0]), "make deopt_list bigger" );
  _deopt_list[_deopt_list_len++] = _blob;

  not_entrant();                // No point in letting fresh callers in
}


// --- deoptimize_now --------------------------------------------------------
// Schedule a deoptimization... then do it.  Blocks until it happens.  Used
// when a Java thread has a synchronous deopt condition, as opposed to e.g. a
// GC thread doing class loading/unloading blowing dependency conditions.
void methodCodeOopDesc::deoptimize_now(int/*Deoptimization::DeoptInfo*/ cause) {
  { MutexLockerAllowGC ml(Compile_lock, JavaThread::current());
deoptimize(cause);
  }
  if( _deopt_list_len > 0 ) {
    VM_Deoptimize vm_op;
VMThread::execute(&vm_op);
  }
}

// --- remove_blob_from_deopt ------------------------------------------------
// Make sure this CodeBlob isn't sitting on the deopt_list.  If it is, remove it.
void methodCodeOopDesc::remove_blob_from_deopt(CodeBlob*cb){
assert_locked_or_safepoint(Compile_lock);
for(uint i=0;i<_deopt_list_len;i++)
    if( _deopt_list[i] == cb ) 
      _deopt_list[i] = _deopt_list[--_deopt_list_len];
}

