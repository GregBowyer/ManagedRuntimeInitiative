/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef NATIVEINST_PD_HPP
#define NATIVEINST_PD_HPP

#include "constants_pd.hpp"
#include "heapRef_pd.hpp"
#include "icache.hpp"
class BoolObjectClosure;
class MacroAssembler;
class OopClosure;

// We have interface for the following instructions:
// - NativeInstruction
// - - NativeCall
// - - NativeMovConst8Reg
// - - NativeMovRegMem
// - - NativeJump
// - - NativeIllegalOpCode
// The base class for different kinds of native instruction abstractions.
// Provides the primitive operations to manipulate code relative to this.

class NativeInstruction : public Instr {
 public:
  enum X86_specific_constants {
    instruction_size = 1,
    // define a couple of aliases here for portability
    nop_instruction_size = instruction_size
  };

  static instr_t breakpoint();

  // Inline-cache uses a cmpclass trap to detect a miss
  bool is_add8();
  bool is_branch();
  bool is_breakpoint();
  inline bool is_jump();

  void  verify() { assert0( this != 0 ); }
  void  print();

  static NativeInstruction* nativeInstruction_at(address addr) {
NativeInstruction*instr=(NativeInstruction*)addr;
#ifdef ASSERT
instr->verify();
#endif
    return instr;
  }

  address addr_at(int offset) const   { return (address)((char*)this + offset); }
int int_at(int offset)const{return*(int*)addr_at(offset);}
  void set_int_at(int offset, jint i) { *(jint*)addr_at(offset) = i; }

  // CAS-insert 4 bytes at this address, which must not cross a 16b boundary.
  // Will spin until it works.  Used to do atomic 4-byte update of X86 ops.
  static void CAS4in16( intptr_t adr, int32_t newbits );
  // CAS-insert 5 bytes at this address, which must not cross a 16b boundary.
  // Will spin until it works.  Used to do atomic 5-byte update of X86 ops.
  static void CAS5in16( intptr_t adr, int64_t newbits );

  // This doesn't really do anything on Intel, but it is the place where
  // cache invalidation belongs, generically:
  void wrote(int offset);

 protected:
  s_char sbyte_at(int offset) const    { return *(s_char*) addr_at(offset); }
  u_char ubyte_at(int offset) const    { return *(u_char*) addr_at(offset); }
};

class NativeBreakpoint:public NativeInstruction{
 public:
  enum CPU_specific_constants {
    instruction_length = 1,
    instruction_size   = instruction_length * instruction_size
  };
  address instruction_address() const            { return (address) pc(); }

  friend NativeBreakpoint* nativeBreakpoint_at(address addr) {
return(NativeBreakpoint*)addr;
  }

  void verify();

  static void insert_breakpoint(address addr) {
    *(instr_t*)addr = breakpoint();
    ICache::invalidate_range((address)addr, instruction_size);
  }

};


class NativeCall: public NativeInstruction {
 public:
  enum X86_specific_constants {
    instruction_size   = 5,
displacement_offset=1//distance from op start to the 4-byte patchable offset
  };
address instruction_address()const{return(address)pc();}

static instr_t make(address dest);
  address destination() const;
  void  set_destination_mt_safe(address dest);
void set_destination(address dest);
  bool set_destination_cas(address dest, address cur_dest);

  // Amount of padding required to properly align call instruction at given pc
static int alignment_padding(address pc);

  void  verify() { assert0( is_call_at((address)this) ); }
  void  print();

  friend NativeCall* nativeCall_at(address addr);

  friend NativeCall* nativeCall_before(address return_address);

  inline static bool is_call_before(address addr) {
    return is_call_at(addr - instruction_size);
  }
  inline static bool is_call_at(address addr) {
    return (((const unsigned char *)addr)[0] == 0xE8); // call opcode
  }

static address get_call_dest_at(address addr);
};

inline NativeCall*nativeCall_at(address addr){
NativeCall*nc=(NativeCall*)addr;
#if defined(ASSERT)
nc->verify();
#endif // defined(ASSERT)
  return nc;
}

