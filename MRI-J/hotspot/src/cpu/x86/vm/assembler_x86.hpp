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
#ifndef ASSEMBLER_PD_HPP
#define ASSEMBLER_PD_HPP

#include "allocation.hpp" 
#include "commonAsm.hpp" 
#include "codeBlob.hpp" 
#include "constants_pd.hpp" 
#include "growableArray.hpp" 
#include "nativeInst_pd.hpp"
#include "register_pd.hpp" 

// Pure low-level X86 assembler.
// Instructions match exactly to HW instructions.
class Assembler:public CommonAsm{
  int _error;                   // assembler errors
public:
  enum Prefix {
    // B = use high 8 registers for base
    // R = use high 8 registers for main operand
    // X = use high 8 registers for index
    // W = force 64-bit width
    REX        = 0x40,
    REX_B      = 0x41,
    REX_X      = 0x42,
    REX_XB     = 0x43,
    REX_R      = 0x44,
    REX_RB     = 0x45,
    REX_RX     = 0x46,
    REX_RXB    = 0x47,
    REX_W      = 0x48,
    REX_WB     = 0x49,
    REX_WX     = 0x4A,
    REX_WXB    = 0x4B,
    REX_WR     = 0x4C,
    REX_WRB    = 0x4D,
    REX_WRX    = 0x4E,
    REX_WRXB   = 0x4F
  };

  Assembler( Arena *arena, CodeBlob::BlobType type, int compile_id, const char *name ) : CommonAsm(arena, type,compile_id,name), _error(0) { }
  Assembler( CodeBlob *cb, address pc ) : CommonAsm(cb, pc), _error(0) { }
  ~Assembler() { guarantee(_error==0, "had some errors"); }

  // X86 Relocation Support

  // X86 supports 4-byte pc-relative offsets.  These appear after calls and
  // long jumps.  If the CodeBlob being used for this assembly has to move
  // (common when it grows too much), then all these offsets are broken.  They
  // fall into 4 camps: relative offsets vs absolute offsets, and in-blob
  // targets and out-of-blob targets.
  //  
  // \ TARGET |      In-Blob         |    Out-of-Blob
  //  \-------+----------------------+----------------------------
  // RELATIVE | Ex:  jmp label       | Ex:  jmp forward_exception
  // OFFSET   | Do nothing           | Adjust displacement
  // ---------+----------------------+----------------------------
  // ABSOLUTE | Ex:  st8i(THR,,#pc); | Ex:  
  // OFFSET   |      in call_VM      |
  //          | Adjust displacement  | Do nothing
  //
  GrowableArray<intptr_t> _relative_relos; // relative pc to the 4-byte displacement
  void emit_relative_offset(address x) {
    // force any grow/move/extend now, lest 'offset' be wrong because
    // the blob moved during the 'emit4'.
    if( _pc+4 > _blob->end() ) grow(4); 
    intptr_t offset = x - (_pc+4);
    assert0(is_int32(offset));
    _relative_relos.push(rel_pc()); // these last 4 bytes need patching if we move
    emit4((int32_t)offset);
  }
  virtual void grow_impl( int sz );     // Grow blob by given size


  // Accessors

  // Emit a lock prefix.  Usage: __ locked()->cas8(...);
  Assembler *locked() { emit1(0xF0); return this; }

  // Emit a repeat prefix.  Usage: __ repeated()->movstr(....);
  Assembler *repeated() { emit1(0xF3); return this; }

  // Emit a repeat prefix.  Usage: __ repne()->scas8(....);
  Assembler *repne() { emit1(0xF2); return this; }
  Assembler *repeq() { emit1(0xF3); return this; }

  // Intel branch prediction overrides. Without a prefix, forward branches are
  // predicted not taken, backward branches are predicted taken.
  // Emit a branch likely prefix.    Usage: __ likely()->jcc(...);
  Assembler *likely()   { emit1(0x3E); return this; }
  // Emit a branch unlikely prefix.  Usage: __ unlikely()->jcc(...);
  Assembler *unlikely() { emit1(0x2E); return this; }

  // Emit a prefix if R>=8, and correct R.
  Register emit_regprefix( Register r ) {
    assert0( r != noreg );
    if( r < 8 ) return r;
emit1(REX_B);
    return (Register)(r-8);
  }

private:
  // Emit a prefix either dst or src are >= 8, and correct as needed.
  // No other prefixes; the opcode specifies the 1-byte size
  int emit_regprefix1( Register &dst, Register &src, Register &idx ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    if( pre ) emit1(pre);
    return pre;
  }
  
  // Emit a prefix either dst or src are >= 8, and correct as needed.
  // Always emit a size prefix to force 2-byte size
  void emit_regprefix2( Register &dst ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    emit1(0x66);
    if( pre ) emit1(pre);
  }
  void emit_regprefix2( Register &dst, Register &src ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    emit1(0x66);
    if( pre ) emit1(pre);
  }
  int emit_regprefix2( Register &dst, Register &src, Register &idx ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    emit1(0x66);
    if( pre ) emit1(pre);
    return pre;
  }
  
