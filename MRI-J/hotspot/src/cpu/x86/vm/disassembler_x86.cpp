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


#include "assembler_pd.hpp"
#include "c1_Runtime1.hpp"
#include "codeCache.hpp"
#include "codeProfile.hpp"
#include "disassembler_pd.hpp"
#include "interpreter.hpp"
#include "interpreter_pd.hpp"
#include "nativeInst_pd.hpp"
#include "oopTable.hpp"
#include "stubCodeGenerator.hpp"
#include "stubRoutines.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

// demangling hooks
#include <cxxabi.h>

void Decoder::reset_reg_tracking() {
  for( uint i=0; i<sizeof(_regs)/sizeof(_regs[0]); i++ )
_regs[i]=Runknown;
  _regs[RSP] = Rstack;
}

void Decoder::print_string(const char* str) {
  while (*str != 0) {
    print_char(*str++);
  }
}

void Decoder::print_register(Register reg){
  const char *rname = 
    ((_regs[reg] == Rstack && reg != RSP) ||
     (_regs[reg] == Rframe) ||
     (_regs[reg] == Rthread) ) 
    ? x86_reg_name(reg)
    : full_reg_name(reg, (address)_pc); // fancy interp name if not already reg-tracking a name
  print_string(rname);
  if( _regs[reg] == Rstack && reg != RSP )
    print_string("_sp");
  if( _regs[reg] == Rframe )
    print_string("_fp");
  if( _regs[reg] == Rthread )
    print_string("_thr");
}
void Decoder::print_dst(Register dst) { 
  if( dst != RSP ) _regs[dst] = Runknown;
  else {                        // RSP moving
    for( uint i=0; i<sizeof(_regs)/sizeof(_regs[0]); i++ )
      if( i != RSP && _regs[i] == Rstack )
_regs[i]=Rframe;
  }
print_register(dst);
}

void Decoder::print_Fregister(FRegister reg) { 
  print_string("F");
  print_char(reg<10 ? '0' : '1');
  print_char('0'+(reg%10));
}

void Decoder::print_immediate(int imm, int base) { 
  print_integer(imm,base); 
}

void Decoder::print_immediate(int imm) { 
  print_integer(imm); 
}

void Decoder::print_offset( Register base, int imm) { 
const char*fldname=NULL;
  if( base == RDI && Interpreter::contains(_op) ) {
    if( imm == in_bytes(methodOopDesc::             const_offset())) fldname = "_constMethod";
    if( imm == in_bytes(methodOopDesc::         constants_offset())) fldname = "_constants";
    if( imm == in_bytes(methodOopDesc::    size_of_locals_offset())) fldname = "_max_locals";
    if( imm == in_bytes(methodOopDesc::size_of_parameters_offset())) fldname = "_size_of_parameters";
  }

if(Universe::heap()){
  BarrierSet* bs = Universe::heap()->barrier_set();
  if (bs->kind() == BarrierSet::CardTableModRef ) {
    CardTableModRefBS* ct = (CardTableModRefBS*)bs;
    if( imm == (intptr_t)ct->byte_map_base )
      fldname = "_cardmark_base";
  }
  }

  if( _regs[base] == Rthread ) {
    if( imm == in_bytes(JavaThread::           sba_max_offset())) fldname = "_sba_max";
    if( imm == in_bytes(JavaThread::           sba_top_offset())) fldname = "_sba_top";
    if( imm == in_bytes(JavaThread::          jvm_lock_offset())) fldname = "_jvm_lock";
    if( imm == in_bytes(JavaThread::          tlab_end_offset())) fldname = "_tlab._end";
    if( imm == in_bytes(JavaThread::          tlab_top_offset())) fldname = "_tlab._top";
    if( imm == in_bytes(JavaThread::         tlab_size_offset())) fldname = "_tlab._size";
    if( imm == in_bytes(JavaThread::        time_block_offset())) fldname = "_time_blocked";
    if( imm == in_bytes(JavaThread::        tlab_start_offset())) fldname = "_tlab._start";
    if( imm == in_bytes(JavaThread::      last_Java_pc_offset())) fldname = "_anchor.last_Java_pc";
    if( imm == in_bytes(JavaThread::      last_Java_sp_offset())) fldname = "_anchor.last_Java_sp";
    if( imm == in_bytes(JavaThread::    active_handles_offset())) fldname = "_active_handles";
    if( imm == in_bytes(JavaThread::    exception_file_offset())) fldname = "_exception_file";
    if( imm == in_bytes(JavaThread::    exception_line_offset())) fldname = "_exception_line";
    if( imm == in_bytes(JavaThread::    hint_block_msg_offset())) fldname = "_hint_block_concurrency_msg";
    if( imm == in_bytes(JavaThread::   hint_block_lock_offset())) fldname = "_hint_block_concurrency_lock";
    if( imm == in_bytes(JavaThread::   hint_block_sync_offset())) fldname = "_hint_block_concurrency_sync";
    if( imm == in_bytes(JavaThread::   jni_environment_offset())) fldname = "_jni_environment";
    if( imm == in_bytes(JavaThread::  interp_only_mode_offset())) fldname = "_interp_only_mode";
    if( imm == in_bytes(JavaThread:: allocated_objects_offset())) fldname = "_allocated_objects._rows";
    if( imm == in_bytes(JavaThread::please_self_suspend_offset()))fldname = "_please_self_suspend";
    if( imm == in_bytes(Thread    ::            vm_tag_offset())) fldname = "_vm_tag";
    if( imm == in_bytes(Thread    :: pending_exception_offset())) fldname = "_pending_exception";
  }

  if( fldname ) print_string(fldname);
  else          print_immediate(imm); // Default no special interpretation
}

void Decoder::print_integer(int64 val, int base) {
  assert0((2 <= base) && (base <= 16));
  char tmp[65];
if(base==16){
    sprintf(tmp, "0x%llx", (julong)val);
print_string(tmp);
  } else {
    const char digit[17] = "0123456789abcdef";
    int64 x = val < 0 ? -val : val;
    int i = 0;

    do {
      tmp[i++] = digit[x % base];
      x = x / base;
    } while (x != 0);

    if (val < 0) tmp[i++] = '-'; // add sign

    if (base != 10) {
      print_integer(base, 10);
      print_char('\'');
    }
    while (i-- > 0) print_char(tmp[i]);
  }
}

// functions used to check op_code prefix (see next for no assert version)
static void no_rex     ( int prefix ) { assert0( prefix == 0 ); }
static void only_rex   ( int prefix ) { assert0( (prefix & 0x80) == 0 ); }
static void only_rexb  ( int prefix ) { assert0( (prefix & ~(Assembler::REX_B)) == 0 ); }
static void only_rexw  ( int prefix ) { assert0( (prefix & ~(Assembler::REX_W)) == 0 ); }
static void only_rexwb ( int prefix ) { assert0( (prefix & ~(Assembler::REX_W|Assembler::REX_B)) == 0 ); }
static void only_rexwr ( int prefix ) { assert0( (prefix & ~(Assembler::REX_W|Assembler::REX_R)) == 0 ); }
static void only_rexrb ( int prefix ) { assert0( (prefix & ~(Assembler::REX_R|Assembler::REX_B)) == 0 ); }
static void only_rexbx ( int prefix ) { assert0( (prefix & ~(Assembler::REX_B|Assembler::REX_X)) == 0 ); }
static void only_rexrxb( int prefix ) { assert0( (prefix & ~(Assembler::REX_R|Assembler::REX_X|Assembler::REX_B)) == 0 ); }
static void only_rexwxb( int prefix ) { assert0( (prefix & ~(Assembler::REX_W|Assembler::REX_X|Assembler::REX_B)) == 0 ); }

// no assert version of above functions -- needed to prevent abort
static bool no_rex_na     ( int prefix ) { return ( prefix == 0 ); }
static bool only_rex_na   ( int prefix ) { return ( (prefix & 0x80) == 0 ); }
static bool only_rexb_na  ( int prefix ) { return ( (prefix & ~(Assembler::REX_B)) == 0 ); }
static bool only_rexw_na  ( int prefix ) { return ( (prefix & ~(Assembler::REX_W)) == 0 ); }
static bool only_rexwb_na ( int prefix ) { return ( (prefix & ~(Assembler::REX_W|Assembler::REX_B)) == 0 ); }
static bool only_rexwr_na ( int prefix ) { return ( (prefix & ~(Assembler::REX_W|Assembler::REX_R)) == 0 ); }
static bool only_rexrb_na ( int prefix ) { return ( (prefix & ~(Assembler::REX_R|Assembler::REX_B)) == 0 ); }
static bool only_rexbx_na ( int prefix ) { return ( (prefix & ~(Assembler::REX_B|Assembler::REX_X)) == 0 ); }
static bool only_rexrxb_na( int prefix ) { return ( (prefix & ~(Assembler::REX_R|Assembler::REX_X|Assembler::REX_B)) == 0 ); }
static bool only_rexwxb_na( int prefix ) { return ( (prefix & ~(Assembler::REX_W|Assembler::REX_X|Assembler::REX_B)) == 0 ); }


static Register rexb( int prefix, int reg ) {
  return (Register)((prefix&1) ? reg += 8 : reg);
}

// strip out the 'reg' field from modrm, and apply any prefix
static Register rexr( int prefix, int modrm ) {
  int reg = (modrm>>3)&7;
  return (Register)((prefix&4) ? reg + 8 : reg);
}

