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


#include "artaObjects.hpp"
#include "bytecodeStream.hpp"
#include "bytecodeTracer.hpp"
#include "ciEnv.hpp"
#include "ciMethod.hpp"
#include "ciStreams.hpp"
#include "codeProfile.hpp"
#include "handles.hpp"
#include "interfaceSupport.hpp"
#include "linkResolver.hpp"
#include "synchronizer.hpp"
#include "vframe.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"

// Bytecode type
// 0 - No profiling
// 1 - CPData_Null
// 2 - CPData_Invoke
// 4 - CPData_Jump
// 8 - CPData_Branch
const u1 CPData::cp_type[256] = {
  0, // 0x00    _nop        
  0, // 0x01    _aconst_null
  0, // 0x02    _iconst_m1  
  0, // 0x03    _iconst_0   
  0, // 0x04    _iconst_1   
  0, // 0x05    _iconst_2   
  0, // 0x06    _iconst_3   
  0, // 0x07    _iconst_4   
  0, // 0x08    _iconst_5   
  0, // 0x09    _lconst_0   
  0, // 0x0a    _lconst_1   
  0, // 0x0b    _fconst_0   
  0, // 0x0c    _fconst_1   
  0, // 0x0d    _fconst_2   
  0, // 0x0e    _dconst_0   
  0, // 0x0f    _dconst_1   
  0, // 0x10    _bipush     
  0, // 0x11    _sipush     
  0, // 0x12    _ldc        
  0, // 0x13    _ldc_w      
  0, // 0x14    _ldc2_w     
  0, // 0x15    _iload      
  0, // 0x16    _lload      
  0, // 0x17    _fload      
  0, // 0x18    _dload      
  0, // 0x19    _aload      
  0, // 0x1a    _iload_0    
  0, // 0x1b    _iload_1    
  0, // 0x1c    _iload_2    
  0, // 0x1d    _iload_3    
  0, // 0x1e    _lload_0    
  0, // 0x1f    _lload_1    
  0, // 0x20    _lload_2    
  0, // 0x21    _lload_3    
  0, // 0x22    _fload_0    
  0, // 0x23    _fload_1    
  0, // 0x24    _fload_2    
  0, // 0x25    _fload_3    
  0, // 0x26    _dload_0    
  0, // 0x27    _dload_1    
  0, // 0x28    _dload_2    
  0, // 0x29    _dload_3    
  0, // 0x2a    _aload_0    
  0, // 0x2b    _aload_1    
  0, // 0x2c    _aload_2    
  0, // 0x2d    _aload_3    
  1, // 0x2e    _iaload     
  1, // 0x2f    _laload     
  1, // 0x30    _faload     
  1, // 0x31    _daload     
  1, // 0x32    _aaload     
  1, // 0x33    _baload     
  1, // 0x34    _caload     
  1, // 0x35    _saload     
  0, // 0x36    _istore     
  0, // 0x37    _lstore     
  0, // 0x38    _fstore     
  0, // 0x39    _dstore     
  0, // 0x3a    _astore     
  0, // 0x3b    _istore_0   
  0, // 0x3c    _istore_1   
  0, // 0x3d    _istore_2   
  0, // 0x3e    _istore_3   
  0, // 0x3f    _lstore_0   
  0, // 0x40    _lstore_1   
  0, // 0x41    _lstore_2   
  0, // 0x42    _lstore_3   
  0, // 0x43    _fstore_0   
  0, // 0x44    _fstore_1   
  0, // 0x45    _fstore_2   
  0, // 0x46    _fstore_3   
  0, // 0x47    _dstore_0   
  0, // 0x48    _dstore_1   
  0, // 0x49    _dstore_2   
  0, // 0x4a    _dstore_3   
  0, // 0x4b    _astore_0   
  0, // 0x4c    _astore_1   
  0, // 0x4d    _astore_2   
  0, // 0x4e    _astore_3   
  1, // 0x4f    _iastore    
  1, // 0x50    _lastore    
  1, // 0x51    _fastore    
  1, // 0x52    _dastore    
  1, // 0x53    _aastore    
  1, // 0x54    _bastore    
  1, // 0x55    _castore    
  1, // 0x56    _sastore    
  0, // 0x57    _pop        
  0, // 0x58    _pop2       
  0, // 0x59    _dup        
  0, // 0x5a    _dup_x1     
  0, // 0x5b    _dup_x2     
  0, // 0x5c    _dup2       
  0, // 0x5d    _dup2_x1    
  0, // 0x5e    _dup2_x2    
  0, // 0x5f    _swap       
  0, // 0x60    _iadd       
  0, // 0x61    _ladd       
  0, // 0x62    _fadd       
  0, // 0x63    _dadd       
  0, // 0x64    _isub       
  0, // 0x65    _lsub       
  0, // 0x66    _fsub       
  0, // 0x67    _dsub       
  0, // 0x68    _imul       
  0, // 0x69    _lmul       
  0, // 0x6a    _fmul       
  0, // 0x6b    _dmul       
  1, // 0x6c    _idiv       
  1, // 0x6d    _ldiv       
  0, // 0x6e    _fdiv       
  0, // 0x6f    _ddiv       
  1, // 0x70    _irem       
  1, // 0x71    _lrem       
  0, // 0x72    _frem       
  0, // 0x73    _drem       
  0, // 0x74    _ineg       
  0, // 0x75    _lneg       
  0, // 0x76    _fneg       
  0, // 0x77    _dneg       
  0, // 0x78    _ishl       
  0, // 0x79    _lshl       
  0, // 0x7a    _ishr       
  0, // 0x7b    _lshr       
  0, // 0x7c    _iushr      
  0, // 0x7d    _lushr      
  0, // 0x7e    _iand       
  0, // 0x7f    _land       
  0, // 0x80    _ior        
  0, // 0x81    _lor        
  0, // 0x82    _ixor       
  0, // 0x83    _lxor       
  0, // 0x84    _iinc       
  0, // 0x85    _i2l        
  0, // 0x86    _i2f        
  0, // 0x87    _i2d        
  0, // 0x88    _l2i        
  0, // 0x89    _l2f        
  0, // 0x8a    _l2d        
  0, // 0x8b    _f2i        
  0, // 0x8c    _f2l        
  0, // 0x8d    _f2d        
  0, // 0x8e    _d2i        
  0, // 0x8f    _d2l        
  0, // 0x90    _d2f        
  0, // 0x91    _i2b        
  0, // 0x92    _i2c        
  0, // 0x93    _i2s        
  0, // 0x94    _lcmp       
  0, // 0x95    _fcmpl      
  0, // 0x96    _fcmpg      
  0, // 0x97    _dcmpl      
  0, // 0x98    _dcmpg      
  8, // 0x99    _ifeq       
  8, // 0x9a    _ifne       
  8, // 0x9b    _iflt       
  8, // 0x9c    _ifge       
  8, // 0x9d    _ifgt       
  8, // 0x9e    _ifle       
  8, // 0x9f    _if_icmpeq  
  8, // 0xa0    _if_icmpne  
  8, // 0xa1    _if_icmplt  
  8, // 0xa2    _if_icmpge  
  8, // 0xa3    _if_icmpgt  
  8, // 0xa4    _if_icmple  
  8, // 0xa5    _if_acmpeq  
  8, // 0xa6    _if_acmpne  
  4, // 0xa7    _goto       
  4, // 0xa8    _jsr        
  0, // 0xa9    _ret        
  0, // 0xaa    _tableswitch
  0, // 0xab    _lookupswitch
  0, // 0xac    _ireturn    
  0, // 0xad    _lreturn    
  0, // 0xae    _freturn    
  0, // 0xaf    _dreturn    
  0, // 0xb0    _areturn    
  0, // 0xb1    _return     
  0, // 0xb2    _getstatic  
  0, // 0xb3    _putstatic  
  1, // 0xb4    _getfield   
  1, // 0xb5    _putfield   
  3, // 0xb6    _invokevirtual
  3, // 0xb7    _invokespecial
  3, // 0xb8    _invokestatic
  3, // 0xb9    _invokeinterface
  0, // 0xba    _xxxunusedxx
  0, // 0xbb    _new        
  0, // 0xbc    _newarray   
  0, // 0xbd    _anewarray  
  1, // 0xbe    _arraylength
  1, // 0xbf    _athrow     
  1, // 0xc0    _checkcast  
  1, // 0xc1    _instanceof 
  1, // 0xc2    _monitorenter
  0, // 0xc3    _monitorexit
  0, // 0xc4    _wide       
  0, // 0xc5    _multianewar
  8, // 0xc6    _ifnull     
  8, // 0xc7    _ifnonnull  
  4, // 0xc8    _goto_w     
  4, // 0xc9    _jsr_w      
  0, // 0xca    _breakpoint 
  0
};


