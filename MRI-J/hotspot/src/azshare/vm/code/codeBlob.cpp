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


#include "bytecode.hpp"
#include "c1_Runtime1.hpp"
#include "codeBlob.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "disassembler_pd.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "javaCalls.hpp"
#include "objectRef_pd.hpp"
#include "pcMap.hpp"
#include "register_pd.hpp"
#include "sharedRuntime.hpp"
#include "signature.hpp"
#include "stubCodeGenerator.hpp"
#include "tickProfiler.hpp"
#include "vreg.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "oop.inline2.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"

// --- name
const char*CodeBlob::name()const{
  static const char *names[] = {"runtime","c2i_adapter","methodStub","native","interpreter","c1","c2", "monitors","vtableStub","vm_version"};
  if (_type < 0 || _type >= bad) {
return"bad";
  }
return names[_type];
}

// --- methodname
// Fancy name only for methods.  Caller must free storage.
const char *CodeBlob::methodname() const {
  if( !is_methodCode() ) return NULL;
  methodCodeOop mcoop = owner().as_methodCodeOop();
if(!mcoop)return NULL;
  methodOop moop = mcoop->method().as_methodOop();
if(!moop)return NULL;
  return moop->name_and_sig_as_C_string();
}

// --- oops_do ---------------------------------------------------------------
// No oops directly in the code; those are all GC'd via the CodeCacheOopTable
// (and referenced indirectly via a list of oop-table indices).  Just do MSBs.
void CodeBlob::oops_do(OopClosure*f){
f->do_oop(&_owner);
  if( _type == methodstub )     // MethodStubs have loads of embedded oops
    MethodStubBlob::oops_do(code_begins(),end(),f);
}

// --- oops_arguments_do -----------------------------------------------------
// oops-do just for arguments when args are passed but the caller is
// not yet claiming responsibility for them (e.g., mid-call resolution).
void CodeBlob::oops_arguments_do(frame fr, OopClosure* f) const {

  // Native calls have all their args handlized.
  if( _type == native ) return;

  // C1 and C2 can call directly into the C++ runtime without a stub but these
  // calls all do not need their arguments GC'd (i.e., by the time a GC point
  // is reached all arguments get handlized).
  if( _type == c1 || _type == c2 ) return;

  guarantee( _type == runtime_stubs, "we only get here for runtime stubs" );

  StubCodeDesc *stub = StubCodeDesc::desc_for(fr.pc());
  Runtime1::StubID id = Runtime1::contains(fr.pc());
  // In a resolve_and_patch_call stub, we have to parse the outgoing signature
  // to find any argument oops.  The call site itself doesn't cover the
  // outgoing oops, and there is no target method yet.
  if( fr.pc() == CodeCache::_caller_must_gc_args_0 ||
      fr.pc() == CodeCache::_caller_must_gc_args_1 ) {
    assert0( (stub && !strcmp(stub->name(), "resolve_and_patch_handler")) ||
             id == Runtime1::frequency_counter_overflow_wrapper_id);
    oops_arguments_do_impl(fr,f);
    return;
  }

  // Assert sanity for stub-generator stubs
  if( stub ) {
    if( !strcmp(stub->name(), "sba_escape_handler") ) return; // no unhandled args from a failed STA/SVB
    if( !strcmp(stub->name(), "new_handler") ) return; // no unhandled args from a new allocation request
    // If we are at an inline-cache we need to construct an official j.l.NPE
    // object.  However, no oops are in callee-save registers and the caller's
    // args are dead because the call isn't happening.  (If we are not at an
    // inline-cache then we just reset the PC to the proper handler and do not
    // do any object construction).
    if( !strcmp(stub->name(), "handler_for_null_ptr_exception") ) return;
    // If we are throwing, then the callers args are long long gone.
    if( !strcmp(stub->name(), "forward_exception") ) return;
    // For safepoints, we generously allow any register to hold oops.
    if( !strcmp(stub->name(), "safepoint_handler") ) return;
    // The args for a blocking lock will be handlerized in the VM
    if( !strcmp(stub->name(), "blocking_lock_stub") ) return;
    // The args for registering a finalized object will be handlerized in the VM
    if( !strcmp(stub->name(), "register_finalizer") ) return;
    // The args for a blocking lock will be handlerized in the VM
    // There are no args saved in registers across an uncommon trap
    if( !strcmp(stub->name(), "deoptimize") ) return;
    if( !strcmp(stub->name(), "uncommon_trap") ) return;
    ShouldNotReachHere();
  }

  // Not a StubGenerator stub; check for sane C1 stubs
  if (id != -1) {
    switch(id) {
    case Runtime1::access_field_patching_id:        return;
    case Runtime1::load_klass_patching_id:          return;
    case Runtime1::monitorenter_id:                 return;
    case Runtime1::new_array_id:                    return;
    case Runtime1::new_instance_id:                 return;
    case Runtime1::new_multi_array_id:              return;
    case Runtime1::register_finalizer_id:           return;
    case Runtime1::throw_array_store_exception_id:  return;
    case Runtime1::throw_class_cast_exception_id:   return;
    case Runtime1::throw_div0_exception_id:         return;
    case Runtime1::throw_index_exception_id:        return;
    case Runtime1::throw_null_pointer_exception_id: return;
    case Runtime1::throw_range_check_failed_id:     return;
    default: tty->print_cr("Unimplemented Runtime1 stub ID %s (%d)", Runtime1::name_for(id), id); Unimplemented();
    }
  }

  // Probably most other stubs are simple returns, but a few need special handling.
  ShouldNotReachHere();
}

