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


#include "ciEnv.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "debug.hpp"
#include "defaultStream.hpp"
#include "disassembler_pd.hpp"
#include "interpreter.hpp"
#include "genCollectedHeap.hpp"
#include "java.hpp"
#include "jniHandles.hpp"
#include "jvm_os.h"
#include "methodOop.hpp"
#include "ostream.hpp"
#include "privilegedStack.hpp"
#include "safepoint.hpp"
#include "stubCodeGenerator.hpp"
#include "systemDictionary.hpp"
#include "universe.hpp"
#include "vmError.hpp"
#include "vmGCOperations.hpp"
#include "vmThread.hpp"
#include "heapDumper.hpp"
#include "vm_version_pd.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#ifndef ASSERT
#  ifdef _DEBUG
   // NOTE: don't turn the lines below into a comment -- if you're getting
   // a compile error here, change the settings to define ASSERT
   ASSERT should be defined when _DEBUG is defined.  It is not intended to be used for debugging
   functions that do not slow down the system too much and thus can be left in optimized code.
   On the other hand, the code should not be included in a production version.
#  endif // _DEBUG
#endif // ASSERT


#ifdef _DEBUG
#  ifndef ASSERT
     configuration error: ASSERT must be defined in debug version
#  endif // ASSERT
#endif // _DEBUG


#ifdef PRODUCT
#  if -defined _DEBUG || -defined ASSERT
     configuration error: ASSERT et al. must not be defined in PRODUCT version
#  endif
#endif // PRODUCT


void warning(const char* format, ...) {
  // In case error happens before init or during shutdown
  if (tty == NULL) ostream_init();

  tty->print("%s warning: ", VM_Version::vm_name());
  va_list ap;
  va_start(ap, format);
  tty->vprint_cr(format, ap);
  va_end(ap);
  if (BreakAtWarning) BREAKPOINT;
}

#ifndef PRODUCT

#define is_token_break(ch) (isspace(ch) || (ch) == ',')

static const char* last_file_name = NULL;
static int         last_line_no   = -1;

// assert/guarantee/... may happen very early during VM initialization.
// Don't rely on anything that is initialized by Threads::create_vm(). For
// example, don't use tty.
bool assert_is_suppressed(const char* file_name, int line_no) {
  if (Thread::current()->_debug_level > 0) return true;
  // The following 1-element cache requires that passed-in
  // file names are always only constant literals.
  if (file_name == last_file_name && line_no == last_line_no)  return true;

  int file_name_len = (int)strlen(file_name);
  // Azul - We do not want to use os::file_separator as that is the
  // separator on the proxy host and we require the separator for the build host.
  const char* base_name = strrchr(file_name, '/');
  if (base_name == NULL)
    base_name = strrchr(file_name, '\\');
  if (base_name == NULL)
    base_name = file_name;

  // scan the SuppressErrorAt option
  const char* cp = SuppressErrorAt;
  for (;;) {
    const char* sfile;
    int sfile_len;
    int sline;
    bool noisy;
    while ((*cp) != '\0' && is_token_break(*cp))  cp++;
    if ((*cp) == '\0')  break;
    sfile = cp;
    while ((*cp) != '\0' && !is_token_break(*cp) && (*cp) != ':')  cp++;
    sfile_len = cp - sfile;
    if ((*cp) == ':')  cp++;
    sline = 0;
    while ((*cp) != '\0' && isdigit(*cp)) {
      sline *= 10;
      sline += (*cp) - '0';
      cp++;
    }
    // "file:line!" means the assert suppression is not silent
    noisy = ((*cp) == '!');
    while ((*cp) != '\0' && !is_token_break(*cp))  cp++;
    // match the line
    if (sline != 0) {
      if (sline != line_no)  continue;
    }
    // match the file
    if (sfile_len > 0) {
      const char* look = file_name;
      const char* look_max = file_name + file_name_len - sfile_len;
      const char* foundp;
      bool match = false;
      while (!match
             && (foundp = strchr(look, sfile[0])) != NULL
             && foundp <= look_max) {
        match = true;
        for (int i = 1; i < sfile_len; i++) {
          if (sfile[i] != foundp[i]) {
            match = false;
            break;
          }
        }
        look = foundp + 1;
      }
      if (!match)  continue;
    }
    // got a match!
    if (noisy) {
      fdStream out(defaultStream::output_fd());
      out.print_raw("[error suppressed at ");
      out.print_raw(base_name);
      char buf[16];
      jio_snprintf(buf, sizeof(buf), ":%d]", line_no);
      out.print_raw_cr(buf);
    } else {
      // update 1-element cache for fast silent matches
      last_file_name = file_name;
      last_line_no   = line_no;
    }
    return true;
  }

  if (!is_error_reported()) {
    // print a friendly hint:
    fdStream out(defaultStream::output_fd());
    out.print_raw_cr("# To suppress the following error report, specify this argument");
    out.print_raw   ("# after -XX: or in .hotspotrc:  SuppressErrorAt=");
    out.print_raw   (base_name);
    char buf[16];
    jio_snprintf(buf, sizeof(buf), ":%d", line_no);
    out.print_raw_cr(buf);
  }
  return false;
}

