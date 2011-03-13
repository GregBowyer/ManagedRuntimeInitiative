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
#include "deoptimization.hpp"
#include "frame.hpp"
#include "gcLocker.hpp"
#include "methodCodeOop.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "safepoint.hpp"
#include "stubRoutines.hpp"

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
#include "register_pd.inline.hpp"
#include "thread_os.inline.hpp"

void NativeInstruction::wrote(int offset) {
  ICache::invalidate_word(addr_at(offset));
}

// --- CAS4in16 --------------------------------------------------------------
// CAS-insert 4 bytes at this address, which must not cross a 16b boundary.
// Will spin until it works.  Used to do atomic 4-byte update of X86 ops.
void NativeInstruction::CAS4in16( intptr_t adr, int32_t newbits ) {
  intptr_t a = (adr & -16);   // Align to 16-byte boundary for 16b CAS
  assert0( a <= adr && (adr+4) <= (a+16) ); // does not span a 16b block
  int64_t* mem = (int64_t*)a;
  union {
    unsigned char b[16];
    struct { uint64_t lo, hi; };
  } u;

  do {
    u.lo = mem[0];
    u.hi = mem[1];
    int off = adr-(intptr_t)a;
for(int i=0;i<4;i++)
      u.b[off+i] = (newbits>>(i<<3))&0xff;
  } while( !cmpxchg16b(u.lo,u.hi,mem,mem[0],mem[1]) );
}

// --- CAS5in16 --------------------------------------------------------------
// CAS-insert 5 bytes at this address, which must not cross a 16b boundary.
// Will spin until it works.  Used to do atomic 5-byte update of X86 ops.
void NativeInstruction::CAS5in16( intptr_t adr, int64_t newbits ) {
  intptr_t a = (adr & -16);   // Align to 16-byte boundary for 16b CAS
  assert0( a <= adr && (adr+5) <= (a+16) ); // does not span a 16b block
  int64_t* mem = (int64_t*)a;
  union {
    unsigned char b[16];
    struct { uint64_t lo, hi; };
  } u;

  do {
    u.lo = mem[0];
    u.hi = mem[1];
    int off = adr-(intptr_t)a;
for(int i=0;i<5;i++)
      u.b[off+i] = (newbits>>(i<<3))&0xff;
  } while( !cmpxchg16b(u.lo,u.hi,mem,mem[0],mem[1]) );
}

// --- destination -----------------------------------------------------------
address NativeCall::destination() const {
  address self = (address)this;
  return self + 5 + *(int*)(self+1);
}

// --- set_destination_mt_safe
void NativeCall::set_destination_mt_safe(address dest) {
  if( destination() == dest ) return;
  int32_t newbits = dest - (((address)this)+5);
  intptr_t adr = ((intptr_t)this)+1; // where the address bits are
  CAS4in16(adr,newbits);
  assert0( destination() == dest );
}

// Amount of padding required to properly align call instruction at given pc
int NativeCall::alignment_padding(address pc){
  // Currently we have - pc: 0xE8 <4-byte relative offset>.  
  // We want the 4-byte field to not cross a 16b line so it can be atomically
  // patched with a CAS16.
  intptr_t a = (intptr_t)pc;
  while( (16-((a+displacement_offset)&15)) < 4 )
    a++;
  return (a-(intptr_t)pc);      // Amount of padding needed to align
}

address NativeJump::jump_destination()const{
  address self = (address)this;
  return self + *(int*)(self+1);
}


//=============================================================================
// --- data ------------------------------------------------------------------
int64 NativeMovConst8Reg::data() const {
  return *(int64*)addr_at(2);
}

// --- set_data --------------------------------------------------------------
void NativeMovConst8Reg::set_data(int64 x) {
  Unimplemented();
}

void NativeNop::create_at(address address, int size_in_bytes) {
  if (size_in_bytes == 5) {
    address[0] = 0x0F;
    address[1] = 0x1F;
    address[2] = 0x44;
    address[3] = 0x00;
    address[4] = 0x00;
  } else {
    Unimplemented();
  }
}

