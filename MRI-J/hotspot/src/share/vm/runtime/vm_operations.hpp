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
#ifndef VM_OPERATIONS_HPP
#define VM_OPERATIONS_HPP


#include "growableArray.hpp"
#include "handles.hpp"
#include "os.hpp"
#include "resourceArea.hpp"
#include "safepointTimes.hpp"
#include "vmTags.hpp"

// The following classes are used for operations
// initiated by a Java thread but that must
// take place in the VMThread.

#define VM_OP_ENUM(type)   VMOp_##type,

// Note: When new VM_XXX comes up, add 'XXX' to the template table. 
#define VM_OPS_DO(template)                       \
  template(Dummy)                                 \
  template(ThreadStop)                            \
  template(ThreadDump)                            \
  template(PrintThreads)                          \
  template(FindDeadlocks)                         \
  template(VMThreadSafepoint)                     \
  template(ForceSafepoint)                        \
  template(ForceAsyncSafepoint)                   \
  template(Deoptimize)                            \
  template(DeoptimizeFrame)                       \
  template(DeoptimizeAll)                         \
  template(ZombieAll)                             \
  template(Verify)                                \
  template(PrintJNI)                              \
  template(HeapDumper)                            \
  template(DeoptimizeTheWorld)                    \
  template(GC_HeapInspection)                     \
  template(GenCollectFull)                        \
  template(GenCollectFullConcurrent)              \
  template(GenCollectForAllocation)               \
  template(ParallelGCFailedAllocation)            \
  template(ParallelGCFailedPermanentAllocation)   \
  template(ParallelGCSystemGC)                    \
  template(CMS_Initial_Mark)                      \
  template(CMS_Final_Remark)                      \
  template(EnableBiasedLocking)                   \
  template(RevokeBias)                            \
  template(BulkRevokeBias)                        \
  template(PopulateDumpSharedSpace)               \
  template(JNIFunctionTableCopier)                \
  template(RedefineClasses)                       \
  template(GetOwnedMonitorInfo)                   \
  template(GetObjectMonitorUsage)                 \
  template(GetCurrentContendedMonitor)            \
  template(GetStackTrace)                         \
  template(GetMultipleStackTraces)                \
  template(GetAllStackTraces)                     \
  template(GetThreadListStackTraces)              \
  template(GetFrameCount)                         \
  template(GetFrameLocation)                      \
  template(ChangeBreakpoints)                     \
  template(GetOrSetLocal)                         \
  template(GetCurrentLocation)                    \
  template(EnterInterpOnlyMode)                   \
  template(ChangeSingleStep)                      \
  template(HeapWalkOperation)                     \
  template(HeapIterateOperation)                  \
  template(ReportJavaOutOfMemory)                 \
  template(HeapWalker)                            \
  template(GrowAllocatedObjects)                  \
  template(Exit)                                  \
  template(JVMPIPostObjAlloc)                     \
  template(JVMPIPostHeapDump)                     \
  template(JVMPIPostMonitorDump)                  \
  template(SmaHeuristicAdjuster)                  \

class VM_Operation: public CHeapObj {
 public:
  enum Mode {
    _safepoint,       // blocking,        safepoint, vm_op C-heap allocated
    _no_safepoint,    // blocking,     no safepoint, vm_op C-Heap allocated
    _no_more_modes
  };

  enum VMOp_Type {
    VM_OPS_DO(VM_OP_ENUM)
    VMOp_Terminating
  };
                
 private:
  Thread*	  _calling_thread;
  long            _timestamp;
  VM_Operation*	  _next;  
  VM_Operation*   _prev;
  
  // The VM operation name array
  static const char* _names[];

 public:
  VM_Operation()  { _calling_thread = NULL; _next = NULL; _prev = NULL; }
  virtual ~VM_Operation() {}

  // VM operation support (used by VM thread)  
  Thread* calling_thread() const                 { return _calling_thread; }  
void set_calling_thread(Thread*thread);

