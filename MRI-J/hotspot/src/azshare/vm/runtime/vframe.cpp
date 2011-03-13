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


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

#include "codeCache.hpp"
#include "jvmtiExport.hpp"
#include "commonAsm.hpp"
#include "safepoint.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "vframe.hpp"
#include "vreg.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

#include "oop.inline2.hpp"

// --- vframe ----------------------------------------------------------------
vframe::vframe(frame fr) : _fr(fr), _ds(LAZY_DEBUG), _sbafid(-1) { }

// --- vframe ----------------------------------------------------------------
vframe::vframe(JavaThread *thread) : 
  _fr(thread->root_Java_frame() ? frame(0,0) : thread->last_frame()),
  _ds(LAZY_DEBUG),
  _sbafid(thread->curr_sbafid()) {
  if (_fr.is_runtime_frame())
    _fr = _fr.sender();
}

// --- scope -----------------------------------------------------------------
const DebugScope *vframe::scope() const {
  if( _ds == LAZY_DEBUG )
    // Conceptually scope() is a 'const' routine, but really it sets a
    // cache under the hood.
    ((vframe*)this)->_ds = CodeCache::find_blob(_fr.pc())->debuginfo(_fr.pc());
  return _ds;
}

// --- next_older ------------------------------------------------------------
// Skip to the next visible Java frame.
// Makes visible inlined compiled frames.
// Skips non-Java frames.
vframe vframe::next_older() const {
assert(!done(),"asking for next but there is no next vframe");
  // Can we walk up inlined Java frames?
if(_fr.is_compiled_frame()){
    const DebugScope *ds = scope();
    if( ds->caller() ) return vframe(_fr,ds->caller(),_sbafid); // Next virtual caller
  }
  // Time to skip a real physical frame
  frame fr = _fr.sender();
  int sbafid = _sbafid;
  while( 1 ) {
    Untested("");
    if( fr.is_java_frame() || fr.is_first_frame() ) {
#ifdef ASSERT
      assert(!_fr.is_runtime_frame(), "unexpected runtime stub");
#endif
      return vframe(fr, LAZY_DEBUG, sbafid);
    }
    if( fr.is_entry_frame() ) sbafid--; // Each entry frame pushes an SBA frame
    fr = fr.sender();
  }
}

// --- next ------------------------------------------------------------------
// Update to the next Java frame in-place
void vframe::next(){
assert(!done(),"asking for next but there is no next vframe");
  // Can we walk up inlined Java frames?
if(_fr.is_compiled_frame()){
    scope();                    // Compute current debug scope
    _ds = _ds->caller();        // Next virtual caller
    if( _ds ) return;           // have one?
    _ds = LAZY_DEBUG;           // Reset for next time
  }
  // Time to skip a real physical frame
  _fr = _fr.sender();
  while( 1 ) {
    if( _fr.is_java_frame() || _fr.is_first_frame() ) return;
    if( _fr.is_entry_frame() ) _sbafid--; // Each entry frame pushes an SBA frame
    _fr = _fr.sender();
  }
#ifdef ASSERT
  assert(!_fr.is_runtime_frame(), "unexpected runtime stub");
#endif
}

// --- method_ref ------------------------------------------------------------
methodRef vframe::method_ref() const {
  if( _ds != LAZY_DEBUG ) return _ds->method(); // cheap test, cheap result
  if (_fr.is_interpreted_frame()) // cheap test, cheap result
    return _fr.interpreter_frame_methodRef();
CodeBlob*cb=CodeCache::find_blob(_fr.pc());
  if (cb->is_native_method())   // A little expensive to do a CC lookup
return cb->method();
return scope()->method();
}

// --- bci -------------------------------------------------------------------
int vframe::bci()const{
  if( _ds != LAZY_DEBUG ) return _ds->bci(); // cheap test, cheap result
  if (_fr.is_interpreted_frame()) // cheap test, cheap result
    return _fr.interpreter_frame_bci();
CodeBlob*cb=CodeCache::find_blob(_fr.pc());
  if (cb->is_native_method())   // A little expensive to do a CC lookup
    return 0;
  return scope()->bci();
}