//=============================================================================
// --- alignment_padding -----------------------------------------------------
// Required alignment padding to allow proper atomic patching.
int NativeInlineCache::alignment_padding(address pc){
  // X86 IC's consist of a cmp4i RAX,#kid; jne DIE; call #target
  // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  // 3D <-- KID--> 0F 75 <-- die --> E8 <-- disp-->

  // The CALL displacement needs to patched atomically to point between static
  // targets, vtables and itables.  It cannot cross a 16-byte CAS boundary.
  // Same as the KID field.
  intptr_t a = (intptr_t)pc;
  while( (16-((a+ kid_offset)&15)) < 4 || // cannot atomically patch KID
         (16-((a+disp_offset)&15)) < 4) // cannot atomically patch displacement
    a++;

  return (a-(intptr_t)pc);      // Amount of padding needed to align
}

// --- fill ------------------------------------------------------------------
void NativeInlineCache::fill(MacroAssembler*masm){
  // X86 IC's consist of a cmp4i RAX,#kid; jne DIE; call #target
  // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  // 3D <-- KID--> OF 75 <-- die --> E8 <-- disp-->
masm->align_inline_cache();
  Label *die = new Label();     // New Label will be freed when ResourceMark unwinds
  masm->emit1(0x3D);            // CMP4I RAX,...
  masm->emit4(1);               // KID #1 - atomically patchable
  masm->jne2 (*die);            // JNE to fixup code at the blob end
  masm->call ((address)0x1);    // Static call to predicted target
  masm->make_ic_slowpath(die);  // Debug info goes here
}

// --- set_clean_unsafely ----------------------------------------------------
// Unsafely force the inline-cache to 'clean' state.  Will break if running
// threads execute while cleaning.  This requires a full safepoint.
void NativeInlineCache::set_clean_unsafely() {
  assert0( SafepointSynchronize::is_at_safepoint());  

  // X86 IC's consist of a cmp4i RAX,#kid; jne DIE; call #target
  // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
  // 3D <-- KID--> OF 75 <-- die --> E8 <-- disp-->
  *kid_addr() = 1;              // set kid to 1
  *addr_at(jne_offset+1)=0x85; // patch JO to a JNE.  No need for atomic.
  set_new_destination(0);
  ICache::invalidate_range(addr_at(0), instruction_size);

  assert0(  is_clean  () );
  assert0( !is_static () );
  assert0( !is_caching() );
  assert0( !is_vcall  () );
  assert0( !is_icall  () );
}

// --- set_static ------------------------------------------------------------
// MT-safely transit from 'clean' to 'static'
void NativeInlineCache::set_static( address code, klassOop expected_klass ) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(return_address()));
  assert( is_clean() || is_static(), "Only clean->static transition or static->static transition allowed" ); 

  // In a thread-safe way, convert this clean IC to a static IC.  We must be
  // safe in the face of racing executor threads.  If the IC is clean, all
  // executors stop at the 1st instruction.  If the IC is static already we
  // merely change the destination.
set_new_destination(code);

  // However, the cmp4i/jne will continue to fail.  Entering the runtime for
  // the cmp4i/jne will end up doing the right thing very slowly.  Patch the
  // cmp4i/jne to a cmp-vs-zero and a jno.
  set_int_at(kid_offset,0);           // patch kid to 0.  No need for atomic.
  *addr_at(jne_offset+1)=0x80;        // patch JNE to a JO.  No need for atomic.
  ICache::invalidate_range(addr_at(0), instruction_size);

  assert0( !is_clean  () );
  assert0(  is_static () );
  assert0( !is_caching() );
  assert0( !is_vcall  () );
  assert0( !is_icall  () );
}

// --- set_caching -----------------------------------------------------------
// MT-safely transit from 'clean' to 'caching'
void NativeInlineCache::set_caching( address code, klassOop expected_klass ) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(return_address()));
assert(is_clean(),"Only clean->caching transition allowed");

  // In a thread-safe way, convert this clean IC to a caching IC.  We must be
  // safe in the face of racing executor threads.  Since the IC is clean, all
  // executors fail the JNE test.
  set_new_destination(code); // The desired target

  // Now "release" the IC by replacing KID with something valid.
  CAS4in16( (intptr_t)addr_at(kid_offset), Klass::cast(expected_klass)->klassId() );
  ICache::invalidate_range(addr_at(0), instruction_size);

  // Verify all is well
  assert0( !is_clean  () );
  assert0( !is_static () );
  assert0(  is_caching() );
  assert0( !is_vcall  () );
  assert0( !is_icall  () );
}