// --- oops_arguments_do_impl ------------------------------------------------
void CodeBlob::oops_arguments_do_impl(frame fr, OopClosure* f) const {
  ResourceMark rm;
  // We are in some trampoline (resolve_and_patch_call) doing a GC.  The
  // pending call has arguments that need GC'ing, but we do not yet know the
  // target method and cannot resolve the target method yet.
  frame cc = fr.sender();       // Compiled caller expected
symbolOop meth_sig;
  bool is_static;
  if( cc.is_entry_frame() ) {
    // There's a rare race condition where the caller is an entry frame, but
    // the target got patched not-entrant before the call could be made.
    methodOop moop = cc.entry_frame_call_wrapper()->callee_method();
    meth_sig = moop->signature();
    is_static = moop->is_static();

  } else {
Bytecode_invoke*call;
if(cc.is_interpreted_frame()){
      // There's a rare race condition where we might need to GC in
      // resolve_and_patch_call but the caller is an interpreted frame.
      call = Bytecode_invoke_at(cc.interpreter_frame_method(),
                                cc.interpreter_frame_bci());
    } else {
      // Normal case: find caller's callsite
CodeBlob*cb=CodeCache::find_blob(cc.pc());
      const DebugScope *ds = cb->debuginfo(cc.pc());
      call = Bytecode_invoke_at(ds->method(), ds->bci());
    }
    meth_sig = call->signature();
    is_static = (call->adjusted_invoke_code() == Bytecodes::_invokestatic);
  }

  int argcnt = 0;               // Size of signature
  if( !is_static ) argcnt++;    // Add receiver
for(SignatureStream ss(meth_sig);!ss.at_return_type();ss.next()){
    argcnt++;
    if (ss.type() == T_LONG || ss.type() == T_DOUBLE) argcnt++;
  }

  BasicType * sig_bt = NEW_RESOURCE_ARRAY(BasicType  ,argcnt);
  VReg::VR * regs = NEW_RESOURCE_ARRAY(VReg::VR,argcnt);
  int i=0;
  if( !is_static ) sig_bt[i++] = T_OBJECT;
for(SignatureStream ss(meth_sig);!ss.at_return_type();ss.next()){
    sig_bt[i++] = ss.type();	// Collect remaining bits of signature
    if (ss.type() == T_LONG || ss.type() == T_DOUBLE)
      sig_bt[i++] = T_VOID;	// Longs & doubles take 2 Java slots
  }
  assert0(i==argcnt);

  // Now get the re-packed compiled-Java layout.  Registers are numbered from
  // the callee's point of view.
  SharedRuntime::java_calling_convention(sig_bt,regs,argcnt,true);

  // Find the oop locations and do the GC thing
for(int i=0;i<argcnt;i++){
    if ((sig_bt[i] == T_OBJECT) || (sig_bt[i] == T_ARRAY)) {
      objectRef *loc = cc.reg_to_addr_oop(VReg::as_VOopReg(regs[i]));
f->do_oop(loc);
    }
  }
}

