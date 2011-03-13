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


#include "bytecode.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "commonAsm.hpp"
#include "frame.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_layout.hpp"
#include "interfaceSupport.hpp"
#include "interpreter_pd.hpp"
#include "javaCalls.hpp"
#include "javaFrameAnchor.hpp"
#include "lvb.hpp"
#include "methodOop.hpp"
#include "ostream.hpp"
#include "pcMap.hpp"
#include "resourceArea.hpp"
#include "signature.hpp"
#include "stubCodeGenerator.hpp"
#include "stubRoutines.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "frame_pd.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"

#include "oop.inline2.hpp"

#ifdef AZ_PROFILER
#include <azprof/azprof_demangle.hpp>
#endif // AZ_PROFILER

bool frame::entry_frame_is_first() const {
return entry_frame_call_wrapper()->anchor()->root_Java_frame();
}


frame frame::sender_entry_frame()const{
  assert(!entry_frame_is_first(), "next Java fp must be non zero");
  // Java frame called from C; skip all C frames and return top C
  // frame of that chunk as the sender
JavaFrameAnchor*jfa=entry_frame_call_wrapper()->anchor_known_walkable();
  return jfa->pd_last_frame();
}

frame frame::java_sender() const {
frame s=sender();
while(!s.is_java_frame()&&!s.is_first_frame())
s=s.sender();
  guarantee(s.is_java_frame(), "tried to get caller of first java frame");
  return s;
}

// Interpreter frames

const char* frame::debug_discovery(CodeBlob **pcb, const DebugScope **pscope, methodOop *pmoop) const {
if(sp()==NULL)return"EMPTY";
CodeBlob*_cb=CodeCache::find_blob(pc());
  *pcb = _cb;
*pscope=NULL;
*pmoop=NULL;

  if( is_interpreted_frame() ) {
    methodOop m = lvb_methodRef(interpreter_frame_method_addr()).as_methodOop();
    *pmoop = m;
return _cb->name();
  } 
if(_cb==NULL)return"UNKNOWN";
  const char *name = StubCodeDesc::name_for(pc());
  if (name) return name;

  methodCodeRef mcr = _cb->owner();
  *pmoop = mcr.not_null() ? mcr.as_methodCodeOop()->method().as_methodOop() : (methodOop)NULL;
if(_cb->is_methodCode()){
    const DebugMap *dm = mcr.as_methodCodeOop()->_debuginfo;
    if( dm ) {
      const DebugScope *ds = dm->get(pc()-(address)_cb);
      if( (intptr_t)ds != NO_MAPPING ) *pscope = ds;
    }
  }
return _cb->name();
}

#ifndef PRODUCT

// Print stack, assuming current thread
void frame::ps(int depth) const {
  ResetNoHandleMark rnhm;
  ResourceMark rm;
Thread*t=Thread::current();
if(!t->is_Java_thread()){
tty->print_cr("Current thread is not a JavaThread.  Need a JavaThread ptr");
    return;
  }
  ps((JavaThread*)t,depth);
}

// Print stack with given thread.
void frame::ps(JavaThread *thread, int depth) const {
  if( depth <= 0 ) return;

  ResourceMark rm;
  HandleMark hm;
  if( (intptr_t)sp() & (wordSize-1) ) {
tty->print_cr("%p is not aligned, I do not think it is a stack pointer",sp());
    return;
  }

  // Just make sure things are visible.
  if( thread != 0 ) {
    if( Thread::current() == thread ) os::make_self_walkable();
    else                              os::make_remote_walkable(thread);
  }

  CodeBlob *_cb;
  const DebugScope *scope;
methodOop moop;
  const char *frame_style = debug_discovery(&_cb,&scope,&moop);

  // Frame header
  if( moop ) tty->print("====== %s: %s =======\n", frame_style, moop->name_and_sig_as_C_string() );
else tty->print("============ %s ============\n",frame_style);

  // Dump out the inline tree for compiled frames.
  if( scope ) scope->print_inline_tree(tty);
  //int stretch_in_bytes = _cb ? (((sender_sp() - sp())<<3) - _cb->framesize_bytes()) : 0;
  //int stretch_in_slots = stretch_in_bytes<<2;
  //if( is_compiled_frame() && stretch_in_slots ) {
  //  // Need to deal with this...
  //  tty->print("!!! stretched !!!\n");
  //}

  // ==========================================================
  // Print out extensive platform-specific frame guts
  pd_ps(thread,scope,moop);
  // ==========================================================


  if( depth == 1 ) return;      // No sender call
  if( depth > 999 ) return;     // Junk depth arg?
  frame fr = sender();          // Try a sender call
if(fr.is_first_frame()){
tty->print_cr("============ NO MORE FRAMES ============\n");
    return;
  }
  fr.ps(thread,depth-1);
}
#endif // !PRODUCT