// --- set_new_destination ---------------------------------------------------
void NativeInlineCache::set_new_destination( address code ) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(return_address()));
  // No safepoint between patching and informing the MCO of the new target!
  No_Safepoint_Verifier nsv();	// Verify no safepoint across lock

  // In a thread-safe way, update the target address of this call.
  call()->set_destination_mt_safe(code);

  // Now tell the owning MethodCodeOop that its CodeBlob depends on
  // the target blob remaining alive.
  CodeCache::find_blob(this)->owner().as_methodCodeOop()->patched_a_call_to(return_address(),code);
}

// --- set_vcall -------------------------------------------------------------
// MT-safely transit from 'clean' or 'caching' to 'vcall'
void NativeInlineCache::set_vcall( address vstub ) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(return_address()));
assert(is_caching(),"Only caching->vcall transition allowed");

  // In a thread-safe way, convert this caching IC to a vcall IC.  We must be
  // safe in the face of racing executor threads.  Since the IC is caching,
  // executors can stop at any instruction.  They can be stopped after
  // executing the cmp4i or the jmp.

  // This is all that is needed for correctness.  It includes the I-cache
  // flush.  After this all new callers will go to the vstub, although a
  // caller might uselessly fail the cmp4i/jne (and go slow in the VM to the
  // correct place)
  set_new_destination( vstub );

  // However, the cmp4i/jne will continue to fail.  Entering the runtime for
  // the cmp4i/jne will end up doing the right thing very slowly.  Patch the
  // cmp4i/jne to a cmp-vs-zero and a jno.
  set_int_at(kid_offset,0);           // patch kid to 0.  No need for atomic.
  *addr_at(jne_offset+1)=0x80;        // patch JNE to a JO.  No need for atomic.
  ICache::invalidate_range(addr_at(0), instruction_size);

  assert0( !is_clean  () );
  assert0( !is_static () );
  assert0( !is_caching() );
  assert0(  is_vcall  () );
  assert0( !is_icall  () );
}

// --- set_icall -------------------------------------------------------------
// MT-safely transit from 'clean' or 'caching' to 'icall'
void NativeInlineCache::set_icall( address istub, oop interface ) {
  assert_locked_or_safepoint(CompiledIC_locks.get_lock(return_address()));
assert(is_caching(),"Only caching->vcall transition allowed");

  // In a thread-safe way, convert this caching IC to an icall IC.  We must be
  // safe in the face of racing executor threads.  Since the IC is caching,
  // executors can stop at any instruction.

  // This is all that is needed for correctness.  It includes the I-cache
  // flush.  After this all new callers will go to the istub, although a
  // caller might uselessly test the wrong KID.
  set_new_destination( istub );

  // However, the cmp4i/jne will continue to fail.  Entering the runtime for
  // the cmp4i/jne will end up doing the right thing very slowly.  Patch the
  // cmp4i/jne to a cmp-vs-zero and a jno.
  set_int_at(kid_offset,0);           // patch kid to 0.  No need for atomic.
  *addr_at(jne_offset+1)=0x80;        // patch JNE to a JO.  No need for atomic.
  ICache::invalidate_range(addr_at(0), instruction_size);

  assert0( !is_clean  () );
  assert0( !is_static () );
  assert0( !is_caching() );
  assert0( !is_vcall  () );
  assert0(  is_icall  () );
}

// --- at --------------------------------------------------------------------
// Create from the embedded NativeCall point.
NativeInlineCache* NativeInlineCache::at(NativeCall* call) {
  // Inline caches are build around variations of this template:
  // cmp4i rax,#kid
  // jne die
  // call target
  NativeInlineCache *ic = (NativeInlineCache*)(call->addr_at(-call_offset));
ic->verify();
  return ic;
}

