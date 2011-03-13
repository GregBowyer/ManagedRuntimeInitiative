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
#ifndef METHODOOP_HPP
#define METHODOOP_HPP

#include "oop.hpp"
#include "bytecodes.hpp"
#include "constMethodOop.hpp"
#include "constantPoolOop.hpp"
#include "instanceKlass.hpp"
#include "vmSymbols.hpp"
#include "invocationCounter.hpp"


// A methodOop represents a Java method. 
//
// Memory layout (each line represents a word). Note that most applications load thousands of methods,
// so keeping the size of this structure small has a big impact on footprint.
//
// We put all oops and method_size first for better gc cache locality.
//
// The actual bytecodes are inlined after the end of the methodOopDesc struct.
//
// There are bits in the access_flags telling whether inlined tables are present.
// Note that accessing the line number and local variable tables is not performance critical at all.
// Accessing the checked exceptions table is used by reflection, so we put that last to make access
// to it fast.
//
// The line number table is compressed and inlined following the byte codes. It is found as the first 
// byte following the byte codes. The checked exceptions table and the local variable table are inlined 
// after the line number table, and indexed from the end of the method. We do not compress the checked
// exceptions table since the average length is less than 2, and do not bother to compress the local 
// variable table either since it is mostly absent.
//
// Note that native_function and signature_handler has to be at fixed offsets
// (required by the interpreter)
//
// |------------------------------------------------------|
// | header                                               |
// | klass                                                |
// |------------------------------------------------------|
// | constMethodOop                 (oop)                 |
// | constants                      (oop)                 |
// |------------------------------------------------------|
// | access_flags                                         |
// | vtable_index                                         |
// |------------------------------------------------------|
// | result_index (C++ interpreter only)                  |
// |------------------------------------------------------|
// | method_size             | max_stack                  |
// | max_locals              | size_of_parameters         |
// |------------------------------------------------------|
// | throwout_count          | num_breakpoints            |
// |------------------------------------------------------|
// | invocation_counter                                   |
// | backedge_counter                                     |
// |------------------------------------------------------|
// | code                           (pointer)             |
// | i2i                            (pointer)             |
// | adapter                        (pointer)             |
// | from_compiled_entry            (pointer)             |
// | from_interpreted_entry         (pointer)             |
// |------------------------------------------------------|
// | native_function       (present only if native)       |
// | signature_handler     (present only if native)       |
// |------------------------------------------------------|


class CheckedExceptionElement;
class LocalVariableTableElement;
class BCI2CPD_mapping;
class CodeProfile;

class methodOopDesc : public oopDesc {
 friend class methodKlass;
 private:
  enum {
    methodId_type_bits   = 2,
    methodId_index_bits  = 11,
    methodId_kid_bits    = 19
  };
  
  enum {
    methodId_type_shift   = 0,
    methodId_index_shift  = methodId_type_shift + methodId_type_bits,
    methodId_kid_shift    = methodId_index_shift + methodId_index_bits
  };
  
  enum {
    methodId_type_mask   = right_n_bits(methodId_type_bits),
    methodId_index_mask  = right_n_bits(methodId_index_bits),
    methodId_kid_mask    = right_n_bits(methodId_kid_bits)
  };
  
  enum {
    methodId_invalid_id = -1,
    methodId_type = 1
  };
  
  constMethodRef    _constMethod;                // Method read-only data.
  constantPoolRef   _constants;                  // Constant pool
  address           _from_interpreted_entry;     // Cache of _code ? _i2c_entry : _i2i_entry
  AccessFlags       _access_flags;               // Access flags
  int               _objectId;                   // Plain self Object ID, no encodings.
  int               _method_id;                  // Method ID for profiling and ARTA. Not all possible methods can be encoded
                                                 //  in this format: (19 bits of kid | 11 bits of method index | 01)
  int               _vtable_index;               // vtable index of this method (see VtableIndexFlag)
                                                 // note: can have vtables with >2**16 elements (because of inheritance)
  u2                _method_size;                // size of this object
  u2                _max_stack;                  // Maximum number of entries on the expression stack
  u2                _max_locals;                 // Number of local variables used by this method
  u2                _size_of_parameters;         // size of the parameter block (receiver + arguments) in words
  u1                _intrinsic_id_cache;         // Cache for intrinsic_id; 0 or 1+vmInt::ID
  u2                _number_of_breakpoints;      // fullspeed debugging support
  InvocationCounter _invocation_counter;         // Incremented before each activation of the method - used to trigger frequency-based optimizations
  InvocationCounter _backedge_counter;           // Incremented before each backedge taken - used to trigger frequencey-based optimizations