// --- debuginfo -------------------------------------------------------------
const DebugScope *CodeBlob::debuginfo(address pc) const {
  assert0( CodeBlob::code_begins() <= pc && pc < CodeBlob::code_ends()+1 );
  int relpc = pc - (address)this;
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( mcoop->_blob != this ) return NULL; // vtable stubs share the mcoop with the real blob
  return mcoop->_debuginfo->get(relpc);
}

// --- debuginfo -------------------------------------------------------------
const DebugMap *CodeBlob::debuginfo() const {
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( mcoop->_blob != this ) return NULL; // vtable stubs share the mcoop with the real blob
  return mcoop->_debuginfo;
}

// --- clean_inline_caches ---------------------------------------------------
void CodeBlob::clean_inline_caches() const {
  if( owner().is_null() ) return; // Permenent blobs have no debug info and no IC's
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( mcoop->_blob != this ) return; // vtable stubs share the mcoop with the real blob
  if( mcoop->_patched_for_deopt ) return; // no inline caches now!
  const DebugMap *dbg = mcoop->_debuginfo;
  if( !dbg ) return;            // Commonly natives have no debug info
  dbg->clean_inline_caches(this);
}

// --- promote ---------------------------------------------------------------
// "Promote" a C1 CodeBlob - make it jump to a suitable C2 method
void CodeBlob::promote(){
  // Not sure we need this: why not just have C1 methods all fail the invoke-counter 
  // test on entry, and that call to Runtime1::frequency_counter_overflow
  // already Does The Right Thing.
  Unimplemented();
}

// --- make_not_entrant ------------------------------------------------------
// Not-Entrant: Prevent future access (as much as possible).
// Patch the 1st op with a 'jmp not_entrant'.
void CodeBlob::make_not_entrant() const {
  // how to assert nothing returns in the middle of this jump?
  NativeJump::patch_entry( CodeBlob::code_begins(), StubRoutines::resolve_and_patch_call_entry() );
}

// --- is_entrant ------------------------------------------------------------
// See if the 1st op is not a 'jmp resolve_and_patch'.
bool CodeBlob::is_entrant() const {
  return 
    !NativeJump::is_jump_at(CodeBlob::code_begins()) ||
    nativeJump_at(CodeBlob::code_begins())->jump_destination() != StubRoutines::resolve_and_patch_call_entry();
}


// --- locked ----------------------------------------------------------------
// Count times an oop is locked in this frame & codeblob
int CodeBlob::locked( frame fr, oop o ) const {
  methodCodeRef mcref = owner();
  if( mcref.is_null() ) return 0; // call stubs hit here
  methodCodeOop mcoop = mcref.as_methodCodeOop();
  if( mcoop->_blob != this ) return 0; // vtable stubs share the mcoop with the real blob
  const DebugMap *dbg = mcoop->_debuginfo;
  if( !dbg ) return 0;            // Commonly natives have no debug info
  const DebugScope *ds = dbg->get( fr.pc() -(address)this );
  if ( !ds ) return 0;
  return ds->count_locks( dbg, fr, o );
}

// --- decode ----------------------------------------------------------------
void CodeBlob::decode() const {
print_dependencies(tty);
  Disassembler::decode(this,tty);
}

// --- verify
void CodeBlob::verify()const{
  assert0( objectRef::is_null_or_heap(&_owner) );
  assert0( owner().as_oop()->is_oop_or_null() );
  assert0( _type < bad );
  // Be more generous on size for VerifyOop and !UseInterpreter (aka Xcomp)
  debug_only( size_t max_size = (300 + 300*UseGenPauselessGC)*K*(1+VerifyOopLevel)*(UseInterpreter?1:4) );
  assert0( !UseCodeBlobSizeCheck || (_size_in_bytes >= 0 && (size_t)_size_in_bytes <= max_size) );
}

