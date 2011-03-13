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
#include "bytecodeTracer.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "disassembler_pd.hpp"
#include "frame.hpp"
#include "javaFrameAnchor.hpp"
#include "jniHandles.hpp"
#include "methodOop.hpp"
#include "signature.hpp"
#include "stubCodeGenerator.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "auditTrail.inline.hpp"
#include "frame.inline.hpp"
#include "frame_pd.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "oop.inline2.hpp"


// --- pd_last_frame
// SP comes from thread
frame JavaFrameAnchor::pd_last_frame(intptr_t* last_Java_sp) {
  return frame(last_Java_sp,(address)_last_Java_pc );
}


// --- pd_last_frame
// SP comes from JFA
frame JavaFrameAnchor::pd_last_frame() {
  return frame( (intptr_t*)_last_Java_sp, (address)_last_Java_pc );
}


// --- pd_sender_robust
// Should only be used for error reporting
frame frame::pd_sender_robust()const{
  assert0( !is_entry_frame() ); // do not ask for sender of an entry frame; instead the caller uses the C anchor
  int framesize_bytes;
  CodeBlob* cb = CodeCache::find_blob(_pc);
  if( cb != NULL ) {
    framesize_bytes = cb->framesize_bytes();
  } else {
    // Assume a native frame with a sane return address and frame pointer layout
    uintptr_t fp = (intptr_t)(sp()[-2]);
    if (fp >= __THREAD_STACK_REGION_START_ADDR__ && fp < __THREAD_STACK_REGION_END_ADDR__) {
      address  spc = (address)((intptr_t*)fp)[1];
      intptr_t* ssp = (intptr_t*)fp + 2;
      return frame(ssp, spc);
    } else { // bogus frame pointer
      return frame(NULL, NULL);
    }
  }
  // do not assert much sanity here: the tick profiler calls here with weird pcs & stacks
  // just try not to crash
  intptr_t *ssp = (intptr_t*)((char*)_sp+framesize_bytes);
  return frame( ssp, (address)ssp[-1] );
}


// --- interpreter_empty_expression_stack
void frame::interpreter_empty_expression_stack(){
  int numlocs = interpreter_frame_method()->max_locals();
  intptr_t *locals = interpreter_frame_locals_base();
  intptr_t*JES_base_address = locals+numlocs;
  uint32_t *offptr = &ifr()->_stk;
  *offptr = (intptr_t)JES_base_address;
}


frame::InterpreterStackSlotType frame::tag_at_address(intptr_t* addr) {
  // We're only going to read the bottom 4 bytes of the stack slot. 
  // The top 4 bytes do not affect the type.
  jint candidate = ((jint*)addr)[1];

  if (candidate == 0 || candidate == -1)
    return single_slot_primitive_type;

  if (candidate == double_slot_primitive_type_empty_slot_id)
    return double_slot_primitive_type;

  // If we made it here, must be a pointer.
  return single_slot_ref_type;
}