void frame::print_value_on(outputStream* st, JavaThread *thread) const {
  NOT_PRODUCT(address begin = pc()-40;)
  NOT_PRODUCT(address end   = NULL;)

  CodeBlob *_cb;
  const DebugScope *scope;
methodOop moop;
  const char *frame_style = debug_discovery(&_cb,&scope,&moop);

  st->print("%s frame (sp=" PTR_FORMAT, frame_style, sp());
  if (sp() != NULL)
st->print(", pc="PTR_FORMAT,pc());

  if (StubRoutines::contains(pc())) {
    st->print_cr(")");
    st->print("(");
    StubCodeDesc* desc = StubCodeDesc::desc_for(pc());
    st->print("~Stub::%s", desc->name());
    NOT_PRODUCT(begin = desc->begin(); end = desc->end();)
  } else if (Interpreter::contains(pc())) {
    st->print_cr(")");
    st->print("(");
    InterpreterCodelet* desc = Interpreter::codelet_containing(pc());
    if (desc != NULL) {
      st->print("~");
      desc->print();
      NOT_PRODUCT(begin = desc->code_begin(); end = desc->code_end();)
    } else {
      st->print("~interpreter"); 
    }
  }
  st->print_cr(")");

  if (_cb != NULL) {
    st->print("     ");
    st->cr();
#ifndef PRODUCT
    if (end == NULL) {
begin=_cb->code_begins();
end=_cb->end();
    }
#endif
  }
}


void frame::print_on(outputStream* st) const {
  print_value_on(st,NULL);
}


// Return whether the frame is in the VM or os indicating a Hotspot problem.
// Otherwise, it's likely a bug in the native library that the Java code calls,
// hopefully indicating where to submit bugs.
static void print_C_frame(outputStream* st, char* buf, int buflen, address pc) {
  // C/C++ frame
  bool in_vm = os::address_is_in_vm(pc);
  st->print(in_vm ? "V" : "C");

  int offset;
  size_t size;
  bool found;

  // libname
found=os::dll_address_to_library_name(pc,buf,buflen,&offset,&size);
  if (found) {
    // skip directory names
    const char *p1, *p2;
    p1 = buf;
    int len = (int)strlen(os::file_separator());
    while ((p2 = strstr(p1, os::file_separator())) != NULL) p1 = p2 + len;
    st->print("  [%s+0x%x]", p1, offset);
  } else {
    st->print("  " PTR_FORMAT, pc);
  }

  found = os::dll_address_to_function_name(pc, buf, buflen, &offset, &size);

  if (found) {
#ifdef AZ_PROFILER
    // TODO: some day demangling should be made to work reliably, and not crash on
    // unusual mangled names.  In the mean time we comment out the buffer to keep
    // the error reporting memory requirements low.  When demangling is turned back
    // on, we need to revise the alt-sig-stack size to be bigger, because the current
    // size is apparently not enough, and we get a SEGV in the midst of error printing.
    //
    // char demangled[O_BUFLEN];
    // int status = azprof::Demangler::demangle(buf, demangled, sizeof(demangled));
    // 
    // if (status == 0) {
    //   st->print("  %s+0x%x", demangled, offset);
    // } else {
    //   st->print("  %s+0x%x", buf, offset);
    // }
#endif // AZ_PROFILER
    st->print("  %s+0x%x", buf, offset);
  }
}