  // ----
  // Linked list of active methodCodes for this method; native wrappers, C1,
  // C2, & OSR methodCodes are all here.  Unlike the _codeOop field below, this
  // field is not read by racing Java executor threads in generated
  // trampolines.  The list is updated via non-blocking CAS operations.
  // Note that this list must exist even in a CORE build, because native
  // methods have generated wrappers.
  methodCodeRef volatile _codeRef_list;

  // Location (if any) of native function address in a native wrapper (once created)
address native_function_addr_addr;

public:
  void insert_methodCode(methodCodeRef);
  void remove_methodCode(methodCodeRef);
  void unlink_methodCode( heapRef base, heapRef *prev, methodCodeOop mco );
  bool set_methodCode(methodCodeRef); // Only sets a methodCodeRef if the list is empty
  methodCodeRef codeRef_list_head() const { return lvb_methodCodeRef((methodCodeRef*)&_codeRef_list); }
heapRef*adr_codeRef_list(){return(heapRef*)&_codeRef_list;}

  // wrappers to manipulate the stored native function address in the native wrapper
  void store_native_function_address_address_in_wrapper( address addr ) { native_function_addr_addr = addr; }
  void overwrite_native_function_address_in_wrapper( int64_t new_addr ) { intptr_t old_addr = *(intptr_t *)native_function_addr_addr; Atomic::cmpxchg_ptr( new_addr, (intptr_t *)native_function_addr_addr, old_addr); }
private:

  // The entry point for calling from compiled code to compiled code is
  // "_code->entry_point()".  Because of tiered compilation and de-opt, this
  // field can change semi-randomly to point at different methodCodeOops.
  // This field is read by racing Java threads in many situations, e.g.
  // transiting from interpreted to compiled code or patching a clean
  // inline-cache site to compiled code.
  methodCodeRef _codeRef;
public:
  methodCodeRef codeRef() const { return lvb_methodCodeRef(&_codeRef); }
  void set_codeRef( methodCodeRef mcr ); // set as the active code a selection from the _codeRef_list
heapRef*adr_codeRef(){return(heapRef*)&_codeRef;}
  void clear_codeRef();                  // Remove the current _codeRef_list selection
void make_native_wrapper_if_needed(TRAPS);
private:
  void set_code_helper(methodCodeRef code, address from_compiled_entry, address from_interpreted_entry);
  void make_i2c_adapters_if_needed();
address make_c2i_adapters_if_needed();

  // Entry point for calling both from and to the interpreter.
  address _i2i_entry;		// All-args-on-stack calling convention
  // Entry point for calling from the interpreter and jumping to compiled
  // code.  Will die if no compiled code.  Set once when method is linked.
  address _i2c_entry;		// load c2c_entry from methodOop and jump to shared I2C code

  // Entry point for calling from compiled code and jumping to the
  // interpreter.  Cannot be called if no compiled code.  Set once (transits
  // from NULL to not-NULL) first time from_compiled_entry() is called.
  address _c2i_entry;           // set methodOop and jump to shared C2I code (which jumps to _i2i_entry)
  // Entry point for calling from compiled code, to compiled code if it exists
  // or else the interpreter via a c2i adapter.  c2i adapters can be lazy, ie
  // this might be a sentinel indicating that I need to build a c2i adapter.
  address _from_compiled_entry;	// Cache of: _code ? _code->entry_point() : _c2i_entry