BasicType frame::interpreter_frame_result(oop* oop_result, jvalue* value_result, uint64_t return_value) {
  assert(is_interpreted_frame() || is_native_frame(), "interpreted/native frame expected");
  methodOop method = NULL;
intptr_t*return_val_addr=NULL;

  if (is_interpreted_frame()) {
method=interpreter_frame_method();
    Unimplemented();
    //return_val_addr = (intptr_t*)interpreter_frame_tos_address();
  } else {
CodeBlob*cb=CodeCache::find_blob(pc());
if(cb!=NULL){
      Unimplemented();
      //method = ((nmethod *)cb)->method();
    }
    // 'return_value' is valid only for a native frame
    return_val_addr = (intptr_t*) &return_value;
  }

assert(method!=NULL,"method is NULL?");

  BasicType type = method->result_type();


  switch (type) {
    case T_OBJECT  : 
    case T_ARRAY   : {
      oop obj;

      if (!method->is_native()) {
objectRef*ref_addr=(objectRef*)return_val_addr;

        if (ref_addr == NULL || (*ref_addr).is_null()) {
*oop_result=NULL;
          break;
        }

        obj = lvb_ref(ref_addr).as_oop();
      } else {
        // Native frame - if return type is T_OBJECT or T_ARRAY
        //                'return_value' contains an objectRef*
        //                'return_val_addr' holds the address of the objectRef*
        jobject return_value_handle = (jobject) *((intptr_t*) return_val_addr);

        obj = JNIHandles::resolve(return_value_handle); 
      }

      assert(obj == NULL || Universe::heap()->is_in(obj), "sanity check");
      *oop_result = obj;
      break;
    }
case T_BOOLEAN:value_result->z=*(jboolean*)return_val_addr;break;
case T_BYTE:value_result->b=*(jbyte*)return_val_addr;break;
case T_CHAR:value_result->c=*(jchar*)return_val_addr;break;
case T_SHORT:value_result->s=*(jshort*)return_val_addr;break;
case T_INT:value_result->i=*(jint*)return_val_addr;break;
case T_LONG:value_result->j=*(jlong*)return_val_addr;break;
case T_FLOAT:value_result->f=*(jfloat*)return_val_addr;break;
case T_DOUBLE:value_result->d=*(jdouble*)return_val_addr;break;
    case T_VOID    : /* Nothing to do */ break;
    default        : ShouldNotReachHere();
  }

  return type;
}

// ----------------------------------------------------------------------------
#ifndef PRODUCT
// Print stack
extern "C" void hsfind1(intptr_t x, bool print_pc, xmlBuffer *xb, Thread *thr);

// Print stack with given thread.
void frame::pd_ps(JavaThread *thread, const DebugScope *scope, methodOop moop) const {
  // Print something for all words in the frame

  if( is_interpreted_frame() ) {
    intptr_t* locals = interpreter_frame_locals_base();

    // Confusingly, if the frame is 'interpreter' but the method is 'native',
    // then I must be building a native wrapper during an interpreter call.
    // Natives have zero locals... but they have parameters which must be GC'd.
    int numlocs = moop->is_native() ? moop->size_of_parameters() : moop->max_locals();
    intptr_t* jexstk = interpreter_frame_tos_at(-1);

    // Print JES
    for( intptr_t *s = jexstk-1; s >=locals+numlocs; s-- ) {
      tty->print(PTR_FORMAT " %016lx %9.9s  ",s,*s,"JES");
      if( ((int*)s)[-1] == frame::double_slot_primitive_type_empty_slot_id )
        tty->print_cr("as double: %g",*(double*)s);
      else
        hsfind1(*s,false/*pc lookup*/, NULL, thread);
    }
    // Print locals
    for( intptr_t *s = locals+numlocs-1; s >= locals; s-- ) {
      char buf[100];
      sprintf(buf,"JL%d",(int)(s-locals));
      tty->print(PTR_FORMAT " %016lx %9.9s  ",s,*s,buf);
      if( ((int*)s)[-1] == frame::double_slot_primitive_type_empty_slot_id )
        tty->print_cr("as double: %g",*(double*)s);
      else
        hsfind1(*s,false/*pc lookup*/, NULL, thread);
    }
    
    tty->cr();
    
    // Normal interpreter frame guts
intptr_t*t=sp();
    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"cpCacheRef");
    hsfind1(*t++,false/*pc lookup*/, NULL, thread);
    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"methodRef");
    ((methodRef*)t)->as_methodOop()->print_short_name(tty);
    tty->cr();
    t++;

    // Locals and stack offsets
    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"locs/stk");
    tty->cr();
    t++;

    // BCI & locks
    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"bci/lcks");
    const int bci = interpreter_frame_bci();
tty->print("bci=%d ",bci);
    const int numlcks = interpreter_frame_monitor_count();