// --- print_xml_on
void CodeBlob::print_xml_on(xmlBuffer* xb, bool ref, intptr_t tag) const {
#ifdef AZ_PROFILER
  ResourceMark rm;
assert(xb,"CodeBlob::print_xml_on() null xmlBuffer*");
    xmlElement blob(xb, ref ? "codeblob_ref" : "codeblob");
const char*name="<unnamed>";
const char*sig=NULL;
    methodCodeHandle method_code_handle(owner());
    methodCodeOop mco = owner().as_methodCodeOop();
    methodOop m = mco ? method_code_handle()->method().as_methodOop() : NULL;
    if( m ) {
name=m->name_as_C_string();
      sig = m->signature()->as_C_string();
      if (!ref) {
        if( LogCompilerOutput && mco->get_codeprofile()->get_debug_output() ) {
          xb->name_value_item(_type==c1 ? "c1debugexists" : "c2debugexists", 1);
        }
      }
    } else {
      name = this->name();
    }
    xb->name_value_item("name", name);
    if (sig) xb->name_value_item("signature", sig);
    xb->name_value_item("type", this->name());
    if (tag != 0) xb->name_value_item("tag", tag);
    if (xb->can_show_addrs()) xb->name_ptr_item("id", this);
    if (!ref) {
      ProfileEntry::print_xml_on(xb);
      if (m) {
        m->print_xml_on(xb,true);
        mco->get_codeprofile()->print_xml_on(m,xb,true);
      }
      const char *view = xb->request()->parameter_by_name("view");
      if ((view == NULL) || (strcasecmp(view, "asm") == 0)) {
        Disassembler::decode_xml(xb, code_begins(), code_ends());
      } else if (strcasecmp(view, "callee") == 0) {
        RpcTreeNode::print_xml(xb, code_begins(), code_ends() - 1, false);
      } else if (strcasecmp(view, "caller") == 0) {
        RpcTreeNode::print_xml(xb, code_begins(), code_ends() - 1, true);
      } else if (strcasecmp(view, "c1cp") == 0) {
        if (!is_c1_method() || !m ) {
          xb->name_value_item("error", "not a C1 method");
        } else {
          Unimplemented();
        }
      } else if (strcasecmp(view, "c2cp") == 0) {
        if (!is_c2_method() || !m ) {
          xb->name_value_item("error", "not a C2 method");
        } else {
          Unimplemented();
        }
      } else if (strcasecmp(view, "c1debug") == 0) {
        if (!is_c1_method() || !m ) {
          xb->name_value_item("error", "not a C1 method");
        } else {
          Unimplemented();
        }
      } else if (strcasecmp(view, "c2debug") == 0) {
        if (!is_c1_method() || !m ) {
          xb->name_value_item("error", "not a C1 method");
        } else {
          Unimplemented();
        }
      } else {
        Disassembler::decode_xml(xb, code_begins(), code_ends());
      }
    }
    return;
#endif // AZ_PROFILER
}

// --- print_on
void CodeBlob::print_on(outputStream*st)const{
  ResourceMark rm;
  if (!st) { st = tty; }
  // --- Text only dump
  intptr_t o = *(intptr_t*)&_owner;
  if( o && RefPoisoning ) o = ~o;

  const char *methname = methodname();
st->print("((struct CodeBlob*)%p) ={ ((objectRef)%p), %s, %s, sz=%d, frame=%d",
            this, (void*)o, name(), methname ? methname : "(null)", _size_in_bytes, _framesize_bytes );
  if( _oop_maps ) {
st->print(", _oop_maps={");
    _oop_maps->print(this,st);
  }
  st->print_cr("}");
}

void CodeBlob::print()const{
  print_on(tty);
}

// --- print_oopmap_at
// Print an oopmap, if any
void CodeBlob::print_oopmap_at(address pc, outputStream *st) const {
  if( !contains(pc) ) return;      // bad pc
  if( !_oop_maps ) return;         // no oopmaps here!
  int relpc = pc - (address)this;
  _oop_maps->print(relpc,st);
}