// These accessors are all only valid if no thread is busy patching
// --- is_clean --------------------------------------------------------------
// Clean: kid is set to 1, and *will* fail.
bool NativeInlineCache::is_clean()const{
  return kid()==1 && *addr_at(jne_offset+1)==0x85;
}

// --- is_static -------------------------------------------------------------
// Static: RAX cannot assumed to be anything useful, and we don't want to
// endlessly fail a useless KID test.  Set the #kid to 0.  Set the JNE to
// a JO (jump overflow) which never happens when comparing to 0.
bool NativeInlineCache::is_static()const{
  if( kid()!=0 || *addr_at(jne_offset+1)!=0x80 ) return false;
  address called = call()->destination();
CodeBlob*cb=CodeCache::find_blob(called);
  if( !cb ) return false;
if(cb->is_methodCode())
    return true;                // native, C1 or C2 target
  if (!cb->is_method_stub())
    return false;               // direct vtable stub?
  NativeMethodStub *nms = (NativeMethodStub*)called;
  CodeBlob *cb1 = CodeCache::find_blob(nms->get_destination());
  if( !cb1 ) return false;
  assert0( cb1->is_vtable_stub() || cb1->is_c2i() );
  return cb1->is_c2i();
}

// --- is_caching ------------------------------------------------------------
bool NativeInlineCache::is_caching() const {
  return kid()> 1 && *addr_at(jne_offset+1)==0x85 && KlassTable::is_valid_klassId(kid());
}

// --- is_vcall --------------------------------------------------------------
bool NativeInlineCache::is_vcall  () const {
  if( kid()!=0 || *addr_at(jne_offset+1)!=0x80 ) return false;
  CodeBlob *cb = CodeCache::find_blob(call()->destination());
  return cb ? cb->is_vtable_stub() : false;
}

// --- is_icall --------------------------------------------------------------
bool NativeInlineCache::is_icall  () const {
  if( kid()!=0 || *addr_at(jne_offset+1)!=0x80 ) return false;
  address called = call()->destination();
CodeBlob*cb=CodeCache::find_blob(called);
  if( !cb ) return false;
if(cb->is_methodCode())
    return false;               // native, C1 or C2 target
  if (!cb->is_method_stub())
    return false;               // direct vtable stub?
  NativeMethodStub *nms = (NativeMethodStub*)called;
  CodeBlob *cb1 = CodeCache::find_blob(nms->get_destination());
  if( !cb1 ) return false;
  assert0( cb1->is_vtable_stub() || cb1->is_c2i() );
  return cb1->is_vtable_stub();
}

// --- expected_klass --------------------------------------------------------
klassOop NativeInlineCache::expected_klass() const { 
  return kid()<=1 ? NULL : KlassTable::getKlassByKlassId(kid()).as_klassOop(); 
}

#ifndef PRODUCT
// --- verify ----------------------------------------------------------------
void NativeInlineCache::verify()const{
  assert( is_clean()  || 
	  is_static() ||
	  is_caching()||
	  is_vcall()  ||
	  is_icall(), "sanity check" );
  expected_klass();             // merely referencing should verify
}
#endif // !PRODUCT

//=============================================================================
// --- NativeAllocationTemplate::at ------------------------------------------
// Verify that the site looks like an allocation site.
// Such sites can be patched between heap & SBA allocation
NativeAllocationTemplate* NativeAllocationTemplate::at(NativeCall* ncall) {
  if( UseC1 || UseC2 ) {
    char* op = (char*)ncall;
    return (op[0]==0xe8) ? (NativeAllocationTemplate*)ncall : NULL;
  } else {
    return NULL;
  }
}

// --- NativeAllocationTemplate::fill ----------------------------------------
void NativeAllocationTemplate::fill(MacroAssembler *masm, address adr) {
  masm->aligned_patchable_call(adr);
}

//=============================================================================
void NativeMovConstReg::verify() {
  Untested(); // Write me..
}

void NativeMovRegMem::verify() {
  Untested(); // Write me..
}

void NativeMovImmReg::verify(){
  Untested(); // Write me..
}