// --- set_buf ---------------------------------------------------------------
static void set_buf(intptr_t* buf, jvmtiDeferredLocalVariable* val) {
  switch (val->type()) {
    case T_BOOLEAN:
      *buf = (intptr_t)val->value().z; break;
    case T_FLOAT:
    {
      jfloat jf = val->value().f;
      jint   ji = *(jint*) &jf;
      *buf = (intptr_t)ji; break;
    }
    case T_DOUBLE:
    {
      jint* halfs = (jint*) &buf[-1];
#ifdef VM_LITTLE_ENDIAN
      halfs[1] = (jint)frame::double_slot_primitive_type_empty_slot_id;
      halfs[0] = 0;
#else
      halfs[0] = (jint)frame::double_slot_primitive_type_empty_slot_id;
      halfs[1] = 0;
#endif
      *(jdouble*) buf = val->value().d;
      break;
    }
    case T_CHAR:
      *buf = (intptr_t)val->value().c; break;
    case T_BYTE:
      *buf = (intptr_t)val->value().b; break;
    case T_SHORT:
      *buf = (intptr_t)val->value().s; break;
    case T_INT:
      *buf = (intptr_t)val->value().i; break;
    case T_LONG:
    {
      jint* halfs = (jint*) &buf[-1];
#ifdef VM_LITTLE_ENDIAN
      halfs[1] = (jint)frame::double_slot_primitive_type_empty_slot_id;
      halfs[0] = 0;
#else
      halfs[0] = (jint)frame::double_slot_primitive_type_empty_slot_id;
      halfs[1] = 0;
#endif
      *(intptr_t*) buf = (intptr_t)val->value().j;
      break;
    }
case T_OBJECT://fall-through
    case T_ARRAY:
    {
      objectRef ref = objectRef((uint64_t)val->value().l);
      *buf = lvb_ref(&ref).raw_value();
      break;
    }
    default:
      ShouldNotReachHere();
  }
}

// --- java_local
intptr_t vframe::java_local(JavaThread *thread, int index) const {
if(_fr.is_compiled_frame()){
    // In rare instances set_locals may have occurred in which case
    // there are local values that are not described by the ScopeValue anymore
    GrowableArray<jvmtiDeferredLocalVariable*>* deferred = NULL;
    GrowableArray<jvmtiDeferredLocalVariableSet*>* list = thread->deferred_locals();

    if (list != NULL ) {
      // In real life this never happens or is typically a single element search
      Unimplemented();
//      for (int i = 0; i < list->length(); i++) {
//        if (list->at(i)->matches((vframe*)this)) {
//          deferred = list->at(i)->locals();
//          break;
//        }
//      }
//
//      if (deferred != NULL) {
//        jvmtiDeferredLocalVariable* val = NULL;
//
//        // Iterate through the deferred locals until we find our desired index
//        int i = 0;
//        while (i < deferred->length()) {
//          val = deferred->at(i);
//          if (val->index() == index) {
//            if (buf != NULL) {
//              set_buf(buf, val);
//            }
//            return val->value();
//          }
//          i++;
//        }
//      }
    }
    DebugScopeValue::Name vreg = scope()->get_local(index);
    if( !DebugScopeValue::is_valid(vreg) ) return 0;
    if( !DebugScopeValue::is_vreg(vreg) ) {
Unimplemented();//debug info constants?
    }
    return *get_frame().reg_to_addr(DebugScopeValue::to_vreg(vreg));
  } 
  return _fr.interpreter_frame_local_at(index);
}