  // Lazily built C heap compiler data structures.  It monotonically moves
  // from NULL to a non-moving BCI2CPD_mapping when a compiler expresses
  // interest (by asking for the mapping).
  BCI2CPD_mapping *_bci2cpd_mapping;
  BCI2CPD_mapping *bci2cpd_impl(); // Not inlined to avoid cycles in header
  // Lazily built C heap compiler data structures used when no methodCodes are
  // created yet.  It monotonically moves from NULL to a non-moving CodeProfile 
  // when a compiler expresses interest (by compiling-for-profiling).  Once an
  // methodCode is added to the moop, future cp updates will be applied to the
  // cp hung under the methodCode.
  CodeProfile *_codeprofile;
public:
  // Get C heap compiler structs as needed
  BCI2CPD_mapping *bci2cpd_map() { return _bci2cpd_mapping ? _bci2cpd_mapping : bci2cpd_impl(); }
  BCI2CPD_mapping *has_bci2cpd_map() const { return _bci2cpd_mapping; }
  // Find out any codeprofile accessible from the moop and don't care about
  // the origin. Search c1 methodCodes first. If not found then try c2 methodCodes,
  // then finally the moop itself. 
  CodeProfile *codeprofile(bool clone);
  // Attach codeprofile to the moop
  CodeProfile *set_codeprofile( CodeProfile *cp, CodeProfile *expected );

  // Return the number of various counters
  int get_codeprofile_count(int kind);
  // Incriment the number of various counters
  void incr_codeprofile_count(int kind);
  // Return the code profile size
  int get_codeprofile_size();
  // Return whether this method has code profile - do a quick check without 
  // acquiring the OsrList_lock
  bool has_codeprofile();

  void free_profiling_structures();
private:
 public:
  void link_method();           // Called once after bytecodes are verified
address from_compiled_entry();
  address from_interpreted_entry() const { return _from_interpreted_entry; }
  // accessors for instance variables
constMethodOop constMethod()const{return lvb_constMethodRef(&_constMethod).as_constMethodOop();}
void set_constMethod(constMethodOop xconst){ref_store_without_check(&_constMethod,(constMethodRef)xconst);}

  // access flag
  AccessFlags access_flags() const               { return _access_flags;  }
  void set_access_flags(AccessFlags flags)       { _access_flags = flags; }

  // name
symbolOop name()const{return constants()->symbol_at(name_index());}
  int name_index() const                         { return constMethod()->name_index();         }
  void set_name_index(int index)                 { constMethod()->set_name_index(index);       }

  // signature
symbolOop signature()const{return constants()->symbol_at(signature_index());}
  int signature_index() const                    { return constMethod()->signature_index();         }
  void set_signature_index(int index)            { constMethod()->set_signature_index(index);       }

  // generics support
symbolOop generic_signature()const{int idx=generic_signature_index();return((idx!=0)?constants()->symbol_at(idx):(symbolOop)NULL);}
  int generic_signature_index() const            { return constMethod()->generic_signature_index(); }
  void set_generic_signature_index(int index)    { constMethod()->set_generic_signature_index(index); }

  // annotations support
  typeArrayOop annotations() const               { return instanceKlass::cast(method_holder())->get_method_annotations_of(method_idnum()); }
  typeArrayOop parameter_annotations() const     { return instanceKlass::cast(method_holder())->get_method_parameter_annotations_of(method_idnum()); }
  typeArrayOop annotation_default() const        { return instanceKlass::cast(method_holder())->get_method_default_annotations_of(method_idnum()); }


  // Helper routine: get klass name + "." + method name + an optional
  // signature as C string, for the purpose of providing more useful
  // NoSuchMethodErrors and fatal error handling. The string is
  // allocated in resource area if a buffer is not provided by the caller.
  char* name_as_C_string();
char*name_as_C_string(char*buf,int size);
  char* name_and_sig_as_C_string() const;
char*name_and_sig_as_C_string(char*buf,int size)const;

  // Static routine in the situations we don't have a methodOop
  static char* name_as_C_string(Klass* klass, symbolOop method_name);
  static char* name_as_C_string(Klass* klass, symbolOop method_name, char* buf, int size);
  static char* name_and_sig_as_C_string(Klass* klass, symbolOop method_name, symbolOop signature);
  static char* name_and_sig_as_C_string(Klass* klass, symbolOop method_name, symbolOop signature, char* buf, int size);
  // object Id
  int objectId();