#undef is_token_break

#else

// Place-holder for non-existent suppression check:
#define assert_is_suppressed(file_name, line_no) (false)

#endif //PRODUCT

bool report_assertion_failure(const char* file_name, int line_no, const char* message) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return false;
VMError err(Thread::current(),message,file_name,line_no);
  err.report_and_die();
  return true;
}

void report_fatal(const char* file_name, int line_no, const char* message) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return;
VMError err(Thread::current(),message,file_name,line_no);
  err.report_and_die();
}

void report_fatal_vararg(const char* file_name, int line_no, const char* format, ...) {  
  char buffer[256];
  va_list ap;
  va_start(ap, format);
  jio_vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);
  report_fatal(file_name, line_no, buffer);
}


// Used by report_vm_out_of_memory to detect recursion.
static jint _exiting_out_of_mem = 0;

// Just passing the flow to VMError to handle error
void report_vm_out_of_memory(const char* file_name, int line_no, size_t size, const char* message) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return;
  
  // We try to gather additional information for the first out of memory  
  // error only; gathering additional data might cause an allocation and a  
  // recursive out_of_memory condition. 
   
  const jint exiting = 1;
  // If we succeed in changing the value, we're the first one in.
  bool first_time_here = Atomic::xchg(exiting, &_exiting_out_of_mem) != exiting;
   
  if (first_time_here) {
    Thread* thread = Thread::current();
    VMError(thread, size, message, file_name, line_no).report_and_die();
  }
vm_abort(true);
}

void report_vm_out_of_memory_vararg(const char* file_name, int line_no, size_t size, const char* format, ...) {  
  char buffer[256];
  va_list ap;
  va_start(ap, format);
  jio_vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);
  report_vm_out_of_memory(file_name, line_no, size, buffer);
}

void report_should_not_call(const char* file_name, int line_no) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return;
VMError err(Thread::current(),"ShouldNotCall()",file_name,line_no);
  err.report_and_die();
}


void report_should_not_reach_here(const char* file_name, int line_no) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return;
VMError err(Thread::current(),"ShouldNotReachHere()",file_name,line_no);
  err.report_and_die();
}


void report_unimplemented(const char* file_name, int line_no) {
  if (Debugging || assert_is_suppressed(file_name, line_no))  return;
VMError err(Thread::current(),"Unimplemented()",file_name,line_no);
  err.report_and_die();
}

void report_java_out_of_memory(const char* message) {
  static jint out_of_memory_reported = 0;

  // A number of threads may attempt to report OutOfMemoryError at around the 
  // same time. To avoid dumping the heap or executing the data collection
  // commands multiple times we just do it once when the first threads reports
  // the error.
  if (Atomic::cmpxchg(1, &out_of_memory_reported, 0) == 0) {
    // create heap dump before OnOutOfMemoryError commands are executed
    if (HeapDumpOnOutOfMemoryError) {
      tty->print_cr("java.lang.OutOfMemoryError: %s", message);
      HeapDumper::dump_heap();
    }

    if (OnOutOfMemoryError && OnOutOfMemoryError[0]) {
      VMError err(message);
      err.report_java_out_of_memory();
    }
  }
}

extern "C" void ps();

void pd_ps(frame fr, JavaThread *thread) { 
#ifndef PRODUCT
  fr.ps(thread,999); 
#endif
}
void pd_psx(intptr_t*sp, address pc, JavaThread *thread) { pd_ps(frame(sp,pc),thread); }

static bool error_reported = false;

// call this when the VM is dying--it might loosen some asserts
void set_error_reported() {
  error_reported = true;
}