// frame::print_on_error() is called by fatal error handler. Notice that we may 
// crash inside this function if stack frame is corrupted. The fatal error 
// handler can catch and handle the crash. Here we assume the frame is valid.
//
// First letter indicates type of the frame:
//    J: Java frame (compiled)
//   Jo: osr method
//   Jn: native method
//    j: Java frame (interpreted)
//    V: VM frame (C/C++)
//    v: Other frames running VM generated code (e.g. stubs, adapters, etc.)
//    C: C/C++ frame
//
// We don't need detailed frame type as that in frame::print_name(). "C"
// suggests the problem is in user lib; everything else is likely a VM bug.

void frame::print_on_error(outputStream* st, char* buf, int buflen, bool verbose) const {
  ThreadInVMfromError tivfe; 

  if (Interpreter::contains(pc())) {
    methodOop m = tivfe.safe_to_print() ? this->interpreter_frame_method() : NULL;
    if (m != NULL) {
      m->name_and_sig_as_C_string(buf, buflen);
st->print("j        %s",buf);
      st->print("+%d", this->interpreter_frame_bci());
    } else {
st->print("j        "PTR_FORMAT,pc());
    }

  } else if (StubRoutines::contains(pc())) {
    StubCodeDesc* desc = StubCodeDesc::desc_for(pc());
    if (desc != NULL) {
st->print("v        ~StubRoutines::%s",desc->name());
    } else {
st->print("v        ~StubRoutines::"PTR_FORMAT,pc());
    }

}else if(CodeCache::contains(pc())){
CodeBlob*_cb=CodeCache::find_blob(pc());
if(_cb!=NULL){
if(_cb->is_methodCode()){
      const char* type = "J ";
      char typebuf[10];
if(_cb->is_native_method()){
        type = "Jn";
        jio_snprintf(typebuf, sizeof(typebuf), "%s     ", type);
      } else {
        if (_cb->owner().as_methodCodeOop()->is_osr_method()) type = "Jo";
        jio_snprintf(typebuf, sizeof(typebuf), "%s (%s)", type, (_cb->is_c1_method() ? "C1" : "C2"));
      }
      methodOop m = tivfe.safe_to_print() ? _cb->owner().as_methodCodeOop()->method().as_methodOop() : NULL;
      if (m != NULL) {
        m->name_and_sig_as_C_string(buf, buflen);
st->print("%s  %s",typebuf,buf);
      } else {
st->print("%s        "PTR_FORMAT,typebuf,pc());
      }
    } else {
st->print("v        blob "PTR_FORMAT,pc());
    }
    } else {  // CodeCache contains pc, but cannot find...
st->print("v        "PTR_FORMAT,pc());
    }

  } else {
    print_C_frame(st, buf, buflen, pc());
  }
}


objectRef* frame::interpreter_callee_receiver_addr(symbolHandle signature) {
  ArgumentSizeComputer asc(signature);
  int size = asc.size();  
  return (objectRef*)interpreter_frame_tos_at(size); 
}

void frame::oops_interpreted_do(JavaThread* thread, OopClosure* f) {
  assert(is_interpreted_frame(), "Not an interpreted frame");
  methodOop m   = interpreter_frame_method();

assert(Universe::heap()->is_in(m),"must be valid oop");
  assert(m->is_method(), "checking frame value");
  debug_only( int bci = interpreter_frame_bci() );
  assert((m->is_native() && bci == 0)  || (!m->is_native() && bci >= 0 && bci < m->code_size()), "invalid bci value");

  // Note: interpreter stack scanning is done elsewhere, 
  // and done in bulk - not frame-by-frame.

  // Note that we need to do this *after* the tagged stack scan.
  // The scanning code uses both the method and its cache, so if we
  // update the pointers before scanning the stack, the scan will get
  // invalid data.
  f->do_oop((objectRef*)interpreter_frame_method_addr());
  f->do_oop((objectRef*)interpreter_frame_cache_addr ());
}