  // JVMTI breakpoints
  Bytecodes::Code orig_bytecode_at(int bci);
  void        set_orig_bytecode_at(int bci, Bytecodes::Code code);
  void set_breakpoint(int bci);
  void clear_breakpoint(int bci);
  void clear_all_breakpoints();
  // Tracking number of breakpoints, for fullspeed debugging.
  // Only mutated by VM thread.
  u2   number_of_breakpoints() const             { return _number_of_breakpoints; }
  void incr_number_of_breakpoints()              { ++_number_of_breakpoints; }
  void decr_number_of_breakpoints()              { --_number_of_breakpoints; }
  // Initialization only
  void clear_number_of_breakpoints()             { _number_of_breakpoints = 0; }

  // index into instanceKlass methods() array
  u2 method_idnum() const           { return constMethod()->method_idnum(); }
  void set_method_idnum(u2 idnum)   { constMethod()->set_method_idnum(idnum); }

  // code size
  int code_size() const                  { return constMethod()->code_size(); }

  // method size
  int method_size() const                        { return _method_size; }
  void set_method_size(int size) {
    assert(0 <= size && size < (1 << 16), "invalid method size");
    _method_size = size;
  }

  // constant pool for klassOop holding this method
constantPoolOop constants()const{return lvb_constantPoolRef(&_constants).as_constantPoolOop();}
constantPoolRef constantsRef()const{return lvb_constantPoolRef(&_constants);}
  void set_constants(constantPoolOop c)          { ref_store_without_check(&_constants, constantPoolRef(c)); }

  // max stack
  int  max_stack() const                         { return _max_stack; }
  void set_max_stack(int size)                   { _max_stack = size; }

  // max locals
  int  max_locals() const                        { return _max_locals; }
  void set_max_locals(int size)                  { _max_locals = size; }

  void clear_intrinsic_id_cache() { _intrinsic_id_cache = 0; }
  // size of parameters
  int  size_of_parameters() const                { return _size_of_parameters; }

  bool has_stackmap_table() const { 
    return constMethod()->has_stackmap_table(); 
  }
  typeArrayOop stackmap_data() const { 
    return constMethod()->stackmap_data();
  }

  // exception handler table
  typeArrayOop exception_table() const
                                   { return constMethod()->exception_table(); }
  void set_exception_table(typeArrayOop e)
                                     { constMethod()->set_exception_table(e); }
  bool has_exception_handler() const
                             { return constMethod()->has_exception_handler(); }

  // Finds the first entry point bci of an exception handler for an
  // exception of klass ex_klass thrown at throw_bci. A value of NULL
  // for ex_klass indicates that the exception klass is not known; in
  // this case it matches any constraint class. Returns -1 if the
  // exception cannot be handled in this method. The handler
  // constraint classes are loaded if necessary. Note that this may 
  // throw an exception if loading of the constraint classes causes
  // an IllegalAccessError (bugid 4307310) or an OutOfMemoryError. 
  // If an exception is thrown, returns the bci of the
  // exception handler which caused the exception to be thrown, which
  // is needed for proper retries. See, for example,
  // InterpreterRuntime::exception_handler_for_exception.
  int fast_exception_handler_bci_for(KlassHandle ex_klass, int throw_bci, TRAPS);

  // invocation counter
  InvocationCounter* invocation_counter()        { return &_invocation_counter; }
  InvocationCounter* backedge_counter()          { return &_backedge_counter; }
  int invocation_count() const                   { return _invocation_counter.count(); }
  int backedge_count() const                     { return _backedge_counter.count(); }
  bool was_executed_more_than(int n) const;
  bool was_never_executed() const                { return !was_executed_more_than(0); }

  // Clear (non-shared space) pointers which could not be relevent
  // if this (shared) method were mapped into another JVM.
  void remove_unshareable_info();

