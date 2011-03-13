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
#ifndef CODEBLOB_HPP
#define CODEBLOB_HPP

#include "allocation.hpp"
#include "growableArray.hpp"
#include "methodCodeOop.hpp"
#include "oop.hpp"

class CodeCache;
class CommonAsm;
class DebugMap;
class DebugScope;
class MacroAssembler;
class RegMap;

// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

// CodeBlobs live in the CodeCache.  They are "owned" by a standard GC'able
// oop.  The CodeBlob lifetime is controlled by the oop, and will be deleted
// as part of reclaiming the owning blob.  A nullRef _owner CodeBlob has to be
// manually deleted.  The actual code follows this structure in memory.  Other
// code control bits, such as a list of active KID's or OID's or inline-cache
// locations are all kept in the owning oop.
class CodeBlob:public CHeapObj{
  methodCodeRef _owner;         // Owning methodCode
  friend class DebugMap;
  
  // OopMaps - I tried hard to not have any non-code junk in CodeBlobs
  // but I can't get past startup without keeping the OopMaps here.
  // Turns out the initial stubs need some OopMaps - and Universe
  // isn't ready to allocate a MethodCodeOop to store them.
  const RegMap * _oop_maps;     // Compressed oop-map
  friend class OopMapStream;    // Only OopMapStream will read this field
  friend class CommonAsm;       // Only CommonAsm    will set  this field
  friend class methodCodeOopDesc;  // Only methodCodeOopDesc will set this field

  // Blobs are this small struct, plus a lot of code that follows them in
  // memory.  Blobs live in the CodeCache, which limits them to the low 32-bit
  // address space (so all PC's are 32 bits).  For this implementation, Blobs
  // are also OS-page aligned and thus OS-page sized.
  int _size_in_bytes;           // Size of this blob

  // Length in code, from _code_begins onward
  int _code_len;

  // Frame size in bytes.  Only set by CommonAsm during the baking process.
  unsigned short _framesize_bytes;
  
public:
  unsigned short _gdb_idx;      // index into GDB info

  // Up to the 64K last bytes (from _code_len backwards) are actually
  // not really code but data.
  unsigned short _non_code_tail;

  // Blob is one of:
  // - i2c/c2i adapter; used to marshal arguments between the interpreter and compiled code
  // - methodStubBlob; used to quickly map a PC to a methodOop (part of the c2i call path)
  // - native wrapper; marshal arguments for a native call
  // - milli-code/stub routines/trampolines
  // - interpreter
  // - C1 code
  // - C2 code
  enum BlobType {
    runtime_stubs,              // bulk runtime stubs (stubGenerator_xxx.cpp et al)
    c2i_adapter,                // re-layout compiled args in interpreted format
    methodstub,                 // used to quickly map a PC to a methodOop (part of the c2i call path)
native,//native method wrapper
    interpreter,                // The whole interpreter
    c1,                         // C1 compiled code
    c2,                         // C2 compiled code
    monitors,                   // bulk allocation of monitors in low memory
    vtablestub,                 // vtable stubs
    vm_version,
    bad
  };
private:
  unsigned int _type:4;

  // Code is sometimes aligned and padded up front
  unsigned char _code_start_offset;
  friend class MethodStubBlob;  // Set by commonAsm and MethodStubBlob

  // Code begins here
  u_char _code_begins[0];   // zero-length array at end of struct
  
  // Create a new Blob with minimum size
  friend class CodeCache;       // Allow the CodeCache to create
  CodeBlob( BlobType type, int size_in_bytes ) : _owner(), _oop_maps(0), _size_in_bytes(size_in_bytes), _code_len(0), _framesize_bytes(0), _non_code_tail(0), _type(type), _code_start_offset(0) { debug_only(verify()); }
  void* operator new(size_t size, char *adr) { return adr; }

public:
  // Dummy blob used to indicate an empty Assembler
  CodeBlob(float dummy) : _owner(), _size_in_bytes(sizeof(CodeBlob)), _type(bad) { }

  // GC Support
  // No oops directly in the code; those are all GC'd via the
  // CodeCacheOopTable (and referenced indirectly via a list of oop-table
  // indices.)  This oops_do is just to get the back-ptr to the owning oop.
  void oops_do(OopClosure* f);
  const RegMap *oop_maps() const { return _oop_maps; }

  // oops-do just for arguments when args are passed but the caller is
  // not yet claiming responsibility for them (e.g., mid-call resolution).
  void oops_arguments_do(frame fr, OopClosure* f) const;
  void oops_arguments_do_impl(frame fr, OopClosure* f) const;

  // Count times an oop is locked in this frame & codeblob
  int locked( frame fr, oop o ) const;

  // Fast accessors
  inline bool contains( void *p ) const { return this <= p && p <= end(); }
  inline bool has_oopmaps() const { return _oop_maps != NULL; }
  inline int  rel_pc( address p ) const { return p - (address)this; }