void CPData::print_line( Bytecodes::Code c, outputStream *st ) {
  if( 0 ) ;
  else if( CPData::is_Invoke(c) ) ((CPData_Invoke*)this)->print_line(st);
  else if( CPData::is_Null  (c) ) ((CPData_Null  *)this)->print_line(st);
  else if( CPData::is_Jump  (c) ) ((CPData_Jump  *)this)->print_line(st);
  else if( CPData::is_Branch(c) ) ((CPData_Branch*)this)->print_line(st);
  else if( Bytecodes::is_defined(c) ) st->print_cr("Bytecode %s is not profiled", Bytecodes::name(c));
  else                                st->print_cr("Bytecode %d is junk and not profiled", c);
}


void CPData_Null::print_line(outputStream*st){
if(_null)st->print("see_null ");
if(_fail)st->print("has_failed ");
if(_rchk)st->print("has_failed_range_check ");
if(_rchk_wide)st->print("has_failed_widened_range_check ");
}

void CPData_Jump::print_line(outputStream*st){
st->print("reached %d",_reached);
}

void CPData_Branch::print_line(outputStream*st){
  st->print("nottaken %d, taken %d (%d%%%%)",_nottaken,_taken,_taken ? (_taken*100/(_nottaken+_taken)):0);
}

methodRef CPData_Invoke::inlined_method() const { 
  return _inlined_method_oid 
    ? methodRef(CodeCacheOopTable::getOopAt(_inlined_method_oid).raw_value())
    : methodRef(); 
}

