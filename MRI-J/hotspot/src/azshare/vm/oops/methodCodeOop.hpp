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
#ifndef METHODCODEOOP_HPP
#define METHODCODEOOP_HPP


#include "oop.hpp"
#include "refsHierarchy_pd.hpp"

class CodeBlob;
class CodeProfile;
class DebugMap;
class PC2PCMap;
class RegMap;
 
class methodCodeOopDesc:public oopDesc{
private: // -------
methodRef _method;//Back pointer to the owning methodOop
public:
  methodRef method() const     { return lvb_methodRef(&_method); }
  void set_method(methodRef m) { ref_store(this,&_method, m); }
heapRef*adr_method()const{return(heapRef*)&_method;}
 
private: // -------
  methodCodeRef _next;          // These things are chained on a list.
public:
  methodCodeRef next() const       { return lvb_methodCodeRef(&_next); }
  void set_next(methodCodeRef mcr) { ref_store(this,&_next, mcr); }
heapRef*adr_next()const{return(heapRef*)&_next;}

private: // -------
  // List of all refs (in)directly referenced by the CodeBlob
objArrayRef _static_refs;
public:
  objArrayRef static_refs() const     { return lvb_objArrayRef(&_static_refs); }
  void set_static_refs(objArrayRef refs) { ref_store(this,&_static_refs, refs); }
heapRef*adr_static_refs()const{return(heapRef*)&_static_refs;}
  void add_static_ref(int kid, TRAPS);

private: // -------
  // Every call site in this blob is directly patched to refer to some other
  // blob.  That target blob needs to stay alive until the call site is
  // patched to somewhere else (usually patched to resolve_and_patch).  Call
  // sites are tracked by the debug info which holds a fixed-sized power-of-2
  // hashtable mapping relpc's to debug info.  This array is a mirror of that
  // hashtable - the same relpcs map here to the target blob's methodCodeOop.
  objArrayRef _mco_call_targets; // Array of all MCOs this blob calls
public:
  objArrayRef mco_call_targets() const { return lvb_objArrayRef(&_mco_call_targets); }
  void set_mco_call_targets(objectRef mcr) { ref_store(this,&_mco_call_targets, mcr); }
heapRef*adr_mco_call_targets()const{return(heapRef*)&_mco_call_targets;}

private: // -------
  objArrayRef _dep_klasses; // Array of all instanceKlasses this blob depends on
  objArrayRef _dep_methods; // Array of all methodOops      this blob depends on
public:
  objArrayRef dep_klasses() const     { return lvb_objArrayRef(&_dep_klasses); }
  objArrayRef dep_methods() const     { return lvb_objArrayRef(&_dep_methods); }
  void set_dep_klasses(objectRef mcr) { ref_store(this,&_dep_klasses, mcr); }
  void set_dep_methods(objectRef mcr) { ref_store(this,&_dep_methods, mcr); }
heapRef*adr_dep_klasses()const{return(heapRef*)&_dep_klasses;}
heapRef*adr_dep_methods()const{return(heapRef*)&_dep_methods;}
#ifndef PRODUCT
  void verify_dependencies( const instanceKlass *ik ) const;
#endif
void print_dependencies(outputStream*st)const;
  // A pair of sentinel methodRefs used to represent special dependencies
  static methodRef zeroImplementors();
  static methodRef noFinalizers();

  // -------
  const DebugMap *const _debuginfo;// Compressed debuginfo

  const PC2PCMap *const _NPEinfo; // Compressed PC-to-PC mappings

  const CodeBlob *const _blob;  // The blob
  heapRef *adr_blob_owner() const;
 
  CodeProfile *const _profile; // CodeProfile is fixed for all time
  CodeProfile *get_codeprofile() const { return _profile; }

  // -1 for no generic handler, or the rel_pc of a generic exception handler
  int _generic_exception_handler_rel_pc; 

  // For compiled blobs, this is the compilation number
  const int _compile_id;

  // For normal blobs, this is InvocationEntryBci.
  // For OSR compiles, this is the BCI at entry.
  const int _entry_bci;
  bool is_osr_method() const { return _entry_bci != InvocationEntryBci; }

  // Start of the deoptimization sled
  int _deopt_sled_relpc;

  // Has/does "Unsafe" access - means that if an un-expected hardware NPE is
  // thrown in this method we'll convert that to a j.l.NPE instead of crashing.
  const bool _has_unsafe;

  // Collection of i/vtable-stubs for this method.  These are cloned per-method
  // in an effort to let hardware BTB's have unique addresses to predict from.
  // We store a bunch of them in each CodeBlob, that are chained.
  // For most of the times, we will never have to create a second CodeBlob
  // and be chained with the previous. However, need chaining incase there are
  // too many Megamorphic calls in a method.
  // Chaining Mechanism: We lazily create and then chain a new CodeBlob
  // if the space left behind is less than Max-ivTableStub size. In that case
  // first word of the possibly next ivTableStub infact contains the next
  // chaining code_begins address of the next CodeBlob.
  // Ref: See get_vtable_stub_impl() to see this mechanism implemented.
private:
  CodeBlob *_vtables; // CodeBlob containing only vtables, possibly chained (see CodeBlob::next_vtable_blob) 
  address _vtable_pc; // Ptr to the next available area in the same vtable
public:
  inline address get_vtable_stub( int idx ) { return get_vtable_stub_impl(idx,false); }
  inline address get_itable_stub( int idx ) { return get_vtable_stub_impl(idx,true ); }
  address get_vtable_stub_impl( int idx, bool is_itable );
  CodeBlob *first_vtable_blob() const { return _vtables; }

  // Patch this call site to the target pc.  Update the parallel
  // _mco_call_targets array.  This is the only correct way to patch call
  // sites, lest the mco_call_targets not faithfully indicate which target
  // blobs need to stay alive.
  void patched_a_call_to( address after_call_pc, address target_pc ) const;

  // size operations
static int header_size(){return sizeof(methodCodeOopDesc)/HeapWordSize;}
static int object_size(){return header_size();}

  // jvmti support:
  void post_compiled_method_load_event();

  void print_value_on( outputStream *st ) const;
  void print_xml_on( xmlBuffer *xb, bool ref ) const;

  // -------
  // Deoptimization support

  // Make this method Not-Entrant: no future callers but existing callers are fine.
  void not_entrant();

  // Make this method Not-Entrant.  Fill the method with deopt groo, so that
  // existing callers will fall through to the deopt-sled at the end without
  // executing any more instructions in this method.
  void deoptimize    (int/*Deoptimization::DeoptInfo*/ cause);
  // Schedule a deoptimization... then do it.  Blocks until it happens.
  void deoptimize_now(int/*Deoptimization::DeoptInfo*/ cause);

public:
  bool _patched_for_deopt;
  static const CodeBlob *_deopt_list[512];
static uint _deopt_list_len;
  static void remove_blob_from_deopt(CodeBlob*);
};

#endif // METHODCODEOOP_HPP