// Normal oops-do for frames
void frame::oops_do(JavaThread *thread, OopClosure* f) {
  if (is_interpreted_frame()) { 
    AuditTrail::log(thread, AuditTrail::INTERPRETED_FRAME, intptr_t(this->_sp), intptr_t(this->_pc));
    oops_interpreted_do(thread, f);

  } else if (is_entry_frame()) {
    AuditTrail::log(thread, AuditTrail::ENTRY_FRAME, intptr_t(this->_sp), intptr_t(this->_pc));
    // Traverse the Handle Block saved in the entry frame
    entry_frame_call_wrapper()->oops_do(f);

  } else {                      // Must be a CodeBlob
CodeBlob*_cb=CodeCache::find_blob(pc());
    AuditTrail::log(thread, AuditTrail::CODEBLOB_FRAME, intptr_t(this->_sp), intptr_t(this->_pc), intptr_t(_cb));
    if (_cb->has_oopmaps()) {    // do oops in this frame
      OopMapStream::do_closure(_cb,*this,f);
    }
    // In cases where perm gen is collected, GC will want to mark oops
    // referenced from methodCodeOops active on thread stacks so as to prevent
    // them from being collected.  However, this visit should be restricted to
    // certain phases of the collection only.  The closure answers whether it
    // wants methodCodeOops to be traced.
    _cb->oops_do(f);
  }  
}


// oops-do just for arguments when args are passed but the caller is
// not yet claiming responsibility for them (e.g., mid-call resolution).
void frame::oops_arguments_do(OopClosure*f){
  if (is_interpreted_frame()) { 
    // collected as part of the tagged stack scan
  } else if (is_entry_frame()) {
    // No GC points during the construction of the entry_frame and
    // then we call the interpreter - which is scanned elsewhere.
  } else {
    // Preserve potential arguments for a callee.  We handle this by
    // dispatching on the codeblob.
    CodeCache::find_blob(pc())->oops_arguments_do(*this, f);
  }  
}

void frame::methodCodes_code_blob_do(){
    Unimplemented();
//  CodeBlob* _cb = CodeCache::find_blob(pc());
//  assert(_cb != NULL, "need something to work with...");
//
//  // If we see an activation belonging to a non_entrant methodCodeOop, we mark it.
//  if (_cb->is_methodCode() && (UseGenPauselessGC || ((methodCode *)_cb)->is_not_entrant())) {
//    ((methodCode*)_cb)->mark_as_seen_on_stack();
//  }
}


void frame::methodCodes_do(){
  if( CodeCache::contains(pc()) &&
      !is_interpreted_frame() &&
      !is_entry_frame() )
    methodCodes_code_blob_do();
}
void frame::gc_prologue( JavaThread *thr ) {
}

// A version of gc_prologue() for use by GC mode threads garbage collecting a JavaThread's stack.
void frame::GPGC_gc_prologue(JavaThread* thr, OopClosure* f) {
}


void frame::GPGC_mutator_gc_prologue(JavaThread* thr, OopClosure* f) {
}

void frame::gc_epilogue( JavaThread *thr) {
}

// --- locked ----------------------
// Count times this oop is locked in this frame.  Generally used to
// inflate a biased-lock into a real monitor.
int frame::locked( oop o ) const {
  if( is_interpreted_frame() ) {
    // interpreter locks kept elsewhere
    return 0;
  } else {
    return CodeCache::find_blob(pc())->locked(*this,o);
  }
}

void frame::verify(JavaThread*thread){
  // for now make sure receiver type is correct
  if (is_interpreted_frame()) {
    methodOop method = interpreter_frame_method();
    guarantee(method->is_method(), "method is wrong in frame::verify");
    if (!method->is_static()) {
      // fetch the receiver
objectRef p=*(objectRef*)interpreter_frame_local_addr(0);
      // oddly enough a null receiver is OK, if the frame came from a
      // deoptimization and the JITs proved the receiver was dead.
      assert0( p.is_null() || p.as_oop() );
      // make sure we have the right receiver type
    }
  }
  oops_do(thread, &VerifyOopClosure::verify_oop);
#ifndef PRODUCT
assert(DerivedPointerTable::is_empty(),"must be empty before verify");
#endif
}


#ifdef ASSERT
bool frame::verify_return_pc(address x) {
if(StubRoutines::returns_to_call_stub(x))return true;
if(CodeCache::contains(x))return true;
if(Interpreter::contains(x))return true;
  return false;
}
#endif


//-----------------------------------------------------------------------------------
// StackFrameStream implementation

StackFrameStream::StackFrameStream(JavaThread *thread) : _fr(thread->last_frame()), _is_done(false) {
}