bool is_error_reported() {
    return error_reported;
}

// ------ helper functions for debugging go here ------------

#ifndef PRODUCT
// All debug entries should be wrapped with a stack allocated
// Command object. It makes sure a resource mark is set and
// flushes the logfile to prevent file sharing problems.

class Command : public StackObj {
 private:
  ResourceMark rm;
  ResetNoHandleMark rnhm;
  HandleMark   hm;
  bool debug_save;
  bool         saved_verify_jvm_lock_at_lvb;
 public:
  static int level;
  Command(const char* str) {
    debug_save = Debugging;
    Debugging = true;  
    Thread::current()->_debug_level++;
    if (level++ > 0)  return;
    tty->cr();
    tty->print_cr("\"Executing %s\"", str);
    saved_verify_jvm_lock_at_lvb = VerifyJVMLockAtLVB;
    VerifyJVMLockAtLVB = false;
  }

  ~Command() {
    tty->flush();
    // Azul - Give the OS enough time to actually flush the buffers before blocking
    // in the debugger again.
    os::sleep_jvmlock_ok(1*1000, false); // 1 secs of pause
    Debugging = debug_save;
    level--;
    Thread::current()->_debug_level--;
    if ( ! Debugging ) {
      VerifyJVMLockAtLVB = saved_verify_jvm_lock_at_lvb;
    }
  }
};

int Command::level = 0;


extern "C" void blob(CodeBlob* cb) {  
  Command c("blob");
}


extern "C" void dump_vtable(address p) {
  Command c("dump_vtable");
  klassOop k = (klassOop)p;
  instanceKlass::cast(k)->vtable()->print();
}


extern "C" void universe() {
  Command c("universe");
  Universe::print();
}


extern "C" void verify() {
  // try to run a verify on the entire system
  // note: this may not be safe if we're not at a safepoint; for debugging,
  // this manipulates the safepoint settings to avoid assertion failures
  Command c("universe verify");
  bool safe = SafepointSynchronize::is_at_safepoint();
  if (!safe) {
    tty->print_cr("warning: not at safepoint -- verify may fail");
    SafepointSynchronize::set_is_at_safepoint();
  }
  // Ensure Eden top is correct before verification
  Universe::heap()->prepare_for_verify();
  Universe::verify(true);
  if (!safe) SafepointSynchronize::set_is_not_at_safepoint();
}


extern "C" void pp(void* p) {
  Command c("pp");
  FlagSetting fl(PrintVMMessages, true);
  if (Universe::heap()->is_in(p)) {
    oop obj = oop(p);
obj->print(tty);
  } else {
tty->print("%p",p);
  }
}


// pv: print vm-printable object
extern"C"void pa(intptr_t p){((AllocatedObj*)p)->print(tty);}
extern "C" void findpc(intptr_t x);

// Prints the Java stack of the current Java thread
extern "C" void ps() { // print stack
  Command c("ps");
  // Make heap parseable by flushing all TLABs.
  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) 
    thread->tlab().make_parsable(false);
JavaThread*p=JavaThread::current();
  if( !((Thread*)p)->is_Java_thread() ) return;
tty->print(" for current thread: ");
p->print();tty->cr();
  if( p->is_Compiler_thread() ) return;
frame fr=os::current_frame();
  p->trace_stack_from(fr);
}

// Prints the frames of the current Java thread
extern "C" void psf() { // print stack frames
  Command c("psf");
  // Make heap parseable by flushing all TLABs.
  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) 
    thread->tlab().make_parsable(false);
JavaThread*p=JavaThread::current();
  if( !((Thread*)p)->is_Java_thread() ) return;
tty->print(" for current thread: ");
p->print();tty->cr();
  if( p->is_Compiler_thread() ) return;
frame fr=os::current_frame();
  fr.ps(p,999);
}

#ifndef PRODUCT
// print stack, given frame guts.
// No "thread" is involved.
extern "C" void ps_fr( intptr_t sp, intptr_t pc ) {
Command c("print_stack");
  frame f((intptr_t*)sp,(address)pc);
  f.ps(3);
}