if(numlcks)tty->print("locks=%d ",numlcks);
    if( bci != InvocationEntryBci ) BytecodeTracer::print_one_bytecode(moop,bci,tty);
    else tty->cr();
    t++;

    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"pad");
    hsfind1(*t++,false/*pc lookup*/, NULL, thread);

    tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"ret adr");
    hsfind1(*t++,true/*pc lookup*/, NULL, thread);

    // Fat Interpreter Frame?
    if( _pc == Interpreter:: iframe_16_stk_args_entry() ||
        _pc == Interpreter::iframe_256_stk_args_entry() ) {
      intptr_t *nsp = pd_sender()._sp;
while(t<nsp){
        tty->print(PTR_FORMAT " %016lx %9.9s  ",t,*t,"fatstk arg");
        hsfind1(*t++,false/*pc lookup*/, NULL, thread);
      }
    }

if(numlcks)tty->cr();
for(int i=0;i<numlcks;i++){
      objectRef *optr = &thread->_lckstk_top[-i-1];
      tty->print("%14p %016lx %9.9s  ",optr,ALWAYS_UNPOISON_OBJECTREF(*optr).raw_value(),"lock stack");
      hsfind1(*(intptr_t*)optr,false/*pc lookup*/, NULL, thread);
    }

  } else {
    CodeBlob *cb = CodeCache::find_blob(_pc);
    if( !cb ) {
      tty->cr();
tty->print_cr("---===  CORRUPTED STACK  ===---");
intptr_t*p=_sp;
for(int i=0;i<20;i++){
        tty->print(PTR_FORMAT " %016lx %9.9s  ",p,*p,"?????????");
        hsfind1(*p++,true,NULL,thread);
      }
      tty->cr();
      return;
    }
    int fs = (is_entry_frame() ? call_stub_frame_size : cb->framesize_bytes())>>3;
    intptr_t *fp = sp()+fs-1;
    intptr_t *s = sp();         // bottom of frame
    while( s < fp ) {           // For the body of the compiled frame
      // Print Address, Value, Message
const char*msg=".........";
      if( cb->is_runtime_stub() ) {
        int x = fp-s;          // index into runtime stub stack
        extern const char *raw_reg_name_strs[];
        msg = x<2 ? "pad" : raw_reg_name_strs[x-2];
      }
      tty->print(PTR_FORMAT " %016lx %9.9s  ",s,*s,msg);
      if( is_compiled_frame() ) {
        int stkoff = (address)s-((address)_sp);
char buf[200];
buf[0]=0;
        VReg::VR vreg = VReg::stk2reg(stkoff);
        assert0( s == reg_to_addr( vreg ) );
        if( scope ) scope->print_compiler_info( vreg, buf );
tty->print(buf);
      }
      hsfind1(*s++,false/*pc lookup*/, NULL, thread);
    }
    tty->print(PTR_FORMAT " %016lx %9.9s  ",s,*s,"ret adr");
    hsfind1(*s++,true/*pc lookup*/, NULL, thread);
  }

  tty->cr();
}

#endif // PRODUCT

void frame::all_threads_print_xml_on(xmlBuffer *xb, int start, int stride) {
  Unimplemented();
}

int frame::thread_print_xml_on(xmlBuffer *xb, intptr_t t) {
//  sys_return_t ret;
  xmlElement xe1(xb, "thread");

  return 0;
}

int frame::thread_id_print_xml_on(xmlBuffer *xb, uint64_t id) {
  Unimplemented();
  return 0;
}

int frame::print_xml_on(xmlBuffer *xb) {
  const char *name;
  if (is_interpreted_frame()) {
methodOop m=interpreter_frame_method();
    name = (m->is_oop() && m->is_method()) ? m->name_and_sig_as_C_string() : "Unknown interpreter frame";
  } else {
address low,hi;
    name = Disassembler::static_pc_to_name(pc(), low, hi, true /* demangle */);
    if (name == NULL) name = "Unknown";
  }
  xmlElement xf(xb, "line_per_frame_word");
  xb->name_value_item("interpreted", is_interpreted_frame() ? "true" : "false");
  xb->name_value_item("name", name);
  xb->name_ptr_item("pc", pc());
  return (strcmp(name, "_start") == 0) || (strcmp(name, "_thread_start") == 0);
}