// --- print_debug_at
void CodeBlob::print_debug_at(address pc, outputStream *st) const {
  if( !contains(pc) ) return;      // bad pc
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( !mcoop ) return;             // No owner - true of, e.g., the interpreter blob
  if( !mcoop->_debuginfo ) return; // no debug info here!
  int relpc = pc - (address)this;
  mcoop->_debuginfo->print(relpc,st);
}

// --- print_npe_at
void CodeBlob::print_npe_at(address pc, outputStream *st) const {
  if( !contains(pc) ) return;      // bad pc
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( !mcoop ) return;             // No owner - true of, e.g., the interpreter blob
  if( !mcoop->_NPEinfo ) return; // no npe info here!
  int relpc = pc - (address)this;
  mcoop->_NPEinfo->print(relpc,this,st);
}

// --- print_dependencies
void CodeBlob::print_dependencies(outputStream*st)const{
  methodCodeOop mcoop = owner().as_methodCodeOop();
  if( !mcoop ) return; // No owner - true of, e.g., the interpreter blob
  mcoop->print_dependencies(st);
}
//=============================================================================
// --- do_closure
// Convenience: apply closure to all oops
void OopMapStream::do_closure( CodeBlob *cb, frame fr, OopClosure *f ) {
  address pc = fr.pc();
  assert0( cb->contains(pc) );
  int rel_pc = pc-(address)cb;
  // Derived pointers first: need to record the derived offset before the base moves
  methodCodeRef mcref = cb->owner();
if(mcref.not_null()){
    const DebugMap *dm = mcref.as_methodCodeOop()->_debuginfo;
    if( dm ) {
      const DebugScope *scope = dm->get(rel_pc);
      scope->gc_base_derived(fr,f);
    }
  }
  // Now normal oops
  cb->_oop_maps->do_closure(rel_pc,fr,f);
}


//--- DerivedPointerTable ----------------------------------------------------
bool DerivedPointerTable::_active = false;
GrowableArray<intptr_t> * DerivedPointerTable::_base_derived_pairs = 
  new (ResourceObj::C_HEAP) GrowableArray<intptr_t>(0,true/*on C heap*/);

//--- DerivedPointerTable::clear ---------------------------------------------
// Called before scavenge/GC
void DerivedPointerTable::clear() {
  assert0( !_active);            // not active now
  assert0( is_empty() );         // not active now
  _active = true;
}
//--- DerivedPointerTable::add -----------------------------------------------
// Called during scavenge/GC
void DerivedPointerTable::add(objectRef*base,objectRef*derived){
  assert0( !UseGenPauselessGC );

  assert_lock_strong(DerivedPointerTableGC_lock);
  assert0( _active );
  _base_derived_pairs->push((intptr_t)base);
  _base_derived_pairs->push((intptr_t)derived);
  intptr_t offset = (*derived).raw_value() - (*base).raw_value();
  assert(offset >= -1000000, "wrong derived pointer info");
  _base_derived_pairs->push(offset);
}

//--- DerivedPointerTable::update_pointers -----------------------------------
// Called after  scavenge/GC  
void DerivedPointerTable::update_pointers() {
  MutexLocker dptgc(DerivedPointerTableGC_lock);
  while( !is_empty() ) {
    intptr_t   offset  =             _base_derived_pairs->pop();
    objectRef* derived = (objectRef*)_base_derived_pairs->pop();
objectRef*base=(objectRef*)_base_derived_pairs->pop();
    intptr_t new_d = base->raw_value() + offset;
    derived->set_raw_value(new_d);
  }
  _active = false;
}
//--- DerivedPointerTable::is_empty ------------------------------------------
bool DerivedPointerTable::is_empty(){
return _base_derived_pairs->length()==0;
}


//=============================================================================
NativeMethodStub* MethodStubBlob::_free_list; // list of free stubs