inline NativeCall* nativeCall_before(address return_address) {
return nativeCall_at(return_address-NativeCall::instruction_size);
}


class NativeMovConst8Reg:public NativeInstruction{
 public:
enum{
instruction_size=10
  };
  int64 data() const;
  void set_data(int64 x);
  static bool is(address adr) { return (adr[0]&0xF8)==0x48 && (adr[1]&0xF8)==0xB8; }
  void verify() { assert0(is(addr_at(0))); }
  friend NativeMovConst8Reg* nativeMovConst8Reg_at(address addr);
};

inline NativeMovConst8Reg* nativeMovConst8Reg_at(address addr) {
  NativeMovConst8Reg* ni = (NativeMovConst8Reg*)addr;
#if defined(ASSERT)
ni->verify();
#endif // defined(ASSERT)
  return ni;
}


// An interface for accessing/manipulating native moves of the form:
//	mov[b/w/l] [reg + offset], reg   (instruction_code_reg2mem)
//      mov[b/w/l] reg, [reg+offset]     (instruction_code_mem2reg
//      mov[s/z]x[w/b] [reg + offset], reg 
//      fld_s  [reg+offset]
//      fld_d  [reg+offset]
//	fstp_s [reg + offset]
//	fstp_d [reg + offset]
//
// Warning: These routines must be able to handle any instruction sequences
// that are generated as a result of the load/store byte,word,long
// macros.  For example: The load_unsigned_byte instruction generates
// an xor reg,reg inst prior to generating the movb instruction.  This
// class must skip the xor instruction.  