  long timestamp() const              { return _timestamp; }
  void set_timestamp(long timestamp)  { _timestamp = timestamp; } 
    
  // Called by VM thread - does in turn invoke doit(). Do not override this
  void evaluate();  
    
  // evaluate() is called by the VMThread and in turn calls doit(). 
  // If the thread invoking VMThread::execute((VM_Operation*) is a JavaThread, 
  // doit_prologue() is called in that thread before transferring control to 
  // the VMThread.
  // If doit_prologue() returns true the VM operation will proceed, and 
  // doit_epilogue() will be called by the JavaThread once the VM operation 
  // completes. If doit_prologue() returns false the VM operation is cancelled.    
  virtual void doit()                            = 0;
  virtual bool doit_prologue()                   { return true; };
virtual void doit_epilogue(){};

  // Type test
  virtual bool is_methodCompiler() const         { return false; }
  
  // Linking
  VM_Operation *next() const			 { return _next; }
  VM_Operation *prev() const                     { return _prev; }
  void set_next(VM_Operation *next)		 { _next = next; }
  void set_prev(VM_Operation *prev)		 { _prev = prev; }
  
  // Configuration. Override these appropriatly in subclasses.             
  virtual VMOp_Type type() const = 0;
  virtual int tag() const                         { return VMOp_unclassified_tag; }
  virtual Mode evaluation_mode() const            { return _safepoint; }  
  virtual bool allow_nested_vm_operations() const { return false; }    
  virtual bool is_cheap_allocated() const         { return false; }
  virtual void oops_do(OopClosure* f)              { /* do nothing */ };

  // CAUTION: <don't hang yourself with following rope>
  // If you override these methods, make sure that the evaluation
  // of these methods is race-free and non-blocking, since these
  // methods may be evaluated either by the mutators or by the
  // vm thread, either concurrently with mutators or with the mutators
  // stopped. In other words, taking locks is verboten, and if there
  // are any races in evaluating the conditions, they'd better be benign.
bool evaluate_at_safepoint()const{return evaluation_mode()==_safepoint;}
bool evaluate_concurrently()const{return false;}

  // Debugging
  void print_on_error(outputStream* st) const;
  const char* name() const { return _names[type()]; }
  static const char* name(int type) { 
    assert(type >= 0 && type < VMOp_Terminating, "invalid VM operation type"); 
    return _names[type]; 
  }
#ifndef PRODUCT
  void print_on(outputStream* st) const { print_on_error(st); }
#endif
};

class VM_VMThreadSafepoint:public VM_Operation{
 protected:
  volatile bool                 _should_clean_self;
           SafepointTimes*      _times;
           SafepointEndCallback _end_callback;
           void*                _user_data;
 public:
  VM_VMThreadSafepoint(bool clean, SafepointTimes* _times, SafepointEndCallback end_callback, void* user_data);
VMOp_Type type()const{return VMOp_VMThreadSafepoint;}

  virtual void doit();

  virtual int          tag() const                        { return VMOp_VMThreadSafepoint_tag; }
  virtual bool         allow_nested_vm_operations() const { return false; }
virtual Mode evaluation_mode()const{return _no_safepoint;}
  virtual bool         is_cheap_allocated() const         { return true; }

  void                 safepoint_vm_thread();
  void                 set_should_clean()                 { _should_clean_self = true; }
  void                 restart_vm_thread();
  SafepointEndCallback end_callback()                     { return _end_callback; }
};

// dummy vm op, evaluated just to force a safepoint
class VM_ForceSafepoint: public VM_Operation {
 public:
  VM_ForceSafepoint() {}  
  void doit()         {}
  VMOp_Type type() const { return VMOp_ForceSafepoint; }
  virtual int tag() const                        { return VMOp_ForceSafepoint_tag; }
  virtual bool is_cheap_allocated() const        { return false; }
};


class VM_Deoptimize: public VM_Operation {
 public:
  VM_Deoptimize() {}
  VMOp_Type type() const                        { return VMOp_Deoptimize; }
  void doit();
  virtual int tag() const                        { return VMOp_Deoptimize_tag; }
};