// peel out a 4-byte immediate
static int imm2( const unsigned char *op ) {
  return (op[1]<<8) + (op[0]<<0);
}

// peel out a 4-byte immediate
static int imm4( const unsigned char *op ) {
  return (op[3]<<24) + (op[2]<<16) + (op[1]<<8) + (op[0]<<0);
}

// peel out an 8-byte immediate
static int64 imm8( const unsigned char *op ) {
  return (imm4(op)&0xFFFFFFFFL) | (((int64)imm4(op+4))<<32);
}

// Read out the modrm field, and print it.  Advance the 'op' pointer
// past the entire addressing mode, including offsets.  Does not
// print the 'r' field (use rexr above for that).
address Decoder::print_modrm_dst( int prefix, address op ) {
  if( (*op & 0xC0) == 0xC0 ) { // register-only, no extra bytes
    print_dst(rexb(prefix, *op++ &7)); 
    return op;
  }
  return print_modrm(prefix,op);
}

address Decoder::print_modrm_flt( int prefix, address op ) {
  if( (*op & 0xC0) == 0xC0 ) { // register-only, no extra bytes
    print_Fregister((FRegister)rexb(prefix, *op++ &7));
    return op;
  }
  return print_modrm(prefix,op);
}

address Decoder::print_modrm( int prefix, address op ) {
  int modrm = *op++;

  if( (modrm & 0xC0) == 0xC0 ) { // register-only, no extra bytes
    print_register(rexb(prefix, modrm&7)); 
    return op;
  }

  // includes memory
  print_char('[');
int base=modrm&7;
  int idx = -1;                 // No SIB index yet
  if( base==RSP ) {             // includes SIB; cannot use RSP as a base
    int sib = *op++;
    int scale = sib>>6;
    idx = (sib>>3)&7;           // Get index from SIB
    if( (prefix&Assembler::REX_X)==Assembler::REX_X ) idx += 8;
    base = sib&7;               // base now comes from SIB
    if( idx != RSP ) {          // RSP means "no index"
      print_register((Register)idx);
      print_char('*');
      print_integer(1<<scale);
      print_char('+');
    }
  }
  Register rbase = rexb(prefix, base);
  if( (modrm & 0xC0) == 0x00 ) { // no offset
    if( base == RBP ) { // disp32 (SIB byte w/no index) or RIP+disp32 (no SIB byte) addressing?
      int off = imm4(op); op += 4;
      if( idx == -1 ) {         // No SIB?
        print_string("RIP");    // The base is RIP
        if( off >= 0 ) print_char('+');
        print_immediate(off,16);
        print_string(" (");
        print_immediate((intptr_t)(op+off),16);
address low,hi;
        const char *str = Disassembler::static_pc_to_name(op+off,low,hi,0);
        if( str ) {
          print_string(" ");
print_string(str);
        }
        print_string(" )");
      } else {                  // Direct addressing
        intptr_t koff = off - (intptr_t)KlassTable::getKlassTableBase();
        int kid = koff>>3;
        if( kid > 0 && kid < 10000000 && KlassTable::is_valid_klassId(kid) ) {
          print_string("ktb+");
          print_integer(kid);
          print_string("*8]  ");
          print_string(KlassTable::pretty_name(kid));
          return op;
        } else if( kid < 0 && kid > -10000000 && CodeCacheOopTable::is_valid_index(kid) ) {
          print_string("ktb ");
          print_integer(kid);
          print_string("*8]  instanceof: ");
          print_string(KlassTable::pretty_name(CodeCacheOopTable::getOopAt(kid).klass_id()));
          return op;
        } else if( kid == 0 ) {
          print_string("ktb");
        } else if( _profile && (intptr_t)_profile <= off && off < (intptr_t)_profile + _profile->size() ) {
          print_string("((CodeProfile*)");
          print_immediate((intptr_t)_profile,16);
          print_string(")+");
          print_integer(off - (intptr_t)_profile);
        } else if( (intptr_t)MacroAssembler::double_constant(1.0) == off ) {
          print_string("1.0d");
        } else if( (intptr_t)MacroAssembler::double_constant(0.5) == off ) {
          print_string("0.5d");
        } else {
          print_immediate(off,16);
        }
      }
    } else                      // Else no offset
      print_register(rbase);    // print base
    // no offset to print
  } else if( (modrm & 0xC0) == 0x40 ) { // byte offset
    print_register(rbase);      // print base
    int off = *(char*)op++;     // byte
    if( off >= 0 ) print_char('+');
    print_offset(rbase,off);
  } else {                      // 32-bit offset
    print_register(rbase);      // print base
    int off = imm4(op); op += 4;
    if( off >= 0 ) print_char('+');
    print_offset(rbase,off);
  }

  print_char(']');
  return op;
}

// Utility to determine if a given address could be a C string
static bool could_be_C_string(address x) {
  // os::address_is_in_vm doesn't quite fit the billing as it only checks
  // for addresses in the text and not data segment. Guess that the data segment
  // will be within 80MB of the text and search
  int one_mb = 1024*1024;
  for (address i=x-40*one_mb; i < x+40*one_mb; i+= one_mb) {
    if( os::address_is_in_vm((address)i) ) {
      return true;
    }
  }
  return false;
}

// Disassembler has come across a region of raw data to print, print it
void Decoder::print_raw_data() {
  assert0(_raw_data > 0);
  const int chunk_size = 8;
  // make amount printed line up next chunk
  int to_print = chunk_size - (((intptr_t)_op) & (chunk_size-1));
  // don't print more than the raw data present
  if (_raw_data < to_print) to_print = _raw_data;
  char tmp[32];
uintptr_t data=0;
for(int i=0;i<to_print;i++){
    data |= (((uintptr_t)(_op[i])) & 0xFF) << (8*i);
  }
  sprintf(tmp, "0x%0*lx ", to_print*2, data);
print_string(tmp);
for(int i=0;i<to_print;i++){
    if (isascii(_op[i])&&!iscntrl(_op[i])) {
      sprintf(tmp, "'%c' ", _op[i]);
print_string(tmp);
    } else {
      print_string("' ' ");
    }
  }
  print_string("// raw data");
  _op += to_print;
  _raw_data -= to_print;
}