  inline int  framesize_bytes() const  { return _framesize_bytes; }
  inline bool is_native_method() const { return _type==native; }
  inline bool is_java_method()   const { return _type==c1 || _type==c2; }
  inline bool is_methodCode()    const { return _type==c1 || _type==c2 || _type==native; }
  inline bool is_c1_method()     const { return _type==c1; }
  inline bool is_c2_method()     const { return _type==c2; }
  inline bool is_c2i()           const { return _type==c2i_adapter; }
  inline bool is_runtime_stub()  const { return _type==runtime_stubs; }
  inline bool is_method_stub()   const { return _type==methodstub; }
  inline bool is_vtable_stub()   const { return _type==vtablestub; }
  inline bool is_interpreter()   const { return _type==interpreter; }

  // Absolute utter end of where code might lie in the blob.
  inline address end() const { return (address)this + _size_in_bytes; }
  inline address code_begins() const { return (address)_code_begins+_code_start_offset; }
  inline address code_ends() const { return code_begins()+_code_len; }
  inline int code_size() const { return _code_len; }
  inline void set_code_len( int cl ) { _code_len = cl; }

  // Vtables chain blobs together, helper to get next blob
  CodeBlob *next_vtable_blob()              { assert0( is_vtable_stub() ); return *((CodeBlob**)code_begins()); }
  void set_next_vtable_blob(CodeBlob *next) { assert0( is_vtable_stub() ); *((CodeBlob**)code_begins()) = next; }

  methodCodeRef owner() const { return lvb_methodCodeRef(&_owner); }
  void set_owner( methodCodeRef mcref ) { ref_store_without_check(&_owner,mcref); }
heapRef*adr_owner()const{return(heapRef*)&_owner;}

  methodRef method() const { return owner().as_methodCodeOop()->method(); }
  const DebugScope *debuginfo(address pc) const;
  const DebugMap *debuginfo() const;
  void clean_inline_caches() const;

  // "Promote" a C1 CodeBlob - make it jump to a suitable C2 method
  void promote();

  // Not-Entrant: Prevent future access (as much as possible).
  // Patch the 1st op with a 'jmp not_entrant'.
  // Can be done with running threads via an atomic patch (I think).
  void make_not_entrant() const;
  bool is_entrant() const;      // still legal to call?

  // Slap the code with some no-op equivalent.  Any thread returning to the
  // function will execute only no-ops until it falls off the end and hits the
  // deopt-sled.  Must be done at a full Safepoint.
  void pd_patch_for_deopt() const;

  // Printing and debugging
  void print_xml_on(xmlBuffer* xb, bool ref, intptr_t tag) const;
  inline void print_xml_on(xmlBuffer* xb, bool ref) const { print_xml_on(xb,ref,0); }
  void print_on( outputStream * ) const;
  void print() const;
  const char *name() const;
  const char *methodname() const; // fancy name only for methods.  Caller frees storage.
  void decode() const;          // disassemble to tty
  void print_oopmap_at (address pc, outputStream *st) const; // print oopmap, if any
  void print_debug_at  (address pc, outputStream *st) const; // print debug info, if any
  void print_npe_at    (address pc, outputStream *st) const; // print npe info, if any
void print_dependencies(outputStream*st)const;//Print compilation dependencies, if any
  void verify() const;
  uint gdb_idx() const { return _gdb_idx; } // index into GDB info
};

// ----
// Return an iterator over all the registers holding oops at this pc.
class OopMapStream:StackObj{
public:
  // Convenience: apply closure to all oops
  static void do_closure( CodeBlob *cb, frame fr, OopClosure *f );
};

// ---
class DerivedPointerTable:AllStatic{
private:
  static bool _active;  // do not record pointers for verify pass etc.
  static GrowableArray<intptr_t> *_base_derived_pairs;
public:  
  static void clear();          // Called before scavenge/GC
  static void add(objectRef *derived, objectRef *base);  // Called during scavenge/GC
  static void update_pointers(); // Called after  scavenge/GC  
  static bool is_empty();
  static bool is_active()            { return _active; }
  static void set_active(bool value) { _active = value; }
};


// ----

class NativeMethodStub; // forward declaration

class MethodStubBlob:AllStatic{
  static NativeMethodStub* _free_list; // list of free stubs
  static void free_stub( NativeMethodStub* stub );
 public:
  // Creation
  static address generate( heapRef moop, address c2i_adapter );
  static void oops_do( address from, address to, OopClosure *f );
  static void unlink( address from, address to, BoolObjectClosure* is_alive );
  static void GPGC_unlink( address from, address to );
};

// ---
// Create a CodeBlob with machine-specific contents.
void pd_create_vtable_stub(MacroAssembler *masm, int vtable_index);
void pd_create_itable_stub(MacroAssembler *masm, int vtable_index);
// Offsets inside of any vtable CodeBlob where hardware NPEs are
// possible and expected.
extern int vtable_npe_offset;
extern int itable_npe_offset;
extern int vtable_ame_offset;
extern int itable_ame_offset;
extern int itable_ame_offset0;
extern int itable_ame_offsetB;

#endif // CODEBLOB_HPP