  // Emit a prefix either dst or src are >= 8, and correct as needed.
  // No other prefixes; the opcode specifies the 4-byte size
  void emit_regprefix4( Register &dst ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( pre ) emit1(pre);
  }
  void emit_regprefix4( Register &dst, Register &src ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( pre ) emit1(pre);
  }
  int emit_regprefix4( Register &dst, Register &src, Register &idx ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    if( pre ) emit1(pre);
    return pre;
  }
  int emit_regprefix4( Register &dst, FRegister &src ) {
    int pre = 0;
    src = (FRegister)freg2reg(src);
    if( dst >= 8 ) { dst = ( Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (FRegister)(src-8); pre |= REX_R; }
    if( pre ) emit1(pre);
    return pre;
  }
  int emit_regprefix4( Register &dst, FRegister &src, Register &idx ) {
    int pre = 0;
    src = (FRegister)freg2reg(src);
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src =(FRegister)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    if( pre ) emit1(pre);
    return pre;
  }
  void emit_regprefix4( FRegister &dst, Register &src ) {
    int pre = 0;
    dst = (FRegister)freg2reg(dst);
    if( dst >= 8 ) { dst = (FRegister)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = ( Register)(src-8); pre |= REX_R; }
    if( pre ) emit1(pre);
  }
  void emit_regprefix4( FRegister &dst, FRegister &src ) {
    int pre = 0;
    src = (FRegister)freg2reg(src);
    dst = (FRegister)freg2reg(dst);
    if( dst >= 8 ) { dst = (FRegister)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (FRegister)(src-8); pre |= REX_R; }
    if( pre ) emit1(pre);
  }
  void emit_regprefix4( FRegister &src ) {
    int pre = 0;
    src = (FRegister)freg2reg(src);
    if( src >= 8 ) { src = (FRegister)(src-8); pre |= REX_R; }
    if( pre ) emit1(pre);
  }
 
  
  // Emit a prefix either dst or src are >= 8, and correct as needed.
  // Always emit a 'W' prefix to force the 8-byte size.
  void emit_regprefix8( Register &dst ) {
    int pre = REX_W;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    emit1(pre);
  }
  void emit_regprefix8( Register &dst, Register &src ) {
    int pre = REX_W;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    emit1(pre);
  }
  
  // Emit a prefix either dst or src are >= 8, and correct as needed.
  // Always emit a 'W' prefix to force the 8-byte size.
  int emit_regprefix8( Register &dst, Register &src, Register &idx ) {
    int pre = REX_W;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    emit1(pre);
    return pre;
  }
  void emit_regprefix8( Register &dst, FRegister &src ) {
    int pre = REX_W;
    src = (FRegister)freg2reg(src);
    if( dst >= 8 ) { dst = ( Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (FRegister)(src-8); pre |= REX_R; }
    emit1(pre);
  }
  int emit_regprefix8( Register &dst, FRegister &src, Register &idx ) {
    int pre = REX_W;
    src = (FRegister)freg2reg(src);
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src =(FRegister)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    if( pre ) emit1(pre);
    return pre;
  }
  void emit_regprefix8( FRegister &dst, Register &src ) {
    int pre = REX_W;
    dst = (FRegister)freg2reg(dst);
    if( dst >= 8 ) { dst = (FRegister)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = ( Register)(src-8); pre |= REX_R; }
    emit1(pre);
  }
  void emit_regprefix8( FRegister &src ) {
    int pre = REX_W;
    src = (FRegister)freg2reg(src);
    if( src >= 8 ) { src = (FRegister)(src-8); pre |= REX_R; }
    emit1(pre);
  }

  // For ABCD registers emit no prefix, for RBP, RSP, RSI, RDI emit an empty
  // REX prefix (to address 8byte parts of registers), for R08 to R15 emit
  // a REX prefix with REX_B set
  void emit_regprefix_bytereg( Register &dst ) {
    if (!is_abcdx(dst)) {
      int pre = REX;
      if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
      emit1(pre);
    }
  }
  void emit_regprefix_bytereg( Register &dst, Register &src ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( !is_abcdx(dst) ) { pre |= REX; }
    if( !is_abcdx(src) ) { pre |= REX; }
    if( pre ) emit1(pre);
  }
  int emit_regprefix_bytereg( Register &dst, Register &src, Register &idx ) {
    int pre = 0;
    if( dst >= 8 ) { dst = (Register)(dst-8); pre |= REX_B; }
    if( src >= 8 ) { src = (Register)(src-8); pre |= REX_R; }
    if( idx >= 8 ) { idx = (Register)(idx-8); pre |= REX_X; }
    if( !is_abcdx(dst) ) { pre |= REX; }
    if( !is_abcdx(src) ) { pre |= REX; }
    if( pre ) emit1(pre);
    return pre;
  }

  // Emit modrm byte to encode 2 register operands
  void emit_regreg( Register reg, Register rm) {
    // Validate the regprefix methods have stripped the high order bits
    assert0 ( reg >= 0 && reg <= 7 );
    assert0 ( rm  >= 0 && rm  <= 7 );
    emit1(0xC0 | reg<<3 | rm);
  }

  // Emit modrm byte to encode 2 floating point register operands
  void emit_regreg( FRegister reg, FRegister rm) {
    // Validate the regprefix methods have stripped the high order bits
    assert0 ( reg >= 0 && reg <= 7 );
    assert0 ( rm  >= 0 && rm  <= 7 );
    emit1(0xC0 | reg<<3 | rm);
  }

  // Emit modrm byte to encode 1 floating point and 1 regular register operands
  void emit_regreg( FRegister reg, Register rm) {
    // Validate the regprefix methods have stripped the high order bits
    assert0 ( reg >= 0 && reg <= 7 );
    assert0 ( rm  >= 0 && rm  <= 7 );
    emit1(0xC0 | reg<<3 | rm);
  }

  // Emit modrm byte to encode 1 floating point and 1 regular register operands
  void emit_regreg( Register reg, FRegister rm) {
    // Validate the regprefix methods have stripped the high order bits
    assert0 ( reg >= 0 && reg <= 7 );
    assert0 ( rm  >= 0 && rm  <= 7 );
    emit1(0xC0 | reg<<3 | rm);
  }

  // Emit modrm byte to encode an opcode and a register operands
  void emit_opreg( int op, Register rm) {
    assert0 ( op >= 0 && op <= 7 );
    // Validate the regprefix methods have stripped the high order bits
    assert0 ( rm >= 0 && rm <= 7 );
    emit1(0xC0 | op<<3 | rm);
  }

  // Emit 'reg' and base+offset addressing mode.
  void emit_regbaseoff( Register reg, Register base, intptr_t off );
  // Emit op and base+offset addressing mode.
  void emit_opbaseoff( int op, Register base, intptr_t off );
  // Emit op and base+offset+index*scale addressing mode.
  void emit_opbaseoffindexscale( int op, Register base, intptr_t off, Register index, int scale, int prefix );
  // Emit 'reg' and base+offset+scale*index addressing mode.
  void emit_fancy( Register reg, Register base, intptr_t off, Register index, int scale, int prefix );

  // One-instruction scaled immediate adds.  The immediate can be symbolic
  // and will be scaled to allow for larger immediates than possible with
  // the plain 'add' versions.
  static int scale_by( int imm, int scale ) {
    assert( (imm & ((1<<scale)-1)) == 0, "not properly aligned" );
    return imm>>scale;		// Verify no bits are lost before scaling
  }

public:
  // push & pop - always of 8 bytes to keep the stack aligned (hence fast).
  void popf()            { emit1(0x9D); } // pop flags
  void pop (Register r)  { r=emit_regprefix(r); emit1(0x58|r); }
  void pushf()           { emit1(0x9C); } // push flags
  void push (Register r) { r=emit_regprefix(r); emit1(0x50|r); }
  void pushi(long imm32);
  void push (Register base, int off) { emit_regprefix4(base); emit1(0xFF); emit_regbaseoff((Register)6,base,off);}
  void pop  (Register base, int off) { emit_regprefix4(base); emit1(0x8F); emit_regbaseoff((Register)0,base,off);}

  void lahf()            { emit1(0x9F); }
  void sahf()            { emit1(0x9E); } // cry?

  // Common int ops
  void int_reg4i( Register dst,           intptr_t imm32, int op, int opax );
  void int_reg8i( Register dst,           intptr_t imm32, int op, int opax );
  void int_mem4i( Register base, int off, intptr_t imm32, int op );
  void int_mem4i( Register base, int off, Register index, int scale, intptr_t imm32, int op );
  void int_mem8i( Register base, int off, intptr_t imm32, int op );
  void int_mem8i( Register base, int off, Register index, int scale, intptr_t imm32, int op );
  void add4i(Register dst ,          long imm32) { if( imm32==1 ) inc4(dst);      else int_reg4i(dst,     imm32,0, 0x05); }
  void add8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,0, 0x05); }
  void add4i(Register base, int off, long imm32) { if( imm32==1 ) inc4(base,off); else int_mem4i(base,off,imm32,0); }
  void add8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,0);       }