// Top-level decode point.  Look at the op & start puzzling
void Decoder::form() {
  static const char *const int_mem[8] = {"add","or_","adc","sbb","and","sub","xor","cmp"};
  static       int   const int_radix[8]={  10 ,  16 ,  10 ,  10 ,  16 ,  10 ,  16 ,  10 };
  static const char *const jcc[16] = {"o ","no","b ","ae","z ","nz","be","a ","s ","ns","p ","po","l ","ge","le","g "};
  static const char *const shf[8] = { "rol", "ror", "rcl", "rcr", "shl", "shr", 0, "sar" };
  static const char *const btst[8] = {0, 0, 0, 0, "btx","bts","btr","btc"};

  const address orig_op = _op;
  int modrm, op;
  int prefix = 0;
  if (_raw_data > 0) { print_raw_data(); return; } // if we're in the midst of printing raw data, print it
  while( *_op == 0x66 ) { prefix = 0x80; _op++; } // old size prefix, sometimes repeated to make big NOP's
  if( *_op == 0x2E ) { print_string("cs:"); _op++; } // CS prefix
  if( *_op == 0x3E ) { print_string("ds:"); _op++; } // DS prefix
  if( *_op == 0x64 ) { print_string("fs:"); _op++; } // FS prefix
  if( *_op == 0x65 ) { print_string("gs:"); _op++; } // GS prefix
  if( *_op == 0xF0 ) { print_string("locked:"); _op++; } // lock prefix
  if( *_op >= Assembler::REX && *_op <= Assembler::REX_WRXB )
    prefix |= *_op++;           // Grab any prefix byte 

  switch( *_op++ ) {            // parse a byte

  case 0x01:  case 0x09:  case 0x11:  case 0x19:
  case 0x21:  case 0x29:  case 0x31:  case 0x39: // op reg/mem,reg
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_string((prefix&0x80) ? "2  " : ((prefix&8) ? "8  " : "4  "));
op=*_op;
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_dst(rexr(prefix,op)); // src
    break;

  case 0x00:  case 0x08:  case 0x10:  case 0x18:
  case 0x20:  case 0x28:  case 0x30:  case 0x38: // op reg/mem,reg
    ONLY_REXRXB(prefix);
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_string("1  ");
op=*_op;
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_dst(rexr(prefix,op)); // src
    break;

  case 0x02:  case 0x0A:  case 0x12:  case 0x1A:
  case 0x22:  case 0x2A:  case 0x32:  case 0x3A: // op reg,reg/mem
    ONLY_REXRXB(prefix);
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_string("1  ");
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x03:  case 0x0B:  case 0x13:  case 0x1B:
  case 0x23:  case 0x2B:  case 0x33:  case 0x3B: // op reg,reg/mem
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_string((prefix&0x80) ? "2  " : ((prefix&8) ? "8  " : "4  "));
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x04:  case 0x0C:  case 0x14:  case 0x1C:
  case 0x24:  case 0x2C:  case 0x34:  case 0x3C: // op al,#imm
    ONLY_REXW(prefix);
    _regs[RAX] = Runknown;
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_string("1i rax,");
    print_immediate(*_op++);
    break;

  case 0x05:  case 0x0D:  case 0x15:  case 0x1D:
  case 0x25:  case 0x2D:  case 0x35:             // op ax,#imm
    ONLY_REXW(prefix);
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_char((prefix&8) ? '8' : '4');
    print_string("i rax");
    if( op == 4/*and*/ && _regs[RAX] == Rstack && imm4(_op) == ~(BytesPerThreadStack-1) ) {
      _regs[RAX] = Rthread;     // Special hack to spot creating Thread*
      print_string("_thr");
    } else 
      _regs[RAX] = Runknown;
    print_string(",");
    print_immediate(imm4(_op), int_radix[op]);
if(Universe::heap()){
      BarrierSet* bs = Universe::heap()->barrier_set();
if(bs->kind()==BarrierSet::CardTableModRef){
CardTableModRefBS*ct=(CardTableModRefBS*)bs;
if(imm4(_op)==(intptr_t)ct->byte_map_base)
        print_string(" // _cardmark_base");
      }
    }
    _op+=4;
    break;

case 0x3D:{
    ONLY_REXW(prefix);
    _regs[RAX] = Runknown;
    print_string(int_mem[op=(_op[-1]>>3)&7]);
    print_char((prefix&8) ? '8' : '4');
    print_string("i rax,");
    print_immediate(imm4(_op)); _op+=4;
    NativeInlineCache *ic = ((NativeInlineCache*)_op-5);
    if( 0 ) ;
    else if( ic->is_clean  () )   print_string(" // IC: CLEAN"  );
    else if( ic->is_static () )   print_string(" // IC: STATIC" );
    else if( ic->is_caching() ) { print_string(" // IC: CACHING "); print_string(KlassTable::pretty_name(ic->kid())); }
    else if( ic->is_vcall  () )   print_string(" // IC: VCALL"  );
    else if( ic->is_icall  () )   print_string(" // IC: ICALL"  );
    break;
  }

  case 0x0F:                    // essentially another prefix byte
    switch( *_op++ ) {          // parse a byte
case 0x05:
      print_string("syscall");
      break;
    case 0x0B:                  // Undefined opcode
      NO_REX(prefix);
      print_string("ud2   // undefined opcode - stop execution");
      break;
    case 0x0D:                  // prefetch - write
      NO_REX(prefix);
      print_string("prefetchW_");
      switch( (*_op>>3)&7 ) {
      case 0: print_string("ex"); break;
      case 1: print_string("mod"); break;
      default: PrintUnimplemented();
      }
      print_char(' ');
      _op = print_modrm(prefix,_op);
      break;
    case 0x18:                  // prefetch - read
      ONLY_REXB(prefix);
      print_string("prefetchR_");
      switch( (*_op>>3)&7 ) {
      case 0: print_string("nto"); break;
      case 1: print_string("t0"); break;
      case 2: print_string("t1"); break;
      case 3: print_string("t2"); break;
      default: PrintUnimplemented();
      }
      print_char(' ');
      _op = print_modrm(prefix,_op);
      break;
    case 0x1F:                  // special NOP mneomic
      if( rexr(prefix,*_op) != 0 ) PrintUnimplemented();
      print_string("nop   ");
      _op = print_modrm(prefix,_op);
      print_string(" // ");
      print_integer(_op-orig_op);
      print_string(" byte nop");
      break;
    case 0x28:                  // aligned FP mov (movap[sd])
      modrm = *_op;
      if( (modrm & 0xC0) == 0xC0 ) print_string((prefix&0x80)==0x80 ? "mov8  " : "mov4  ");
      else                         print_string((prefix&0x80)==0x80 ? "ld8a  " : "ld4a  ");
      print_Fregister((FRegister)rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      break;
    case 0x29:                  // aligned FP mov (movap[sd])
      modrm = *_op;
      if( (prefix & Assembler::REX_W) != 0 ) PrintUnimplemented();
      if( (modrm & 0xC0) == 0xC0 ) print_string((prefix&0x80)==0x80 ? "mov8  " : "mov4  ");
      else                         print_string((prefix&0x80)==0x80 ? "st8a  " : "st4a  ");
      _op = print_modrm_flt(prefix,_op);
      print_char(',');
      print_Fregister((FRegister)rexr(prefix,modrm)); // dst
      break;
    case 0x2E:                  // FP cmp
      modrm = *_op;            
      print_string((prefix&0x80)==0x80 ? "cmp8  " : "cmp4  ");
      print_Fregister((FRegister)rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      break;
case 0x38:
      switch( *_op++ ) {          // parse a byte
case 0x17:
        if( prefix&0x80 ) {
          print_string("ptest ");
          print_Fregister((FRegister)rexr(prefix, *_op));
          print_char(',');
          _op = print_modrm_flt(prefix,_op);
        } else Unimplemented(); // MMX?
        break;
      default: Unimplemented(); break;
      }
      break;
    case 0x40:  case 0x41:  case 0x42:  case 0x43: // conditional move
    case 0x44:  case 0x45:  case 0x46:  case 0x47:
    case 0x48:  case 0x49:  case 0x4A:  case 0x4B:
    case 0x4C:  case 0x4D:  case 0x4E:  case 0x4F:
      modrm = *_op;
      print_string((prefix & 8) ? "cmov8" : "cmov4");
      print_string(jcc[_op[-1]&0x0F]);  print_string(" ");
      print_dst(rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm(prefix,_op);
      break;
case 0x54:
      print_string(prefix&0x80 ? "andd  " : "andf  ");
      print_Fregister((FRegister)rexr(prefix, *_op));
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      if( _op[-6] == 0x04 && _op[-5] == 0x25 && 
          (address)((intptr_t)*(jint*)(_op-4)) == 
          MacroAssembler::quadword_constant( CONST64(0x7FFFFFFFFFFFFFFF), CONST64(0x7FFFFFFFFFFFFFFF) ) )
        print_string("  // absD");
      break;
case 0x57:
      print_string(prefix&0x80 ? "xord  " : "xorf  ");
      print_Fregister((FRegister)rexr(prefix, *_op));
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      if( _op[-6] == 0x1C && _op[-5] == 0x25 && 
          (address)((intptr_t)*(jint*)(_op-4)) == 
          MacroAssembler::quadword_constant( CONST64(0x8000000000000000), CONST64(0x8000000000000000) ) )
        print_string("  // negD");
      break;
      break;
    case 0x6E:                  // FP mov (movd/q)
      if( (prefix & 0x80)==0 ) PrintUnimplemented();    // MMX registers?
      modrm = *_op;
      _regs[rexr(prefix,modrm)] = ((modrm & 0xC0) == 0xC0 && (prefix & 8)!=0) ? _regs[rexb(prefix, modrm&7)] : Runknown;
      if( (modrm & 0xC0) == 0xC0 ) print_string((prefix & 8) ? "mov8  " : "mov4  ");
      else                         print_string((prefix & 8) ? "ld8   " : "ld4   ");
      print_Fregister((FRegister)rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm(prefix,_op);
      break;
case 0x74:
      if( (prefix & 0x80)==0 ) PrintUnimplemented();    // MMX registers?
      modrm = *_op;
      print_string("pcmpeqb ");
      print_Fregister((FRegister)rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      break;
    case 0x7E:                  // FP mov (movd/q)
      if( (prefix & 0x80)==0 ) PrintUnimplemented();    // MMX registers?
      modrm = *_op;
      _regs[rexr(prefix,modrm)] = ((modrm & 0xC0) == 0xC0 && (prefix & 8)!=0) ? _regs[rexb(prefix, modrm&7)] : Runknown;
      if( (modrm & 0xC0) == 0xC0 ) print_string((prefix & 8) ? "mov8  " : "mov4  ");
      else                         print_string((prefix & 8) ? "ld8   " : "ld4   ");
      _op = print_modrm(prefix,_op);
      print_char(',');
      print_Fregister((FRegister)rexr(prefix,modrm)); // src
      break;
case 0x7F:
      if( (prefix & 0x80) == 0 ) PrintUnimplemented(); // MMX registers?
      if( (prefix & 0x08) != 0 ) PrintUnimplemented(); // No 'wide'?
      modrm = *_op;
      _regs[rexr(prefix,modrm)] = ((modrm & 0xC0) == 0xC0 && (prefix & 8)!=0) ? _regs[rexb(prefix, modrm&7)] : Runknown;
      print_string( ((modrm & 0xC0) == 0xC0) ? "mov16 " : "st16  ");
      _op = print_modrm(prefix,_op);
      print_char(',');
      print_Fregister((FRegister)rexr(prefix,modrm)); // src
      break;
    case 0x80:  case 0x81:  case 0x82:  case 0x83: // long jumps
    case 0x84:  case 0x85:  case 0x86:  case 0x87:
    case 0x88:  case 0x89:  case 0x8A:  case 0x8B:
    case 0x8C:  case 0x8D:  case 0x8E:  case 0x8F:
      NO_REX(prefix);
      print_char('j'); print_string(jcc[_op[-1]&0x0F]);  print_string("   ");
      op = imm4(_op); _op+=4;     // offset
      print_jump_target(_op+op);  // jcc targets are PC relative
      break;
    case 0x90:  case 0x91:  case 0x92:  case 0x93: // long jumps
    case 0x94:  case 0x95:  case 0x96:  case 0x97:
    case 0x98:  case 0x99:  case 0x9A:  case 0x9B:
    case 0x9C:  case 0x9D:  case 0x9E:  case 0x9F:
      // To encode non 8byte registers a uniform rex is expected
      if( (0xF8 & *_op) != 0xC0 ) PrintUnimplemented();
      print_string("set"); print_string(jcc[_op[-1]&0x0F]);  print_string(" ");
      print_dst(rexb(prefix,(*_op++)&7)); // dst
      break;
    case 0xA3:                  // bit-test register
    case 0xAB:                  // bit-set  register
    case 0xB3:                  // bit-clr  register
    case 0xBB:                  // bit-flip register
      ONLY_REXWB(prefix);
      print_string(btst[(_op[-1]>>3)&7]);
      print_char((prefix&8) ? '8' : '4');
      print_string("  ");
op=*_op;
      _op = print_modrm(prefix,_op);
      print_char(',');
      print_dst(rexr(prefix,op)); // src
      break;
    case 0xAE:                  // fences
      switch( *_op++ ) {
      case 0xE8: print_string("lfence"); break;
      case 0xF0: print_string("mfence"); break;
      case 0xF8: print_string("sfence"); break;
      default: PrintUnimplemented();
      }
      break;
    case 0xAF:                  // imul - signed multiply
      print_string("muls");
      modrm = (prefix&8) ? '8' : '4';
      if( (prefix&0x80) == 0x80 ) modrm = '2';
      print_char(modrm);
      print_char(' ');
      print_dst(rexr(prefix,*_op)); // dst
      print_char(',');
      _op = print_modrm(prefix,_op);
      break;
    case 0xB1:                  // CAS8
      assert0( (*_op & 0xC0) != 0xC0 );
      print_string("cas");
      print_char((prefix&8) ? '8' : '4');
      print_string("  ");
      print_dst(rexr(prefix,*_op)); // dst
      print_char(',');
      _op = print_modrm(prefix,_op);
      break;
    case 0xB6: case 0xB7:       // ldz1/ldz2
    case 0xBE: case 0xBF:       // lds1/lds2 - 'wide' is required to sign-extend to 64
      if( (*_op & 0xC0) == 0xC0 && (prefix & 0x40) != 0x40 ) {
        print_string("mov");
        print_char  ( (_op[-1]&0x8) ? 's' : 'z' ); // lds vs ldz
        print_string("x4");
        print_char  ( '1'+(_op[-1]&1) );           // lds1 vs lds2
        print_char  (' ');
      } else {
        print_string( (*_op & 0xC0) == 0xC0 ? "mv" : "ld");
        print_char( (_op[-1]&0x8) ? 's' : 'z' ); // lds vs ldz
        print_char( '1'+(_op[-1]&1) );           // lds1 vs lds2
        print_string("  ");
      }
      print_dst(rexr(prefix,*_op)); // dst
      print_char(',');
      _op = print_modrm(prefix,_op);
      break;
    case 0xBA:                  // bit-test immediate
      ONLY_REXWB(prefix);
      print_string(btst[(*_op>>3)&7]);
      print_char((prefix&8) ? '8' : '4');
      print_string("i ");
      _op = print_modrm(prefix,_op);
      print_char(',');
      print_immediate(*_op++);
      break;
    case 0xC3: // move non-temporal
      ONLY_REXWR(prefix);
      modrm = *_op;
      print_string((prefix&8)?"st8   ":"st4   ");
      _op = print_modrm(prefix,_op);
      print_char(',');
      print_register(rexr(prefix,modrm));
      print_string(" // hint: non-temporal, data will not be read again");
      break;
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: // bswap
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:
      ONLY_REXWB(prefix);
      print_string("bswap");
      print_string((prefix&8) ? "8 " : "4 ");
      print_dst(rexb(prefix,_op[-1]&7));
      if( (prefix&8) == 0 ) print_string(" // destroy register?");
      break;
case 0xD7:
      if( (prefix & 0x80)==0 ) PrintUnimplemented();    // MMX registers?
      modrm = *_op;
      print_string("pmovmskb ");
      print_register(rexr(prefix,modrm)); // dst
      print_char(',');
      _op = print_modrm_flt(prefix,_op);
      break;
case 0xEF:
      if( prefix&0x80 ) {
        print_string("pxor  ");
        print_Fregister((FRegister)rexr(prefix, *_op));
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
      } else PrintUnimplemented(); // MMX?
      break;
    default: 
      print_integer(0x0F   ,16);  print_char(' ');
      print_integer(_op[-1],16);  print_char(' ');
      print_integer(_op[ 0],16);  print_char(' ');
      print_integer(_op[ 1],16);  print_char(' ');
      print_string("// unknown instruction");
    }
    break;

  case 0x40:  case 0x41:  case 0x42:  case 0x43:
  case 0x44:  case 0x45:  case 0x46:  case 0x47:
    ONLY_REXWB(prefix);
    print_string("inc   ");
    print_dst(rexb(prefix,_op[-1]&7)); 
    break;
 
  case 0x48:  case 0x49:  case 0x4a:  case 0x4b:
  case 0x4c:  case 0x4d:  case 0x4e:  case 0x4f: 
    ONLY_REXWB(prefix);
    print_string("dec   ");
    print_dst(rexb(prefix,_op[-1]&7)); 
    break;

  case 0x50:  case 0x51:  case 0x52:  case 0x53:
  case 0x54:  case 0x55:  case 0x56:  case 0x57: 
    ONLY_REXB(prefix);
    print_string("push  ");
    print_register(rexb(prefix,_op[-1]&7)); 
    break;
  case 0x58:  case 0x59:  case 0x5a:  case 0x5b:
  case 0x5c:  case 0x5d:  case 0x5e:  case 0x5f: 
    ONLY_REXB(prefix);
    print_string("pop   ");
    print_dst(rexb(prefix,_op[-1]&7)); 
    break;

  case 0x63:                    // movsxd
    ONLY_REX(prefix);
    if( (*_op & 0xC0) == 0xC0 ) print_string("movsx84 ");
    else                        print_string("lds4  ");
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x68:                    // pushi #8
    ONLY_REX(prefix);
    print_string("pushi ");
    if( prefix&0x80 ) { print_immediate(imm2(_op)   ); _op += 2; }
    else              { print_immediate(imm4(_op),16); _op += 4; }
    break;

  case 0x69:                    // imul dst,src,#32b
  case 0x6B:                    // imul dst,src,#8b
op=*_op;
    print_string("muls");
    print_string((prefix&0x80) ? "2 " : ((prefix&8) ? "8 " : "4 "));
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    print_char(',');

    if( op == 0x6B )       { print_immediate(    *_op++);             }
    else if( prefix&0x80 ) { print_immediate(imm2(_op)   ); _op += 2; }
    else                   { print_immediate(imm4(_op),16); _op += 4; }
    break;

  case 0x6A:                    // pushi #8
    NO_REX(prefix);
    print_string("pushi ");
    print_immediate(*(char*)_op++);
    break;

  case 0x70:  case 0x71:  case 0x72:  case 0x73: // short jumps
  case 0x74:  case 0x75:  case 0x76:  case 0x77:
  case 0x78:  case 0x79:  case 0x7A:  case 0x7B:
  case 0x7C:  case 0x7D:  case 0x7E:  case 0x7F: {
    NO_REX(prefix);
    print_char('j'); print_string(jcc[_op[-1]&0x0F]);  print_string("   ");
    op = *(char*)_op++;         // offset
    print_jump_target(_op+op);  // jcc targets are PC relative
    break;
  }

  case 0x80:                    // cmp1i - byte compare
    ONLY_REXBX(prefix);
    op=rexr(prefix,*_op);
    print_string(int_mem[op]);
    print_string("1i ");
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_immediate(*(signed char *)_op++);
    break;

  case 0x81:                    // add 32-bit immediate
    print_string(int_mem[op=rexr(prefix,modrm=*_op)]);
    print_char((prefix&0x80) ? '2' : ((prefix&8) ? '8' : '4'));
    print_string("i ");
    // special hack to spot creating Thread* from SP
    if( (modrm & 0xC0) == 0xC0 ) {
      int dst = rexb(prefix, modrm&7);
      _regs[dst] = (op == 4/*andi*/ && _regs[dst] == Rstack &&  // dest is crushed or holds Thread*
                    imm4(_op+1)== ~(BytesPerThreadStack-1)) ? Rthread : Runknown;
    }
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_immediate((prefix&0x80)?imm2(_op):imm4(_op),int_radix[op]);
if(Universe::heap()){
      BarrierSet* bs = Universe::heap()->barrier_set();
      if (bs->kind() == BarrierSet::CardTableModRef ) {
      CardTableModRefBS* ct = (CardTableModRefBS*)bs;
      if( imm4(_op) == (intptr_t)ct->byte_map_base )
        print_string(" // _cardmark_base");
      }
    }
    _op += (prefix&0x80)?2:4;
    break;

  case 0x83:                    // add 8-bit immediate
    print_string(int_mem[op=rexr(prefix,*_op)]);
    print_char((prefix&0x80) ? '2' : ((prefix&8) ? '8' : '4'));
    print_string("i ");
    _op = print_modrm_dst(prefix,_op);
    print_char(',');
    print_immediate(*(char*)_op,int_radix[op]); 
    _op+=1;
    break;

  case 0x84:                    // test byte
    ONLY_REXRB(prefix);
    print_string("test1 ");
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x85:                    // test
    print_string((prefix&8) ? "test8 " : "test4 ");
    print_dst(rexr(prefix,*_op)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x86:                    // xchg byte
    NO_REX(prefix); // Actually allowed, but I'm not decoding them here
    // Actually only decode swap of AH/AL or BH/BL, etc
    // and decode as 'bswap2 RAX'
    modrm = *_op++;
    if( (modrm & 0xC0) != 0xC0 ) PrintUnimplemented();
    if( (((modrm >> 3)&7) ^ (modrm&7)) != 4 ) PrintUnimplemented();
    print_string("bswap2 ");
    print_dst((Register)(modrm&3));
    break;

  case 0x87:                    // xchg
    modrm = *_op;
    if ((prefix & 8) != 0 && rexr(prefix,modrm) == RAX) {
      // is this a verify_oop? look for a call to verify oop after this
      // NB this code can't handle fully SIB bytes or operand size overrides
      address     dest = (address)-1;
      bool has_sib = ((modrm & 0xC0) != 0xC0) && ((modrm & 7) == 4);
      if (has_sib) _op++; // sib fix up
      u_char  *next_op = _op;
      if        ((modrm & 0xC0) == 0x00 || (modrm & 0xC0) == 0xC0) {
        if (_op[1] == 0xE8) { // call opcode?
          dest    = _op+6+imm4(_op+2);
          next_op += 1/*modrm*/     +1/*call opcode*/+4/*call disp*/+2/*rex+xchg8 opcode*/+1/*xchg8 modrm*/;
        }
      } else if ((modrm & 0xC0) == 0x40) {
        if (_op[2] == 0xE8) { // call opcode?
          dest = _op+7+imm4(_op+3);
          next_op += 2/*modrm+disp*/+1/*call opcode*/+4/*call disp*/+2/*rex+xchg8 opcode*/+2/*xchg8 modrm+disp*/;
        }
      } else /* ((modrm & 0xC0) == 0x80) */ {
        if (_op[5] == 0xE8) { // call opcode?
          dest = _op+10+imm4(_op+6);
          next_op += 5/*modrm+disp*/+1/*call opcode*/+4/*call disp*/+2/*rex+xchg8 opcode*/+5/*xchg8 modrm+disp*/;
        }
      }
      if (has_sib) { _op--; next_op++; } // more sib fix up
if(dest!=(address)-1){
StubCodeDesc*desc=StubCodeDesc::desc_for(dest);
        if (desc != NULL && strcmp(desc->name(), "verify_oop") == 0) {
          // Looks like a verify oop
          print_string("verify_oop ");
          print_modrm(prefix,_op);
          _op = next_op;
          break;
        }
      }
    }
    // track reg contents a little
    _regs[rexr(prefix,modrm)] = ((modrm & 0xC0) == 0xC0 && (prefix & 8)!=0) ? _regs[rexb(prefix, modrm&7)] : Runknown;
    print_string(prefix & 0x80 ? "xchg2 " : ((prefix & 8) ? "xchg8 " : "xchg4 "));
    print_dst(rexr(prefix,modrm)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x88:                    // store byte
    ONLY_REXRXB(prefix);
    modrm = *_op;
    print_string("st1   ");
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_register(rexr(prefix,modrm)); // src
    break;

  case 0x89:                    // store
    modrm = *_op;
    if( (modrm & 0xC0) == 0xC0) // track reg contents a little
      _regs[rexb(prefix, modrm&7)] = (prefix & 8) ? _regs[rexr(prefix,modrm)] : Runknown;
    if( (modrm & 0xC0) == 0xC0 ) print_string(prefix & 0x80 ? "mov2  " : ((prefix & 8) ? "mov8  " : "movz4 "));
    else                         print_string(prefix & 0x80 ? "st2   " : ((prefix & 8) ? "st8   " : "st4   "));
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_register(rexr(prefix,modrm)); // src
    break;

  case 0x8b:                    // both reg-reg and load from memory
    modrm = *_op;
    // track reg contents a little
    _regs[rexr(prefix,modrm)] = ((modrm & 0xC0) == 0xC0 && (prefix & 8)!=0) ? _regs[rexb(prefix, modrm&7)] : Runknown;
    if( (modrm & 0xC0) == 0xC0 ) print_string(prefix & 0x80 ? "mov2  " : ((prefix & 8) ? "mov8  " : "movz4 "));
    else                         print_string(prefix & 0x80 ? "ld2   " : ((prefix & 8) ? "ld8   " : "ldz4  "));
    print_dst(rexr(prefix,modrm)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x8d:                    // lea
    modrm = *_op;
    _regs[rexr(prefix,modrm)] = Runknown; // assume register crush
    print_string(prefix & 0x80 ? "lea2  " : ((prefix & 8) ? "lea8  " : "lea4  "));
    print_dst(rexr(prefix,modrm)); // dst
    print_char(',');
    _op = print_modrm(prefix,_op);
    break;

  case 0x8f:                    // pop into memory
    NO_REX(prefix);
    op=rexr(prefix,*_op);
    switch( op ) {
    case 0:    print_string("pop   "); break;
    default:   PrintUnimplemented();
    }
    _op = print_modrm(prefix,_op);
    break;

  case 0x90:                    // special NOP mneomic
    print_string("nop   // ");
    print_integer(_op-orig_op);
    print_string(" byte nop");
    break;

  case 0x99:                    // Sign-extend RAX into RDX
    ONLY_REXW(prefix);
    print_string("cdq");
    print_char( (prefix&8) ? '8' : '4');
    print_string("  rdx,rax // sign extend rax into rdx");
    break;

  case 0x98:                    // Sign-extend EAX into RAX
    ONLY_REXW(prefix);
    print_string("cwd");
    print_char( (prefix&8) ? '8' : '4');
    print_string("  RAX // sign extend RAX");
    break;

  case 0x9B:
    switch(*_op++){
case 0xDD:
      switch( (*_op>>3)&7 ) {
      case 7:
        print_string("x87_stsw ");
        _op = print_modrm(prefix,_op);
        break;
      default: PrintUnimplemented();
      }
      break;
    default: PrintUnimplemented();
    }
    break;

  case 0x9c:                    // push flags; also used at start of a call to 'stop'
    NO_REX(prefix);
    if( _op[0] == 0x50 && _op[1] == 0x51 && _op[23] == 0xbf && _op[56] == 0x9D && _op[57] == 0xCC ) {
      // appears to be a call to 'stop'
      print_string("stop  // ");
      print_string((const char *)imm4(_op+24));
      _op += 58;
    } else if( _op[0] == 0x50 && _op[1] == 0x51 && _op[24] == 0xbf && _op[61] == 0x9D && _op[62] == 0xCC ) {
      // appears to be a call to 'stop' with a far string
      print_string("stop  // X");
      print_string((const char *)imm8(_op+25));
      _op += 63;
    } else 
      print_string("pushf // push flags"); 
    break;
  case 0x9d: NO_REX(prefix); print_string("popf  // pop flags"); break;

case 0xa4:
    ONLY_REXW(prefix);
    print_string("movs1 // *rdi++ = *rsi++");
    _regs[RAX] = _regs[RSI] = Runknown;
    break;

  case 0xa5:  case 0xa7:  case 0xad:  
    if(      _op[-1] == 0xa5 ) print_string("movs");
    else if( _op[-1] == 0xa7 ) print_string("cmps");
    else if( _op[-1] == 0xad ) print_string("lods");
    if(      prefix == 0x80             ) print_char('2');
    else if( prefix == Assembler::REX_W ) print_char('8');
    else {                NO_REX(prefix); print_char('4'); }
    print_string(" // *rdi++ = *rsi++");
    _regs[RAX] = _regs[RSI] = Runknown;
    break;

case 0xa8:
    NO_REX(prefix);
    print_string("testi rax,");
    print_immediate(*_op++);
    break;

case 0xa9:
    NO_REX(prefix);
    print_string("testi rax,");
    print_immediate(imm4(_op));
    _op+=4;
    break;

case 0xac:
    ONLY_REXW(prefix);
    print_string("lods1 // al = *rsi++");
    _regs[RAX] = _regs[RSI] = Runknown;
    break;

  case 0xaf:                    // C1 patching?
    print_string("// unknown - assumed to be C1 patching info");
    _op += 5;                   // 5 bytes of patching info
    break;

  case 0xb4:  case 0xb5:  case 0xb6:  case 0xb7:
    if( (prefix & Assembler::REX) == 0 ) PrintUnimplemented(); // byte operation so REX prefix expected
    // fall through
  case 0xb0:  case 0xb1:  case 0xb2:  case 0xb3: { // move 8-bit immediate into gpr
    ONLY_REXB(prefix); 
    print_string("mov1  ");
    print_dst(rexb(prefix,_op[-1]&7));
    print_char(',');
    print_immediate(*_op++,10);
    break;
  }

  case 0xb8:  case 0xb9:  case 0xba:  case 0xbb:
  case 0xbc:  case 0xbd:  case 0xbe:  case 0xbf: { // move 32-bit immediate into gpr
    ONLY_REXWB(prefix);
    print_string("mov8i ");
    print_dst(rexb(prefix,_op[-1]&7));
    print_char(',');
    if( prefix & 8 ) {
      int64 x = imm8(_op); _op+=8;
print_integer(x,16);
    } else {
      unsigned int x = imm4(_op); _op+=4;
print_integer(x,16);
      if     ( x==0x3f800000LL ) print_string("  // 1.0f");
      else if( x==0x40000000LL ) print_string("  // 2.0f");
      else if( x==0x7FFFFFFFLL ) print_string("  // MAX_INT");
      else if( could_be_C_string((address)x) ) {
        // is x really a pointer to a string?
        char *s = (char*)x;
        if ( isalnum(s[0]) && isalnum(s[1]) && isalnum(s[2]) ) {
          print_string("  // \"");
print_string(s);
          print_char('"');
        }
      }
    }
    break;
  }

  case 0xc1: {                  // shift
    print_string(shf[((modrm=*_op)>>3)&7]);
    modrm = (prefix&8) ? '8' : '4';
    if( (prefix&0x80) == 0x80 ) modrm = '2';
    print_char(modrm);
    print_string("i ");
    int dst = rexb(prefix,modrm&7);
    int oldregs = _regs[dst];
    _op = print_modrm_dst(prefix,_op);
    print_char(',');
    print_immediate(*_op++);
    // some register tracking
    if( _op[-1] == 64-objectRef::klass_id_shift && 
        (modrm&0xC0) == 0xC0 ) {
      if( ((modrm>>3)&7) == 4/*shl*/ )  _regs[dst] = Rstrip1;
      if( ((modrm>>3)&7) == 5/*shr*/ && oldregs   == Rstrip1 )
        print_string("      // strip metadata");
    }
    break;
  }
  case 0xc3: NO_REX(prefix); print_string("ret"); break;

  case 0xc6:                    // immediate to reg/mem
    ONLY_REXBX(prefix);
    if( ((*_op) & 0xC0) != 0xC0 ) print_string("st");
    else print_string("mov");
    print_string("1i ");
    if( ((*_op) & 0xC0) != 0xC0 ) print_char(' ');
    _op = print_modrm(prefix,_op);
    print_char(',');
    print_immediate(*_op++);
    break;

  case 0xc7:                    // immediate to reg/mem
    if( ((*_op) & 0xC0) != 0xC0 ) print_string("st");
    else print_string("mov");
    modrm = (prefix&8) ? '8' : '4';
    if( (prefix&0x80) == 0x80 ) modrm = '2';
    print_char(modrm);
    print_string("i ");
    if( ((*_op) & 0xC0) != 0xC0 ) print_char(' ');
    _op = print_modrm(prefix,_op);
    print_char(',');
    if( (prefix&0x80) == 0x80 ) { modrm = imm2(_op);  _op+=2; }
    else                        { modrm = imm4(_op);  _op+=4; }
    print_immediate(modrm, (modrm>>16)?16:10);
    break;

  case 0xc8:                    // enter
    NO_REX(prefix);
    print_string("enter ");
    print_immediate(imm2(_op)); _op+=2;
    print_char(',');
    print_immediate((*_op++)&31);
    break;
  case 0xc9: NO_REX(prefix); print_string("leave"); break;
  case 0xcb: NO_REX(prefix); print_string("far return // did you really mean it?"); break;

  case 0xcc: NO_REX(prefix); print_string("int3  // break to debugger"); break; // into debugger

  case 0xd1:                    // shift-by-1
    ONLY_REXWB(prefix);
    print_string(shf[op=(*_op>>3)&7]);
    print_char((prefix&8) ? '8' : '4');
    print_string("i ");
    _op = print_modrm_dst(prefix,_op);
    print_string(",1");
    break;

  case 0xd3:                    // shift-by-cl
    ONLY_REXWB(prefix);
    print_string(shf[op=(*_op>>3)&7]);
    print_char((prefix&8) ? '8' : '4');
    print_string("  ");
    _op = print_modrm_dst(prefix,_op);
    print_string(",cl");
    break;

case 0xd7:
    NO_REX(prefix);
    print_string("xlat   // al = [rbx+al]");
    _regs[RAX] = Runknown;
    break;

case 0xd9:
    switch( (*_op>>3)&7 ) {
    case 0:
      print_string("x87_ld4 ST(0),");
      _op = print_modrm(prefix,_op);
      break;
    case 3:
      print_string("x87_st4p ");
      _op = print_modrm(prefix,_op);
      print_string(",ST(0)");
      break;
    case 6:
      switch(*_op++) {
      case 0xF2:  NO_REX(prefix); print_string("x87_tan // also pushes 1.0"); break;
      default: PrintUnimplemented();
      }
      break;
    case 7:
      switch(*_op++) {
      case 0xF8:  NO_REX(prefix); print_string("x87_prem ST(0),ST(1)");   break;
      case 0xFE:  NO_REX(prefix); print_string("x87_sin"); break;
      case 0xFF:  NO_REX(prefix); print_string("x87_cos"); break;
      default: PrintUnimplemented();
      }
      break;
    default: PrintUnimplemented();
    }
    break;

case 0xdd:
    switch( (*_op>>3)&7 ) {
    case 0:
      print_string("x87_ld8 ST(0),");
      _op = print_modrm(prefix,_op);
      break;
    case 3:
      if( *_op==0xd8 ) {        // rare x87 pop form for fptan
        print_string("x87_mv8p ST(0),ST(0) // pop");
      } else {
        print_string("x87_st8p ");
        _op = print_modrm(prefix,_op);
        print_string(",ST(0)");
      }
      break;
    default: PrintUnimplemented();
    }
    break;

case 0xdf:
    NO_REX(prefix);
    switch( *_op++ ) {
case 0xC0:
      print_string("x87_freep ST(0)");
      break;
    default: PrintUnimplemented();
    }
    break;

case 0xe0:{
    NO_REX(prefix);       // actually allows REX.W I've not implemented
    print_string("loopne ");
    op = *(char*)_op++;         // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
    _regs[RCX] = Runknown;      // crush RCX
    break;
  }

case 0xe1:{
    NO_REX(prefix);       // actually allows REX.W I've not implemented
    print_string("loope ");
    op = *(char*)_op++;         // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
    _regs[RCX] = Runknown;      // crush RCX
    break;
  }

case 0xe2:{
    NO_REX(prefix);      // actually allows REX.W I've not implemented
    print_string("loop  ");
    op = *(char*)_op++;         // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
    _regs[RCX] = Runknown;      // crush RCX
    break;
  }

case 0xe3:{
    NO_REX(prefix);     // actually allows REX.W I've not implemented
    print_string("jrcxz ");
    op = *(char*)_op++;         // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
    break;
  }

  case 0xe8: {                  // call
    NO_REX(prefix); 
    int x = imm4(_op); _op+=4;
    StubCodeDesc *desc = StubCodeDesc::desc_for(_op+x);
    if (desc != NULL && strcmp(desc->name(), "verify_oop") == 0) { // Looks like a verify oop
      print_string("verify_oop rax");
      break;
    }
    print_string("call  ");
    print_jump_target(_op+x);   // call targets are PC relative
    int rbx = _regs[RBX];       // RBX not blown by nearly any call, and holds RBX_THR often
    reset_reg_tracking();
    _regs[RBX] = rbx;
    break;
  }

case 0xe9:{
    NO_REX(prefix);
    print_string("jmp   ");
    op = imm4(_op); _op += 4;   // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
#ifdef AZ_PROXIED
    if(_blob && _blob->is_native_method() && _blob->contains(_op+op) &&
       ((((intptr_t)_op+op) & 7) == 0) &&
       _blob->method().as_methodOop()->is_remote()) {
      // remote c2n adapter, raw data for invocation
      _raw_data = op; // record raw data for processing
    }
#endif // AZ_PROXIED
    reset_reg_tracking();
    break;
  }

case 0xeb:{
    NO_REX(prefix);
    print_string("jmp   ");
    op = *(signed char *)_op++;                // offset
    print_jump_target(_op+op);  // jmp targets are PC relative
#ifdef AZ_PROXIED
    if(_blob && _blob->is_native_method() && _blob->contains(_op+op) &&
       op >= 40 && ((((intptr_t)_op+op) & 7) == 0) &&
       _blob->method().as_methodOop()->is_remote()) {
      // remote c2n adapter, raw data for invocation
      _raw_data = op; // record raw data for processing
    }
#endif // AZ_PROXIED
    reset_reg_tracking();
    break;
  }

  case 0xf2:                    // Cool FP ops...
    if( *_op >= Assembler::REX && *_op <= Assembler::REX_WRXB )
      prefix |= *_op++;         // Grab any prefix byte 
    switch( *_op++ ) {
case 0x0F:
      switch( op = *_op++ ) {
case 0x10:
        modrm = *_op;
        ONLY_REXRXB(prefix);
        if( (modrm & 0xC0) == 0xC0 ) print_string("mov8  ");
        else                         print_string("ld8   ");
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x11:
        modrm = *_op;
        ONLY_REXRXB(prefix);
        if( (modrm & 0xC0) == 0xC0 ) print_string("mov8  ");
        else                         print_string("st8   ");
        _op = print_modrm_flt(prefix,_op);
        print_char(',');
        print_Fregister((FRegister)rexr(prefix,modrm)); // dst
        break;
case 0x2A:
        print_string((prefix&Assembler::REX_W)==Assembler::REX_W ? "cvt_l2d " : "cvt_i2d ");
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm(prefix,_op);
        break;
case 0x2C:
        print_string((prefix&Assembler::REX_W)==Assembler::REX_W ? "cvt_d2l " : "cvt_d2i ");
        modrm = *_op;
        print_dst(rexr(prefix,modrm)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x51:case 0x58:case 0x59:case 0x5C:case 0x5E:
        ONLY_REXRXB(prefix);
        if     ( op == 0x51 ) print_string("sqrtsd ");
        else if( op == 0x58 ) print_string("addd  ");
        else if( op == 0x59 ) print_string("muld  ");
        else if( op == 0x5C ) print_string("subd  ");
        else if( op == 0x5E ) print_string("divd  ");
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x5A:
        print_string("cvt_d2f ");
        modrm = *_op;
        print_Fregister((FRegister)rexr(prefix,modrm)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
      default:
        PrintUnimplemented();
        break;
      }
      break;
case 0xAF:
      print_string("repnz scas8 // while( *di++!=rax; rcx-- )");
      break;
    default:      PrintUnimplemented();
    }
    break;

  case 0xf3:                    // Cool FP ops...
    if( *_op >= Assembler::REX && *_op <= Assembler::REX_WRXB )
      prefix |= *_op++;         // Grab any prefix byte 
    switch( op = *_op++ ) {
    case 0x0F:                  // Ok, FP ops go here...
      switch( op = *_op++ ) {
case 0x10:case 0x6F:
        modrm = *_op;
        ONLY_REXRXB(prefix);
        if( (modrm & 0xC0) == 0xC0 ) {
          if( op == 0x10 ) print_string("mov4  ");
          else             print_string("mov16u ");
        } else {
          if( op == 0x10 ) print_string("ld4   ");
          else             print_string("ld16u ");
        }
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x11:case 0x7F:
        modrm = *_op;
        ONLY_REXRXB(prefix);
        if( (modrm & 0xC0) == 0xC0 ) {
          if( op == 0x11 ) print_string("mov4  ");
          else             print_string("mov16u ");
        } else {
          if( op == 0x11 ) print_string("st4   ");
          else             print_string("st16u ");
        }
        _op = print_modrm_flt(prefix,_op);
        print_char(',');
        print_Fregister((FRegister)rexr(prefix,modrm)); // dst
        break;
case 0x2A:
        print_string((prefix&Assembler::REX_W)==Assembler::REX_W ? "cvt_l2f " : "cvt_i2f ");
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm(prefix,_op);
        break;
case 0x2C:
        print_string((prefix&Assembler::REX_W)==Assembler::REX_W ? "cvt_f2l " : "cvt_f2i ");
        modrm = *_op;
        print_dst(rexr(prefix,modrm)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x5A:
        print_string("cvt_f2d ");
        modrm = *_op;
        print_Fregister((FRegister)rexr(prefix,modrm)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
case 0x51:case 0x58:case 0x59:case 0x5C:case 0x5E:
        ONLY_REXRXB(prefix);
        if     ( op == 0x51 ) print_string("sqrtsf ");
        else if( op == 0x58 ) print_string("addf  ");
        else if( op == 0x59 ) print_string("mulf  ");
        else if( op == 0x5C ) print_string("subf  ");
        else if( op == 0x5E ) print_string("divf  ");
        print_Fregister((FRegister)rexr(prefix,*_op)); // dst
        print_char(',');
        _op = print_modrm_flt(prefix,_op);
        break;
      default:
        PrintUnimplemented();
      }
      break;
    case 0x66: // size override
      switch( op = *_op++ ) {
case 0xa5:
        print_string("rep movs2 *di++,*si++; rcx--");
        break;
      default:
        PrintUnimplemented();
      }
      break;
    case 0xc3: // Gnu oddity to avoid a single-byte RET penalty on Athalon
      print_string("repz: ret"); // The repz prefix is ignored here.
      break;
case 0xa4:
      print_string("rep movs1 *di++,*si++; rcx--");
      break;
case 0xa5:
      print_string("rep movs8 *di++,*si++; rcx--");
      break;
case 0xa7:
      print_string("repz cmps *di++,*si++; rcx--");
      break;
case 0xab:
      print_string("rep stos8 *di++,rax; rcx--");
      break;
case 0xae:
      print_string("rep scas1 *di++,rax; rcx--");
      break;
    default:
      printf("%2.2x %2.2x %2.2x // unknown instruction\n",_op[-2],_op[-1],_op[0]);
      break;
    }
    break;
    
case 0xf6:
  case 0xf7:                    // unary math group
    // No rex asserts; support size prefix (16b), wide (64b), X & B.
    modrm = (prefix&8) ? '8' : '4';
    if( (prefix&0x80) == 0x80 ) modrm = '2';
    if( _op[-1] == 0xf6 ) modrm = '1';
    
op=*_op;
    switch( (op>>3)&7 ) {
    case 0:  print_string("test"); print_char(modrm); print_string("i "); break;
    case 1:  PrintUnimplemented();
    case 2:  print_string("not"); print_char(modrm); print_string("  "); break;
    case 3:  print_string("neg"); print_char(modrm); print_string("  "); break;
    case 4:  print_string("mulu"); print_char(modrm); print_string(" rdx:rax = rax*"); break;
    case 5:  print_string("muls"); print_char(modrm); print_string(" rdx:rax = rax*"); break;
    case 6:  print_string("divu"); print_char(modrm); print_string(" rdx:rax = rax/"); break;
    case 7:  print_string("divs"); print_char(modrm); print_string(" rdx:rax = rax/"); break;
    }
    _op=print_modrm_dst(prefix,_op);
    if( ((op>>3)&7) == 0 ) {    // testi has many immediate forms
      print_char(',');
      switch(modrm) {
      case '1': modrm = *_op++; break;
      case '2': modrm = imm2(_op); _op+=2; break;
      case '4': modrm = imm4(_op); _op+=4; break; // zero extended
      case '8': modrm = imm4(_op); _op+=4; break; // sign extended
      }
      print_immediate(modrm,16);
    }

    // Generate false LCP warning, Intel compiler coding rule 22
    if((*orig_op == 0xF7) && (((intptr_t)orig_op & 15) == 14)) print_string(" // Warning: false LCP stall");

    break;

  case 0xfe:                    // inc/dec?
    ONLY_REXWXB(prefix);
    op=rexr(prefix,*_op);
    switch( op ) {
    case 0:  print_string("inc1  "); break;
    case 1:  print_string("dec1  "); break;
    default: PrintUnimplemented();
    }
    _op = print_modrm(prefix,_op);
    reset_reg_tracking();
    break;

case 0xfc:
    NO_REX(prefix);
    print_string("cld   // clear direction flag (forwards)");
    break;

case 0xfd:
    NO_REX(prefix);
    print_string("std   // set direction flag (backwards)");
    break;

case 0xff:
    op=rexr(prefix,*_op);
    modrm = (prefix&8) ? '8' : '4';
    if( (prefix&0x80) == 0x80 ) modrm = '2';
    switch( op ) {
    case 0:    print_string("inc"); print_char(modrm); print_string("  "); break;
    case 1:    print_string("dec"); print_char(modrm); print_string("  "); break;
    case 2:    print_string("call  "); reset_reg_tracking();  break;
    case 4:    print_string("jmp   "); reset_reg_tracking();  break;
    case 6:    print_string("push  "); break;
    default:   PrintUnimplemented();
    }
    _op = print_modrm(prefix,_op);
    break;
  default:
    print_integer(_op[-1],16);  print_char(' ');
    print_integer(_op[ 0],16);  print_char(' ');
    print_integer(_op[ 1],16);  print_char(' ');
    print_string("// unknown instruction");
    break;
  }
}

char* Decoder::decode(Instr* pc) {
  _pos = 0;
  _pc = pc;
  _op = (u_char*)pc;
  if (!_assertcond) form();
  print_char('\0');
  return _buf;
}

const char *Disassembler::static_pc_to_name(address pc, address& low, address& hi, bool demangle) {
  low = hi = pc;
  // the interpreter is generated into a buffer blob
InterpreterCodelet*ic=Interpreter::codelet_containing(pc);
if(ic!=NULL){
    low = ic->code_begin();
    hi = ic->code_end();
    return ic->_description;
  }
  if (Interpreter::contains(pc)) return "somewhere_in_the_interpreter";
  // the stubroutines are generated into a buffer blob
StubCodeDesc*sc=StubCodeDesc::desc_for(pc);
if(sc!=NULL){
    char *stubname = NEW_RESOURCE_ARRAY(char, 100);
    sprintf(stubname,"%s::%s", sc->group(), sc->name());
    low = sc->begin();
    hi = sc->end();
    return stubname;
  }
  if (StubRoutines::contains(pc)) return "unnamed_stub_routine";
  // this lookup is unsafe - blob can be collected by gc at any moment
CodeBlob*blob=CodeCache::find_blob(pc);
if(blob!=NULL){
    low = (address)blob;
    hi = blob->end();
    Runtime1::StubID id = Runtime1::contains(pc);
    if( (int)id != -1 ) {
      low = Runtime1::entry_for(id);
      if( (int)id+1 < Runtime1::number_of_ids )
        hi = Runtime1::entry_for(Runtime1::StubID(id+1))-1;
      return Runtime1::name_for(id);
    }
    const char *methname = blob->methodname();
    return methname ? methname : blob->name();
  }
  char *mangled = NEW_RESOURCE_ARRAY(char, 256);
  int offset;
  size_t size;
  if (os::dll_address_to_function_name(pc, mangled, 256, &offset, &size)) {
    if( !demangle ) return mangled;
    low = pc - offset;
    hi = low + size;
    int status;
    char *demangled0 = __cxxabiv1::__cxa_demangle(mangled,NULL,NULL,&status);
    if( status !=0 ) return mangled;
char*demangled=NEW_RESOURCE_ARRAY(char,strlen(demangled0)+1);
    strcpy(demangled,demangled0);
free(demangled0);
    return demangled;
  }
  return NULL;
}

void Disassembler::print_static_pc_xml(xmlBuffer *xb, address pc) {
CodeBlob*blob=CodeCache::find_blob(pc);
if(blob!=NULL){
    blob->print_xml_on(xb, true, NULL);
    return;
  }
StubCodeDesc*sc=StubCodeDesc::desc_for(pc);
if(sc!=NULL){
    sc->print_xml_on(xb, true);
    return;
  }
  char *mangled = NEW_RESOURCE_ARRAY(char, 256);
  int offset;
  size_t size;
  if (os::dll_address_to_function_name(pc, mangled, 256, &offset, &size)) {
    xmlElement xe(xb, "pc_ref");
    xb->name_ptr_item("address", (void*) pc);
    xb->name_value_item("name", mangled);
#ifdef AZ_PROFILER
    //char *demangled = NEW_RESOURCE_ARRAY(char, 1024);
    //if (azprof::Demangler::demangle(mangled, demangled, 1024) == 0) {
    //  xb->name_value_item("pretty_name", demangled);
    //}
#endif // AZ_PROFILER
  }
}

const char *Disassembler::pc_to_name(address pc, int oplen) {
  char *pcname_buf = NEW_RESOURCE_ARRAY(char, 256);
char*s=pcname_buf;
  sprintf(s,"%8p", pc );
  s += 2+8;                   // '0x' + 8 chars
*s++=':';
*s++=' ';

  // Now up to 10 bytes of opcode printout
for(int i=0;i<10;i++){
    if( i<oplen ) sprintf(s,"%02x",pc[i]);
    else sprintf(s,"  ");
    s += 2;
  }
*s++=' ';
*s++='\0';
  return pcname_buf;
}


void Decoder::print_branch_target(Instr* pc) {
  char tmp[32];
  sprintf(tmp, "%9p", pc);
print_string(tmp);
}

void Decoder::print_jump_target(u_char *adr) {
  print_integer((intptr_t)adr,16); // target
address low,hi;
  const char *name = Disassembler::static_pc_to_name(adr,low,hi,true);
  if( name ) {
    print_string("  // ");
    if( _blob && !_blob->contains(adr) ) print_string(name);
    if( adr != low ) {
      print_char('+');
      print_immediate(adr-low,16);
    }
  }
}


void Disassembler::decode(Decoder* decoder, address begin, address end, outputStream* st) {
  if( st==NULL ) st = tty;
  Instr* p = (Instr*)begin;
  Instr* e = (Instr*)end;
  decoder->reset_reg_tracking();
  CodeBlob *cb = decoder->_blob = CodeCache::find_blob(p);
  methodCodeOop mcoop = cb ? cb->owner().as_methodCodeOop() : NULL;
  if( mcoop ) 
    decoder->_profile = mcoop->_profile; 
  bool is_code = true;

while(p<e){
    ResourceMark rm;
    // An oopmap here?
    if( cb ) cb->print_oopmap_at ((address)p,st);
    if( cb ) cb->print_debug_at((address)p,st);
    if( mcoop && mcoop->_generic_exception_handler_rel_pc == ((address)p-(address)cb) )
st->print_cr("                    // Generic exception handler");
    if( cb && is_code && (address)p >= cb->code_ends() - cb->_non_code_tail && cb->code_size() != 0) {
st->print_cr("                    // Start of data");
      is_code = false;
      decoder->_raw_data = end - (address)p;
    }

    // Crack the instruction into a string
    const char* decoded_instr = decoder->decode(p);

    // Now try and come up with something about where the PC is 
    const char *pc_name = pc_to_name((address)p, decoder->oplen());

st->print("%s  %s",pc_name,decoded_instr);
    if( cb ) cb->print_npe_at((address)p,st);
    st->cr();

    // catch an oplen of zero, so we don't fall into an infinite loop
    int oplen=decoder->oplen();
    if (oplen <=0) {break; }
    
    // Skip to next op.  
    p = p->pc(oplen);
  }
}

void Disassembler::decode(address begin, address end, outputStream* st) {
  Decoder decoder;
  decode(&decoder, begin, end, st);
}


void Disassembler::decode(const CodeBlob* cb, outputStream* st) {
st->print_cr("Decoding CodeBlob %p",cb);
  Decoder decoder;
  decode(&decoder, cb->code_begins(), cb->code_ends(), st);
}


void Disassembler::decode(methodCodeRef nm, outputStream* st) {
st->print_cr("Decoding methodCode %p",nm.as_oop());
st->print_cr("...currently unimplemented.\n");
  // Unimplemented();
//  Decoder decoder;
//  Instr* p = (Instr*)nm->instructions_begin();
//  Instr* e = (Instr*)nm->instructions_end();
//
//  while (p < e) {
//    // Crack the instruction into a string
//    const char* decoded_instr = decoder.decode(p);
//    // Now try and come up with something about where the PC is 
//    const char *pc_name = pc_to_name((address)p);
//
//    st->print("%s   %s", pc_name, decoded_instr);
//    if ((p->opcode() == SPECIAL) && (p->special_func() == BREAK) && (p->imm9() == 0xdf)) {
//      if ((p->pc(-10)->opcode() == MOVHI)
//          && (p->pc(-11)->opcode() == MOVMID)
//          && (p->pc(-12)->opcode() == MOVLO)) {
//        int64 str = (((int64)(p->pc(-10)->uimm21())) << 41)
//        | (((int64)(p->pc(-11)->uimm21())) << 20)
//        |  ((int64)(p->pc(-12)->imm21()));
//        st->print("  ; %s", (const char*)str);
//      }
//    }
//    Instr* p0 = p;
//    p = p->pc(1);
//    nm->print_code_comment_on(st, 40, (address)p0, (address)p);
//    st->cr();
//  }
}

void Disassembler::decode_xml(xmlBuffer *xb, Instr *begin, Instr *end) {
CodeBlob*self_blob=CodeCache::find_blob(begin);
  xb->set_non_blocking(false);
  { xmlElement c(xb, "code_block");
    Decoder dec;
    dec.reset_reg_tracking();
    dec._blob = self_blob;

    const int byte_count = end - begin;
    int *tick_counts = NEW_RESOURCE_ARRAY(int, byte_count);
    memset(tick_counts, 0, sizeof(int) * byte_count);

    // collect all the ticks belonging to this piece of code
    ProfileIterator it(xb);
    it.filt().set_range((address) begin, (address) end);
    ProfileEntry *pe;
    while ((pe = it.next()) != NULL) {
address cur;
      if (pe->is_perfcnt_tick()) {
        cur = ((UserProfileEntry*) pe)->pc();
      } else if (pe->is_tlb_tick()) {
        cur = ((TlbProfileEntry *) pe)->pc();
      } else {
        continue;
      }
      tick_counts[cur - (address)begin]++;
    }

    // TODO -- Need to fix this... but not here.
    //
    // Some of the code in the codecache seems to have issues. Some of them
    // are huge. Some of them have invalid end addresses. And many of them
    // have unimplemented and assert opcodes.
    //
    // Luckily, the ones that have invalid end addresses, also are huge,
    // and have asserts. So, as a temporary fix, we will cap any huge
    // code blocks, if they have asserts.
    //
address cur=(address)begin;
address stopptr=(address)end;
    address capptr = (address)begin + (16 * 1024);     // Cap at 16K
 
while(cur<stopptr){
      if (dec._assertcond) {
        if (stopptr > capptr) stopptr = capptr;
        dec._assertcond = false;
      }
      xmlElement cl(xb, "code_line");
      int tick_count = tick_counts[cur - (address)begin];
      if (tick_count != 0) {
        { xmlElement elem(xb, "code_percent", xmlElement::delayed_LF);
          xb->print("%.2f%%", (100.0 * tick_count) / it.filt().matched());
        }
        { xmlElement elem(xb, "code_ticks", xmlElement::delayed_LF);
          xb->print("%d", tick_count);
        }
      }
      { xmlElement elem(xb, "code_address", xmlElement::delayed_LF);
        xb->print("%p", cur);
      }
      { xmlElement elem(xb, "code_binary", xmlElement::delayed_LF);
        xb->print("0x%08llx", *(jlong*)cur); // misaligned load of 8 bytes
      }
      { 
        xmlElement elem(xb, "code_asm", xmlElement::delayed_LF);
        xb->print(dec.decode((Instr*)cur));
      }
      if( NativeCall::is_call_at(cur) || NativeJump::is_jump_at(cur) ) {
        address target = NativeCall::is_call_at(cur) ? ((NativeCall*)cur)->destination() : ((NativeJump*)cur)->jump_destination();
CodeBlob*target_blob=CodeCache::find_blob(target);
        if (target_blob && target_blob != self_blob ) {
          xmlElement elem(xb, "code_target", xmlElement::delayed_LF);
          target_blob->print_xml_on(xb,true);
        } else {
StubCodeDesc*sc=StubCodeDesc::desc_for(target);
if(sc!=NULL){
            xmlElement elem(xb, "code_target", xmlElement::delayed_LF);
            sc->print_xml_on(xb,true);
          } else {
            // make html links for C++ call targets
            ResourceMark rm;
            char *mangled = NEW_RESOURCE_ARRAY(char, 256);
            int offset;
            size_t size;
            if (os::dll_address_to_function_name(target, mangled, 256, &offset, &size)) {
              char *demangled = NEW_RESOURCE_ARRAY(char, 1024);
              xmlElement elem1(xb, "code_target", xmlElement::delayed_LF);
              xmlElement elem2(xb, "pc_ref", xmlElement::delayed_LF);
              xb->name_ptr_item("address", (void*) target);
              xb->name_value_item("name", mangled);
            }
          }
        } 
      }
      // catch an oplen of zero, so we don't fall into an infinite loop
      int oplen=dec.oplen();
      if (oplen <= 0) { break; }

      // ok, move on to decoding the next op
      cur += oplen;
    }
  }
}