void vframe::update_local(JavaThread* thread, BasicType type, int index, jvalue value) {
  frame fr = this->get_frame();

  // AZUL - We use extra slots to accomodate tags for longs and doubles
  // in the compiler as well.
if(type==T_LONG||type==T_DOUBLE){
index=index+1;
  }

#ifdef ASSERT
  Unimplemented();
  //CodeBlob* b = CodeCache::find_blob(fr.pc());
  //assert(b->is_patched_for_deopt(), "frame must be scheduled for deoptimization");
#endif /* ASSERT */
GrowableArray<jvmtiDeferredLocalVariableSet*>*deferred=thread->deferred_locals();
  if (deferred != NULL ) {
    // See if this vframe has already had locals with deferred writes
    int f;
    for ( f = 0 ; f < deferred->length() ; f++ ) {
      if (deferred->at(f)->matches(this)) {
	// Matching, vframe now see if the local already had deferred write
	GrowableArray<jvmtiDeferredLocalVariable*>* locals = deferred->at(f)->locals();
	int l;
	for (l = 0 ; l < locals->length() ; l++ ) {
	  if (locals->at(l)->index() == index) {
	    locals->at(l)->set_value(value);
	    return;
	  }
	}
	// No matching local already present. Push a new value onto the deferred collection
	locals->push(new jvmtiDeferredLocalVariable(index, type, value));
	return;
      }
    }
    // No matching vframe must push a new vframe
  } else {
    // No deferred updates pending for this thread.
    // allocate in C heap
    deferred =  new(ResourceObj::C_HEAP) GrowableArray<jvmtiDeferredLocalVariableSet*> (1, true);
    thread->set_deferred_locals(deferred);
  }
  // Because the frame is patched for deopt and we will push in
  // registers in uncommon_trap, we will use the sender's sp to compare
  deferred->push(new jvmtiDeferredLocalVariableSet(method(), bci(), fr.pd_sender().sp()));
  assert(deferred->top()->id() == fr.pd_sender().sp(), "Huh? Must match");
  deferred->top()->set_local_at(index, type, value);
}


void vframe::set_java_local(int index, jvalue value, BasicType type) {
  assert0(_fr.is_interpreted_frame());
  intptr_t buf = 0;
  address addr = ((address)(&buf)) + sizeof(intptr_t);
  JavaValue v(type);
  switch (type) {
    case T_BOOLEAN:  *(jboolean *)(addr - sizeof(jboolean)) = value.z; break;
    case T_FLOAT:    *(jfloat *)(addr - sizeof(jfloat))     = value.f; break;
    case T_DOUBLE:   *(jdouble *)(addr - sizeof(jdouble))   = value.d; break;

      // these need to be sign-extended to 64 bits
    case T_CHAR:     *(intptr_t *)(addr - sizeof(intptr_t)) = (intptr_t)value.c; break;
    case T_BYTE:     *(intptr_t *)(addr - sizeof(intptr_t)) = (intptr_t)value.b; break;
    case T_SHORT:    *(intptr_t *)(addr - sizeof(intptr_t)) = (intptr_t)value.s; break;
    case T_INT:      *(intptr_t *)(addr - sizeof(intptr_t)) = (intptr_t)value.i; break;
    case T_LONG:     *(intptr_t *)(addr - sizeof(intptr_t)) = (intptr_t)value.j; break;

case T_OBJECT://fall-through
    case T_ARRAY:    *((objectRef*)(addr - sizeof(jobject))) = *((objectRef*) value.l); break;
    default:         ShouldNotReachHere();
  }
  // Azul: in interpreter we use two stack slots for doubles, longs --
  // value in 2nd slot -- so use index+1 for them
if(type==T_LONG||type==T_DOUBLE){
    get_frame().set_interpreter_frame_local_at(index+1, buf);
  } else {
    get_frame().set_interpreter_frame_local_at(index, buf);
  }
}


// True if this is the top-most compiled vframe for this physical frame, such
// that a 'next' call will change to a new physical frame.  Used by
// deoptimization.
bool vframe::top_inlined_frame() const {
  assert0( _fr.is_compiled_frame());
  return !scope()->caller();
}