extern "C" void psf_fr( ) {
Command c("print_stack");
  JavaThread* p = JavaThread::active();
  frame fr(0,0);
if(p->jvm_lock_is_free()){
if(p->root_Java_frame()){
tty->print_cr("At root frame");
      return;
    } 
fr=p->last_frame();
  } else {
    JavaFrameAnchor a;          // Guess that the JavaFrameAnchor is setup
    p->copy_anchor_out(a);      // Jump thru hoops to dodge privacy issues
    fr = a.pd_last_frame((intptr_t*)a.last_Java_sp_raw());
  }

  while (!fr.is_first_frame()) { 
    fr.ps();
    fr = fr.sender();
  }
}
#endif

extern "C" void threads() {
  Command c("threads");
  Threads::print(false, true);
}


extern "C" void psd() {
  Command c("psd");
  SystemDictionary::print();
}


extern "C" void safepoints() {
  Command c("safepoints");
  SafepointSynchronize::print_state();
}


extern "C" void pss() { // print all stacks
  Command c("pss");
  Threads::print(true, true);
}


extern "C" void debug() {		// to set things up for compiler debugging
  Command c("debug");
  PrintVMMessages = PrintCompilation = true;
  PrintInlining = PrintAssembly = true;
  tty->flush();
}


extern "C" void ndebug() {		// undo debug()
  Command c("ndebug");
  PrintCompilation = false;
  PrintInlining = PrintAssembly = false;
  tty->flush();
}


extern "C" void hsflush()  {
  Command c("flush");
  tty->flush();
  // The destructor of the command will sleep to allow the OS to empty queued buffers.
}


// Given a heap address that was valid before the most recent GC, if
// the oop that used to contain it is still live, prints the new
// location of the oop and the address. Useful for tracking down
// certain kinds of naked oop and oop map bugs.
extern "C" void pnl(intptr_t old_heap_addr) {
  // Print New Location of old heap address
  Command c("pnl");
#ifndef VALIDATE_MARK_SWEEP
  tty->print_cr("Requires build with VALIDATE_MARK_SWEEP defined (debug build) and RecordMarkSweepCompaction enabled");
#else
  MarkSweep::print_new_location_of_heap_address((HeapWord*) old_heap_addr);
#endif
}


static address same_page(address x, address y) {
  intptr_t page_bits = -os::vm_page_size();
  if ((intptr_t(x) & page_bits) == (intptr_t(y) & page_bits)) {
    return x;
  } else if (x > y) {
    return (address)(intptr_t(y) | ~page_bits) + 1;
  } else {
    return (address)(intptr_t(y) & page_bits);
  }
}


// Another interface that isn't ambiguous in dbx.
// Can we someday rename the other find to hsfind?
extern "C" void hsfind(intptr_t x) {
  Command c("hsfind");
  Debug::find(x, false, tty);
}

extern "C" void find(intptr_t x) {
  Command c("find");
  Debug::find(x, false, tty);
}


extern "C" void findpc(intptr_t x) {
  Command c("findpc");
  Debug::find(x, true, tty);
}
#endif // PRODUCT