//=============================================================================
// Move a pointer-to-moop into R10 as required for c2i adapters.  Note that
// all callee-save registers and all argument registers may be live here.
// This pretty much limits us to RAX, R10, & R11.  RAX is also then used for
// the inline-cache idiom.  The value is NOT loaded (since the read-barrier
// code is so bulky and these code patterns are common).  The C2I adapter will
// have to do a proper load/read-barrier.
// General layout:
//   mov8i r10,&moop
//   jmp   c2i_adapter
//   nop_align
//   [moop]


// --- fill ------------------------------------------------------------------
void NativeMethodStub::fill(heapRef moop, address c2i_adapter) {
  address self = (address)this;
  *self++ = Assembler::REX_B;   // prefix for R10
  *self++ = 0xB8 | (R10-8);     // mov8 opcode
  *(int32_t*)self = (int32_t)(intptr_t)this+oop_offset; // mov8i rax,&moop
  self += sizeof(int32_t);
  address pc = self+5;          // end of jump instruction
  *self++ = 0xE9;               // jmp long
  *(int32_t*)self = (c2i_adapter-pc);
  self += sizeof(int32_t);
  self = (address)round_to((intptr_t)self,HeapWordSize);
  assert0( self-(address)this == oop_offset );
  POISON_AND_STORE_REF((heapRef*)self,moop);
self+=sizeof(heapRef);
  assert0( self-(address)this <= instruction_size );
}

// --- oops_do ---------------------------------------------------------------
void NativeMethodStub::oops_do(OopClosure*f){
  f->do_oop((objectRef*)pc(oop_offset));
}

// --- is_alive --------------------------------------------------------------
bool NativeMethodStub::is_alive(BoolObjectClosure* is_alive) const {
  oop x = get_oop().as_oop();
return is_alive->do_object_b(x);
}

// --- get_oop ---------------------------------------------------------------
heapRef NativeMethodStub::get_oop( ) const {
  return ALWAYS_UNPOISON_OBJECTREF(*(objectRef*)pc(oop_offset));
}

// --- set_oop ---------------------------------------------------------------
void NativeMethodStub::set_oop(heapRef obj){
  *((objectRef*)pc(oop_offset)) = obj;
}

// --- oop_addr --------------------------------------------------------------
heapRef* NativeMethodStub::oop_addr() const {
  return (heapRef*)pc(oop_offset);
}

// --- patch_entry -----------------------------------------------------------
void NativeJump::patch_entry(address entry,address dest){
  CAS5in16((intptr_t)entry,
           (((dest-(entry+5))&0xFFFFFFFF) << 8 ) | 0xE9L/*long jump opcode*/ );
}

void NativeGeneralJump::verify() {
  const unsigned char *bits = (const unsigned char*)this;
  const unsigned char op = bits[0];
  assert0( op==0xE9 || // unconditional short
           op==0xEB || // unconditional long
           (op&0xF0) == 0x70 || // conditional short
           (op==0x0F && (bits[1]&0xF0)==0x80) || // conditional long
           (op >= 0xE0 && op <= 0xE3) ); // loop/loope/loopne/jrcxz
}


void NativeGeneralJump::insert_unconditional(address code_pos, address entry) {
  intptr_t disp = (intptr_t)entry - ((intptr_t)code_pos + 1 + 4);
#ifdef AMD64
  guarantee(disp == (intptr_t)(int32_t)disp, "must be 32-bit offset");
#endif // AMD64

  *code_pos = unconditional_long_jump;
  *((int32_t *)(code_pos+1)) = (int32_t) disp;
  ICache::invalidate_range(code_pos, instruction_size);
}