void or_1i(Register dst,int imm8);
  void or_4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,1, 0x0D); }
  void or_8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,1, 0x0D); }
  void or_4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,1);       }
  void or_8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,1);       }
  void adc4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,2, 0x15); }
  void adc8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,2, 0x15); }
  void adc4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,2);       }
  void adc8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,2);       }
  void sbb4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,3, 0x1D); }
  void sbb8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,3, 0x1D); }
  void sbb4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,3);       }
  void sbb8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,3);       }
  void and4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,4, 0x25); }
  void and8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,4, 0x25); }
  void and4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,4);       }
  void and8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,4);       }
  void sub4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,5, 0x2D); }
  void sub8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,5, 0x2D); }
  void sub4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,5);       }
  void sub8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,5);       }
  void cmp4i(Register dst ,          long imm32) { int_reg4i(dst,     imm32,7, 0x3D); }
  void cmp8i(Register dst ,          long imm32) { int_reg8i(dst,     imm32,7, 0x3D); }
  void cmp4i(Register base, int off, long imm32) { int_mem4i(base,off,imm32,7);       }
  void cmp8i(Register base, int off, long imm32) { int_mem8i(base,off,imm32,7);       }
  void xor4i(Register dst ,          long imm32);
  void xor8i(Register dst ,          long imm32);
  void xor4i(Register base, int off, long imm32);
  void xor8i(Register base, int off, long imm32);
  void xor4i(Register base, int off, Register index, int scale, long imm32);
  void xor8i(Register base, int off, Register index, int scale, long imm32);
  void mul4i(Register dst, Register src, long imm32);
  void mul8i(Register dst, Register src, long imm32);

  void add4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x03); emit_regreg(dst, src); }
  void add8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x03); emit_regreg(dst, src); }
  void or_4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x0B); emit_regreg(dst, src); }
  void or_8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x0B); emit_regreg(dst, src); }
  void adc4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x13); emit_regreg(dst, src); }
  void adc8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x13); emit_regreg(dst, src); }
  void sbb4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x1B); emit_regreg(dst, src); }
  void sbb8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x1B); emit_regreg(dst, src); }
  void and4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x23); emit_regreg(dst, src); }
  void and8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x23); emit_regreg(dst, src); }
  void sub4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x2B); emit_regreg(dst, src); }
  void sub8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x2B); emit_regreg(dst, src); }
  void xor4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x33); emit_regreg(dst, src); }
  void xor8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x33); emit_regreg(dst, src); }
  void cmp1 (Register dst , Register src )      { emit_regprefix_bytereg(src, dst); emit1(0x3A); emit_regreg(dst, src); }
  void cmp4 (Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x3B); emit_regreg(dst, src); }
  void cmp8 (Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x3B); emit_regreg(dst, src); }
  void test1(Register dst , Register src )      { emit_regprefix_bytereg(src, dst); emit1(0x84); emit_regreg(dst, src); }
  void test2(Register dst , Register src )      { emit_regprefix2(src, dst); emit1(0x85); emit_regreg(dst, src); }
  void test4(Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x85); emit_regreg(dst, src); }
  void test8(Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x85); emit_regreg(dst, src); }
  void xchg4(Register dst , Register src )      { emit_regprefix4(src, dst); emit1(0x87); emit_regreg(dst, src); }
  void xchg8(Register dst , Register src )      { emit_regprefix8(src, dst); emit1(0x87); emit_regreg(dst, src); }
  void not2 (Register src )                     { emit_regprefix2(src);      emit1(0xF7); emit_opreg (2,   src); }
  void not4 (Register src )                     { emit_regprefix4(src);      emit1(0xF7); emit_opreg (2,   src); }
  void not4 (Register base, int off )           { emit_regprefix4(base); emit1(0xF7); emit_opbaseoff (2, base, off); }
  void not4 (Register base, intptr_t off, Register index, int scale);
  void not8 (Register src )                     { emit_regprefix8(src);      emit1(0xF7); emit_opreg (2,   src); }
  void not8 (Register base, int off )           { emit_regprefix8(base); emit1(0xF7); emit_opbaseoff (2, base, off); }
  void not8 (Register base, intptr_t off, Register index, int scale);
  void neg4 (Register src )                     { emit_regprefix4(src);      emit1(0xF7); emit_opreg (3,   src); }
  void neg4 (Register base, intptr_t off, Register index, int scale);
  void neg8 (Register src )                     { emit_regprefix8(src);      emit1(0xF7); emit_opreg (3,   src); }
  void neg8 (Register base, intptr_t off, Register index, int scale);
  void mul4 (Register src )                     { emit_regprefix4(src);      emit1(0xF7); emit_opreg (5,   src); }
  void mul8 (Register src )                     { emit_regprefix8(src);      emit1(0xF7); emit_opreg (5,   src); }
  void neg4 (Register base, int off )           { emit_regprefix4(base); emit1(0xF7); emit_opbaseoff (3, base, off); }
  void neg8 (Register base, int off )           { emit_regprefix8(base); emit1(0xF7); emit_opbaseoff (3, base, off); }
  void mul4 (Register dst, Register src)        { emit_regprefix4(src, dst); emit1(0x0F); emit1(0xAF); emit_regreg(dst, src); }
  void mul4 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void mul4i(Register dst,  Register base, intptr_t off, Register index, int scale, int imm32);
  void mul8i(Register dst,  Register base, intptr_t off, Register index, int scale, int imm32);
  void mul8 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void mul8 (Register dst, Register src)        { emit_regprefix8(src, dst); emit1(0x0F); emit1(0xAF); emit_regreg(dst, src); }
  void div4 (Register src )                     { emit_regprefix4(src);      emit1(0xF7); emit_opreg (7,   src); }
  void div8 (Register src )                     { emit_regprefix8(src);      emit1(0xF7); emit_opreg (7,   src); }
  void cdq4 ( )                                 {                            emit1(0x99); }
  void cdq8 ( )                                 { emit1(REX_W);              emit1(0x99); }
  void testi(Register dst , long imm32 );
  // These next 2 are not valid ops for x86-64 (they are in the same place in
  // the opcode map as the REX prefix). The inc/dec to modrm versions work fine.
  //void dec8 (Register src )                     { emit_regprefix8(src);      emit1(0x48+src); }
  //void inc8 (Register src )                     { emit_regprefix8(src);      emit1(0x40+src); }
  void inc1 (Register base,int off) {base=emit_regprefix (base); emit1(0xFE); emit_regbaseoff((Register)0x0,base,off);}
  void inc2 (Register src )         {     emit_regprefix2(src ); emit1(0xFF); emit_opreg(0, src); }
  void inc4 (Register src )         {     emit_regprefix4(src ); emit1(0xFF); emit_opreg(0, src); }
  void inc4 (Register base,int off) {     emit_regprefix4(base); emit1(0xFF); emit_regbaseoff((Register)0x0,base,off);}
  void inc4 (Register base, int off, Register index, int scale);
  void inc8 (Register src )         {     emit_regprefix8(src ); emit1(0xFF); emit_opreg(0, src); }
  void dec1 (Register base,int off) {base=emit_regprefix (base); emit1(0xFE); emit_regbaseoff((Register)0x1,base,off);}
  void dec4 (Register src )         {     emit_regprefix4(src ); emit1(0xFF); emit_opreg(1, src); }
  void dec8 (Register src )         {     emit_regprefix8(src ); emit1(0xFF); emit_opreg(1, src); }

  void sub2 (Register base,int off,Register src) {emit_regprefix2(base,src); emit1(0x29); emit_regbaseoff(src,base,off);}

  void add4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x03); emit_regbaseoff(dst,base,off);}
  void add8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x03); emit_regbaseoff(dst,base,off);}
  void or_4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x0B); emit_regbaseoff(dst,base,off);}
  void or_8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x0B); emit_regbaseoff(dst,base,off);}
  void adc8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x13); emit_regbaseoff(dst,base,off);}
  void sbb8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x1B); emit_regbaseoff(dst,base,off);}
  void and4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x23); emit_regbaseoff(dst,base,off);}
  void and8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x23); emit_regbaseoff(dst,base,off);}
  void sub2 (Register dst,Register base,int off) {emit_regprefix2(base,dst); emit1(0x2B); emit_regbaseoff(dst,base,off);}
  void sub4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x2B); emit_regbaseoff(dst,base,off);}
  void sub8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x2B); emit_regbaseoff(dst,base,off);}
  void xor4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x33); emit_regbaseoff(dst,base,off);}
  void xor8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x33); emit_regbaseoff(dst,base,off);}
  void cmp1 (Register dst,Register base,int off) {emit_regprefix_bytereg(base,dst); emit1(0x3A); emit_regbaseoff(dst,base,off);}
  void cmp4 (Register dst,Register base,int off) {emit_regprefix4(base,dst); emit1(0x3B); emit_regbaseoff(dst,base,off);}
  void cmp8 (Register dst,Register base,int off) {emit_regprefix8(base,dst); emit1(0x3B); emit_regbaseoff(dst,base,off);}

  // Simple instructions
  void nop(int i = 1);
  void addr_nop_4();
  void addr_nop_5();
  void addr_nop_7();
  void addr_nop_8();
  void mov8i(Register r, const char *ptr);
  void mov8i( Register dst, Label &thepc );
  void mov8i(Register dst,  int64_t  imm64, bool can_destroy_flags); // a real 64-bit value stuft in a register
  void mov8u(Register dst,  uint32_t zimm,  bool can_destroy_flags); // a 32-bit unsigned value in a 64-bit register
  void mov8i(Register dst,  int32_t  simm,  bool can_destroy_flags); // a 32-bit signed value in a 64-bit register
  void mov8i(Register dst,  int64_t  imm64 ) { mov8i(dst, imm64, true); }
  void mov8u(Register dst,  uint32_t zimm )  { mov8u(dst, zimm,  true); }
  void mov8i(Register dst,  int32_t  simm )  { mov8i(dst, simm,  true); }
  void mov1i(Register dst,  int int8 ) { emit_regprefix_bytereg(dst); emit1(0xB0+dst); emit1(int8); }
  void mov8 (Register dst,  Register src)           { emit_regprefix8(dst ,src); emit1(0x89  ); emit_regreg(src, dst); }
  void mov4 (Register dst,  Register src)           { emit_regprefix4(dst ,src); emit1(0x89  ); emit_regreg(src, dst); }
  void st1  (Register base, int off, Register src ) { emit_regprefix_bytereg(base,src); emit1(0x88  ); emit_regbaseoff(src,base,off); }
  void st1  (Register base, int off, Register index, int scale, Register src);
  void st1i (Register base, int off, int imm );
  void st1i (Register base, address off, int imm );
  void st1i (Register base, int off, Register index, int scale, int imm );
  void st2  (Register base, int off, Register src ) { emit_regprefix2(base,src); emit1(0x89  ); emit_regbaseoff(src,base,off); }
  void st2  (Register base, int off, Register index, int scale, Register src);
  void st2i (Register base, int off, int imm );
  void st2i (Register base, int off, Register index, int scale, int imm );
  void st4  (Register base, int off, Register src ) { emit_regprefix4(base,src); emit1(0x89  ); emit_regbaseoff(src,base,off); }
  void st4  (Register base, int off, Register index, int scale, Register src);
  void st4i (Register base, int off, long imm32 );
  void st4i (Register base, int off, Register index, int scale, long imm32 );
  void st8  (Register base, int off, Register src ) { emit_regprefix8(base,src); emit1(0x89  ); emit_regbaseoff(src,base,off); }
  void st8  (Register base, int off, Register index, int scale, Register src);
  void st8  (address low_adr, Register src);
  void st8i (Register base, int off, long imm32 );
  void st8i (Register base, int off, Label &thepc );
  void st8i (Register base, int off, Register index, int scale, long imm32 );
  void ldz1 (Register dst,  Register base, int off) { emit_regprefix4(base,dst); emit2(0xB60F); emit_regbaseoff(dst,base,off); }
  void ldz1 (Register dst,  Register base, int off, Register index, int scale);
  void ldz2 (Register dst,  Register base, int off) { emit_regprefix4(base,dst); emit2(0xB70F); emit_regbaseoff(dst,base,off); }
  void ldz2 (Register dst,  Register base, int off, Register index, int scale);
  void ldz4 (Register dst,  Register base, int off) { emit_regprefix4(base,dst); emit1(0x8B  ); emit_regbaseoff(dst,base,off); }
  void ldz4 (Register dst,  Register base, int off, Register index, int scale);
  void ldz4 (Register dst,  address adr)            { emit_regprefix4(dst); emit1(0x8B); emit1(0x04|dst<<3); emit1(0x25); assert0(is_int32((intptr_t)adr)); emit4((intptr_t)adr); }
  void lds1 (Register dst,  Register base, int off) { emit_regprefix8(base,dst); emit2(0xBE0F); emit_regbaseoff(dst,base,off); }
  void lds1 (Register dst,  Register base, int off, Register index, int scale);
  void lds2 (Register dst,  Register base, int off) { emit_regprefix8(base,dst); emit2(0xBF0F); emit_regbaseoff(dst,base,off); }
  void lds2 (Register dst,  Register base, int off, Register index, int scale);
  void lds4 (Register dst,  Register base, int off) { emit_regprefix8(base,dst); emit1(0x63  ); emit_regbaseoff(dst,base,off); }
  void lds4 (Register dst,  Register base, int off, Register index, int scale);

  void ld4  (Register dst,  Register base, int off) { emit_regprefix4(base,dst); emit1(0x8B  ); emit_regbaseoff(dst,base,off); }
  void ld4  (Register dst,  Register base, int off, Register index, int scale);

  void ld8  (Register dst,  Register base, int off) { emit_regprefix8(base,dst); emit1(0x8B  ); emit_regbaseoff(dst,base,off); }
  void ld8  (Register dst,  Register base, int off, Register index, int scale);
  void ld8  (Register dst,  address low_adr);

  void lea4 (Register dst,  Register base, int off) { if( off ) { emit_regprefix4(base,dst); emit1(0x8D  ); emit_regbaseoff(dst,base,off);} else { mov4(dst,base); } }
  void lea4 (Register dst,  Register base, int off, Register index, int scale);
  void lea  (Register dst,  Register base, int off) { if( off ) { emit_regprefix8(base,dst); emit1(0x8D  ); emit_regbaseoff(dst,base,off);} else { mov8(dst,base); } }
  void lea  (Register dst,  Register base, int off, Register index, int scale);
  void add4 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void add4 (Register base, intptr_t off, Register index, int scale, Register src );
  void or_4 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void or_4 (Register base, intptr_t off, Register index, int scale, Register src );
  void xor4 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void xor4 (Register base, intptr_t off, Register index, int scale, Register src );
  void and4 (Register base, intptr_t off, Register index, int scale, Register src );
  void add2i(Register base, intptr_t off, int imm16);
  void add8 (Register base, intptr_t off, Register src);
  void add8 (Register base, intptr_t off, Register index, int scale, Register src);
  void add8 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void sub8 (Register base, intptr_t off, Register index, int scale, Register src);
  void or_8 (Register base, intptr_t off, Register index, int scale, Register src );
  void or_8 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void or_4i(Register base, intptr_t off, Register index, int scale, int imm32);
  void or_8i(Register base, intptr_t off, Register index, int scale, int imm32);
  void xor8 (Register dst, Register base, intptr_t off, Register index, int scale);
  void xor8 (Register base, intptr_t off, Register index, int scale, Register src );
  void and8 (Register base, intptr_t off, Register index, int scale, Register src );
  void add4i(address ptr ,  long imm32);
  void add4i(Register base, intptr_t off, Register index, int scale, int imm32);
  void add8i(Register base, intptr_t off, Register index, int scale, int imm32);
  void and4 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void and8 (Register dst,  Register base, intptr_t off, Register index, int scale);
  void and8i(Register base, intptr_t off, Register index, int scale, int imm32);
  void and4i(Register base, intptr_t off, Register index, int scale, int imm32);
  void inc4 (address ptr) { emit1(0xFF); emit_regbaseoff(Register(0x0),noreg,(intptr_t)ptr); }
  void inc8 (address ptr) { emit1(REX_W); inc4(ptr); }
  void inc8 (Register base, int off, Register index, int scale);