// Print 1 line only.  Enabled in product mode for XML dumping.
extern "C" void hsfind1(intptr_t x, bool print_pc, xmlBuffer *xb, Thread *thread ) {
  outputStream *S = xb ? (outputStream*)xb : tty;
  if( !xb ) S->flush();
  address addr = (address)x;

  if( x>>32 == frame::double_slot_primitive_type_empty_slot_id ) {
S->print_cr("long/double interpreter stack tag");
    return;
  }

  // Do CodeCache and PC lookups on 'x'
  CodeBlob* b = CodeCache::find_blob_unsafe(addr);
  if( !print_pc && b ) {
    const char *methname = b->methodname();
S->print_cr("((struct CodeBlob*)%p)+%ld, %s, %s",
                b, addr-(address)b, b->name(), methname ? methname : "(null)");
    return;
  }
  if (b != NULL) {
    if (!b->is_methodCode()) {
      // the interpreter is generated into a buffer blob
      InterpreterCodelet* i = Interpreter::codelet_containing(addr);
      if (i != NULL) { 
        S->print(i->_description);
S->cr();
        return; 
      }
      if (Interpreter::contains(addr)) {
S->print_cr("pointing into interpreter code (not bytecode specific)");
        return;
      }
      // the stubroutines are generated into a buffer blob
      StubCodeDesc* d = StubCodeDesc::desc_for(addr);
      if (d != NULL) { 
        d->print_on(S); 
S->cr();
        return; 
      }
      if (StubRoutines::contains(addr)) {
S->print_cr("pointing to an (unnamed) stub routine");
        return;
      }
    } else {
      const char *methname = b->methodname();
S->print_cr("((struct CodeBlob*)%p)+%ld, %s, %s",
                  b, addr-(address)b, b->name(), methname ? methname : "(null)");
      return;
    }
S->cr();
    return; 
  }

  if( (uint64_t)x == (uint64_t)badHeapWordVal ) {
S->cr();
    return;
  }

  // Try to remove any ref-poisoning
  objectRef ref(x);             // raw-value ref constructor
  if( RefPoisoning && ref.is_poisoned() && ((int64_t)x) != ((int32_t)x)/*not a properly signextended 32-bit int*/  ) {
    x = x ^ -1;
S->print("unpoison="INTPTR_FORMAT" ",x);
  }

  // Try to strip 'Ref-ness' off of x.
#if defined(AZUL)
  int sid  =(x>>objectRef::space_shift) & objectRef::space_mask;
  if( x != 0 && 
      (sid == (uint64_t)objectRef::new_space_id || sid == (uint64_t)objectRef::old_space_id) )
    addr = (address)(x & heapRef::va_mask);
#endif

  // Try a general heap lookup on 'x'.
  if( Universe::heap()->is_in(addr) && ((oopDesc*)addr)->is_oop(0) ) { 
    // Scan the heap for nearest oop beginning, requires TLABs to be reset.
HeapWord*p=Universe::heap()->block_start_debug(addr);
    if( p && (address)p != addr ) {  // Found derived ptr base?
      // Since p != addr, addr is in the middle of some oop
      if( oop(p)->is_oop() && oop(p)->is_constMethod() &&
	  constMethodOop(p)->contains(addr)) {
	HandleMark hm(thread);
	methodHandle mh (thread, constMethodOop(p)->method());
	if (!mh->is_native()) {
          S->print("bci = %d  %s", mh->bci_from(address(ref.as_oop())), 
                   Bytecodes::name(Bytecodes::java_code(Bytecodes::cast(*addr))));
	}
      } else if( oop(p)->is_oop() && oop(p)->is_array() ) {
        S->print("DERIVED FROM="PTR_FORMAT" ", p );
        oop(p)->print_value_on(S);
      } else {
S->print("IN HEAP BUT NOT OOP; NEAREST OOP="PTR_FORMAT,p);
      }
    } else if( ((oop)addr)->is_oop() ) { // p == addr, check for plain oop
oop(addr)->print_value_on(S);
    } else {                    // Huh?  Junk pointer in middle of heap?
S->print("BAD OOP");
    }
S->cr();
    return; 
  }
#if defined(AZUL)
  // Try a general stack search for SBA objects
  if( UseSBA && sid == (uint64_t)objectRef::stack_space_id ) {
    stackRef sr(x);
    if( sr.as_oop() != NULL ) {
      addr = (address)sr.as_address(thread->sba_thread());
      if( ((oopDesc*)addr)->is_oop() ) {
        HandleMark hm;
        int fid = sr.preheader()->fid();
tty->print("fid=%d ",fid);
oop(addr)->print_value_on(S);
      }
    }
  }
#endif

  if (JNIHandleBlock::any_contains((jobject) addr)) {
const char*msg="is a local jni handle";
    if (JNIHandles::is_global_handle((jobject) addr)) 
msg="is a global jni handle";
    else if (JNIHandles::is_weak_global_handle((jobject) addr))
msg="is a weak global jni handle";
    S->print_cr(msg);
    return;
  }

  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) {
    // Check for priviledge stack
    if (thread->privileged_stack_top() != NULL && thread->privileged_stack_top()->contains(addr)) {
S->print_cr(PTR_FORMAT"is pointing into the priviledge stack for thread: "PTR_FORMAT,addr,thread);
      return;
    }
    // If the addr is a java thread print information about that.
    if (addr == (address)thread) {
      const char *name = thread->get_thread_name();
      S->print("JavaThread %s ", name ? name : "NULL" );
thread->print_thread_state_on(S);
S->cr();
      return;
    }
  }
  
  // Try an OS specific find
  if (print_pc && os::find(addr, S)) return;

S->cr();
  return;
}

#ifndef PRODUCT

class LookForRefInGenClosure : public OopsInGenClosure {
public:
  oop target;
  void do_oop(objectRef* o) {
if(o!=NULL&&(*o).as_oop()==target){
tty->print_cr(PTR_FORMAT,o);
    }
  }
};