GrowableArray<objectRef>* vframe::monitors(JavaThread* thread) const {
GrowableArray<objectRef>*result=new GrowableArray<objectRef>(5);
  frame fr = get_frame();

  if (fr.is_interpreted_frame()) {
    Unimplemented();
    //for( objectRef* mptr = fr.interpreter_frame_monitor_begin(); mptr->raw_value() != (uint64_t)frame::mons_end_sentinel; mptr-- )
    //  result->push(*mptr);

}else if(fr.is_native_frame()){
    Unimplemented();
//    CodeBlob* b = CodeCache::find_blob(fr.pc());
//    assert0( b->is_native_method() );
//    methodOop moop = b->owner().as_methodCodeOop()->method().as_methodOop();
//    oop obj;
//    if (moop->is_synchronized()) {
//      oop locked_oop;
//      if (moop->is_static()) {
//        locked_oop = Klass::cast(moop->method_holder())->java_mirror();
//      } else { 
//        // look in oop map for call to native method
//        OopMapSet* oopmaps = nm->oop_maps();
//        int pc_offset = fr.pc() - nm->instructions_begin();
//        OopMap* oopmap = oopmaps->find_map_at_offset(pc_offset); 
//        // Get the first oop (receiver) out of the map
//        OopMapStream oms(oopmap,OopMapValue::oop_value);
//        oms.next();               // Warmup the stream
//        OopMapValue omv = oms.current();
//        assert0(omv.type() == OopMapValue::oop_value);
//        objectRef* loc = fr.reg_to_addr_oop(thread, omv.reg(), 0/*stretch_in_slots*/);
//        locked_oop = (*loc).as_oop();
//      }
//      if (locked_oop != NULL) { // ?
//        BasicLock* bl = locked_oop->mark()->locker();
//        result->push(new MonitorInfo(locked_oop, bl));
//      }
//    }
//
//  } else {         // compiled frame
//
//  Unimplemented();
//    GrowableArray<MonitorValue*>* monitors = scope()->monitors();
//    if (monitors == NULL) { return result; }
//    for (int index = 0; index < monitors->length(); index++) {
//      uint64_t buf;
//      bool is_c1 = nm()->is_compiled_by_c1();
//      MonitorValue* mv = monitors->at(index);
//      mv->owner()->get(thread, (intptr_t*)&buf, fr, is_c1);
//      objectRef locked_ref = objectRef(buf);
//      oop locked_oop = locked_ref.as_oop();
//      assert(locked_oop == NULL || !locked_oop->is_unlocked(), "object must be null or locked");
//      BasicLock *bl = (BasicLock*)fr.reg_to_addr(thread,mv->basic_lock().vreg(),0);
//      
//      if (locked_oop != NULL) { // can this happen?
//        result->push(new MonitorInfo(locked_oop, bl));
//      }
//    }
//
  }

  return result;
}


//GrowableArray<objectRef>* vframe::locked_monitors() const {
//  GrowableArray<objectRef>* locks = NULL; //monitors(thread);
//  // Do not count a last monitor if it is not locked yet
//  Unimplemented();
//  return locks;
//}


//-------- Print TTY methods  -----------------------------------------------------------------------------------------

// Stop trying to print stacks to the tty or if so print it using its own functions.
void vframe::print(JavaThread* thread) const
{
  vframe::print_on(thread, tty);
}

void vframe::print_on(JavaThread* thread, outputStream *st) const
{
  // RKANE: Do we need a ResourceMark object here?  
  ResourceMark rm;

  frame fr = get_frame();
  const char *frame_style = "UNKNOWN";
CodeBlob*cb=CodeCache::find_blob(fr.pc());
  if( fr.is_interpreted_frame() ) 
    frame_style = "INTERPRETER";
  else if( cb && cb->is_native_method() ) 
    frame_style = "NATIVE";
  else if( cb && cb->is_c1_method() )
    frame_style = "COMPILER1";
  else if( cb && cb->is_c2_method() )
    frame_style = "COMPILER2";

  if (!st) st=tty;
  st->print("====== %s: %s @ %d =======\n", frame_style, method()->name_and_sig_as_C_string(), bci() );
st->print_cr(" ### NOT printing any further information on tty.");
st->print_cr("Stop trying to print stacks to the tty or if so print it using its own functions.");
  return; 
}


static bool print_lock( bool first, objectRef o, bool is_object_wait, outputStream *st) {
  if( o.is_null() || (!(o.is_stack() || o.is_heap())) ) return first; // Ignore null
  if( !st ) st=tty;
  const char *msg = is_object_wait ? "waiting on" : (first ? "waiting to lock" : "locked");
  const char *name = o.as_oop()->klass()->klass_part()->external_name();
  st->print_cr("\t- %s <" INTPTR_FORMAT "> (a %s)", msg, o.raw_value(), name );
  return false;
}