void and1i(Register dst,int imm8);
void test1i(Register dst,int imm8);
  void test1i(Register base, int off, Register index, int scale, int imm8);
  void test4i(Register dst,  long imm32);
  void test4i(Register base, int off, Register index, int scale, long imm32);
void cmp1i(Register dst,int imm8);
  void cmp4 (Register dst, address ptr);
  void cmp4 (Register dst, Register base, int off, Register index, int scale);
  void cmp4i(address ptr ,  long imm32);
  void cmp4i(Register base, int off, Register index, int scale, long imm32);
  void cmp8i(Register base, int off, Register index, int scale, long imm32);
  void cmp1i(Register base, address off, int imm );
  void cmp1i(Register base, int off, Register index, int scale, int imm8);
  void cmp8 (Register dst, address ptr) { emit_regprefix8(dst); cmp4(dst,ptr); }
  void cmp8 (Register dst, Register base, int off, Register index, int scale);
  void sub4 (Register dst, Register base, int off, Register index, int scale);
  void sub4 (Register base, intptr_t off, Register index, int scale, Register src );
  void sub4i(Register base, intptr_t off, Register index, int scale, int imm32);
  void sub8 (Register dst, Register base, int off, Register index, int scale);
  void test4(Register dst, Register base, int off, Register index, int scale);
  void test8(Register dst, Register base, int off, Register index, int scale);
  void xchg (Register src, Register base, int off) { emit_regprefix8(base,src); emit1(0x87); emit_regbaseoff(src,base,off); }
  void xchg (Register src, Register base, int off, Register index, int scale);

  void cas4 (Register base, int off, Register src ) { emit_regprefix4(base,src); emit2(0xB10F); emit_regbaseoff(src,base,off); }
  void cas4 (Register base, int off, Register index, int scale, Register src);
  void cas8 (Register base, int off, Register src ) { emit_regprefix8(base,src); emit2(0xB10F); emit_regbaseoff(src,base,off); }
  void cas8 (Register base, int off, Register index, int scale, Register src);

  // Sign/zero-extend to 8 bytes
  void movsx81(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0xBE0F); emit_regreg(dst, src); }
  void movsx82(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0xBF0F); emit_regreg(dst, src); }
void movsx84(Register dst,Register src);
  void movzx81(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0xB60F); emit_regreg(dst, src); }
  void movzx82(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0xB70F); emit_regreg(dst, src); }
  void movzx41(Register dst, Register src) { emit_regprefix_bytereg(src,dst); emit2(0xB60F); emit_regreg(dst, src); }
  void movzx42(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0xB70F); emit_regreg(dst, src); }
  void movsx41(Register dst, Register src) { emit_regprefix_bytereg(src,dst); emit2(0xBE0F); emit_regreg(dst, src); }