  // Compiled code access.
  u1 _compilable;               // Set should not attempt compile; 0/1 for C1, 0/2 for C2
  bool is_c1_compilable() const { return (_compilable&1)==0; }
  bool is_c2_compilable() const { return (_compilable&2)==0; }
  void set_not_compilable(int c12) { _compilable|=c12; }
  methodCodeOop lookup_c1() const; // A C1 methodCode or NULL
  methodCodeOop lookup_c2() const; // A C2 methodCode or NULL
  methodCodeOop lookup_osr_for(int bci) const; // A C2 methodCodeOop at this BCI or NULL
  methodCodeRef lookup_c1_ref() const; // A C1 methodCode or nullRef
  
  // methodID
  int method_id() const                           { return _method_id; }
void set_method_id(int index);
  static methodOop from_method_id(int id);

  // vtable index
  enum VtableIndexFlag {
    // Valid vtable indexes are non-negative (>= 0).
    // These few negative values are used as sentinels.
    invalid_vtable_index    = -4,  // distinct from any valid vtable index
    garbage_vtable_index    = -3,  // not yet linked; no vtable layout yet
    nonvirtual_vtable_index = -2   // there is no need for vtable dispatch
    // 6330203 Note:  Do not use -1, which was overloaded with many meanings.
  };
  DEBUG_ONLY(bool valid_vtable_index() const     { return _vtable_index >= nonvirtual_vtable_index; })
  int  vtable_index() const                      { assert(valid_vtable_index(), "");
                                                   return _vtable_index; }
  void set_vtable_index(int index)               { _vtable_index = index; }

  // interpreter entry
  address interpreter_entry() const              { return _i2i_entry; }
void set_interpreter_entry(address entry){_i2i_entry=entry;}
  int  interpreter_kind(void) {
     return constMethod()->interpreter_kind();
  }
  void set_interpreter_kind();
  void set_interpreter_kind(int kind) {
    constMethod()->set_interpreter_kind(kind);
  }

  // native function (used for native methods only)
  enum {
    native_bind_event_is_interesting = true
  };
  address native_function() const                { return *(native_function_addr()); }
  // Must specify a real function (not NULL).
  // Use clear_native_function() to unregister.
  void set_native_function(address function, bool post_event_flag);
  bool has_native_function() const;
  void clear_native_function();
  void set_is_remote_method()                    { _access_flags.set_is_remote_method(); }

#ifndef PRODUCT
  // operations on invocation counter
  void print_invocation_count() const;
#endif

  // byte codes
  address code_base() const           { return constMethod()->code_base(); }
  bool    contains(address bcp) const { return constMethod()->contains(bcp); }

  void print_codes(outputStream *out, CodeProfile *cp ) const; // prints byte codes
void print_codes(int from,int to,outputStream*out)const PRODUCT_RETURN;

  // checked exceptions
  int checked_exceptions_length() const
                         { return constMethod()->checked_exceptions_length(); }
  CheckedExceptionElement* checked_exceptions_start() const
                          { return constMethod()->checked_exceptions_start(); }

  // localvariable table
  bool has_localvariable_table() const
                          { return constMethod()->has_localvariable_table(); }
  int localvariable_table_length() const
                        { return constMethod()->localvariable_table_length(); }
  LocalVariableTableElement* localvariable_table_start() const
                         { return constMethod()->localvariable_table_start(); }
  const char* localvariable_name(int slot, int bci, bool &out_of_scope) const;

  bool has_linenumber_table() const
                              { return constMethod()->has_linenumber_table(); }
  u_char* compressed_linenumber_table() const
                       { return constMethod()->compressed_linenumber_table(); }

  // method holder (the klassOop holding this method)
klassOop method_holder()const{return constants()->pool_holder();}

  void compute_size_of_parameters(Thread *thread); // word size of parameters (receiver if any + arguments)
  symbolOop klass_name() const;                  // returns the name of the method holder
  BasicType result_type() const;                 // type of the method result
  bool is_returning_oop() const                  { BasicType r = result_type(); return (r == T_OBJECT || r == T_ARRAY); }
  bool is_returning_fp() const                   { BasicType r = result_type(); return (r == T_FLOAT || r == T_DOUBLE); }

  // Checked exceptions thrown by this method (resolved to mirrors)
objArrayHandle resolved_checked_exceptions(intptr_t sba_hint,TRAPS){return resolved_checked_exceptions_impl(this,sba_hint,THREAD);}