// --- generate
address MethodStubBlob::generate( heapRef moop, address c2i_adapter ) {
  // NativeMethodStubs must be jumped-to directly and are packed back-to-back.
  // Hence they start CodeEntryAligned, and each later one has to be
  // CodeEntryAligned so we expect the instruction_size to be a multiple.
  assert0( round_to(NativeMethodStub::instruction_size,CodeEntryAlignment) == NativeMethodStub::instruction_size );
  NativeMethodStub *nms;
  do {
    // The _free_list is a racing CAS-managed link-list.  Must read the
    // _free_list exactly ONCE before the CAS attempt below, or otherwise know
    // we have something that used to be on the free_list and is not-null.  In
    // generally, if we re-read the free_list we have to null-check the result.
    nms = _free_list;
    if( !nms ) {
      // CodeCache makes CodeBlobs.  Make a CodeBlob typed as a methodCodeStub.
      CodeBlob *cb = CodeCache::malloc_CodeBlob( CodeBlob::methodstub, 256*NativeMethodStub::instruction_size );
      address adr = (address)round_to((intptr_t)cb->code_begins(),CodeEntryAlignment);
      cb->_code_start_offset = adr-(address)cb->_code_begins;
      while( adr+NativeMethodStub::instruction_size < cb->end() ) {
        free_stub((NativeMethodStub*)adr);
        adr += NativeMethodStub::instruction_size;
      }
      // The last not-null thing jammed on the freelist.
      nms = (NativeMethodStub*)(adr-NativeMethodStub::instruction_size);
    }
  } while( Atomic::cmpxchg_ptr(*(NativeMethodStub**)nms,&_free_list,nms) != nms );
  nms->fill( moop, c2i_adapter );
return(address)nms;
}

// --- free_stub -------------------------------------------------------------
// Linked-list head insert.  Racing with other threads to remove or insert.
// Since it's only at the head we can use a CAS.
void MethodStubBlob::free_stub( NativeMethodStub* stub ) {
  stub->set_oop(nullRef);
  NativeMethodStub *old = *(NativeMethodStub**)stub = _free_list;
  while( Atomic::cmpxchg_ptr(stub,&_free_list,old) != old )
    old = *(NativeMethodStub**)stub = _free_list;
}

// --- oops_do ---------------------------------------------------------------
// Find all the embedded oops
void MethodStubBlob::oops_do( address from, address to, OopClosure *f ) {
  address adr = (address)round_to((intptr_t)from,CodeEntryAlignment);
  for( ; adr+NativeMethodStub::instruction_size < to; adr+=NativeMethodStub::instruction_size ) 
    ((NativeMethodStub*)adr)->oops_do(f);
}

// --- unlink ----------------------------------------------------------------
// Unlink any MSB's whose method has died.
void MethodStubBlob::unlink( address from, address to, BoolObjectClosure* is_alive ) {
  address adr = (address)round_to((intptr_t)from,CodeEntryAlignment);
  for( ; adr+NativeMethodStub::instruction_size < to; adr+=NativeMethodStub::instruction_size ) {
    NativeMethodStub *nms = (NativeMethodStub*)adr;
    if( nms->get_oop().not_null() && !nms->is_alive(is_alive) ) 
      free_stub(nms);
  }
}

// --- unlink ----------------------------------------------------------------
// GPGC unlink any MSB's whose method has died.
void MethodStubBlob::GPGC_unlink( address from, address to ) {
  address adr = (address)round_to((intptr_t)from,CodeEntryAlignment);
  for( ; adr+NativeMethodStub::instruction_size < to; adr+=NativeMethodStub::instruction_size ) {
    NativeMethodStub *nms = (NativeMethodStub*)adr;
heapRef ref=nms->get_oop();

if(ref.not_null()){
      assert(ref.is_old(), "CodeCache should only have old-space oops");

      if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(ref);
      }

      assert(ref.as_oop()->is_oop(), "not oop");

      if ( ! GPGC_Marks::is_old_marked_strong_live(ref) ) {
        free_stub(nms);
      } else {
        // Any NativeMethodStub we don't free, we instead must mark through the objectRef to
        // get consistent NMT bits and remapped addresses.
        GPGC_OldCollector::mark_to_live(nms->oop_addr());
      }
    }
  }
}