class VM_Verify: public VM_Operation {
 private:
  KlassHandle _dependee;
 public:
  VM_Verify() {}
  VMOp_Type type() const { return VMOp_Verify; }
  void doit();
  virtual int tag() const                        { return VMOp_Verify_tag; }
};


class VM_PrintThreads: public VM_Operation {
 private:
  outputStream* _out;
  bool _print_concurrent_locks;
 public:
  VM_PrintThreads()                                                { _out = tty; _print_concurrent_locks = PrintConcurrentLocks; }
  VM_PrintThreads(outputStream* out, bool print_concurrent_locks)  { _out = out; _print_concurrent_locks = print_concurrent_locks; }
  VMOp_Type type() const                                           {  return VMOp_PrintThreads; }
  void doit();
  bool doit_prologue();
  void doit_epilogue();
  virtual int tag() const  { return VMOp_PrintThreads_tag; }
};

class VM_PrintJNI: public VM_Operation {
 private:
  outputStream* _out;
 public:
  VM_PrintJNI() 			{ _out = tty; }
  VM_PrintJNI(outputStream* out)  	{ _out = out; }
  VMOp_Type type() const                { return VMOp_PrintJNI; }
  void doit();
};

class DeadlockCycle;
class VM_FindDeadlocks: public VM_Operation {
private:
  bool           _concurrent_locks;
  DeadlockCycle* _deadlocks;
  xmlBuffer *    _xb;
  outputStream* const _out;

public:
VM_FindDeadlocks(bool concurrent_locks):_concurrent_locks(concurrent_locks),_out(tty),_xb(NULL),_deadlocks(NULL){};
VM_FindDeadlocks(xmlBuffer*xb):_concurrent_locks(true),_out(tty),_xb(xb),_deadlocks(NULL){};
  ~VM_FindDeadlocks();

  DeadlockCycle* result()      { return _deadlocks; };
  VMOp_Type type() const       { return VMOp_FindDeadlocks; }
  void doit();
  bool doit_prologue();
  void print_xml_on(DeadlockCycle *cycle);
  virtual int tag() const  { return VMOp_FindDeadlocks_tag; }
};

class ThreadDumpResult;
class ThreadSnapshot;
class ThreadConcurrentLocks;

class VM_ThreadDump : public VM_Operation {
 private:
  ThreadDumpResult*              _result;
  int                            _num_threads;
  GrowableArray<instanceHandle>* _threads;
  int                            _max_depth;
  bool                           _with_locked_monitors;
  bool                           _with_locked_synchronizers;

  ThreadSnapshot* snapshot_thread(JavaThread* java_thread, ThreadConcurrentLocks* tcl);

 public:
  VM_ThreadDump(ThreadDumpResult* result,
                int max_depth,  // -1 indicates entire stack
                bool with_locked_monitors, 
                bool with_locked_synchronizers);

  VM_ThreadDump(ThreadDumpResult* result,
                GrowableArray<instanceHandle>* threads,
                int num_threads, // -1 indicates entire stack
                int max_depth,
                bool with_locked_monitors, 
                bool with_locked_synchronizers);

  VMOp_Type type() const { return VMOp_ThreadDump; }
  void doit();
  bool doit_prologue();
  void doit_epilogue();
};


class VM_Exit: public VM_Operation {
 private:
  int  _exit_code;
  static volatile bool _vm_exited;
  static Thread * _shutdown_thread;
  static void wait_if_vm_exited();
 public:
  VM_Exit(int exit_code) {
    _exit_code = exit_code;
  }
  static int wait_for_threads_in_native_to_block();
  static int set_vm_exited();
  static bool vm_exited()                      { return _vm_exited; }
  static void block_if_vm_exited() {
    if (_vm_exited) {
      wait_if_vm_exited();
    }
  }
  VMOp_Type type() const { return VMOp_Exit; }
  void doit();
  virtual int tag() const                      { return VMOp_Exit_tag; }
};
#endif // VM_OPERATIONS_HPP