class NativeMovRegMem: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    instruction_code_xor		= 0x33,
    instruction_extended_prefix	        = 0x0F,
    instruction_code_mem2reg_movzxb     = 0xB6,
    instruction_code_mem2reg_movsxb     = 0xBE,
    instruction_code_mem2reg_movzxw     = 0xB7,
    instruction_code_mem2reg_movsxw     = 0xBF,
    instruction_operandsize_prefix      = 0x66,
    instruction_code_reg2meml	        = 0x89,
    instruction_code_mem2regl	        = 0x8b,
    instruction_code_reg2memb	        = 0x88,
    instruction_code_mem2regb	        = 0x8a,
    instruction_code_float_s	        = 0xd9,
    instruction_code_float_d	        = 0xdd,
    instruction_code_long_volatile      = 0xdf,
    instruction_code_xmm_ss_prefix      = 0xf3,
    instruction_code_xmm_sd_prefix      = 0xf2,
    instruction_code_xmm_code           = 0x0f,
    instruction_code_xmm_load           = 0x10,
    instruction_code_xmm_store          = 0x11,

    instruction_code_rex_prefix_begin   = 0x40,
    instruction_code_rex_prefix_end     = 0x4f,
    
    instruction_size	                = 4,
    instruction_offset	                = 0,
    instruction_offset_size             = 4,
    data_offset	                        = 2,
    next_instruction_offset	        = 4
  };

  address instruction_address() const {
    u_char instr_0 = *addr_at(instruction_offset);
    if ( (instr_0 == instruction_operandsize_prefix) ||
         (instr_0 == instruction_extended_prefix) ) {
      return addr_at(instruction_offset+1);
    }
else if(instr_0==instruction_code_xor){
      return addr_at(instruction_offset+2);
    }
    else return addr_at(instruction_offset);
  }

  address next_instruction_address() const {
    switch (*addr_at(instruction_offset)) {
    case instruction_operandsize_prefix:
    case instruction_extended_prefix:
      return instruction_address() + instruction_size + 1;
    case instruction_code_reg2meml:
    case instruction_code_mem2regl:
    case instruction_code_reg2memb:
    case instruction_code_mem2regb:
    case instruction_code_xor:
      return instruction_address() + instruction_size + 2;
    default:
      return instruction_address() + instruction_size;
    }
  }

  int decode_offset() const {
int offset=0;
    bool has_rex = false;
    bool is_sse = false;
    while ( (*addr_at(instruction_offset+offset) == instruction_operandsize_prefix) || 
            (*addr_at(instruction_offset+offset) == instruction_extended_prefix) ) {
      offset++;
    }

    if (*addr_at(instruction_offset+offset) == instruction_code_xmm_ss_prefix ||
        *addr_at(instruction_offset+offset) == instruction_code_xmm_sd_prefix) {
      offset += 2; // SSE ops are 3 bytes of opcode
      is_sse = true;
    }

    if (*addr_at(instruction_offset+offset) >= instruction_code_rex_prefix_begin &&
        *addr_at(instruction_offset+offset) <= instruction_code_rex_prefix_end) {
      offset++;
      has_rex = true;
    }
 
    if (*addr_at(instruction_offset+offset) == instruction_code_xor) {
      Unimplemented();
    }

    if        (*addr_at(instruction_offset+offset) == instruction_extended_prefix) {
      offset += 2; // extended opcode instructions are 2 bytes
    } else {
      offset++;    // Regular 1 byte of opcode
    }

    char modrm = *addr_at(instruction_offset+offset);
    assert( (modrm & 0xC0) != 0xC0, "reg-reg modrm not supported" );
    assert( (modrm & 0x38) != (0x4 << 3) || has_rex || is_sse , "Unexpected SIB byte" );
    offset++;

    return offset;
  }

  int offset() const { 
    return int_at(decode_offset());
  }

  void  set_offset(int x) {
int offset=0;
    if (*addr_at(instruction_offset) >= instruction_code_rex_prefix_begin &&
        *addr_at(instruction_offset) <= instruction_code_rex_prefix_end) {
      offset += 1;
    }

    if ( (*addr_at(instruction_offset+offset) == instruction_operandsize_prefix) || 
         (*addr_at(instruction_offset+offset) == instruction_extended_prefix) ) {
      offset += data_offset + 1;
    }
else if(*addr_at(instruction_offset+offset)==instruction_code_xor||
*addr_at(instruction_offset+offset)==instruction_code_xmm_ss_prefix||
*addr_at(instruction_offset+offset)==instruction_code_xmm_sd_prefix){
offset+=data_offset+2;
    }
    else {
offset+=data_offset;
    }

    set_int_at(decode_offset(), x);
  }

  void  add_offset_in_bytes(int add_offset)     { set_offset ( ( offset() + add_offset ) ); }
  void  copy_instruction_to(address new_instruction_address);

  void verify();
  void print ();

  // unit test stuff
  static void test() {}

 private:
  inline friend NativeMovRegMem* nativeMovRegMem_at (address address);
};