void vframe::print_lock_info(JavaThread* thread, bool youngest, outputStream *st) {
  ResourceMark rm;
  frame fr = get_frame();       // Shorthand notation

  // First, assume we have the monitor locked.  If we haven't found an owned
  // monitor before and this is the first frame, then we need to see if the
  // thread is blocked.
  bool first = (youngest && thread->is_hint_blocked());

  // Print out all monitors that we have locked or are trying to lock
  if( fr.is_interpreted_frame() ) {
    int x = fr.interpreter_frame_monitor_count();
    // Not Correct; this always (re)prints the most recent X monitors
for(int i=0;i<x;i++){
      first = print_lock( first, ALWAYS_UNPOISON_OBJECTREF(thread->_lckstk_top[-i-1]), false, st );
    }      
    
}else if(fr.is_native_frame()){
CodeBlob*cb=CodeCache::find_blob(fr.pc());
    assert0( cb->is_native_method() );
    methodCodeOop mco = cb->owner().as_methodCodeOop();
    methodOop moop = mco->method().as_methodOop();
    bool is_object_wait = youngest && moop->name() == vmSymbols::wait_name() && 
      instanceKlass::cast(moop->method_holder())->name() == vmSymbols::java_lang_Object();
    if( moop->is_synchronized() && moop->is_static() ) {
      first = print_lock( first, objectRef(Klass::cast(moop->method_holder())->java_mirror()), false, st );
    } else if( is_object_wait ) {
      // For synchronized native methods, there should be a single lock.
      // For object.wait, there is a single oop argument being wait'd upon.
      const RegMap *lm = cb->oop_maps();
      VOopReg::VR lck = lm->get_sole_oop(cb->rel_pc(fr.pc()));
      objectRef *loc = fr.reg_to_addr_oop(lck);
      first = print_lock( first, *loc, is_object_wait, st );
    } else if( moop->is_synchronized() ) {
      // For synchronized native methods, there should be a single lock.
      const DebugScope *ds = scope();
      DebugScopeValue::Name lck = ds->get_lock(0);
      objectRef *loc = (objectRef*)fr.reg_to_addr(DebugScopeValue::to_vreg(lck));
      first = print_lock( first, *loc, is_object_wait, st );
    } else if (thread->current_park_blocker() != NULL) {
oop obj=thread->current_park_blocker();
      first = print_lock( first, objectRef(obj), false, st );
    }

  } else {                      // Hopefully a compiled frame
    const DebugScope *ds = scope();
for(uint i=0;i<ds->numlocks();i++){
      DebugScopeValue::Name lck = ds->get_lock(i);
      first = print_lock( first, *fr.reg_to_addr(DebugScopeValue::to_vreg(lck)), false, st );
    }
  }
}



//---------xml output----------------------------------------------------------------------------
void vframe::print_to_xml(JavaThread* thread, xmlBuffer *xb) const {
  ResourceMark rm;
assert(!xb,"vframe::print_to_xml() called with NULL xmlBuffer pointer.");
  frame fr = get_frame();
  const char *frame_style = "UNKNOWN";
CodeBlob*cb=CodeCache::find_blob(fr.pc());
  if( fr.is_interpreted_frame() ) 
    frame_style = "INTERPRETER";
  else if( cb && cb->is_native_method() ) 
    frame_style = "NATIVE";
  else if( cb && cb->is_c1_method() )
    frame_style = "COMPILER1";
  else if( cb && cb->is_c2_method() )
    frame_style = "COMPILER2";

  // Frame header
  xb->print("====== %s: %s @ %d =======\n", frame_style, method()->name_and_sig_as_C_string(), bci() );
xb->print_cr(" ### NOT printing any further information on tty.");
xb->print_cr("Stop trying to print stacks to the tty or if so print it using its own functions.");
  return; 
}


static bool print_to_xml_lock( bool first, objectRef o, bool is_object_wait, xmlBuffer* xb ) {
  if( o.is_null() || (!(o.is_stack() || o.is_heap())) ) return first; // Ignore null
  const char *tag = is_object_wait ? "wait" : (first ? "block" : "lock");
  xmlElement xl(xb, tag);
  o.as_oop()->print_xml_on(xb, true);
  xb->name_ptr_item("raw_value", o.as_oop());
  return false;
}