void CPData_Invoke::set_inlined_method( ciMethod *m, int cpd_offset ) {
  _inlined_method_oid = m->objectId();
  _cpd_offset = cpd_offset;
}

void CPData_Invoke::print_line(outputStream*st){
  CPData_Null::print_line(st);
st->print("reached %4d",_site_count);
  if (_inlined_method_oid != 0) {
st->print(", C1 inlined, C2 %s ---",InliningFailureID2Name[_inlining_failure_id]);
  } else {
st->print(", C1 NOT inlined, C2 %s",InliningFailureID2Name[_inlining_failure_id]);
  }

  // Now add type based profiling data
  if (callee_count() != 0) {
st->print(", Types!: ");
  } else {
st->print(", Types: ");
  }
for(uint i=0;i<NumCalleeHistogramEntries;i++){
    st->print("[k=%d,c=%d]",_callee_histogram_klassids[i],_callee_histogram_count[i]);
  }
st->print(" and %d overflow ",_callee_histogram_num_overflowed);
}

#ifndef PRODUCT
// Make sure that code profile won't leak
intptr_t CodeProfile::_allocation_stats[CodeProfile::_max_allocation_kinds];
intptr_t CodeProfile::_alive_stats[CodeProfile::_max_alive_kinds];
intptr_t CodeProfile::_free_stats[CodeProfile::_max_free_kinds];
#endif

// ---  CodeProfile  make  ---------------------------------------------------
CodeProfile *CodeProfile::make( methodOop m ) { 
  size_t cp_size = sizeof(CodeProfile) + m->bci2cpd_map()->maxsize();
  NOT_PRODUCT(update_allocation_stats(_newly_allocated);)
  CodeProfile *cp = (CodeProfile*)NEW_C_HEAP_ARRAY(char,cp_size);
  memset( cp, 0, cp_size );
  cp->_size = cp_size;
#ifdef CPMAGIC
  cp->installMagic(m);
#endif
  return cp;
}

CodeProfile *CodeProfile::make( ciMethod *m ) { 
  size_t cp_size = sizeof(CodeProfile) + m->bci2cpd_map()->maxsize();
  NOT_PRODUCT(update_allocation_stats(_newly_allocated);)
  CodeProfile *cp = (CodeProfile*)NEW_C_HEAP_ARRAY(char,cp_size);
  memset( cp, 0, cp_size );
  cp->_size = cp_size;
#ifdef CPMAGIC
  cp->installMagic(m);
#endif
  return cp;
}