class LookForRefInObjectClosure : public ObjectClosure {
private:
  LookForRefInGenClosure look_in_object;
public:
  LookForRefInObjectClosure(oop target) { look_in_object.target = target; }
  void do_object(oop obj) {
    obj->oop_iterate(&look_in_object);
  }
};


static void findref(intptr_t x) {
  GenCollectedHeap *gch = GenCollectedHeap::heap();
  LookForRefInGenClosure lookFor;
  lookFor.target = (oop) x;
  LookForRefInObjectClosure look_in_object((oop) x);

  tty->print_cr("Searching heap:");
  gch->object_iterate(&look_in_object);

  tty->print_cr("Searching strong roots:");
  Universe::oops_do(&lookFor, false);
  JNIHandles::oops_do(&lookFor);   // Global (strong) JNI handles
  Threads::oops_do(&lookFor);
  ObjectSynchronizer::oops_do(&lookFor);
  //FlatProfiler::oops_do(&lookFor);
  SystemDictionary::oops_do(&lookFor);

  tty->print_cr("Done.");
}

class FindClassObjectClosure: public ObjectClosure {
  private:
    const char* _target;
  public:
    FindClassObjectClosure(const char name[])  { _target = name; }

    virtual void do_object(oop obj) {
      if (obj->is_klass()) {
        Klass* k = klassOop(obj)->klass_part();
        if (k->name() != NULL) {
          ResourceMark rm;
          const char* ext = k->external_name();
          if ( strcmp(_target, ext) == 0 ) { 
tty->print_cr("Found "PTR_FORMAT,obj);
obj->print(tty);
          }
        }
      }
    }
};

// 
extern "C" void findclass(const char name[]) {
  Command c("findclass");
  if (name != NULL) {
    tty->print_cr("Finding class %s -> ", name);
    FindClassObjectClosure srch(name);
    Universe::heap()->permanent_object_iterate(&srch);
  }
}


extern "C" void hsfindref(intptr_t x) {
  Command c("hsfindref");
  findref(x);
}


// int versions of all methods to avoid having to type type casts in the debugger

void pp(intptr_t p)          { pp((void*)p); }
void pp(oop p)               { pp((void*)p); }

void Debug::find(intptr_t x, bool print_pc, outputStream* st) {
  address addr = (address)x;

  CodeBlob* b = CodeCache::find_blob_unsafe(addr);
  if (b != NULL) {
    // the interpreter is generated into a buffer blob
    InterpreterCodelet* i = Interpreter::codelet_containing(addr);
    if (i != NULL) {
i->print_on(st);
      return;
    }
    if (Interpreter::contains(addr)) {
st->print_cr(PTR_FORMAT" is pointing into interpreter code (not bytecode specific)",addr);
      return;
    }
    // the stubroutines are generated into a buffer blob
    StubCodeDesc* d = StubCodeDesc::desc_for(addr);
    if (d != NULL) {
d->print_on(st);
if(print_pc)st->cr();
      return;
    }
    if (StubRoutines::contains(addr)) {
st->print_cr(PTR_FORMAT" is pointing to an (unnamed) stub routine",addr);
      return;
    }
    // the InlineCacheBuffer is using stubs generated into a buffer blob
if(print_pc&&b->is_methodCode()){
      ResourceMark rm;
st->print("%p: Compiled ",addr);
      Unimplemented();
      //b->method()->print_value_on(st);
st->print("  = (CodeBlob*)"PTR_FORMAT,b);
st->cr();
      return;
    }
b->print_on(st);
    return;
  }

  if (JNIHandles::is_global_handle((jobject) addr)) {
st->print_cr(PTR_FORMAT"is a global jni handle",addr);
    return;
  }
  if (JNIHandles::is_weak_global_handle((jobject) addr)) {
st->print_cr(PTR_FORMAT"is a weak global jni handle",addr);
    return;
  }
  if (JNIHandleBlock::any_contains((jobject) addr)) {
st->print_cr(PTR_FORMAT"is a local jni handle",addr);
    return;
  }

  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) {
    // Check for priviledge stack
    if (thread->privileged_stack_top() != NULL && thread->privileged_stack_top()->contains(addr)) {
st->print_cr(PTR_FORMAT"is pointing into the priviledge stack for thread: "PTR_FORMAT,addr,thread);
      return;
    }
    // If the addr is a java thread print information about that.
    if( addr == (address)thread ) {
thread->print_on(st);
      return;
    }
  }

  // in SBA space?
  objectRef r = *(objectRef*)&addr;
  oop op = (oop)r.as_address();