void vframe::print_to_xml_lock_info(JavaThread* thread, bool youngest, xmlBuffer* xb) {
  ResourceMark rm;
  frame fr = get_frame();       // Shorthand notation

  // First, assume we have the monitor locked.  If we haven't found an owned
  // monitor before and this is the first frame, then we need to see if the
  // thread is blocked.
  bool first = (youngest && thread->is_hint_blocked());

  // Print out all monitors that we have locked or are trying to lock
  if( fr.is_interpreted_frame() ) {
    int x = fr.interpreter_frame_monitor_count();
    // Not Correct; this always (re)prints the most recent X monitors
for(int i=0;i<x;i++){
      first = print_to_xml_lock( first, ALWAYS_UNPOISON_OBJECTREF(thread->_lckstk_top[-i-1]), false, xb );
    }      
    
}else if(fr.is_native_frame()){
CodeBlob*cb=CodeCache::find_blob(fr.pc());
    assert0( cb->is_native_method() );
    methodCodeOop mco = cb->owner().as_methodCodeOop();
    methodOop moop = mco->method().as_methodOop();
    bool is_object_wait = youngest && moop->name() == vmSymbols::wait_name() && 
      instanceKlass::cast(moop->method_holder())->name() == vmSymbols::java_lang_Object();
    if( moop->is_synchronized() && moop->is_static() ) {
      first = print_to_xml_lock( first, objectRef(Klass::cast(moop->method_holder())->java_mirror()), false, xb );
    } else if( is_object_wait ) {
      // For synchronized native methods, there should be a single lock.
      // For object.wait, there is a single oop argument being wait'd upon.
      const RegMap *lm = cb->oop_maps();
      VOopReg::VR lck = lm->get_sole_oop(cb->rel_pc(fr.pc()));
      objectRef *loc = fr.reg_to_addr_oop(lck);
      first = print_to_xml_lock( first, *loc, is_object_wait, xb );
    } else if( moop->is_synchronized() ) {
      // For synchronized native methods, there should be a single lock.
      const DebugScope *ds = scope();
      DebugScopeValue::Name lck = ds->get_lock(0);
      objectRef *loc = (objectRef*)fr.reg_to_addr(DebugScopeValue::to_vreg(lck));
      first = print_to_xml_lock( first, *loc, is_object_wait, xb );
    } else if (thread->current_park_blocker() != NULL) {
oop obj=thread->current_park_blocker();
      first = print_to_xml_lock( first, objectRef(obj), false, xb );
    }

  } else {                      // Hopefully a compiled frame
    const DebugScope *ds = scope();
for(uint i=0;i<ds->numlocks();i++){
      DebugScopeValue::Name lck = ds->get_lock(i);
      first = print_to_xml_lock( first, *fr.reg_to_addr(DebugScopeValue::to_vreg(lck)), false, xb );
    }
  }
}


