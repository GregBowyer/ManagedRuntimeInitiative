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
#ifndef DISASSEMBLER_PD_HPP
#define DISASSEMBLER_PD_HPP

#include "nativeInst_pd.hpp"
#include "ostream.hpp"

// Used by disassembler for unimplemented -- don't abort.
#define PrintUnimplemented() { print_string(" // TODO: "); print_string(__FILE__); print_string(":"); print_integer(__LINE__); }

// Used by disassembler for prefix assertions -- don't abort.
#define PrintAssert(cond, label) { if (!cond) { print_string(" // TODO:"); print_string(label); print_string(" "); print_string(__FILE__); print_string(":"); print_integer(__LINE__); print_string(" "); _assertcond = true; } }

#define NO_REX(prefix) { PrintAssert(no_rex_na(prefix),"no_rex"); }
#define ONLY_REX(prefix) { PrintAssert(only_rex_na(prefix), "only_rex"); }
#define ONLY_REXB(prefix) { PrintAssert(only_rexb_na(prefix), "only_rexb"); }
#define ONLY_REXW(prefix) { PrintAssert(only_rexw_na(prefix), "only_rexw"); }
#define ONLY_REXWB(prefix) { PrintAssert(only_rexwb_na(prefix), "only_rexwb"); }
#define ONLY_REXWR(prefix) { PrintAssert(only_rexwr_na(prefix), "only_rexwr"); }
#define ONLY_REXRB(prefix) { PrintAssert(only_rexrb_na(prefix), "only_rexrb"); }
#define ONLY_REXBX(prefix) { PrintAssert(only_rexbx_na(prefix), "only_rexbx"); }
#define ONLY_REXRXB(prefix) { PrintAssert(only_rexrxb_na(prefix), "only_rexrxb"); }
#define ONLY_REXWXB(prefix) { PrintAssert(only_rexwxb_na(prefix), "only_rexwxb"); }

class CodeBlob;
class CodeProfile;
 
class Decoder:public StackObj{
  friend class Disassembler;
 private:
  enum { buffer_length = 256 };
  char   _buf[buffer_length]; // output buffer
int _pos;//position in output buffer
  Instr* _pc; // current PC being decoded
  int    _raw_data;             // bytes of raw data following instruction
  u_char *_op;                  // moving opcode parse ptr
  int _regs[16];                // track 16 GPR contents a little
  enum {
    Runknown,                   // default case
    Rstack,                     // always set for RSP register
    Rframe,                     // set if some reg holds old RSP when RSP moves
    Rthread,                    // set when computed Thread from RSP
    Rstrip1,                    // 1st half of meta-data stripping
    Rmax
  };

  // top-level breakdown
  void form();

 protected:
  inline void print_char(char c)  { if (_pos < buffer_length) _buf[_pos++] = c; }
  void print_string(const char* str);
  void print_integer(int64 val, int base);
  inline void print_integer(int64 val) { print_integer(val,10); }

  void print_raw_data();
void print_register(Register reg);
  void print_Fregister(FRegister reg);
void print_dst(Register dst);//same as print_register, but crushes dst reg tracking
  void print_immediate(int imm, int base);
  void print_immediate(int imm); // any special syntax for immediates
  void print_offset( Register base, int imm); // any special syntax for mem+offset printing
  void print_branch_target(Instr* pc);
  void print_jump_target(address pc);
  address print_modrm( int prefix, address op );
  address print_modrm_dst( int prefix, address op );
  address print_modrm_flt( int prefix, address op );
  
 public:
  Decoder() : _raw_data(0), _blob(0), _profile(0), _assertcond(false) {}
  char*  decode(Instr* pc);     // decode 1 reg
  int oplen() const { return _op - (u_char*)_pc; }
  void reset_reg_tracking();    // reset the register tracking
  CodeBlob *_blob;              // optional blob
  CodeProfile *_profile;        // optional code profile

  bool _assertcond;		// assert (is this decoder broken?)
};

class Disassembler:public StackObj{
  public:
  static const char *static_pc_to_name(address pc, address& low, address& hi, bool demangle);
  static const char *pc_to_name(address pc, int oplen);
  static void print_static_pc_xml(xmlBuffer *xb, address pc);
  static void decode(Decoder* decoder, address begin, address end, outputStream* st = tty);
static void decode(address begin,address end,outputStream*st=tty);
  static void decode(const CodeBlob* cb, outputStream* st = tty);
  static void decode(methodCodeRef nm, outputStream* st = tty);

  static void decode_xml(xmlBuffer *xb, Instr *begin, Instr *end);
  static void decode_xml(xmlBuffer *xb, address begin, address end) {
    decode_xml(xb, (Instr*) begin, (Instr*) end);
  }
};

#endif // DISASSEMBLER_PD_HPP