void movsx42(Register dst,Register src);
void bswap2(Register reg);
  void bswap4 (Register reg ) { emit_regprefix4(reg); emit1(0x0F); emit1(0xC8 | reg); }
  void bswap8 (Register reg ) { emit_regprefix8(reg); emit1(0x0F); emit1(0xC8 | reg); }

  // Prefetches
  void prefetchnta (Register base, int off ) { emit_regprefix4(base); emit2(0x180F); emit_opbaseoff(0, base, off); }
  void prefetchnta (Register base, int off, Register index, int scale ) { Register none = Register(0); int prefix = emit_regprefix4(base,none,index); emit2(0x180F); emit_opbaseoffindexscale(0, base, off, index, scale, prefix); }
  void prefetch0   (Register base, int off ) { emit_regprefix4(base); emit2(0x180F); emit_opbaseoff(1, base, off); }
  void prefetch0   (Register base, int off, Register index, int scale ) { Register none = Register(1); int prefix = emit_regprefix4(base,none,index); emit2(0x180F); emit_opbaseoffindexscale(1, base, off, index, scale, prefix); }
  void prefetch1   (Register base, int off ) { emit_regprefix4(base); emit2(0x180F); emit_opbaseoff(2, base, off); }
  void prefetch1   (Register base, int off, Register index, int scale ) { Register none = Register(2); int prefix = emit_regprefix4(base,none,index); emit2(0x180F); emit_opbaseoffindexscale(2, base, off, index, scale, prefix); }
  void prefetch2   (Register base, int off ) { emit_regprefix4(base); emit2(0x180F); emit_opbaseoff(3, base, off); }
  void prefetch2   (Register base, int off, Register index, int scale ) { Register none = Register(3); int prefix = emit_regprefix4(base,none,index); emit2(0x180F); emit_opbaseoffindexscale(3, base, off, index, scale, prefix); }

  // Shifts
  void shift4(Register &dst, int imm5) { assert0( is_uint5(imm5) );  emit_regprefix4(dst); }
  void shift4(Register &base, Register &index, int imm5) { assert0( is_uint5(imm5) );  emit_regprefix4(base, index); }
  void shift8(Register &dst, int imm6) { assert0( is_uint6(imm6) );  emit_regprefix8(dst); }
  void shift8(Register &base, Register &index, int imm5) { assert0( is_uint6(imm5) );  emit_regprefix8(base, index); }
  void shf( Register dst, int imm56, int op );
  void shf( Register base, intptr_t off, Register index, int scale, int imm56, int op );
  void rol4i(Register dst, int imm5 ) { shift4(dst,imm5); shf(dst,imm5,0); }
  void rol4i(Register base, intptr_t off, Register index, int scale, int imm5 ) { shift4(base,index,imm5); shf(base,off,index,scale,imm5,0); }
  void rol8i(Register dst, int imm6 ) { shift8(dst,imm6); shf(dst,imm6,0); }
  void rol8i(Register base, intptr_t off, Register index, int scale, int imm6 ) { shift8(base,index,imm6); shf(base,off,index,scale,imm6,0); }
  void rol4 (Register dst, Register RCX );
  void rol4 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void rol8 (Register dst, Register RCX );
  void rol8 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void ror4i(Register dst, int imm5 ) { shift4(dst,imm5); shf(dst,imm5,1); }
  void ror4i(Register base, intptr_t off, Register index, int scale, int imm5 ) { shift4(base,index,imm5); shf(base,off,index,scale,imm5,1); }
  void ror8i(Register dst, int imm6 ) { shift8(dst,imm6); shf(dst,imm6,1); }
  void ror8i(Register base, intptr_t off, Register index, int scale, int imm6 ) { shift8(base,index,imm6); shf(base,off,index,scale,imm6,1); }
  void ror4 (Register dst, Register RCX );
  void ror4 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void ror8 (Register dst, Register RCX );
  void ror8 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void shl4i(Register dst, int imm5 ) { shift4(dst,imm5); shf(dst,imm5,4); }
  void shl4i(Register base, intptr_t off, Register index, int scale, int imm5 ) { shift4(base,index,imm5); shf(base,off,index,scale,imm5,4); }
  void shl8i(Register dst, int imm6 ) { shift8(dst,imm6); shf(dst,imm6,4); }
  void shl8i(Register base, intptr_t off, Register index, int scale, int imm6 ) { shift8(base,index,imm6); shf(base,off,index,scale,imm6,4); }
  void shl4 (Register dst, Register RCX );
  void shl4 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void shl8 (Register dst, Register RCX );
  void shl8 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void shr4i(Register dst, int imm5 ) { shift4(dst,imm5); shf(dst,imm5,5); }
  void shr4i(Register base, intptr_t off, Register index, int scale, int imm5 ) { shift4(base,index,imm5); shf(base,off,index,scale,imm5,5); }
  void shr8i(Register dst, int imm6 ) { shift8(dst,imm6); shf(dst,imm6,5); }
  void shr8i(Register base, intptr_t off, Register index, int scale, int imm6 ) { shift8(base,index,imm6); shf(base,off,index,scale,imm6,5); }
  void shr4 (Register dst, Register RCX );
  void shr4 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void shr8 (Register dst, Register RCX );
  void shr8 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void sar4i(Register dst, int imm5 ) { shift4(dst,imm5); shf(dst,imm5,7); }
  void sar4i(Register base, intptr_t off, Register index, int scale, int imm5 ) { shift4(base,index,imm5); shf(base,off,index,scale,imm5,7); }
  void sar8i(Register dst, int imm6 ) { shift8(dst,imm6); shf(dst,imm6,7); }
  void sar8i(Register base, intptr_t off, Register index, int scale, int imm6 ) { shift8(base,index,imm6); shf(base,off,index,scale,imm6,7); }
  void sar4 (Register dst, Register RCX );
  void sar4 (Register base, intptr_t off, Register index, int scale, Register RCX );
  void sar8 (Register dst, Register RCX );
  void sar8 (Register base, intptr_t off, Register index, int scale, Register RCX );

  // Bit tests.  Sets CF from the bit, then does: btx- nothing, btc- flip bit,
  // bts- set bit, btr- clear bit.  Not atomic.
  void btx4i(Register src, int imm5 ) { shift4(src,imm5); emit2( 0xBA0F ); emit_opreg(4, src); emit1( imm5 ); }
  void btx8i(Register src, int imm6 ) { shift8(src,imm6); emit2( 0xBA0F ); emit_opreg(4, src); emit1( imm6 ); }
  void btx4 (Register src, Register bit ); // bit test
  void btx8 (Register src, Register bit ); // bit test
  void btc4i(Register src, int imm5 ) { shift4(src,imm5); emit2( 0xBA0F ); emit_opreg(7, src); emit1( imm5 ); }
  void btc8i(Register src, int imm6 ) { shift8(src,imm6); emit2( 0xBA0F ); emit_opreg(7, src); emit1( imm6 ); }
  void btc4 (Register src, Register bit ); // bit test and complement
  void btc8 (Register src, Register bit ); // bit test and complement
  void bts4i(Register src, int imm5 ) { shift4(src,imm5); emit2( 0xBA0F ); emit_opreg(5, src); emit1( imm5 ); }
  void bts8i(Register src, int imm6 ) { shift8(src,imm6); emit2( 0xBA0F ); emit_opreg(5, src); emit1( imm6 ); }
  void bts4 (Register src, Register bit ); // bit test and set
  void bts8 (Register src, Register bit ); // bit test and set
  void btr4i(Register src, int imm5 ) { shift4(src,imm5); emit2( 0xBA0F ); emit_opreg(6, src); emit1( imm5 ); }
  void btr8i(Register src, int imm6 ) { shift8(src,imm6); emit2( 0xBA0F ); emit_opreg(6, src); emit1( imm6 ); }
  void btr4 (Register src, Register bit ); // bit test and reset
  void btr8 (Register src, Register bit ); // bit test and reset

  void btx4i(Register base, int off, int imm5 );
  void bts4i(Register base, int off, int imm5 );
  void btx8 (Register base, int off, Register bit ); // bit test bitmap in memory
  void bts8 (Register base, int off, Register bit ); // bit test and set bitmap in memory
  void btx4i(address, int imm5 );                    // bit test in memory

  // Conditional move
  void cmov4bl(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x420F); emit_regreg(dst, src);}
  void cmov4ae(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x430F); emit_regreg(dst, src);}
  void cmov4eq(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x440F); emit_regreg(dst, src);}
  void cmov4ne(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x450F); emit_regreg(dst, src);}
  void cmov4be(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x460F); emit_regreg(dst, src);}
  void cmov4ab(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x470F); emit_regreg(dst, src);}
  void cmov4lt(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x4C0F); emit_regreg(dst, src);}
  void cmov4ge(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x4D0F); emit_regreg(dst, src);}
  void cmov4le(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x4E0F); emit_regreg(dst, src);}
  void cmov4gt(Register dst, Register src) { emit_regprefix4(src,dst); emit2(0x4F0F); emit_regreg(dst, src);}
  void cmov8bl(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x420F); emit_regreg(dst, src);}
  void cmov8ae(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x430F); emit_regreg(dst, src);}
  void cmov8eq(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x440F); emit_regreg(dst, src);}
  void cmov8ne(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x450F); emit_regreg(dst, src);}
  void cmov8be(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x460F); emit_regreg(dst, src);}
  void cmov8ab(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x470F); emit_regreg(dst, src);}
  void cmov8lt(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x4C0F); emit_regreg(dst, src);}
  void cmov8ge(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x4D0F); emit_regreg(dst, src);}
  void cmov8le(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x4E0F); emit_regreg(dst, src);}
  void cmov8gt(Register dst, Register src) { emit_regprefix8(src,dst); emit2(0x4F0F); emit_regreg(dst, src);}

  void cmov4bl(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x420F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4ae(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x430F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4eq(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x440F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4ne(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x450F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4be(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x460F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4ab(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x470F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4lt(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x4C0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4ge(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x4D0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4le(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x4E0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov4gt(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix4(base,dst,index); emit2(0x4F0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8bl(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x420F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8ae(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x430F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8eq(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x440F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8ne(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x450F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8be(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x460F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8ab(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x470F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8lt(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x4C0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8ge(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x4D0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8le(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x4E0F); emit_fancy(dst, base, off, index, scale, prefix);}
  void cmov8gt(Register dst, Register base, intptr_t off, Register index, int scale) { int prefix = emit_regprefix8(base,dst,index); emit2(0x4F0F); emit_fancy(dst, base, off, index, scale, prefix);}

  // Misc instructions
  void os_breakpoint()      { emit1(0xCC); } // Must be size 1; used to align-pad
  void call ( address x) {
assert0(x);
    emit1(0xE8);
emit_relative_offset(x);
  }
  void call ( Register r){ emit_regprefix4(r); emit1(0xFF); emit1(0xD0 | r); }
  // generate a call operation that will have its target address aligned to 4bytes
  void aligned_patchable_call(address target) {
    _bra_pcs.push(rel_pc());
    _bra_idx.push(CALL);        // Record an alignment request
call(target);
  }
  // record that inline cache alignment is required at this PC
  void align_inline_cache() {
    _bra_pcs.push(rel_pc());
    _bra_idx.push(IC);        // Record an alignment request
  }
  void cld  ()           { emit1(0xFC); } // clear direction flag (forwards  copy)
  void std  ()           { emit1(0xFD); } // set   direction flag (backwards copy)
  void enter( int off  ) { emit1(0xC8); emit2(off); emit1(0); } // push RBP; move RBP,RSP; sub RSP,#off
  void leave()           { emit1(0xC9); } // move RSP,RBP; pop RBP
  void ret  ()           { emit1(0xC3); } // return (pop ret adr and jump to it)
  void lods1()           {               emit1(0xAC); } //  al=*rsi++
  void lods2()           { emit1(0x66);  emit1(0xAD); } //  ax=*rsi++;
  void lods4()           {               emit1(0xAD); } // eax=*rsi++;
  void lods8()           { emit1(REX_W); emit1(0xAD); } // rax=*rsi++;
  void movs1()           {             ; emit1(0xA4); } // *rdi++=*rsi++; 
  void movs2()           { emit1(0x66);  emit1(0xA5); } // *rdi++=*rsi++; 
  void movs4()           {               emit1(0xA5); } // *rdi++=*rsi++; 
  void movs8()           { emit1(REX_W); emit1(0xA5); } // *rdi++=*rsi++; 
  void cmps8()           { emit1(REX_W); emit1(0xA7); } // *rsi++ == *rdi++?
  void stos8()           { emit1(REX_W); emit1(0xAB); } // *rdi++=rax; 
  void scas1()           {               emit1(0xAE); } // *rsi++ == al?
  void scas8()           { emit1(REX_W); emit1(0xAF); } // *rsi++ == rax?
  void mfence()          { emit1(0x0F); emit1(0xAE); emit1(0xF0); }
  void lfence()          { emit1(0x0F); emit1(0xAE); emit1(0xE8); } // Loads  cannot be ordered with loads
  void sfence()          { emit1(0x0F); emit1(0xAE); emit1(0xF8); } // Stores cannot be ordered with stores

  void seto ( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x900F); emit_opreg(0, dst); }
  void setno( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x910F); emit_opreg(0, dst); }
  void setbl( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x920F); emit_opreg(0, dst); }
  void setae( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x930F); emit_opreg(0, dst); }
  void setz ( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x940F); emit_opreg(0, dst); }
  void setnz( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x950F); emit_opreg(0, dst); }
  void setbe( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x960F); emit_opreg(0, dst); }
  void seta ( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x970F); emit_opreg(0, dst); }
  void sets ( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x980F); emit_opreg(0, dst); }
  void setns( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x990F); emit_opreg(0, dst); }
  void setpe( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9A0F); emit_opreg(0, dst); }
  void setpo( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9B0F); emit_opreg(0, dst); }
  void setlt( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9C0F); emit_opreg(0, dst); }
  void setge( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9D0F); emit_opreg(0, dst); }
  void setle( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9E0F); emit_opreg(0, dst); }
  void setgt( Register dst ) { emit_regprefix_bytereg(dst); emit2(0x9F0F); emit_opreg(0, dst); }

  // Patch all branches.  May grow code if short branches are out of range.
  virtual void patch_branches(); // patch all the current branches
  void patch_branches_impl();    // machine-specific non-virtual implementation
  virtual void reset_branches(); // setup for the next round of branches
  virtual bool has_variant_branches() const;
private:
  void bump_rel_pcs(GrowableArray<intptr_t> *rel_pcs, int *apc, bool adjust_bra, bool skip_alignment);

public:

  void bind ( Label& L ) { L.bind(this,rel_pc()); }
  void bind ( Label& L, address );

private:
  void xjmp ( int opx, Label& L ); // variant-jump helper
public:
  void jov  ( Label& L ) { xjmp(0x70,L); } // jump if OF==1
  void jno  ( Label& L ) { xjmp(0x71,L); } // jump if OF==0
  void jnae ( Label& L ) { xjmp(0x72,L); } // jump if CF==1
  void jbl  ( Label& L ) { xjmp(0x72,L); } // jump if CF==1
  void jae  ( Label& L ) { xjmp(0x73,L); } // jump if CF==0
  void jnb  ( Label& L ) { xjmp(0x73,L); } // jump if CF==0
  void jze  ( Label& L ) { xjmp(0x74,L); } // jump if ZF==1
  void jeq  ( Label& L ) { xjmp(0x74,L); } // jump if ZF==1
  void jnz  ( Label& L ) { xjmp(0x75,L); } // jump if ZF==0
  void jne  ( Label& L ) { xjmp(0x75,L); } // jump if ZF==0
  void jna  ( Label& L ) { xjmp(0x76,L); } // jump if CF==1  or  ZF==1
  void jbe  ( Label& L ) { xjmp(0x76,L); } // jump if CF==1  or  ZF==1
  void jnbe ( Label& L ) { xjmp(0x77,L); } // jump if CF==0  and ZF==0
  void jab  ( Label& L ) { xjmp(0x77,L); } // jump if CF==0  and ZF==0
  void jsg  ( Label& L ) { xjmp(0x78,L); } // jump if SF==1
  void jns  ( Label& L ) { xjmp(0x79,L); } // jump if SF==0
  void jpe  ( Label& L ) { xjmp(0x7A,L); } // jump if PF==1
  void jpo  ( Label& L ) { xjmp(0x7B,L); } // jump if PF==0
  void jlt  ( Label& L ) { xjmp(0x7C,L); } // jump if SF!=OF
  void jge  ( Label& L ) { xjmp(0x7D,L); } // jump if SF==OF
  void jle  ( Label& L ) { xjmp(0x7E,L); } // jump if SF!=OF or  ZF==1
  void jgt  ( Label& L ) { xjmp(0x7F,L); } // jump if SF==OF and ZF==0
  void jmp  ( Label& L ) { xjmp(0xEB,L); }

  // These forms always only allow a 1-byte displacement and have no 4-byte
  // displacement form (jrcxz is close to having a 4-byte form but requires
  // blowing flags: "test rcx,rcx; jeq").  Hence they have extra debugging
  // support.
  void yjmp ( int opx, Label& L, const char *f, int l ); // variant-jump helper
#define jrcxz1(L)   qrcxz  (L,__FILE__,__LINE__)
#define jloop1(L)   qloop  (L,__FILE__,__LINE__)
#define jloopne1(L) qloopne(L,__FILE__,__LINE__)
#define jloope1(L)  qloopeq(L,__FILE__,__LINE__)
  void qrcxz  ( Label& L, const char *f, int l ) { yjmp(0xE3,L,f,l); } // short form for RCX==0
  void qloop  ( Label& L, const char *f, int l ) { yjmp(0xE2,L,f,l); } // dec RCX, jump if ECX!=0
  void qloopne( Label& L, const char *f, int l ) { yjmp(0xE0,L,f,l); } // dec RCX, jump if ECX!=0
  void qloopeq( Label& L, const char *f, int l ) { yjmp(0xE1,L,f,l); } // dec RCX, jump if ECX!=0

  // Jumping to out-of-blob targets.  Always a 4-byte displacement.
  void jmp  ( address t );
void jmp2(Label&L);//forced 5-byte jump opcode
  void jae  ( address t );
  void jeq  ( address t );
  void jne  ( address t );
void jne2(Label&L);//forced 6-byte jump opcode

  void jmp8(Register direct_adr   ) { emit_regprefix4(direct_adr); emit1(0xFF); emit_opreg(4, direct_adr); }
  void jmp8(Register base, int off) { emit_regprefix4(base      ); emit1(0xFF); emit_regbaseoff(Register(0x4),base,off); }
  void jmp8(Register base, int off, Register index, int scale);
  void jmp8(Register base, Label& L, Register index, int scale);

  // Interpreter indirect branches are mostly not predictable.  The
  // default prediction is the next instruction following the 'jmp8'.
  // The "undefined opcode" stops the X86 from speculating ahead and
  // then needing to flush the pipeline.
  void ud2() { emit2(0x0B0F); }

  // FP register ops
  void mov4(FRegister dst, FRegister src);
  void mov8(FRegister dst, FRegister src);
void mov4(FRegister dst,Register src);
void mov4(Register dst,FRegister src);
void mov8(FRegister dst,Register src);
void mov8(Register dst,FRegister src);
  void ld4 (FRegister dst,  Register base, int off);
  void ld4 (FRegister dst,  Register base, int off, Register index, int scale );
  void ld4 (FRegister dst,  address low_adr);
  void ld8 (FRegister dst,  Register base, int off);
  void ld8 (FRegister dst,  Register base, int off, Register index, int scale );
  void ld8 (FRegister dst,  address low_adr);

  void st4 ( Register base, int off, FRegister src);
  void st4  (Register base, int off, Register index, int scale, FRegister src);
  void st8 ( Register base, int off, FRegister src);
  void st8  (Register base, int off, Register index, int scale, FRegister src);
  void cmp4(FRegister dst,  FRegister src);
  void cmp4(FRegister dst, address ptr);
  void cmp4(FRegister src,  Register base, int off);
  void cmp4(FRegister src,  Register base, int off, Register index, int scale);
  void cmp8(FRegister src,  Register base, int off);
  void cmp8(FRegister dst,  FRegister src);
  void cmp8(FRegister dst, address ptr);
  void cmp8(FRegister src,  Register base, int off, Register index, int scale);

  void cvt_d2f(FRegister dst, FRegister src);
  void cvt_d2f(FRegister dst,  Register base, int off);
  void cvt_d2f(FRegister dst,  Register base, int off, Register index, int scale);
void cvt_d2i(Register dst,FRegister src);
void cvt_d2l(Register dst,FRegister src);
  void cvt_f2d(FRegister dst, FRegister src);
  void cvt_f2d(FRegister dst,  Register base, int off);
  void cvt_f2d(FRegister dst,  Register base, int off, Register index, int scale);
void cvt_f2i(Register dst,FRegister src);
void cvt_f2l(Register dst,FRegister src);
void cvt_i2d(FRegister dst,Register src);
  void cvt_i2d(FRegister src,  Register base, int off, Register index, int scale);
void cvt_i2f(FRegister dst,Register src);
  void cvt_i2f(FRegister src,  Register base, int off, Register index, int scale);
void cvt_l2d(FRegister dst,Register src);
  void cvt_l2d(FRegister src,  Register base, int off, Register index, int scale);
void cvt_l2f(FRegister dst,Register src);
  void cvt_l2f(FRegister src,  Register base, int off, Register index, int scale);
  void addf(FRegister dst,  FRegister src);
  void addf(FRegister dst,  Register base, int off);
  void addf(FRegister dst,  Register base, int off, Register index, int scale);
  void subf(FRegister dst,  FRegister src);
  void subf(FRegister dst,  Register base, int off);
  void subf(FRegister dst,  Register base, int off, Register index, int scale);
  void mulf(FRegister dst,  FRegister src);
  void mulf(FRegister dst,  Register base, int off);
  void mulf(FRegister dst,  Register base, int off, Register index, int scale);
  void mulf(FRegister dst, address ptr);  
  void divf(FRegister dst,  FRegister src);
  void divf(FRegister dst,  Register base, int off);
  void divf(FRegister dst,  Register base, int off, Register index, int scale);
  void addd(FRegister dst,  FRegister src);
  void addd(FRegister dst,  Register base, int off);
  void addd(FRegister dst,  Register base, int off, Register index, int scale);
  void subd(FRegister dst, FRegister src);
  void subd(FRegister dst,  Register base, int off);
  void subd(FRegister dst,  Register base, int off, Register index, int scale);
  void muld(FRegister dst,  FRegister src);
  void muld(FRegister dst,  Register base, int off);
  void muld(FRegister dst,  Register base, int off, Register index, int scale);
  void divd(FRegister dst, FRegister src);
  void divd(FRegister dst,  Register base, int off);
  void divd(FRegister dst,  Register base, int off, Register index, int scale);
  void sqrtd(FRegister dst, FRegister src);
  void sqrtd(FRegister dst,  Register base, int off);
  void sqrtd(FRegister dst,  Register base, int off, Register index, int scale);
  void sqrts(FRegister dst, FRegister src);
  void sqrts(FRegister dst,  Register base, int off);
  void sqrts(FRegister dst,  Register base, int off, Register index, int scale);

  // Fast zeroing support ops
  void xorf(FRegister dst, FRegister src); // zero FP register
  void xord(FRegister dst, FRegister src); // zero FP register

  // FP mask (neg/abs) support ops
  void xorf(FRegister dst, address ptr);
  void xord(FRegister dst, address ptr);
  void andf(FRegister dst, address ptr);
  void andd(FRegister dst, address ptr);

  // Compare Packed Data for Equal
  void pcmpeqb(FRegister dst, FRegister src);
void pmovmskb(Register dst,FRegister src);

  // SSE ops for special intrinsics and stubs
  void st16( Register base, int off, FRegister src ); // aligned 16b store
  void ld16u( FRegister dst, Register base, int off, Register index, int scale); // unaligned 16b load
  void xor16(FRegister dst, FRegister src);
  void xor16(FRegister dst, Register base, int off, Register index, int scale);
  void test16(FRegister dst, FRegister src);

  void rdtsc() { emit2(0x310F); }

  virtual void align(intx);
void align_with_nops(int modulus);

  NativeMovConstReg* nativeMovConstReg_before(int rel_pc);
  NativeMovRegMem* nativeMovRegMem_before (int rel_pc);

  // Useful x87 operations for the macro assemblers
  void x87_ld4  (Register base, int off); // push value onto x87 stack
  void x87_ld8  (Register base, int off); // push value onto x87 stack
  void x87_st4p (Register base, int off); // pop value from x87 stack
  void x87_st8p (Register base, int off); // pop value from x87 stack
  void x87_prem();                        // partial remainder
  void x87_stsw (Register base, int off); // store status word
  void x87_freep();                       // undocumented free stack and pop operation
  void x87_fldln2();                      // push log_e(2) onto x87 stack
  void x87_fldlg2();                      // push log_10(2) onto x87 stack
  void x87_fyl2x();                       // st(1):= st(1)*log_2(st(0)); pop st(0)

  void cpuid();
};


struct RKeepIns { static RKeepIns a; }; // Registers read & preserved by a macro
struct RInOuts  { static RInOuts  a; }; // Registers killed by a macro

// ---
class MacroAssembler: public Assembler {
  static void stop_helper( const char *msg );
public:
  MacroAssembler(Arena *arena, CodeBlob::BlobType type, int compile_id, const char *name) : Assembler(arena, type, compile_id, name) {}
  MacroAssembler(CodeBlob *cb, address pc) : Assembler(cb,pc) {}

  void always_poison(Register dst)           { not8(dst); }
  void always_unpoison(Register dst)         { not8(dst); }
  void poison(Register dst)                  { Label done; null_chk(dst, done); not8(dst); bind(done); }
  void unpoison(Register dst)                { Label done; null_chk(dst, done); not8(dst); bind(done); }

  void move4( Register dst, Register src )   { if( dst != src ) mov4(dst,src); }
  void move8( Register dst, Register src )   { if( dst != src ) mov8(dst,src); }
  void move4( FRegister dst, FRegister src ) { if( dst != src ) mov4(dst,src); }
  void move8( FRegister dst, FRegister src ) { if( dst != src ) mov8(dst,src); }
  void destroy (Register dst)                { bswap4(dst); } // well known operation to kill a register
  void pushall();               // all 15 regs, not SP, not flags, not floats
  void popall();                // all 15 regs, not SP, not flags, not floats
  void getthr( Register thr );  // Get Thread into register; fairly fast
  void gettid( Register thr );  // Get Thread-ID into register; fairly fast
  void cvta2va( Register ref ); // Convert ref to a plain ptr, stripping off meta-data
  void cvta2va( Register dst, Register ref ) { move8 (dst, ref); cvta2va(dst); }
  void ref2kid( Register ref ); // Strip out the KID field and set zero flag, may cause NPE if !KIDInRef
  void ref2kid( Register dst, Register ref );
  void ref2kid_no_npe( Register dst, Register ref ); // Get KID bits and hide NPEs, in event of NPE dst holds null
  // short jump if register or memory is null
  void null_chk     ( Register base, int off, Label &L ) { cmp8i(base,off,0); jeq(L); }
  void null_chk     ( Register tst          , Label &L ) { test8(tst,tst)   ; jeq(L); }
  // This version just takes a hardware fault if reg is NULL
  void null_check(Register reg) { cmp4i(reg,0,0); }

  static bool needs_explicit_null_check(int offset);

  // Read Barriers
  int       lvb( RInOuts, Register dst, Register tmp, RKeepIns, Register base, int offset, bool can_be_null );
  int       lvb( RInOuts, Register dst, Register tmp, RKeepIns, Register base, int offset, Register index, int scale, bool can_be_null );

  void find_oop_in_oop_array(RKeepIns, Register Roop_to_find, RInOuts, Register Roop_array,
                             Register RCX_count, Register Rtmp, Register Rtmp2, Label& not_found);

  // Load & Barrier all together.
  int ldref_lvb( RInOuts, Register dst, Register tmp, RKeepIns, Register base, int offset, bool can_be_null )
               { ld8(dst,base,offset); return lvb(RInOuts::a, dst, tmp, RKeepIns::a,base,offset, can_be_null); }
  int ldref_lvb( RInOuts, Register dst, Register tmp, RKeepIns, Register base, int offset, Register index, int scale, bool can_be_null)
               { ld8(dst,base,offset,index,scale); return lvb(RInOuts::a, dst, tmp, RKeepIns::a,base,offset,index,scale, can_be_null); }

  typedef enum {
      LVB_Framesize = 112,
      LVB_Ret_Addr  = 104,   // saved automagically by 'call'
      LVB_Saved_RBP =  96,   // canonical RBP save location
      LVB_Result    =  88,   // location of fixed up value after trap handler returns
      LVB_Value     =  80,   // value loaded, known not-NULL and not-poisoned
      LVB_TAVA      =  72,   // address loaded from
      LVB_Saved_RAX =  64,   // saved stuff
      LVB_Saved_RCX =  56,   // saved stuff
      LVB_Saved_RDX =  48,   // saved stuff
      LVB_Saved_RSI =  40,   // saved stuff
      LVB_Saved_RDI =  32,   // saved stuff
      LVB_Saved_R08 =  24,   // saved stuff
      LVB_Saved_R09 =  16,   // saved stuff
      LVB_Saved_R10 =   8,   // saved stuff
      LVB_Saved_R11 =   0    // saved stuff
  } LVB_FrameLayout;

public:
  // Write barrier (ref store check) to be performed prior to write/CAS
  void pre_write_barrier(RInOuts, Register Rtmp0, Register Rtmp1,
                         RKeepIns, Register Rbase, int off,  Register Rindex, int scale, Register Rval,
Label&safe_to_store);

  // Write barrier (ref store check) to be performed prior to write/CAS.
  // Not for SBA.  The store-check can trigger generational card-marking.
  // Not a Safepoint.  No GC.
  void pre_write_barrier_HEAP(RInOuts, Register Rtmp0, Register Rtmp1,
                              RKeepIns, Register Rdst, Register Rsrc );

  // VM calls

  // Arrange for the stack to be crawled.  Otherwise no special interpreter or
  // runtime support.
  void call_VM_plain (address entry, Register arg1, Register arg2, Register arg3, Register arg4, Register arg5 );
  void call_VM_plain (address entry, Register arg1, Register arg2, Register arg3, Register arg4) { call_VM_plain(entry, arg1, arg2, arg3, arg4, noreg); }
  // Really a plain call instruction; no GC allowed; all standard C++ registers blown.
  void call_VM_leaf  (address entry, Register arg0, Register arg1, Register arg2, Register arg3, Register arg4, Register arg5 );
  void call_VM_leaf  (address entry, Register arg0, Register arg1) { call_VM_leaf(entry, arg0,  arg1, noreg, noreg, noreg, noreg); }
  void call_VM_leaf  (address entry, Register arg0)                { call_VM_leaf(entry, arg0, noreg, noreg, noreg, noreg, noreg); }
  // Arranges for the stack to be crawled but requires aligned stacks.
  // No registers saved, nor arguments setup.
  int  call_native(address, Register THR, int thr_offset, Label *nativefuncaddrpc);
  // Call into the VM from compiled code.  Preserves ALL registers (except RAX
  // & F00), and allows for GC and deopt on values in registers.  Assumes
  // aligned stack.  Will pass RDI as Thread; other args should be placed
  // in RSI, RDX, etc as normal for C++ calls.  The 'save_mask' lists the
  // registers to be saved.
  int call_VM_compiled(address adr, uint32_t save_mask, Register arg1, Register arg2) {
    return call_VM_compiled(adr, save_mask, arg1, arg2, noreg, noreg, 0); }
  int call_VM_compiled(address adr, uint32_t save_mask, Register arg1, Register arg2, Register arg3, Register arg4) {
    return call_VM_compiled(adr, save_mask, arg1, arg2, arg3, arg4, 0); }
  int call_VM_compiled(address, uint32_t save_mask, Register arg1, Register arg2, Register arg3, Register arg4, int stackArgs);

  // Fast-path lock attempt.  Does not mod 'ref'.  Blows RAX and the
  // temps.  May fall into the slow-path or branch to the fast_locked.
  // Sets RAX to the loaded markWord on the slow-path.
  // Sets R09 to the shifted thread-id on the slow-path.
  int fast_path_lock( RInOuts, Register tmp0, Register Ro9, Register Rax, RKeepIns, Register Rref, Label &fast_locked );
  // Fast-path unlock attempt.  Blows RAX.  Strips 'ref' to 'oop'.
  // May fall into the fast-path or branch to the slow_unlock.
  void fast_path_unlock( RInOuts, Register Rref, Register Rmark, Register Rtmp, Label &slow_unlock );

  // This routines should emit JVMTI PopFrame handling and ForceEarlyReturn code.
  // The implementation is only non-empty for the InterpreterMacroAssembler,
  // as only the interpreter handles PopFrame and ForceEarlyReturn requests.
  virtual void check_and_handle_popframe();

  // Recovering klassRef from kid or object from oid
  void oop_from_OopTable(RInOuts, Register dst, Register tmp, RKeepIns, Register idx);
  void oop_from_OopTable(RInOuts, Register dst, Register tmp, int idx);

  // Loads objectRef's klassRef by looking up the objectRef KID in the KlassTable.
  void kid2klass(RInOuts, Register klass_ref, Register tmp, RKeepIns, Register kid);
  void kid2klass(RInOuts, Register klass_ref, Register tmp, int kid);
  void ref2klass(RInOuts, Register klass_ref, Register tmp, Register ref); // can fault if !KIDInRef
  void ref2klass_no_npe(RInOuts, Register klass_ref, Register tmp, Register ref); // mask faults
  void ref2klass(RInOuts, Register klass_ref, Register tmp1, Register tmp2, RKeepIns, Register ref) {
    mov8 (tmp2, ref);
    ref2klass (RInOuts::a, klass_ref, tmp1, tmp2);
  }
  // Compare 2 classes setting the flags
  void compare_klasses(RInOuts, Register scratch, RKeepIns, Register ref1, Register ref2);

  // Make an inline-cache-failed branch target.
  // Cleaned up when 'baking' into a CodeBlob.
  GrowableArray<Label*> _ic_slow;  // slow-path failed-cache target
  GrowableArray<Label*> _ic_debug; // debug info location for the IC; just after the call
  void make_ic_slowpath(Label *slow);
  void pre_bake();              // fill in these branch targets at the end.

  // debugging code gen
  void stop( const char *msg );
  void untested(const char* msg) { char* buf = new char[512]; sprintf(buf, "untested: %s", msg); stop(buf); }
  void unimplemented(const char* msg)        { char* buf = new char[512]; sprintf(buf, "unimplemented: %s"        , msg); stop(buf); }
  void should_not_reach_here(const char* msg){ char* buf = new char[512]; sprintf(buf, "should_not_reach_here: %s", msg); stop(buf); }

  // Oop verification code
  enum OopVerificationReason {
    OopVerify_IncomingArgument,
    OopVerify_OutgoingArgument,
    OopVerify_ReturnValue,
    OopVerify_Store,
    OopVerify_StoreBase,
    OopVerify_Load,
    OopVerify_LoadBase,
    OopVerify_ConstantPoolLoad,
    OopVerify_Move,
    OopVerify_Spill,
    OopVerify_Fill,
    OopVerify_NewOop,
    OopVerify_OopTableLoad,
    OopVerify_OverWrittenField,
    OopVerify_Sanity,
    OopVerify_VtableLoad,
    OopVerify_OopMap_Exception,
    OopVerify_OopMap_PreCall,
    OopVerify_OopMap_PostCall,
    OopVerify_OopMap_PreNew,
    OopVerify_OopMap_PostNew,
    OopVerify_OopMap_PreSafepoint,
    OopVerify_OopMap_PostSafepoint
  };
  bool should_verify_oop(OopVerificationReason reason) {
    switch( reason ){
    case OopVerify_IncomingArgument: return VerifyOopLevel > 1;
    case OopVerify_OutgoingArgument: return VerifyOopLevel > 2;
    case OopVerify_ReturnValue     : return VerifyOopLevel > 1;
    case OopVerify_Store           : return VerifyOopLevel > 0;
    case OopVerify_StoreBase       : return VerifyOopLevel > 3;
    case OopVerify_Load            : return VerifyOopLevel > 1;
    case OopVerify_LoadBase        : return VerifyOopLevel > 3;
    case OopVerify_ConstantPoolLoad: return VerifyOopLevel > 2;
    case OopVerify_Move            : return VerifyOopLevel > 3;
    case OopVerify_Spill           : return VerifyOopLevel > 3;
    case OopVerify_Fill            : return VerifyOopLevel > 3;
    case OopVerify_NewOop          : return VerifyOopLevel > 1;
    case OopVerify_OopTableLoad    : return VerifyOopLevel > 2;
    case OopVerify_OverWrittenField: return VerifyOopLevel > 3;
    case OopVerify_Sanity          : return VerifyOopLevel > 4;
    case OopVerify_VtableLoad      : return VerifyOopLevel > 2;
    case OopVerify_OopMap_Exception: return VerifyOopLevel > 2;
    case OopVerify_OopMap_PreCall  : return VerifyOopLevel > 3;
    case OopVerify_OopMap_PostCall : return VerifyOopLevel > 4;
    case OopVerify_OopMap_PreNew   : return VerifyOopLevel > 3;
    case OopVerify_OopMap_PostNew  : return VerifyOopLevel > 4;
    case OopVerify_OopMap_PreSafepoint : return VerifyOopLevel > 3;
    case OopVerify_OopMap_PostSafepoint: return VerifyOopLevel > 4;
    default: ShouldNotReachHere(); return false;
    }
  }
  void verify_not_null_oop(Register reg, OopVerificationReason reason);
  void verify_oop(Register reg, OopVerificationReason reason);
  void verify_oop(VOopReg::VR reg, OopVerificationReason reason);
  int  verify_oop(Register Rbase, int off, Register Rindex, int scale, OopVerificationReason reason);
void verify_ref_klass(Register obj);
  void verify_oopmap(const OopMap2 *map, OopVerificationReason reason);

  void verify_kid(Register kid);

  // idiv that handles special min_int/-1 case
int corrected_idiv4(Register reg);
int corrected_idiv8(Register reg);

  // Load float constant into register
  void float_constant(FRegister dest, jfloat constant, Register scratch);
  // Give an address of a float constant or NULL if none exist
  static address float_constant(jfloat constant);
  // Load double constant into register
  void double_constant(FRegister dest, jdouble constant, Register scratch);
  // Give an address of a double constant or NULL if none exist
  static address double_constant(jdouble constant);
  // Give an address of an aligned quadword constant or NULL if none exist
  static address quadword_constant(jlong lo, jlong hi);
  // Give an address of a long constant
  address long_constant(jlong constant);
  // blob-local address holding the double constant
address add_tmp_word(double d);

  // Floating point remainder using x87, destroys the contents of SP-8
  void remf(FRegister dst,  FRegister src);
  void remd(FRegister dst,  FRegister src);

  // Floating point log using x87, destroys the contents of SP-8
  void flog(FRegister dst, FRegister src);
  void flog10(FRegister dst, FRegister src);

  // Floating point negate using SSE xor
  void negf(FRegister dst);
  void negd(FRegister dst);

  // Floating point absolute using SSE xor
  void absf(FRegister dst);
  void absd(FRegister dst);

  // [df]2[il] that correctly handle NaN and max int/long
  void corrected_d2i(Register dest, FRegister src, Register scratch);
  void corrected_d2l(Register dest, FRegister src, Register scratch);
  void corrected_f2i(Register dest, FRegister src, Register scratch);
  void corrected_f2l(Register dest, FRegister src, Register scratch);

  void fcmp(Register dest, FRegister left, FRegister right, int unordered_result);
  void fcmp(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, int unordered_result);
  void dcmp(Register dest, FRegister left, FRegister right, int unordered_result);
  void dcmp(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, int unordered_result);
private:
  void fcmp_helper(Register dest, FRegister left, FRegister right, bool is_float, int unordered_result);
  void fcmp_helper_mem(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, bool is_float, int unordered_result);

public:
  // Conditional move 2 constant helpers
  void cmov4eqi(Register dst, int x, int y);
  void cmov4nei(Register dst, int x, int y);
  void cmov4lti(Register dst, int x, int y);
  void cmov4lei(Register dst, int x, int y);
  void cmov4gei(Register dst, int x, int y);
  void cmov4gti(Register dst, int x, int y);
  void cmov4bei(Register dst, int x, int y);
  void cmov4aei(Register dst, int x, int y);

  // Bump the per-thread allocation summary counters
  void add_to_allocated_objects( RInOuts, Register Rtmp, RKeepIns, Register Rkid, Register Rsiz, Register Rthr);

  // Compare 2 primitive arrays for equality, fall-through to success
  void prim_arrays_equal(int element_size, RInOuts, Register Ra1, Register Ra2,
                         Register Rtmp1, Register Rtmp2, FRegister Rxmm1, FRegister Rxmm2,
Label&fail);
};

#endif // ASSEMBLER_PD_HPP