  // Access flags
  bool is_public() const                         { return access_flags().is_public();      }
  bool is_private() const                        { return access_flags().is_private();     }
  bool is_protected() const                      { return access_flags().is_protected();   }
  bool is_package_private() const                { return !is_public() && !is_private() && !is_protected(); }
  bool is_static() const                         { return access_flags().is_static();      }
  bool is_final() const                          { return access_flags().is_final();       }
  bool is_synchronized() const                   { return access_flags().is_synchronized();}
  bool is_native() const                         { return access_flags().is_native();      }
  bool is_abstract() const                       { return access_flags().is_abstract();    }
  bool is_strict() const                         { return access_flags().is_strict();      }
  bool is_synthetic() const                      { return access_flags().is_synthetic();   }
  bool is_remote() const                         { return access_flags().is_remote_method(); }
  
  // returns true if contains only return operation
  bool is_empty_method() const;

  // returns true if this is a vanilla constructor
  bool is_vanilla_constructor() const;

  // checks method and its method holder
  bool is_final_method() const;
  bool is_strict_method() const;

  // returns true if the method has any backward branches.
  bool has_loops() { 
    return access_flags().loops_flag_init() ? access_flags().has_loops() : compute_has_loops_flag(); 
  };

  bool compute_has_loops_flag();
  
  bool has_jsrs() { 
    return access_flags().has_jsrs();
  };
  void set_has_jsrs() {
    _access_flags.set_has_jsrs();
  }

  // returns true if the method has any monitors.
  bool has_monitors() const                      { return is_synchronized() || access_flags().has_monitor_bytecodes(); } 
  bool has_monitor_bytecodes() const             { return access_flags().has_monitor_bytecodes(); }
  
  void set_has_monitor_bytecodes()               { _access_flags.set_has_monitor_bytecodes(); }

  // monitor matching. This returns a conservative estimate of whether the monitorenter/monitorexit bytecodes
  // propererly nest in the method. It might return false, even though they actually nest properly, since the info.
  // has not been computed yet.
  bool guaranteed_monitor_matching() const       { return access_flags().is_monitor_matching(); }
  void set_guaranteed_monitor_matching()         { _access_flags.set_monitor_matching(); }

  // true if method needs no dynamic dispatch (final and/or no vtable entry)
  bool can_be_statically_bound() const;

  // returns true if the method is an accessor function (setter/getter).
  bool is_accessor() const;

  // returns true if the method is an initializer (<init> or <clinit>).
  bool is_initializer() const;

  // compiled code support.
  // NOTE: code() is inherently racy as deopt can be clearing code
  // simultaneously. Use with caution.
  bool has_compiled_code() const                 { return _codeRef.not_null(); }

  // sizing
  static int object_size(bool is_native);
  static int header_size()                       { return sizeof(methodOopDesc)/HeapWordSize; }
  int object_size() const                        { return method_size(); }

  bool object_is_parsable() const                { return method_size() > 0; }

  // interpreter support
static ByteSize oid_offset(){return byte_offset_of(methodOopDesc,_objectId);}
static ByteSize codeRef_offset(){return byte_offset_of(methodOopDesc,_codeRef);}
  static ByteSize const_offset()                 { return byte_offset_of(methodOopDesc, _constMethod       ); }
  static ByteSize constants_offset()             { return byte_offset_of(methodOopDesc, _constants         ); }
  static ByteSize access_flags_offset()          { return byte_offset_of(methodOopDesc, _access_flags      ); }
  static ByteSize size_of_locals_offset()        { return byte_offset_of(methodOopDesc, _max_locals        ); }
  static ByteSize size_of_parameters_offset()    { return byte_offset_of(methodOopDesc, _size_of_parameters); }
  static ByteSize from_compiled_offset()         { return byte_offset_of(methodOopDesc, _from_compiled_entry);}  
  static ByteSize invocation_counter_offset()    { return byte_offset_of(methodOopDesc, _invocation_counter); }
  static ByteSize backedge_counter_offset()      { return byte_offset_of(methodOopDesc, _backedge_counter);   }
  static ByteSize native_function_offset()       { return in_ByteSize(sizeof(methodOopDesc));                 }
  static ByteSize from_interpreted_offset()      { return byte_offset_of(methodOopDesc, _from_interpreted_entry ); }
  static ByteSize interpreter_entry_offset()     { return byte_offset_of(methodOopDesc, _i2i_entry ); }
  static ByteSize max_stack_offset()             { return byte_offset_of(methodOopDesc, _max_stack         ); } 