void vframe::print_xml_on(JavaThread *thread, xmlBuffer *xb) const {
  ResourceMark rm;
  frame fr = get_frame();

  const char *frame_style = "UNKNOWN";
CodeBlob*cb=CodeCache::find_blob(fr.pc());
  if( fr.is_interpreted_frame() ) 
    frame_style = "INTERPRETER";
  else if( cb && cb->is_native_method() ) 
    frame_style = "NATIVE";
  else if( cb && cb->is_c1_method() )
    frame_style = "COMPILER1";
  else if( cb && cb->is_c2_method() )
    frame_style = "COMPILER2";

  // Frame header
  xb->name_value_item("frame_style",frame_style);

  // Java Locals
  if (!fr.is_java_frame()) 
    return; // no more printing to be done
  
  const methodOop moop = method();
  if (moop->is_native()) 
    return; // no more printing to be done
  
  const int max_locals = moop->max_locals();
  
if(fr.is_compiled_frame()){
    for( int i=0; i<max_locals; i++ ) {
      xmlElement je(xb, "java_element");
      { xmlElement n(xb, "name", xmlElement::delayed_LF);
        bool out_of_scope = true;
        const char* name = moop->localvariable_name(i, bci(), out_of_scope);
        if( name ) xb->print("%s%s%s", out_of_scope ? "{": "", name, out_of_scope ? "}": "");
        else       xb->print("JL%-2d", i);
      }
      DebugScopeValue::Name vreg = _ds->get_local(i);
      if( !DebugScopeValue::is_valid(vreg) ) {
        xb->name_value_item("type","invalid");
        xb->name_ptr_item("value",0);
      } else {
        intptr_t *data_ptr = fr.reg_to_addr(DebugScopeValue::to_vreg(vreg));
        const int rel_pc = cb->rel_pc(fr.pc());
        const bool isoop = cb->oop_maps()->is_oop( rel_pc, VReg::as_VOopReg(DebugScopeValue::to_vreg(vreg)) );
        if( isoop ) {
          xb->name_value_item("type", "oop");
          oop o = ((objectRef*)data_ptr)->as_oop();
          o->print_xml_on(xb, true);
        } else {
          xb->name_value_item("type", "normal");
          xb->name_ptr_item("value", (void*)*data_ptr);
        }
      }
    }
}else if(fr.is_interpreted_frame()){
    for( int i = 0; i < max_locals; i++ ) {
      frame::InterpreterStackSlotType tag = fr.tag_at_address(fr.interpreter_frame_local_addr(i));
      if (tag == frame::double_slot_primitive_type)
i++;//skip the tag slot
      xmlElement je(xb, "java_element");
      { xmlElement n(xb, "name", xmlElement::delayed_LF);
        bool out_of_scope = true;
        const char* name = moop->localvariable_name(i, bci(), out_of_scope);
        if (name != NULL) {
          xb->print("%s%s%s", out_of_scope ? "{": "", name, out_of_scope ? "}": "");
        } else {
          xb->print("JL%-2d", i);
        }
      }
      intptr_t local_value = fr.interpreter_frame_local_at(i);
      switch (tag) {
      case frame::single_slot_primitive_type: {
        xb->name_value_item("type", "single_slot_primitive_type");
        xb->name_ptr_item("value", (void*)local_value);
        break;
      }
      case frame::double_slot_primitive_type: {
        xb->name_value_item("type", "double_slot_primitive_type");
        xb->name_ptr_item("value", (void*)local_value);
        break;
      }
      case frame::single_slot_ref_type: {
        xb->name_value_item("type", "oop");
        oop o = objectRef((uint64_t)local_value).as_oop();
        o->print_xml_on(xb, true);
        break;
      }
      default: ShouldNotReachHere(); break;
      }
    }
  }
}


//----------------------------------------------------------------------------------------------------------------------
//
// Step back n frames, skip any pseudo frames in between.
// This function is used in Class.forName, Class.newInstance, Method.Invoke, 
// AccessController.doPrivileged.
//
// NOTE that in JDK 1.4 this has been exposed to Java as
// sun.reflect.Reflection.getCallerClass(), which can be inlined.
// Inlined versions must match this routine's logic. 
// Native method prefixing logic does not need to match since
// the method names don't match and inlining will not occur.
// See, for example,
// Parse::inline_native_Reflection_getCallerClass in
// opto/library_call.cpp.

// Most of the vframe operations are functional, in that they return a new
// vframe.  These versions are 'update-in-place' because that's the expected
// coding/usage style.
void vframe::security_get_caller_frame(int depth){
  while (!done()) {
    if (Universe::reflect_invoke_cache()->is_same_method(method())) {
      // This is Method.invoke() -- skip it
    } else if (Klass::cast(method()->method_holder())
                 ->is_subclass_of(SystemDictionary::reflect_method_accessor_klass())) {
      // This is an auxilary frame -- skip it
    } else {
      // This is non-excluded frame, we need to count it against the depth
      if (depth-- <= 0) {
        // we have reached the desired depth, we are done
        break;
      }
    }
    if (method()->is_prefixed_native()) {
      skip_prefixed_method_and_wrappers();
    } else {
      next();
    }
  }
}
  