CodeProfile *CodeProfile::make( FAMPtr old_cp ) { 
if(old_cp==0)return NULL;
  size_t old_size = FAM->getInt("((struct CodeProfile*)%p)->_size", old_cp);
  CodeProfile *cp = (CodeProfile*)NEW_C_HEAP_ARRAY(char,old_size);

  FAM->mapNewToOldPtr(cp, old_cp);

  uintptr_t *pos = (uintptr_t*)cp;
  uint i=0;
  for(; i<old_size-8; i+=8) {
    *pos = (uintptr_t)FAM->getLong("((uintptr_t*)%p)[%d]", old_cp, i>>3);
    pos++;
  }
for(;i<old_size;i++){
    ((char*)cp)[i] = (char)FAM->getInt("((char*)%p)[%d]", old_cp, i);
  }

  FAMPtr old_do = FAM->getOldPtr("((struct CodeProfile*)%p)->_debug_output", old_cp);
  if (old_do == 0) {
    cp->_debug_output = NULL;
  } else {
    cp->_debug_output = (stringStream*)tty;
  }

  return cp;
}

void CodeProfile::set_debug_output(stringStream* out) {
_debug_output=out;
}

stringStream* CodeProfile::get_debug_output() const {
  return _debug_output;
}

// ---  CodeProfile  clone ---------------------------------------------------
CodeProfile *CodeProfile::clone() {
size_t cp_size=(size_t)_size;
  NOT_PRODUCT(update_allocation_stats(_cloned);)
  CodeProfile *cp = (CodeProfile*)NEW_C_HEAP_ARRAY(char,cp_size);
  memcpy( cp, this, cp_size);
  // Get our own _debug_output
  if (_debug_output) {
    cp->_debug_output = new (ResourceObj::C_HEAP) stringStream(_debug_output);
  }
  return cp;
}

// ---  CodeProfile  clone ---------------------------------------------------
CodeProfile *CodeProfile::clone_into_arena(Arena *arena) {
size_t cp_size=(size_t)_size;
  NOT_PRODUCT(update_allocation_stats(_cloned);)
  CodeProfile *cp = (CodeProfile*)NEW_ARENA_ARRAY(arena, char,cp_size);
  memcpy( cp, this, cp_size);
  // Get our own _debug_output
  if (_debug_output) {
    cp->_debug_output = new (arena) stringStream(_debug_output);
  }
  return cp;
}

// ---  CodeProfile  grow  ---------------------------------------------------
CodeProfile *CodeProfile::grow( int oldsize, int newsize ) { 
  if( newsize == oldsize ) return this;
  oldsize += sizeof(CodeProfile);
  newsize += sizeof(CodeProfile);
  CodeProfile *cp = (CodeProfile*)REALLOC_C_HEAP_ARRAY(char,this,newsize); 
  memset( ((char*)cp)+oldsize, 0, newsize-oldsize );
  cp->_size = newsize;
  return cp;
}

// ---  CodeProfile  grow_to_inline  -----------------------------------------
CodeProfile *CodeProfile::grow_to_inline( int oldsize, int inloff, ciMethod *caller, int bci, ciMethod *callee ) {
assert(LogCompilerOutput||_debug_output==NULL,"Should be null");
  CodeProfile *cp = grow(oldsize, oldsize + callee->bci2cpd_map()->maxsize());
  CPData *cpd = cp->cpdoff_to_cpd(inloff,caller->bci2cpd_map()->bci_to_cpdoff(bci));
  CPData_Invoke *cpd_i = (CPData_Invoke*)cpd;
  cpd_i->set_inlined_method(callee,oldsize); // new stuff starts where old stuff ends
#ifdef CPMAGIC
  cp->installMagic(callee, oldsize);
#endif
  return cp;
}

// ---  CodeProfile  print  --------------------------------------------------
void CodeProfile::print( const methodOopDesc *const moop, outputStream *st ) const { 
  if (!st) st=tty;
  ThreadInVMfromUnknown tivfu;  // So I can touch the _moop field
  HandleMark hm;
  ResourceMark rm;
st->print("Code Profile (%p) for ",(address)this);
  moop->print_short_name(st);
  if( invoke_count() ) {
st->print(", invoked by C1 %d",invoke_count());
    _invoke_count.print_short(st);
st->print(" times");
  }
  if( backedge_count() ) {
st->print(", backedge count is %d",backedge_count());
    _backedge_count.print_short(st);
  }
  if( _throwout_count ) st->print(", throwout %d times",_throwout_count);
  int deopt = moop->has_bci2cpd_map()->deopt_count();
  if( deopt ) st->print(", deopted %d times",deopt);
  st->cr();
  print_impl(moop,0,0,-1,st); 
}