inline NativeMovRegMem* nativeMovRegMem_at (address address) {
  NativeMovRegMem* test = (NativeMovRegMem*)(address - NativeMovRegMem::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

// An interface for accessing/manipulating native mov reg, [imm32] instructions.
// (used to manipulate inlined 32bit data dll calls, etc.)
class NativeMovConstReg: public NativeInstruction {
 public:
  enum Intel_specific_constants {
instruction_code=0xB8,
instruction_size=8,
    instruction_offset	        =    0,
data_offset=4,
    next_instruction_offset	=    8,
    register_mask	        = 0x07
  };

  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  { return addr_at(next_instruction_offset); }
  int data() const                          { return int_at(data_offset); }
  void  set_data(int x)                     { set_int_at(data_offset, x); }

  void  verify();
  void  print();
  
  // unit test stuff
  static void test() {}

  // Creation
  inline friend NativeMovConstReg* nativeMovConstReg_at(address address);
};

inline NativeMovConstReg* nativeMovConstReg_at(address address) {
  NativeMovConstReg* test = (NativeMovConstReg*)(address - NativeMovConstReg::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

// An interface for accessing/manipulating native mov reg, imm32 instructions i
// aka mov8i reg, imm32
class NativeMovImmReg:public NativeInstruction{
 public:
  enum Intel_specific_constants {
instruction_code=0xB8,//this will be adjusted according to dest
instruction_size=6,
    instruction_offset	        =    0,
    data_offset	                =    1, // unused but may be used if want to patch 
                                        // many different variants of "mov reg, imm"
    next_instruction_offset	=    6,
    imm_size                    =    4,
    register_mask	        = 0x07
  };

  address instruction_address() const       { return addr_at(instruction_offset); }
  address next_instruction_address() const  { return addr_at(next_instruction_offset); }
  int   data(int data_offset) const         { return int_at(data_offset); }
  void  set_data(int data_offset, int x)    { set_int_at(data_offset, x); }

  void  verify();
  void  print();
  
  // unit test stuff
  static void test() {}

  // Creation
  inline friend NativeMovConstReg* nativeMovConstReg_at(address address);
};

inline NativeMovImmReg*nativeMovImmReg_at(address address){
NativeMovImmReg*test=(NativeMovImmReg*)(address-NativeMovImmReg::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

class NativeNop:public NativeInstruction{
 public:
  static void create_at(address address, int size_in_bytes);
};

class NativeJump: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    instruction_code	        = 0xe9,
    instruction_size	        =    5,
    instruction_offset	        =    0,
    data_offset	                =    1,
    next_instruction_offset	=    5
  };

  address jump_destination() const;
static void patch_entry(address entry,address dest);
  void verify() { assert0( is_jump_at((address)this) ); }
  friend NativeJump* nativeJump_at(address addr);
  inline static bool is_jump_at(address addr) {
    return ((const unsigned char *)addr)[0] == 0xE9; // jump opcode
  }
};

inline NativeJump*nativeJump_at(address addr){
NativeJump*nj=(NativeJump*)addr;
#if defined(ASSERT)
nj->verify();
#endif // defined(ASSERT)
  return nj;
}


class NativeGeneralJump: public NativeInstruction {
 public:
  enum Intel_specific_constants {
    // Constants does not apply, since the lengths and offsets depends on the actual jump
    // used
    // Instruction codes:
    //   Unconditional jumps: 0xE9    (rel32off), 0xEB (rel8off)
    //   Conditional jumps:   0x0F8x  (rel32off), 0x7x (rel8off)
    unconditional_long_jump  = 0xe9,
    unconditional_short_jump = 0xeb,
    instruction_size = 5
  };

  static void insert_unconditional(address code_pos, address entry);
  address jump_destination() const;

  void verify();
  
  friend NativeGeneralJump* nativeGeneralJump_at(address address);
  static void replace_mt_safe(address instr_addr, address code_buffer);

};

inline NativeGeneralJump* nativeGeneralJump_at(address address) {
  NativeGeneralJump* jump = (NativeGeneralJump*)(address);
#ifdef ASSERT
  jump->verify();
#endif
  return jump;
}



// An interface for making MethodStubs: load R10 with
// the methodOop and jump to an c2i adapter.
class NativeMethodStub:public NativeInstruction{
public:
enum{
    instruction_size = 32,  // Must be a multple of CodeEntryAlignment
    jmp_offset = 6,             // start of jmp
    oop_offset = 16
  };
 
  static NativeMethodStub *cast(address i) { return (NativeMethodStub*)i; }
  int jump_offset() const;
  address get_destination() const { return nativeJump_at(addr_at(jmp_offset))->jump_destination(); }
  void fill(heapRef moop, address c2i_adapter);
  void set_oop( heapRef moop );
heapRef get_oop()const;
  heapRef* oop_addr() const;
  void oops_do(OopClosure* f);
  bool is_alive(BoolObjectClosure* is_alive) const;
};

//=============================================================================
// Native implementation of the inline-cache template.  Inline-caches have 5
// states (clean, static, caching, vcall, icall).  See more comments in
// compiledIC.hpp.  For X86 alone, they require the receiver's klass ID in
// RAX.  The remaining ops are: "cmp4i RAX,#kid; jne DIE; call target".  The
// kid can be extracted via: "mov8 RAX,RDI; ref2kid(RAX);" or optimized by the
// compilers.  The initial value of #kid is set to 1, which is never a valid
// KID and always fails.  #kid has to be atomically patchable, as does the
// target.
class NativeInlineCache:public NativeInstruction{
public:
enum{
    cmp_offset= 0,              // CMP4i RAX,... 0x3D
    kid_offset= 1,              // start of 4-byte KID
    jne_offset= 5,
    call_offset=11,             // start of call instruction
    disp_offset=12,             // start of 4-byte displacement
instruction_size=16
  };
private:
  // These are the raw IC manipulators.  Only friend CompiledIC properly
  // guards the transitions so only {legal,safe} transitions occur.
  friend class CompiledIC;
  // Unsafely force the inline-cache to 'clean' state.  Will break if running
  // threads execute while cleaning.  This requires a full safepoint.
  void set_clean_unsafely();
  // MT-safely transit from 'clean' to 'static'
  void set_static( address code, klassOop expected_klass );
  // MT-safely transit from 'clean' to 'caching'
  void set_caching( address code, klassOop expected_klass );
  void set_new_destination( address code );
  // MT-safely transit from 'clean' or 'caching' to 'vcall'
  void set_vcall( address vstub );
  // MT-safely transit from 'clean' or 'caching' to 'icall'
  void set_icall( address istub, oop interface );

public:

  // Bits and pieces
  int32_t *kid_addr() const { return (int32_t*)    addr_at( kid_offset) ; }
  NativeCall *call () const { return nativeCall_at(addr_at(call_offset)); }
  int kid() const { return *kid_addr(); }

  // Create from the embedded NativeCall point.
  static NativeInlineCache* at(NativeCall*);
  // Insert a new clean inline cache into the code buffer
static void fill(MacroAssembler*masm);
address return_address()const{return addr_at(instruction_size);}
  // Required alignment padding to allow proper atomic patching.
static int alignment_padding(address pc);

  // These accessors are all only valid if no thread is busy patching
  bool is_clean  () const;
  bool is_static () const;
  bool is_caching() const;
  bool is_vcall  () const;
  bool is_icall  () const;
  address destination() const { return call()->destination(); }
  klassOop expected_klass() const;
  void verify() const PRODUCT_RETURN;
};

//=============================================================================
// Native implementation of the allocation template.
// Allocation templates have fixed or varying-size alternatives, and heap vs
// SBA alternatives.
// R09: size in bytes
// R10: element count (forced 0 for objects)
// R11: kid
// Z - failed, RAX - blown
// NZ- OK, RAX- new oop, stripped; RCX - new oop w/mark, R11- blown
class NativeAllocationTemplate:public NativeInstruction{
  NativeCall* call() const { return nativeCall_at(addr_at(call_offset)); }
public:
  enum CPU_specific_constants {
    pf_offset        = 0,
    call_offset      = pf_offset + instruction_size,
    instruction_size = call_offset + NativeCall::instruction_size
  };
  // Create from the embedded NativeCall point.
  static NativeAllocationTemplate* at(NativeCall*);
  // Insert a new allocation template into the code buffer.
  static void fill(MacroAssembler *masm, address alloc_stub);

  // Force heap allocation, for failed SBA allocation sites
  bool set_heap( );
  bool set_stack();             // Back to stack allocation from heap

  // Get & set allocation-size hints.  The returned size is an *approximation*
  // to the set size; it is either the same or a larger size, or set as the
  // varying-size value (size==0).  Size is in Bytes.
  void set_size_hint( int size_hint );
  int  get_size_hint( ) const;

  void verify() const PRODUCT_RETURN;
};

inline bool NativeInstruction::is_jump()         { return ubyte_at(0) == NativeJump::instruction_code ||
                                                          ubyte_at(0) == 0xEB; /* short jump */ }

#endif // NATIVEINST_PD_HPP