address o=(address)op;
  if( UseSBA && Threads::sba_find_owner(o) != NULL && op->is_oop() ) {
op->print_on(st);
    return;
  }

  // in the heap?
HeapWord*p;
if(Universe::heap()->is_in(o)){
    if( op->is_oop(false) ) p = (HeapWord*)op;
    else p = Universe::heap()->block_start(o);
if(p!=NULL&&
      (op->is_oop(false) || Universe::heap()->block_is_obj(p)) ) {
      if (o != addr) { // was it a ref?
st->print_cr("(oop pointer is "PTR_FORMAT")\n",o);
      }
oop(p)->print_on(st);
if(p!=(HeapWord*)o&&oop(p)->is_method()&&
        methodOop(p)->contains(o) && !methodOop(p)->is_native()) {
        st->print_cr("bci_from(%p) = %d; print_codes():", o, methodOop(p)->bci_from(o));
        methodOop(p)->print_codes(st,NULL);
      }
    }
    return;
  }


  // Try an OS specific find
  if (os::find(addr, tty)) {
    return;
  }

  if (print_pc) {
st->print_cr(PTR_FORMAT": probably in C++ code; check debugger",addr);
    Disassembler::decode(same_page(addr-40,addr),same_page(addr+40,addr));
    return;
  }

st->print_cr(PTR_FORMAT" is pointing to unknown location",addr);
}

#endif // PRODUCT

void Debug::examine(char* command, address addr, outputStream* st) {
  char format     = 'x';
  char size       = 'g';
  int  item_size  = ptr_size;
  int  item_count = 1;

  if (command[1] != 0) {
    if (command[1] != '/') {
st->print_cr("Illegal command format: %s",command);
      return;
    }

    char tmp_f;
    char tmp_s;
    int  tmp_ic;
    if (sscanf(command, "x/%d%c%c", &tmp_ic, &tmp_s, &tmp_f) == 3) {
      item_count = tmp_ic;
size=tmp_s;
format=tmp_f;
    } else if (sscanf(command, "x/%d%c", &tmp_ic, &tmp_f) == 2) {
      item_count = tmp_ic;
format=tmp_f;
    } else if (sscanf(command, "x/%c", &tmp_f) == 1) {
format=tmp_f;
    } else {
      st->print_cr("Illegal formating string: %s", command+2);
      return;
    }
  }

  // NEEDS_CLEANUP Parse format and size!
  char formatter[16];
  switch (size) {
    case 'g': item_size = int64_size; break;
    case 'w': item_size = int32_size; break;
    case 'h': item_size = int16_size; break;
    case 'b': item_size =  int8_size; break;
    default : st->print_cr("Illegal size specifier: %c", size); return; break;
  }
  if( ((intptr_t)addr & (item_size-1)) != 0 ) {
    st->print_cr("addr %p is not aligned to %d",addr,item_size);
    return;
  }

  switch (format) {
case'x':{
      switch (item_size) {
        case int8_size : strcpy(formatter,    "0x%02x"); break;
        case int16_size: strcpy(formatter,    "0x%04x"); break;
        case int32_size: strcpy(formatter,    "0x%08x"); break;
        case int64_size: strcpy(formatter, "0x%016llx"); break;
        default        : ShouldNotReachHere();           break;
      }
      break;
    }
    default: {
st->print_cr("Unrecognized format: %c",format);
      break;
    }
  }

  address end = addr + item_count*item_size;

  int line_items = 0;
st->print(PTR_FORMAT":",addr);
while(addr<end){
    if (line_items == 4) {
st->cr();
st->print(PTR_FORMAT":",addr);
      line_items = 0;
    }
st->print("  ");
    switch (item_size) {
      case int8_size : st->print(formatter,  *(int8_t*)addr); break;
      case int16_size: st->print(formatter, *(int16_t*)addr); break;
      case int32_size: st->print(formatter, *(int32_t*)addr); break;
      case int64_size: st->print(formatter, *(int64_t*)addr); break;
      default        : ShouldNotReachHere();                   break;
    }
    addr = addr + item_size;
    line_items++;
  }
st->cr();
} 