void CodeProfile::print_xml_on(const methodOopDesc* const moop, xmlBuffer* xb, bool ref, int depthmax) const {
assert(xb,"CodeProfile::print_xml_on() called with NULL xmlBuffer pointer.");
  const char *name = moop->name_and_sig_as_C_string();
  if (ref) {
    xmlElement blob(xb, "codeprofile_ref");
    xb->name_value_item("name", name); 
    xb->name_ptr_item("id", this);
    xb->name_value_item("moop", xb->object_pool()->append_oop(moop));
  } else {
    xmlElement blob(xb, "codeprofile");
    xb->name_value_item("name", name);
    { xmlElement xf(xb, "name_value_table");
    xb->name_value_item("invoke",invoke_count());
    xb->name_value_item("backedgecount",backedge_count());
    if( _throwout_count ) xb->name_value_item("throwout",_throwout_count);
    int deopt = moop->has_bci2cpd_map()->deopt_count();
    if( deopt ) xb->name_value_item("deopt",deopt);
    }
    print_to_xml_impl(moop,0,0,xb,depthmax);
  }
}

void CodeProfile::artificially_populate_code_profile( methodOopDesc *moop ) {
if(moop==NULL)return;

  HandleMark hm;
methodHandle mh(Thread::current(),moop);
  symbolHandle klass_name_h (Thread::current(), mh->klass_name());
  symbolHandle method_name_h(Thread::current(), mh->name());
  symbolHandle method_sig_h (Thread::current(), mh->signature());

  int32_t seed = (int32_t)(klass_name_h->slow_identity_hash()  ^
                           method_name_h->slow_identity_hash() ^
                           method_sig_h->slow_identity_hash()  ^
                           CompileTheWorldSeed);

  const int mask = (1<<29)-1;   // Limit values to positive shorts
  _invoke_count.set(os::random_impl(&seed)&mask, os::random_impl(&seed)&mask);
  BCI2CPD_mapping *bci2cpd = moop->has_bci2cpd_map();
  BytecodeStream str((methodOop)moop);
  Bytecodes::Code c;
while((c=str.next())>=0){
    int cpdoff = bci2cpd->bci_to_cpdoff(str.bci());
    if (cpdoff == BCI2CPD_mapping::unprofiled_bytecode_offset) continue;
    CPData *cpd = (CPData*)((address)this + sizeof(CodeProfile) + cpdoff);
    if( CPData::is_Invoke(c) ) {
      CPData_Invoke *cpdi = (CPData_Invoke*)cpd;
      cpdi->_site_count = os::random_impl(&seed)&mask;
    }
    else if( CPData::is_Null  (c) ) {
    }
    else if( CPData::is_Jump  (c) ) {
    }
    else if( CPData::is_Branch(c) ) {
      CPData_Branch *cpdb = (CPData_Branch*)cpd;
      cpdb->_nottaken = os::random_impl(&seed)&mask;
      cpdb->_taken    = os::random_impl(&seed)&mask;
    }
    else {
      ShouldNotReachHere();
    }
  }
}

// ---  CodeProfile  print_impl  ---------------------------------------------
// If depthmax==-1, there is no max
void CodeProfile::print_impl( const methodOopDesc *const moop, int inloff, int depth, int depthmax, outputStream *st ) const {
  if (!st) st=tty;
  BCI2CPD_mapping *bci2cpd = moop->has_bci2cpd_map();
  char buf[512];
  stringStream sst(buf, 512);
for(int i=0;i<depth;i++){//Initial indent for all lines
    sst.write("  ", 2);
  }
  const int initial_pos = sst.size();

  // walk all bytecodes
  BytecodeStream str((methodOop)moop);
  Bytecodes::Code c;
while((c=str.next())>=0){
    // Construct a 1-liner for the bytecode+profile bits
    sst.set_buf_pos(initial_pos);
    sst.print("%4d ",str.bci());

    BytecodeTracer::print_one_bytecode(moop, str.bci(), &sst);
    sst.unget();         // remove trailing CR

    // Pad out; put profile data to the side
const uint indent=30;
    while( sst.size() < indent ) sst.put(' ');

    // Get and print any profile bits
    int cpdoff = bci2cpd->bci_to_cpdoff(str.bci());
methodOop inl_moop=NULL;
    int inl_off=0;
    if( cpdoff != BCI2CPD_mapping::unprofiled_bytecode_offset ) {
      int total_offset = inloff+cpdoff/*offset to profile data*/+sizeof(CodeProfile);
      sst.print("+%-4d ",total_offset);

      CPData *cpd = cpdoff_to_cpd(inloff,cpdoff);
      cpd->print_line(c,&sst);
      CPData_Invoke *cpd_i = cpd->isa_Invoke(c);
      if( cpd_i ) {             // Check for inlined profile data
        inl_moop = cpd_i->inlined_method().as_methodOop();
        inl_off  = cpd_i->cpd_offset();
      }
    }
    buf[sst.size()] = '\0';

    // Print the 1-liner
st->print_cr(buf);

    // Now recursively nested-ly print inlined profile data
    if( inl_moop && (depth<depthmax || depthmax==-1))            // Was inlined?
      print_impl( inl_moop, inl_off, depth+1, depthmax, st ); 
  }
}