// MT-safe patching of a long jump instruction.
// First patches first word of instruction to two jmp's that jmps to them
// selfs (spinlock). Then patches the last byte, and then atomicly replaces
// the jmp's with the first 4 byte of the new instruction.
void NativeGeneralJump::replace_mt_safe(address instr_addr, address code_buffer) {
   assert (instr_addr != NULL, "illegal address for code patching (4)");
   NativeGeneralJump* n_jump =  nativeGeneralJump_at (instr_addr); // checking that it is a jump

   // Temporary code
   unsigned char patch[4];
   assert(sizeof(patch)==sizeof(int32_t), "sanity check");
   patch[0] = 0xEB;       // jmp rel8
   patch[1] = 0xFE;       // jmp to self
   patch[2] = 0xEB;
   patch[3] = 0xFE;
   
   // First patch dummy jmp in place
   *(int32_t*)instr_addr = *(int32_t *)patch;
    n_jump->wrote(0);

   // Patch 4th byte
   instr_addr[4] = code_buffer[4];

    n_jump->wrote(4);

   // Patch bytes 0-3
   *(jint*)instr_addr = *(jint *)code_buffer;  

    n_jump->wrote(0);

#ifdef ASSERT
   // verify patching
   for ( int i = 0; i < instruction_size; i++) {
     address ptr = (address)((intptr_t)code_buffer + i);     
     int a_byte = (*ptr) & 0xFF;
     assert(*((address)((intptr_t)instr_addr + i)) == a_byte, "mt safe patching failed");
   }
#endif

}



address NativeGeneralJump::jump_destination() const {  
  int op_code = ubyte_at(0);
  bool is_rel32off = (op_code == 0xE9 || op_code == 0x0F);
  int  offset  = (op_code == 0x0F)  ? 2 : 1;
  int  length  = offset + ((is_rel32off) ? 4 : 1);
  
  if (is_rel32off) 
    return addr_at(0) + length + int_at(offset);
  else
    return addr_at(0) + length + sbyte_at(offset);
}


// --- pd_patch_for_deopt ----------------------------------------------------
// Slap the code with some no-op equivalent.  Any thread returning to the
// function will execute only no-ops until it falls off the end and hits the
// deopt-sled.  Must be done at a full Safepoint.
void CodeBlob::pd_patch_for_deopt() const {
  // Because it's the fabulous variable-length X86, we'll have trouble
  // inserting any sane instruction here.  The issue is: at the return-site
  // we might have any short instruction, followed by another call out.
  //  
  //  PREPATCH:                                       POSTPATCH:
  //  ...                                             ... nop*
  //  call foo           <-- deopt happens inside     nop;nop;nop;nop;nop
  //  add_2byte_op x,y   <-- will return here    -->  nop ; // start sliding
  //  call bar                                        nop;nop;nop;nop;nop
  //  blah_op            <-- also returns here   -->  nop ; // start sliding
  //  ...                                             ... nop*
  //  ret                                             nop
  //  deopt_sled             enter deopt sled here--> deopt_sled
  //
  // Assuming all folks returning from a call - including exception throws!!!
  // - put the return PC on the stack and use a 'ret', then we can pick up the
  // return PC from below RSP.
  // 
  // Alternative patch: jam a 'call deopt_sled' at each return point.  Does
  // not need the return PC below the stack.  Requires return-points be no
  // closer than 5 bytes apart.

  // Since patching is done at a full Safepoint, no threads are moving through
  // the code and we can patch the code in any old order we want.
  assert0( NativeJump::is_jump_at(code_begins()) ); // code already made Not Entrant
  address patch_start = code_begins()+NativeJump::instruction_size;
  int patch_length = (address)this + owner().as_methodCodeOop()->_deopt_sled_relpc -  patch_start;
  memset( patch_start, 0x90/*nop byte*/, patch_length );
}

// --- pd_fetch_return_values ------------------------------------------------
// Fetch 0, 1, or 2 return values from a deopt-in-progress and stuff them into
// buf.  Later, buf will be unpacked onto the Java Execution stack.
intptr_t Deoptimization::pd_fetch_return_values( JavaThread *thread, BasicType return_type ) {
  frame fr = thread->last_frame().sender();
  switch( return_type ) {
  case T_LONG:
  case T_BOOLEAN:
  case T_CHAR:
  case T_BYTE:
  case T_SHORT:
  case T_INT:
  case T_OBJECT:
  case T_ARRAY:  return *fr.reg_to_addr(gpr2reg(RAX));
  case T_FLOAT:
  case T_DOUBLE: return *fr.reg_to_addr(fpr2reg(F00));
  case T_VOID:   return 0;      // deoptimizing from a void function
  default:       ShouldNotReachHere();  return 0;
  }
}