  // Static methods that are used to implement member methods where an exposed this pointer
  // is needed due to possible GCs
static objArrayHandle resolved_checked_exceptions_impl(methodOop this_oop,intptr_t sba_hint,TRAPS);

  // Returns the byte code index from the byte code pointer
  int     bci_from(address bcp) const;
  address bcp_from(int     bci) const;

  // Returns the line number for a bci if debugging information for the method is prowided,
  // -1 is returned otherwise.
  int line_number_from_bci(int bci) const;

  // Reflection support
  bool is_overridden_in(klassOop k) const;

  // RedefineClasses() support:
  bool is_old() const                               { return access_flags().is_old(); }
  void set_is_old()                                 { _access_flags.set_is_old(); }
  bool is_obsolete() const                          { return access_flags().is_obsolete(); }
  void set_is_obsolete()                            { _access_flags.set_is_obsolete(); }

  // JVMTI Native method prefixing support:
  bool is_prefixed_native() const                   { return access_flags().is_prefixed_native(); }
  void set_is_prefixed_native()                     { _access_flags.set_is_prefixed_native(); }

  // Rewriting support
  static methodHandle clone_with_new_data(methodHandle m, u_char* new_code, int new_code_length, 
                                          u_char* new_compressed_linenumber_table, int new_compressed_linenumber_size, TRAPS);

  // Get this method's jmethodID -- allocate if it doesn't exist
  jmethodID jmethod_id()                            { methodHandle this_h(this);
                                                      return instanceKlass::jmethod_id_for_impl(method_holder(), this_h); }

  // Lookup the jmethodID for this method.  Return NULL if not found.
  // NOTE that this function can be called from a signal handler
  // (see AsyncGetCallTrace support for Forte Analyzer) and this
  // needs to be async-safe. No allocation should be done and
  // so handles are not used to avoid deadlock.
  jmethodID find_jmethod_id_or_null()               { return instanceKlass::cast(method_holder())->jmethod_id_or_null(this); }

  // JNI static invoke cached itable index accessors
  int cached_itable_index()                         { return instanceKlass::cast(method_holder())->cached_itable_index(method_idnum()); }
  void set_cached_itable_index(int index)           { instanceKlass::cast(method_holder())->set_cached_itable_index(method_idnum(), index); }

  // Support for inlining of intrinsic methods
  vmIntrinsics::ID intrinsic_id() const { // returns zero if not an intrinsic
    const u1& cache = _intrinsic_id_cache;
    if (cache != 0) {
      return (vmIntrinsics::ID)(cache - 1);
    } else {
      vmIntrinsics::ID id = compute_intrinsic_id();
      *(u1*)&cache = ((u1) id) + 1;   // force the cache to be non-const
      vmIntrinsics::verify_method(id, (methodOop) this);
      assert((vmIntrinsics::ID)(cache - 1) == id, "proper conversion");
      return id;
    }
  }

  // Inline cache support
  void cleanup_inline_caches();

  // Find if klass for method is loaded
  bool is_klass_loaded_by_klass_index(int klass_index) const;
  bool is_klass_loaded(int refinfo_index, bool must_be_resolved = false) const;

  static methodOop method_from_bcp(address bcp);

  // Resolve all classes in signature, return 'true' if successful
  static bool load_signature_classes(methodHandle m, TRAPS);

  // Return if true if not all classes references in signature, including return type, has been loaded
  static bool has_unloaded_classes_in_signature(methodHandle m, TRAPS);

  // Printing
void print_short_name(outputStream*st)const/*PRODUCT_RETURN*/;//prints as klassname::methodname; Exposed so field engineers can debug VM
  void print_name(outputStream* st)              PRODUCT_RETURN; // prints as "virtual void foo(int)"