// ---  CodeProfile  print_to_xml_impl  ---------------------------------------------
// If depthmax==-1, there is no max
void CodeProfile::print_to_xml_impl( const methodOopDesc* const moop, int inloff, int depth, xmlBuffer *xb, int depthmax ) const {
assert(xb,"CodeProfile::print_to_xml_impl() called with NULL xmlBuffer pointer.");
  Unimplemented();
}

// ---  CodeProfileEntry  init  ----------------------------------------------
static void helper( jint mapping[], int off[], int bci, int log, int size ) {
  mapping[bci] = off[log];
  off[log] += size;
}

void BCI2CPD_mapping::init( methodOop moop ) {
  _deopt_count = 0;
  int codesize = _codesize = moop->code_size();
  int len = codesize+1;
  jint *mapping = NEW_C_HEAP_ARRAY(jint,len);
  memset( mapping, unprofiled_bytecode_offset, len*sizeof(jint) );
  // Stream thru bytecodes.  Find total data size needed at each
  // alignment (byte, short, int, long).
  int log_sizes[4] = {0,0,0,0};
BytecodeStream s(moop);
  Bytecodes::Code c;
  while( (c=s.next()) >= 0 ) {
    if( 0 ) ;
    else if( CPData::is_Invoke(c) ) log_sizes[CPData_Invoke ::log_min_align()] += CPData_Invoke ::size(); 
    else if( CPData::is_Null  (c) ) log_sizes[CPData_Null   ::log_min_align()] += CPData_Null   ::size(); 
    else if( CPData::is_Jump  (c) ) log_sizes[CPData_Jump   ::log_min_align()] += CPData_Jump   ::size(); 
    else if( CPData::is_Branch(c) ) log_sizes[CPData_Branch ::log_min_align()] += CPData_Branch ::size(); 
    else ;
  }

  // Compact CPData's based on min alignment restrictions.  
  // Basically packs the data better.
  int off[4];
  off[3] = 0;                   // 8-byte, or 1<<3
  off[2] = off[3]+log_sizes[3]; // 4-byte, or 1<<2
  off[1] = off[2]+log_sizes[2]; // 2-byte, or 1<<1
  off[0] = off[1]+log_sizes[1]; // 1-byte, or 1<<0

  // Now record the final bci->cpdata mapping
  BytecodeStream t(moop);
while((c=t.next())>=0){
    if( 0 ) ;
    else if( CPData::is_Invoke(c) ) helper(mapping,off,t.bci(),CPData_Invoke::log_min_align(),CPData_Invoke::size());
    else if( CPData::is_Null  (c) ) helper(mapping,off,t.bci(),CPData_Null  ::log_min_align(),CPData_Null  ::size());
    else if( CPData::is_Jump  (c) ) helper(mapping,off,t.bci(),CPData_Jump  ::log_min_align(),CPData_Jump  ::size());
    else if( CPData::is_Branch(c) ) helper(mapping,off,t.bci(),CPData_Branch::log_min_align(),CPData_Branch::size());
    else ;
  }

  int max = (off[0]+3)&~3;      // Round up, so next CPData array starts aligned
  mapping[len-1] = max;;        // max CPData size in last element
  _mapping = mapping;           // Assign to read-only location
}

void BCI2CPD_mapping::init( FAMPtr old_map ) {
  FAM->mapNewToOldPtr(this, old_map);

  _codesize = FAM->getInt("((struct BCI2CPD_mapping*)%p)->_codesize", old_map);
  _mapping = NEW_C_HEAP_ARRAY(jint,_codesize+1);
  // Since we made the structure _codesize+1, we dont have to worry about overrunning the
  // buffer by an int
  for(int i=0; i<=_codesize; i+=2) {
    ((uintptr_t*)_mapping)[i>>1] = FAM->getLong("((uintptr_t*)(((struct BCI2CPD_mapping*)%p)->_mapping))[%d]", old_map, i>>1);
  }
  _deopt_count     = FAM->getInt("((struct BCI2CPD_mapping*)%p)->_deopt_count", old_map);
  _last_deopt_tick = FAM->getLong("((struct BCI2CPD_mapping*)%p)->_last_deopt_tick", old_map);
}