// Most of the vframe operations are functional, in that they return a new
// vframe.  These versions are 'update-in-place' because that's the expected
// coding/usage style.
void vframe::skip_prefixed_method_and_wrappers(){
  ResourceMark rm;
  HandleMark hm;

  int    method_prefix_count = 0;
  char** method_prefixes;
method_prefixes=JvmtiExport::get_all_native_method_prefixes(&method_prefix_count);
  KlassHandle prefixed_klass(method()->method_holder());
  const char* prefixed_name = method()->name()->as_C_string();
  size_t prefixed_name_len = strlen(prefixed_name);
  int prefix_index = method_prefix_count-1;

  while (!done()) {
    next();
    if (method()->method_holder() != prefixed_klass()) {
      break; // classes don't match, can't be a wrapper
    }
    const char* name = method()->name()->as_C_string();
    size_t name_len = strlen(name);
    size_t prefix_len = prefixed_name_len - name_len;
    if (prefix_len <= 0 || strcmp(name, prefixed_name + prefix_len) != 0) {
      break; // prefixed name isn't prefixed version of method name, can't be a wrapper
    }
    for (; prefix_index >= 0; --prefix_index) {
      const char* possible_prefix = method_prefixes[prefix_index];
      size_t possible_prefix_len = strlen(possible_prefix);
      if (possible_prefix_len == prefix_len && 
          strncmp(possible_prefix, prefixed_name, prefix_len) == 0) {
        break; // matching prefix found
      }
    }
    if (prefix_index < 0) {
      break; // didn't find the prefix, can't be a wrapper
    }
    prefixed_name = name;
    prefixed_name_len = name_len;
  }
}


// Most of the vframe operations are functional, in that they return a new
// vframe.  These versions are 'update-in-place' because that's the expected
// coding/usage style.
void vframe::skip_reflection_related_frames() {
  while (!done() &&
         (true &&
          (Klass::cast(method()->method_holder())->is_subclass_of(SystemDictionary::reflect_method_accessor_klass()) ||
           Klass::cast(method()->method_holder())->is_subclass_of(SystemDictionary::reflect_constructor_accessor_klass())))) {
    next();
  }
}


jvmtiDeferredLocalVariableSet::jvmtiDeferredLocalVariableSet(methodRef method, int bci, intptr_t* id) {
  _method = method;
  _bci = bci;
  _id = id;
  // Always will need at least one, must be on C heap
  _locals = new(ResourceObj::C_HEAP) GrowableArray<jvmtiDeferredLocalVariable*> (1, true);
}

jvmtiDeferredLocalVariableSet::~jvmtiDeferredLocalVariableSet() {
  for (int i = 0; i < _locals->length() ; i++ ) {
    delete _locals->at(i);
  }
  delete _locals;
}

bool jvmtiDeferredLocalVariableSet::matches(vframe* vf) {
  frame fr = vf->get_frame();
  if (!fr.is_compiled_frame()) return false;
  // Because the frame was deoptimized and we pushed in registers in uncommon_trap
  // we use the sender's sp to compare
  return fr.pd_sender().sp() == id() && vf->method() == method() && vf->bci() == bci();
}

void jvmtiDeferredLocalVariableSet::set_local_at(int idx, BasicType type, jvalue val) {
  int i;
  for ( i = 0 ; i < locals()->length() ; i++ ) {
    if ( locals()->at(i)->index() == idx) {
      assert(locals()->at(i)->type() == type, "Wrong type");
      locals()->at(i)->set_value(val);
      return;
    }
  }
  locals()->push(new jvmtiDeferredLocalVariable(idx, type, val));
}

void jvmtiDeferredLocalVariableSet::oops_do(OopClosure* f) {
  
  f->do_oop((objectRef*) &_method);
  for ( int i = 0; i < locals()->length(); i++ ) {
    if ( locals()->at(i)->type() == T_OBJECT) {
f->do_oop(locals()->at(i)->ref_addr());
    }
  }
}

jvmtiDeferredLocalVariable::jvmtiDeferredLocalVariable(int index, BasicType type, jvalue value) {
  _index = index;
  _type = type;
  _value = value;
}