  // Helper routine used for method sorting
  static void sort_methods(objArrayOop methods,
                           objArrayOop methods_annotations,
                           objArrayOop methods_parameter_annotations,
                           objArrayOop methods_default_annotations,
                           bool idempotent = false);

  // size of parameters
  void set_size_of_parameters(int size)          { _size_of_parameters = size; }
 private:
  // Helper routine for intrinsic_id().
  vmIntrinsics::ID compute_intrinsic_id() const;

  // Inlined elements
  address* native_function_addr() const          { assert(is_native(), "must be native"); return (address*) (this+1); }

 public:
  // Garbage collection support
  heapRef*  adr_constMethod() const              { return (heapRef*)&_constMethod;     }
  heapRef*  adr_constants() const                { return (heapRef*)&_constants;       }
};


// Utility class for compressing line number tables
#include "compressedStream.hpp"
class CompressedLineNumberWriteStream: public CompressedWriteStream {
 private:
  int _bci;
  int _line;
 public:
  // Constructor
  CompressedLineNumberWriteStream(int initial_size) : CompressedWriteStream(initial_size), _bci(0), _line(0) {}
  CompressedLineNumberWriteStream(u_char* buffer, int initial_size) : CompressedWriteStream(buffer, initial_size), _bci(0), _line(0) {}

  // Write (bci, line number) pair to stream
  void write_pair_regular(int bci_delta, int line_delta);

  inline void write_pair_inline(int bci, int line) {
    int bci_delta = bci - _bci;
    int line_delta = line - _line;
    _bci = bci;
    _line = line;
    // Skip (0,0) deltas - they do not add information and conflict with terminator.
    if (bci_delta == 0 && line_delta == 0) return;
    // Check if bci is 5-bit and line number 3-bit unsigned.
    if (((bci_delta & ~0x1F) == 0) && ((line_delta & ~0x7) == 0)) {
      // Compress into single byte.
      jubyte value = ((jubyte) bci_delta << 3) | (jubyte) line_delta;
      // Check that value doesn't match escape character.
      if (value != 0xFF) {
        write_byte(value);
        return;
      }
    }
    write_pair_regular(bci_delta, line_delta);
  }

  void write_pair(int bci, int line) { write_pair_inline(bci, line); }

  // Write end-of-stream marker
  void write_terminator()                        { write_byte(0); }
};


// Utility class for decompressing line number tables

class CompressedLineNumberReadStream: public CompressedReadStream {
 private:
  int _bci;
  int _line;
 public:
  // Constructor
  CompressedLineNumberReadStream(u_char* buffer);
  // Read (bci, line number) pair from stream. Returns false at end-of-stream.
  bool read_pair();
  // Accessing bci and line number (after calling read_pair)
  int bci() const                               { return _bci; }
  int line() const                              { return _line; }
};


/// Fast Breakpoints.

// If this structure gets more complicated (because bpts get numerous),
// move it into its own header.

// There is presently no provision for concurrent access
// to breakpoint lists, which is only OK for JVMTI because
// breakpoints are written only at safepoints, and are read
// concurrently only outside of safepoints.

class BreakpointInfo : public CHeapObj {
 private:
  Bytecodes::Code  _orig_bytecode;
  int              _bci;
  u2               _name_index;       // of method
  u2               _signature_index;  // of method
  BreakpointInfo*  _next;             // simple storage allocation

 public:
  BreakpointInfo(methodOop m, int bci);

  // accessors
  Bytecodes::Code orig_bytecode()                     { return _orig_bytecode; }
  void        set_orig_bytecode(Bytecodes::Code code) { _orig_bytecode = code; }
  int         bci()                                   { return _bci; }

  BreakpointInfo*          next() const               { return _next; }
  void                 set_next(BreakpointInfo* n)    { _next = n; }

  // helps for searchers
  bool match(methodOop m, int bci) {
    return bci == _bci && match(m);
  }

  bool match(methodOop m) {
    return _name_index == m->name_index() &&
      _signature_index == m->signature_index();
  }

  void set(methodOop method);
  void clear(methodOop method);
};

#endif // METHODOOP_HPP