// ---  BCI2CPD_mapping  free  -----------------------------------------------
void BCI2CPD_mapping::free(){
  memset( (void*)_mapping, 0xf7, (_codesize+1)*sizeof(jint) );
  FREE_C_HEAP_ARRAY(jint,_mapping);
  _codesize = -1;
  _mapping = (jint*)0xdeadbeef;
  Atomic::membar();
}

// ---  BCI2CPD_mapping  did_deopt  ------------------------------------------
// Record repeated endless deopt attempts.  Ignore deopts close in time, which
// tend to come from two sources: recursive stack-overflow JCK tests where
// we'll end up with 8000 deopts as the recursive code unwinds, and times when
// 100's of threads simultaneously launch into new code - and each thread
// triggers the SAME deopt, and a single compile will fix them all.
void BCI2CPD_mapping::did_deopt(const CodeProfile *const cp, const methodOopDesc* const moop, const int bci) {
  while( true ) {
jlong now=os::elapsed_counter();
    jlong was = _last_deopt_tick;
    if( (now - was) < (jlong)(10*M) )     // Ignore deopts repeated close in time
      return;
    // Attempt to atomically update the tick counter.
    // Update is atomic so that for 1000 racing threads all deopting at once
    // only one thread does the update.
    if( Atomic::cmpxchg(now,&_last_deopt_tick,was) != was )
      continue;
    _deopt_count++;             // We updated the last_deopt_tick
    if( AllowEndlessDeopt && (_deopt_count >= endless_deopt_count()) && (_deopt_count < endless_deopt_count()+20) ) {
tty->print_cr("---- Endless deopts at bci %d: ----",bci);
      cp->print(moop,tty);
      assert0( _deopt_count < endless_deopt_count() );
    }
  }
}

#ifdef CPMAGIC
void CodeProfile::installMagic(ciMethod* method, int offset) {
  BCI2CPD_mapping *bci2cpd = method->bci2cpd_map();
ciBytecodeStream s(method);
  Bytecodes::Code c;
  while( (c=s.next()) >= 0 ) {  // Scan for inlining
    int cpdoff = bci2cpd->bci_to_cpdoff(s.cur_bci());
    if (cpdoff == BCI2CPD_mapping::unprofiled_bytecode_offset) continue;
    ((CPData*)((address)this + sizeof(CodeProfile) + cpdoff + offset))->_magic = CPData::_magicbase | (CPData::cp_type[c] & 0x0f);
  }
}

void CodeProfile::installMagic(methodOop method, int offset) {
  BCI2CPD_mapping *bci2cpd = method->has_bci2cpd_map();
BytecodeStream s(method);
  Bytecodes::Code c;
  while( (c=s.next()) >= 0 ) {  // Scan for inlining
    int cpdoff = bci2cpd->bci_to_cpdoff(s.bci());
    if (cpdoff == BCI2CPD_mapping::unprofiled_bytecode_offset) continue;
    ((CPData*)((address)this + sizeof(CodeProfile) + cpdoff + offset))->_magic = CPData::_magicbase | (CPData::cp_type[c] & 0x0f);
  }
}
#endif

// ----------------------------------------------------------------------------
void CodeProfile::update_escape_info() {
  // do nothing here for Azul's CodeProfiles
}

bool CodeProfile::has_escape_info(){
return eflag_set(CodeProfile::estimated);
}

void CodeProfile::set_arg_local(int i){
  set_nth_bit(_arg_local, i);
}

void CodeProfile::set_arg_stack(int i){
  set_nth_bit(_arg_stack, i);
}

void CodeProfile::set_arg_returned(int i){
  set_nth_bit(_arg_returned, i);
}

bool CodeProfile::is_arg_local(int i)const{
  return is_set_nth_bit(_arg_local, i);
}

bool CodeProfile::is_arg_stack(int i)const{
  return is_set_nth_bit(_arg_stack, i);
}

bool CodeProfile::is_arg_returned(int i)const{
  return is_set_nth_bit(_arg_returned, i);
}

