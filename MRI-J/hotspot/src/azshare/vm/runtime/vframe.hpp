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
#ifndef VFRAME_HPP
#define VFRAME_HPP


#include "allocation.hpp"
#include "frame.hpp"

class DebugScope;

// vframes are virtual stack frames representing Java source level
// activations.  A single frame may hold several source level activations in
// the case of optimized code.  The debugging stored with the optimized code
// enables us to unfold a frame as a stack of vframes.

// If you want to crawl physical frames and see the gory details, use the
// frame.sender() mechanism.  If you only want to see Java-visible stuff,
// use this mechanism.

// vframes are treated as immutable functional objects; as such they support
// both update-in-place and recursive stack-allocated usage models.

// USAGE: Update-in-place, e.g., a for-loop:
// 
//     for( vframe vf; !vf.done(); vf.next() ) {
//       ...vf.method()... vf.bci()...
//     }
//
// USAGE: recursive model
// 
//     JavaThread::do_stack( vframe vf ) {
//       if( vf.done() ) return;
//       PRE:  ...vf.method()... vf.bci()...
//       do_stack( vf.next_older() ); // Implicit stack-allocation of a new vframe object
//       POST: ...vf.method()... vf.bci()...
//     }

// vframes carry no oops and are GC safe.

#define LAZY_DEBUG ((const DebugScope *)-1)
class vframe:public StackObj{
  frame _fr;                    // Raw frame behind the virtual frame.
  const DebugScope *_ds;        // DebugScope tracking the current virtual frame
  int _sbafid;                  // SBA frame tracking, or -1 for not tracked
  // Start in the middle of stack
  vframe(frame fr, const DebugScope *ds, int sbafid) : _fr(fr), _ds(ds), _sbafid(sbafid) { }
public:
  // Constructors
  // Start at beginning of named threads stack
vframe(JavaThread*thread);
  // Start in the middle of stack, for JVMPI
  vframe(frame fr);

  // Access to Java-visible bits
frame get_frame()const{return _fr;}
methodRef method_ref()const;
  methodOop method() const { return method_ref().as_methodOop(); }
  int bci() const;
  intptr_t java_local(JavaThread* thread, int index) const;
  int sbafid() const { return _sbafid; }
  const DebugScope *scope() const;

  // Setting Java-visible bits.  This is only directly legal for interpreted
  // frames.  For compiled frames a deopt is triggered and the updates are
  // just recorded for now.  During the actual deopt event the updates are
  // played back after the interpreter frames are built.
  void set_java_local(int index, jvalue value, BasicType t);

  // Update a local in a compiled frame. Update happens when deopt occurs
  void update_local(JavaThread* thread, BasicType type, int index, jvalue value);

  GrowableArray<objectRef>*        monitors(JavaThread* thread) const;
  //GrowableArray<objectRef>* locked_monitors(/*JavaThread* thread*/) const;

  void jvmpi_fab_heavy_monitors(JavaThread* thread, bool query, int* fab_index, int frame_count, GrowableArray<ObjectMonitor*>* fab_list);

  // TTY output
  void print(JavaThread* thread) const;
  void print_on(JavaThread* thread, outputStream *st) const;
  void print_lock_info(JavaThread* thread, bool youngest, outputStream* st);

  // arta
  void print_to_xml(JavaThread* thread, xmlBuffer *xb) const;
  void print_to_xml_lock_info(JavaThread* thread, bool youngest, xmlBuffer* xb);

  // True if we have done a 'next_older' on the eldest Java frame.
  bool done() const { return (_fr.id()==0 && _fr.pc()==0) || _fr.is_first_frame(); }

  // True if this is the top-most compiled vframe for this physical frame,
  // such that a 'next' call will change to a new physical frame.  Used by
  // deoptimization.  The top-most vframe has a DebugScope with a null caller.
  bool top_inlined_frame() const;

  // Return the next older vframe.
  vframe next_older() const;    // Return next older vframe
void next();//Update-in-place next older vframe

  // Skip some Java frames related to security and reflection.
  void security_get_caller_frame(int depth);
  void skip_prefixed_method_and_wrappers();
  void skip_reflection_related_frames();

  void print_xml_on(JavaThread *thread, xmlBuffer *xb) const;
};


// In order to implement set_locals for compiled vframes we must
// store updated locals in a data structure that contains enough
// information to recognize equality with a vframe and to store
// any updated locals.

class jvmtiDeferredLocalVariable;
class jvmtiDeferredLocalVariableSet : public CHeapObj {
private:

  methodRef _method;           // must be GC'd
  int       _bci;
  intptr_t* _id;
  GrowableArray<jvmtiDeferredLocalVariable*>* _locals;

 public:
  // JVM state
  methodOop                         method()         const  { return lvb_methodRef(&_method).as_methodOop(); }
  int                               bci()            const  { return _bci; }
  intptr_t*                         id()             const  { return _id; }
  GrowableArray<jvmtiDeferredLocalVariable*>* locals()         const  { return _locals; }
  void                              set_local_at(int idx, BasicType typ, jvalue val);

  // Does the vframe match this jvmtiDeferredLocalVariableSet
  bool                              matches(vframe* vf);
  // GC
  void                              oops_do(OopClosure* f);

  // constructor
  jvmtiDeferredLocalVariableSet(methodRef method, int bci, intptr_t* id);

  // destructor
  ~jvmtiDeferredLocalVariableSet();


};

class jvmtiDeferredLocalVariable : public CHeapObj {
  public:

    jvmtiDeferredLocalVariable(int index, BasicType type, jvalue value);

    BasicType type(void)                   { return _type; }
    int index(void)                        { return _index; }
    jvalue value(void)                     { return _value; }
    // Only mutator is for value as only it can change
    void set_value(jvalue value)           { _value = value; }
    // For gc
objectRef*ref_addr(void){return(objectRef*)&_value.l;}

  private:

    BasicType         _type;
    jvalue            _value;
    int               _index;

};

#endif // VFRAME_HPP