// ----------------------------------------------------------------------------
// How do I find where to record Heroic Optimization failures?  In general, C2
// out-inlines C1, and CodeProfiles match C1 inlining.  Hence when walking the
// C2 inline-tree, I end up "tiling" it with C1 CodeProfiles.  I'm looking for
// the leaf C1 CodeProfile which contains the BCI that failed - that's where
// I'll record failure.  
//
// Suppose C2 inlines A->B->X->Y (where A is the top-level call and Y the
// youngest inlined call).  C1 inlines A->B and X->Y.  I've a failure in Y.
// If I mark it in the CodeProfile for Y, then all future compiles of Y don't
// attempt the optimization.  Instead I want to mark it in the inlining of
// X->Y, so that I dont attempt the optimization only if Y is inlined inside
// of X; other uses of Y get their own failure bits.
//
CPData *CodeProfile::find_nested_cpdata( JavaThread *thread, vframe vf, methodCodeOop mco, /*outgoing arg*/ CodeProfile* *p_cp, bool check_deopt) {
  // vframes are stacked backwards from CodeProfiles: vframes go from youngest
  // to oldest but inlining is oldest-to-youngest.  Hence I recurse on vframes
  // and do the work on the post-pass from the recursion.
  BCI2CPD_mapping *bci2cpd = vf.method()->bci2cpd_map();
  int is_top_frame = vf.top_inlined_frame();

  if ( !is_top_frame ) { // Not at top?  Recurse.
    CodeProfile *x_cp;
    // Recursive case: C2 has inlined
    CPData *cpd = find_nested_cpdata( thread, vf.next_older(), mco, &x_cp, check_deopt );
    // Resulting cpd better be an invoke
    CPData_Invoke *cpd_i = (CPData_Invoke*)cpd;
    if( cpd_i->is_inlined() ) { // Inlined in the CodeProfile?
      * p_cp = x_cp;      // CodeProfile has inlined this call as well
      CPData *cpd = x_cp->cpdoff_to_cpd(cpd_i->cpd_offset(),
					bci2cpd->bci_to_cpdoff(vf.bci()));
      return cpd;
    }
    // For c1 methods if the vf says that it is not the top-inlined frame, 
    // its code profile must indicate that it is inlined
    else if (mco->_blob->is_c1_method()) {
      ShouldNotReachHere();
    }
    // vf suggests that it is inlined but the cpd_i in the caller     
    // disagrees, meaning that the c2 method is tiling up code profile 
    // information together. Fall through to find the code profile used when 
    // the callee is acting as the top-level method, which must exist.
  }

  // Top-level (oldest method).  
  // Also come here for cases where a prior CodeProfile didn't inline enough.
  // Don't clone the cp since OsrList_lock is already acquired by caller.
  CodeProfile *cp = (is_top_frame && mco->_blob->is_c1_method()) 
    // The cp accessed through method returns the cp from the first mco on the
    // current _codeRef_list, which may not be the same one (usually more inlined)
    // referenced when the c1 method was created.  This means that we may
    // return a CPData_Invoke which will fail the is_inlined() test above,
    // even though the vf suggests it is an inlined frame.  So if we are
    // asking for top frame here, return the cp found on the mco.
    ?  mco->get_codeprofile() 
    // If the faulting methodcodeoop is not a c1, we want to flag the exception on
    // the cp of the head mco on the _codeRef_list, so that c2 compiler can see it
    // in the next compilation attempt.
    : vf.method()->codeprofile(false);

  // Since we compiled better already exist a CodeProfile
  assert0( cp ); 
  * p_cp = cp;                  // Pass CP to caller
  if( check_deopt && !mco->_patched_for_deopt ) // Check for endless deopt cycles
    bci2cpd->did_deopt(cp,vf.method(),vf.bci());
  int cpdoff = bci2cpd->bci_to_cpdoff( vf.bci() );
  if( cpdoff == BCI2CPD_mapping::unprofiled_bytecode_offset )
    return NULL;
  return cp->cpdoff_to_cpd(0,cpdoff);
}


#ifdef CPMAGIC
bool CPData::is_CPData() { return FAM || ((_magic&0xf0) == _magicbase);  }
bool CPData::is_Null()   { return FAM || (is_CPData() && (_magic&0x01)); }
bool CPData::is_Invoke() { return FAM || (is_CPData() && (_magic&0x02)); }
bool CPData::is_Jump()   { return FAM || (is_CPData() && (_magic&0x04)); }
bool CPData::is_Branch() { return FAM || (is_CPData() && (_magic&0x08)); }
#else
bool CPData::is_CPData() { return true; }
bool CPData::is_Null()   { return true; }
bool CPData::is_Invoke() { return true; }
bool CPData::is_Jump()   { return true; }
bool CPData::is_Branch() { return true; }
#endif
