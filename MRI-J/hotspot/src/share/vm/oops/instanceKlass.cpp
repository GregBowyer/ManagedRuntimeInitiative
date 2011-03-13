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


#include "artaObjects.hpp"
#include "arguments.hpp"
#include "cardTableExtension.hpp"
#include "classLoader.hpp"
#include "collectedHeap.hpp"
#include "deoptimization.hpp"
#include "fieldDescriptor.hpp"
#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "instanceKlass.hpp"
#include "javaCalls.hpp"
#include "jni.hpp"
#include "jvmtiExport.hpp"
#include "markSweep.hpp"
#include "methodCodeOop.hpp"
#include "mutexLocker.hpp"
#include "objArrayKlass.hpp"
#include "objArrayKlassKlass.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "perfData.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "psScavenge.hpp"
#include "resourceArea.hpp"
#include "rewriter.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "threadService.hpp"
#include "tickProfiler.hpp"
#include "utf8.hpp"
#include "verifier.hpp"
#include "vmSymbols.hpp"
#include "xmlBuffer.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"
#include "gpgc_oldCollector.inline.hpp"

bool instanceKlass::should_be_initialized() const {
  return !is_initialized();
}

klassVtable* instanceKlass::vtable() const {
  return new klassVtable(as_klassOop(), start_of_vtable(), vtable_length() / vtableEntry::size());
}

klassItable* instanceKlass::itable() const {
  return new klassItable(as_klassOop());
}

void instanceKlass::eager_initialize(Thread *thread) {
  if (!EagerInitialization) return;

  if (this->is_not_initialized()) {
    // abort if the the class has a class initializer
    if (this->class_initializer() != NULL) return;

    // abort if it is java.lang.Object (initialization is handled in genesis)
    klassOop super = this->super();
    if (super == NULL) return;

    // abort if the super class should be initialized
    if (!instanceKlass::cast(super)->is_initialized()) return;

    // call body to expose the this pointer
    instanceKlassHandle this_oop(thread, this->as_klassOop());
    eager_initialize_impl(this_oop);
  }
}


void instanceKlass::eager_initialize_impl(instanceKlassHandle this_oop) {
  EXCEPTION_MARK;
ObjectLocker ol(this_oop());

  // abort if someone beat us to the initialization
  if (!this_oop->is_not_initialized()) return;  // note: not equivalent to is_initialized()

  ClassState old_state = this_oop->_init_state;
  link_class_impl(this_oop, true, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    // Abort if linking the class throws an exception.

    // Use a test to avoid redundantly resetting the state if there's
    // no change.  Set_init_state() asserts that state changes make
    // progress, whereas here we might just be spinning in place.
    if( old_state != this_oop->_init_state )
      this_oop->set_init_state (old_state);
  } else {
    // linking successfull, mark class as initialized
    this_oop->set_init_state (fully_initialized);
    // trace 
    if (TraceClassInitialization) {
      ResourceMark rm(THREAD);
      tty->print_cr("[Initialized %s without side effects]", this_oop->external_name());
    }
  }
}


// See "The Virtual Machine Specification" section 2.16.5 for a detailed explanation of the class initialization
// process. The step comments refers to the procedure described in that section.
// Note: implementation moved to static method to expose the this pointer.
void instanceKlass::initialize(TRAPS) {
  if (this->should_be_initialized()) {
    HandleMark hm(THREAD);
    instanceKlassHandle this_oop(THREAD, this->as_klassOop());
    initialize_impl(this_oop, CHECK);
    // Note: at this point the class may be initialized
    //       OR it may be in the state of being initialized
    //       in case of recursive initialization!
  } else {
    assert(is_initialized(), "sanity check");
  }
}


bool instanceKlass::verify_code(
  instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS) {
  // Verify the bytecodes
  Verifier::Mode mode = 
    throw_verifyerror ? Verifier::ThrowException : Verifier::NoException;
  return Verifier::verify(this_oop, mode, CHECK_false);
}


// Used exclusively by the shared spaces dump mechanism to prevent
// classes mapped into the shared regions in new VMs from appearing linked.

void instanceKlass::unlink_class() {
  assert(is_linked(), "must be linked");
  _init_state = loaded;
}

void instanceKlass::link_class(TRAPS) {    
  assert(is_loaded(), "must be loaded");
  if (!is_linked()) {
    instanceKlassHandle this_oop(THREAD, this->as_klassOop());  
    link_class_impl(this_oop, true, CHECK);
  }
}

// Called to verify that a class can link during initialization, without
// throwing a VerifyError.
bool instanceKlass::link_class_or_fail(TRAPS) {
  assert(is_loaded(), "must be loaded");
  if (!is_linked()) {
    instanceKlassHandle this_oop(THREAD, this->as_klassOop());  
    link_class_impl(this_oop, false, CHECK_false);
  }
  return is_linked();
}

bool instanceKlass::link_class_impl(
    instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS) {
  // check for error state
  if (this_oop->is_in_error_state()) {
    ResourceMark rm(THREAD);
    THROW_MSG_(vmSymbols::java_lang_NoClassDefFoundError(), 
               this_oop->external_name(), false);  
  }
  // return if already verified
  if (this_oop->is_linked()) {
    return true;
  }

  // Timing
  // timer handles recursion
  assert(THREAD->is_Java_thread(), "non-JavaThread in link_class_impl");
  JavaThread* jt = (JavaThread*)THREAD;
  PerfTraceTimedEvent vmtimer(ClassLoader::perf_class_link_time(),
                        ClassLoader::perf_classes_linked(),
                        jt->get_thread_stat()->class_link_recursion_count_addr());
  
  // link super class before linking this class
  instanceKlassHandle super(THREAD, this_oop->super());
  if (super.not_null()) {
    if (super->is_interface()) {  // check if super class is an interface
      ResourceMark rm(THREAD);
      Exceptions::fthrow(  
        THREAD_AND_LOCATION,
        vmSymbolHandles::java_lang_IncompatibleClassChangeError(),
        "class %s has interface %s as super class",
        this_oop->external_name(),
        super->external_name()
      );
      return false;
    }

    link_class_impl(super, throw_verifyerror, CHECK_false);
  }
  
  // link all interfaces implemented by this class before linking this class
  objArrayHandle interfaces (THREAD, this_oop->local_interfaces());
  int num_interfaces = interfaces->length();
  for (int index = 0; index < num_interfaces; index++) {
    HandleMark hm(THREAD);
    instanceKlassHandle ih(THREAD, klassOop(interfaces->obj_at(index)));
    link_class_impl(ih, throw_verifyerror, CHECK_false);
  }
  
  // in case the class is linked in the process of linking its superclasses
  if (this_oop->is_linked()) {
    return true;
  }
   
  // verification & rewriting
  {
ObjectLocker ol(this_oop());
    // rewritten will have been set if loader constraint error found
    // on an earlier link attempt
    // don't verify or rewrite if already rewritten
    if (!this_oop->is_linked()) {
      if (!this_oop->is_rewritten()) {
        {
          assert(THREAD->is_Java_thread(), "non-JavaThread in link_class_impl");
          JavaThread* jt = (JavaThread*)THREAD;
          // Timer includes any side effects of class verification (resolution,
          // etc), but not recursive entry into verify_code().
          PerfTraceTime timer(ClassLoader::perf_class_verify_time(),
                            jt->get_thread_stat()->class_verify_recursion_count_addr());
          bool verify_ok = verify_code(this_oop, throw_verifyerror, THREAD);
          if (!verify_ok) {
            return false;
          }
        }

        // Just in case a side-effect of verify linked this class already
        // (which can sometimes happen since the verifier loads classes 
        // using custom class loaders, which are free to initialize things)
        if (this_oop->is_linked()) {
          return true;
        }

        // also sets rewritten
        this_oop->rewrite_class(CHECK_false);
      }
  
      // Initialize the vtable and interface table after
      // methods have been rewritten since rewrite may
      // fabricate new methodOops.
      // also does loader constraint checking
      if (!this_oop()->is_shared()) {
        ResourceMark rm(THREAD);
        this_oop->vtable()->initialize_vtable(true, CHECK_false);
        this_oop->itable()->initialize_itable(true, CHECK_false);
      }
#ifdef ASSERT
      else {
        ResourceMark rm(THREAD);
        this_oop->vtable()->verify(tty, true);
        // In case itable verification is ever added.
        // this_oop->itable()->verify(tty, true);
      }
#endif
      this_oop->set_init_state(linked);
      if (JvmtiExport::should_post_class_prepare()) {
        Thread *thread = THREAD;
        assert(thread->is_Java_thread(), "thread->is_Java_thread()");
        JvmtiExport::post_class_prepare((JavaThread *) thread, this_oop());
      }
    }
  }    
  return true;
}


// Rewrite the byte codes of all of the methods of a class.
// Three cases:
//    During the link of a newly loaded class.
//    During the preloading of classes to be written to the shared spaces.
//	- Rewrite the methods and update the method entry points.
//
//    During the link of a class in the shared spaces.
//	- The methods were already rewritten, update the metho entry points.
//
// The rewriter must be called exactly once. Rewriting must happen after
// verification but before the first method of the class is executed.

void instanceKlass::rewrite_class(TRAPS) {
  assert(is_loaded(), "must be loaded");
  instanceKlassHandle this_oop(THREAD, this->as_klassOop());  
  if (this_oop->is_rewritten()) {
    assert(this_oop()->is_shared(), "rewriting an unshared class?");
    return;
  }
  Rewriter::rewrite(this_oop, CHECK); // No exception can happen here
  this_oop->set_rewritten();
}


// --- initialize_impl2
// Loading superclasses before taking the class-loading lock here seems to be
// allowed by the Spec - which is otherwise ENTIRELY unclear here.  It's an
// executable spec and not a declarative spec - which means I can do anything I
// want here as long as you can't tell otherwise.  Since most of these actions
// are invisible at the Java level I think this hack is both legal - and closes
// a narrow deadlock possibility.

// Deadlock: Class A has children B & C; A.<clinit> directly references B.
// All classes are loaded but not initialized.
// Thread1 touches class B.  Takes the lock for init'ing B.
// Thread2 touches class C.  Takes the lock for init'ing C.
// Thread2 then initializes A; takes the lock for init'ing A.
// Thread1 then attempts to init A; blocks on the lock for init'ing A (held by Thread2).
// Thread2 then runs A.<clinit> - which attempts to init B.
// Thread2 then blocks on the init for class B (held by Thread1).
// The 2 threads are now deadlocked.
  
// This deadlock is senseless because in a tree structure you can always acquire
// locks in rank order and avoid the deadlock.  ClassLoadSupersFirst removes the
// deadlock by initializing the superclass before taking the child class init lock.

// Alas, this deadlock hit in a large customer running a well known, common 3rd
// party library.
static bool initialize_impl2(bool take_init_lock, instanceKlassHandle this_oop, TRAPS) {
  // refer to the JVM book page 47 for description of steps
  // Step 1
ObjectLocker ol(this_oop());

  Thread *self = THREAD; // it's passed the current thread

  // Step 2
  while(this_oop->is_being_initialized() && !this_oop->is_reentrant_initialization(self)) {      
ol.waitUninterruptibly(CHECK_(true));
  }
  
  // Step 3
  if (this_oop->is_being_initialized() && this_oop->is_reentrant_initialization(self))
    return true;
  
  // Step 4 
  if (this_oop->is_initialized())
    return true;
  
  // Step 5
  if (this_oop->is_in_error_state()) {
THROW_(vmSymbols::java_lang_NoClassDefFoundError(),true);
  }
  
  if( take_init_lock ) {
      // Step 6
this_oop->set_init_state(instanceKlass::being_initialized);
    this_oop->set_init_thread(self);
  }

  return false;
}


void instanceKlass::initialize_impl(instanceKlassHandle this_oop, TRAPS) {
  // Make sure klass is linked (verified) before initialization
  // A class could already be verified, since it has been reflected upon.
  this_oop->link_class(CHECK);

  // Refer to the JVM book page 47 for description of steps
  // Steps 1-6 normally, 1-5 if ClassLoadSupersFirst
  if( initialize_impl2(!ClassLoadSupersFirst, this_oop, THREAD) )
    return;
  
  // Step 7
  klassOop super_klass = this_oop->super();
  if (super_klass != NULL && !this_oop->is_interface() && Klass::cast(super_klass)->should_be_initialized()) {
    Klass::cast(super_klass)->initialize(THREAD);

    if (HAS_PENDING_EXCEPTION) {
      Handle e(THREAD, PENDING_EXCEPTION);
      CLEAR_PENDING_EXCEPTION;
      {
        EXCEPTION_MARK;
        this_oop->set_initialization_state_and_notify(initialization_error, THREAD); // Locks object, set state, and notify all waiting threads
        CLEAR_PENDING_EXCEPTION;   // ignore any exception thrown, superclass initialization error is thrown below
      }
      THROW_OOP(e());
    }
  }

  // Nothing normally, or steps 1-6 if ClassLoadSupersFirst - because now the
  // Super has been initialized first.  See above Largish comment on why.
  if( ClassLoadSupersFirst ) 
    if( initialize_impl2(true, this_oop, THREAD) )
      return;

  // Step 8  
  {
    assert(THREAD->is_Java_thread(), "non-JavaThread in initialize_impl");
    JavaThread* jt = (JavaThread*)THREAD;
    // Timer includes any side effects of class initialization (resolution,
    // etc), but not recursive entry into call_class_initializer().
    PerfTraceTimedEvent timer(ClassLoader::perf_class_init_time(),
                              ClassLoader::perf_classes_inited(),
                              jt->get_thread_stat()->class_init_recursion_count_addr());
    this_oop->call_class_initializer(THREAD);
  }

  // Step 9
  if (!HAS_PENDING_EXCEPTION) {    
    this_oop->set_initialization_state_and_notify(fully_initialized, CHECK);
    { ResourceMark rm(THREAD);
      debug_only(this_oop->vtable()->verify(tty, true);)
    }
  }
  else {    
    // Step 10 and 11
    Handle e(THREAD, PENDING_EXCEPTION);
    CLEAR_PENDING_EXCEPTION;
    { 
      EXCEPTION_MARK;
      this_oop->set_initialization_state_and_notify(initialization_error, THREAD);
      CLEAR_PENDING_EXCEPTION;   // ignore any exception thrown, class initialization error is thrown below
    }
    if (e->is_a(SystemDictionary::error_klass())) {
      THROW_OOP(e());
    } else {
      JavaCallArguments args(e);
      THROW_ARG(vmSymbolHandles::java_lang_ExceptionInInitializerError(),
                vmSymbolHandles::throwable_void_signature(),
                &args);      
    }
  }
}
  

// Note: implementation moved to static method to expose the this pointer.
void instanceKlass::set_initialization_state_and_notify(ClassState state, TRAPS) {
  instanceKlassHandle kh(THREAD, this->as_klassOop());
  set_initialization_state_and_notify_impl(kh, state, CHECK);
}

void instanceKlass::set_initialization_state_and_notify_impl(instanceKlassHandle this_oop, ClassState state, TRAPS) {    
ObjectLocker ol(this_oop());
  this_oop->set_init_state(state);
  ol.notify_all(CHECK);  
}

void instanceKlass::process_interface_and_abstract(){
  if( is_abstract() ) return;  // Only interested in concrete implementors
  // (Note: Interfaces are never on the subklass list.)

  assert_lock_strong(Compile_lock);  
  // CodeCache can only be updated by a thread_in_VM and they will all be
  // stopped dring the safepoint so CodeCache will be safe to update without
  // holding the CodeCache_lock.
  klassRef this_as_ref(this->as_klassOop());
  
  objArrayOop interfaces = transitive_interfaces();
int len=interfaces->length();
for(int i=0;i<len;i++){
instanceKlass*iface=instanceKlass::cast(klassOop(interfaces->obj_at(i)));
assert(iface->is_interface(),"expected interface");
    int nof = iface->nof_implementors();
    if( nof == 0 ) {            // Parent now gets a concrete impl
      ref_store_without_check(&iface->_implementor, this_as_ref);
    } else if( nof == 1 ) {     // Else we get 2+ implementors
      if( this->is_subtype_of(iface->implementor().as_klassOop()) ) {
        // Actually, allow a tree of concrete implementors as long as only a  
        // single root klass directly implements.
      } else {                  // Else we get 2+ implementors
        *(intptr_t*)&iface->_implementor = -1;
      }
    }

    iface->deoptimize_codeblobrefs_dependent_on_class_hierarchy(this);
  }


  // Note self as sole implementor of self
  ref_store_without_check(&_implementor, this_as_ref);
  
  // Invariant: nof_implementors() returns the number of direct children with
  // 1 or more concrete subklasses (including self).  So a subtree consisting
  // solely of abstract classes does not count.  

  // Example class hierchary:
  //   abstract class A - The 1 implementor() is B
  //     abstract class B - The 1 implementor() is C
  //       concrete class C - The 1 implementor() is C
  //         abstract class D - No implementors()

  // Now we add E:
  //   abstract class A - The 1 implementor() is B
  //     abstract class B - The 1 implementor() is C
  //       concrete class C - Two or more direct implementors() (self and E)
  //         abstract class D - The 1 implementor() is E
  //           concrete class E - The 1 implementor() is E

  // Example2: Add a new concrete class C2 parallel to C, child of B
  //   abstract class A - The 1 implementor() is B
  //     abstract class B - Two or more direct implementors() (C1 and C2)
  //       concrete class C1 - The 1 implementor() is C1
  //       concrete class C2 - The 1 implementor() is C2

  
  // Simple incremental update loop on nof_implementors():
  // If the parent nof_implementors() does not change, then no need to go up further.
  // If parent class is already at '2', it cannot change.
  // Parent changes if we add a concrete child, or an abstract child goes from
  // 0 to 1 implementors.

  // First pass I am adding a concrete child, later passes do not 'add' a
  // class but can see an abstract child go from 0 to 1.
  klassOop child = this->as_klassOop();
  klassOop parent = super();
  while( parent ) {             // While not root
instanceKlass*pik=instanceKlass::cast(parent);
    int p_nof = pik->nof_implementors();
    if( p_nof >= 2 ) break; // Parent already maxed out, cannot make it more conservative

    // parent has 0 or 1 impls
    if( p_nof == 0 ) {          // Parent now gets a new concrete impl
      ref_store_without_check(&pik->_implementor, child);
      // Recursively repeat for parents
    } else {                    // Else parent has an impl already
      *(intptr_t*)&pik->_implementor = -1;
      break;                    // No further change possible 'cause we did not add a new direct subclass
    }
    child = parent;             // Link uphill
    parent = pik->super();
  }
}

void instanceKlass::deoptimize_code_dependent_on( ) const {
  // Deoptimize compiled code for dependee and all its superclasses
klassOop d=this->super();
  while( d ) {
instanceKlass*ik=instanceKlass::cast(d);
    ik->deoptimize_codeblobrefs_dependent_on_class_hierarchy(this);
    d = ik->super();
  }
}

void instanceKlass::deoptimize_evol_dependent_on() const {
  Unimplemented();
//  // Deoptimize compiled code for all methods of this evolving class
//  deoptimize_nmethods_dependent_on_class_hierarchy(this);
}


void instanceKlass::dec_implementor(BoolObjectClosure*is_alive){
  // Remove interface dependencies first
  objArrayOop interfaces = transitive_interfaces();
int len=interfaces->length();
for(int i=0;i<len;i++){
instanceKlass*iface=instanceKlass::cast(klassOop(interfaces->obj_at(i)));
    if( iface->implementor().as_oop() == this->as_klassOop() )
      iface->_implementor = nullRef;
  }

  // If removing NO concrete implementors, then parent classes are not affected.
  if( nof_implementors()==0 ) return;
  _implementor = nullRef;       // Lose the implementor, if any

  klassRef superref = super_ref();
  if( superref.is_null() ) return; // Object is a root, no parent

  // Check if the super-klass is ALSO being unloaded.  We are only interested
  // in the unloaded/loaded transition.  In general there is a tree of classes
  // being unloaded.  We do not care about the inside of the tree - just where
  // the unloaded root impacts the remaining classes.
  if( !is_alive->do_object_b(superref.as_oop()) ) return;
  
  // Remove an implementor from our parent
  instanceKlass::cast(superref.as_klassOop())->dec_implementor_impl(is_alive);
}

// Call here when we definitely are losing a (recursive) concrete child implementor
void instanceKlass::dec_implementor_impl(BoolObjectClosure*is_alive){
  assert0( is_alive->do_object_b(this->as_klassOop()) );
  int nof = nof_implementors();
  assert0( nof > 0 );           // Better know we had an implementor to lose
  _implementor = nullRef;       // Lose the implementor, if any

  if( nof > 1 ) {       // Hard case: we lost one (of maybe many) implementors
    // Compute if we are down to 1 implementor or still at 2+
    // Concrete self counts as an implementor
    if( !is_abstract() ) {
      _implementor = POISON_KLASSREF(klassRef(this->as_klassOop()));
    }
    // Visit all children and count implementors
    objectRef sref = subklass_oop();
    klassOop s = (klassOop)sref.as_oop();
    while( s ) {
      // Does the have some implementors?
instanceKlass*sk=instanceKlass::cast(s);
      if( sk->nof_implementors() > 0 ) { // Found an implementor?
        if( nof_implementors() == 0 ) { // Self going from 0->1 implementors?
          _implementor = ALWAYS_POISON_OBJECTREF(sref); // Yes, then record the
                                                        // One True Implementor
        } else {                // Else going from 1 to 2
          *(intptr_t*)&_implementor = -1;
          break;                // No need to count further, got too many already
        }
      }
      // Walk all children of self, looking for implementors
      sref = sk->next_sibling_oop();
      s = (klassOop)sref.as_oop();
    }
  }
  // Unless we drop to 0, then we did not lower our parent's count of concrete children
  if( nof_implementors() > 0 ) return;

  // We need to account for a 1->0 transition, when this class is still live;
  // otherwise the dependencies wont match up to the _implementor.  
  if (nof == 1)
deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl(NULL);

  // Recursively inform parent that it lost an implementor
  objectRef super_ref = super();
  if( super_ref.is_null() ) return; // Object is a root, no parent
  instanceKlass::cast((klassOop)super_ref.as_oop())->dec_implementor_impl(is_alive);
}


// Call here when we definitely are losing a (recursive) concrete child implementor
void instanceKlass::GPGC_dec_implementor(){
  // Remove interface dependencies first
#ifdef ASSERT
  objectRef* ti_addr = adr_transitive_interfaces();
  objectRef ti_ref = UNPOISON_OBJECTREF(*ti_addr, ti_addr);
  assert0(ti_ref.is_old());
#endif // ASSERT
  objArrayOop interfaces = transitive_interfaces();
int len=interfaces->length();
for(int i=0;i<len;i++){
instanceKlass*iface=instanceKlass::cast(klassOop(interfaces->obj_at(i)));
    if( iface->implementor().as_oop() == this->as_klassOop() )
      iface->_implementor = nullRef;
  }

  // If removing NO concrete implementors, then parent classes are not affected.
  if( nof_implementors()==0 ) return;
  _implementor = nullRef;       // Lose the implementor, if any

  heapRef super_ref = GPGC_Collector::perm_remapped_only((heapRef*)adr_super());
  if( super_ref.is_null() ) return; // Object is a root, no parent

  // Check if the super-klass is ALSO being unloaded.  We are only interested
  // in the unloaded/loaded transition.  In general there is a tree of classes
  // being unloaded.  We dont care about the inside of the tree - just where
  // the unloaded root impacts the remaining classes.
  if( !GPGC_Marks::is_old_marked_strong_live(super_ref)
    && !GPGC_Marks::is_old_marked_final_live(super_ref) )
  {
    return;
  }
  
  // Remove an implementor from our parent
  instanceKlass::cast((klassOop)super_ref.as_oop())->GPGC_dec_implementor_impl();
}

void instanceKlass::GPGC_dec_implementor_impl(){
  assert0( GPGC_Marks::is_any_marked_strong_live(this->as_klassOop())
    || GPGC_Marks::is_any_marked_final_live(this->as_klassOop()) );

  int nof = nof_implementors();
  _implementor = nullRef;       // Lose the implementor, if any

  if( nof > 1 ) {       // Hard case: we lost one (of maybe many) implementors
    // Compute if we are down to 1 implementor or still at 2+
    // Concrete self counts as an implementor
    if( !is_abstract() ) {
      _implementor = POISON_KLASSREF(klassRef(this->as_klassOop()));
    }
    // Visit all children and count implementors
    heapRef sref = GPGC_Collector::perm_remapped_only((heapRef*)adr_subklass());
    klassOop s = (klassOop)sref.as_oop();
    while( s ) {
      // Does the have some implementors?
instanceKlass*sk=instanceKlass::cast(s);
      if( sk->nof_implementors() > 0 ) { // Found an implementor?
        if( nof_implementors() == 0 ) { // Self going from 0->1 implementors?
          _implementor = ALWAYS_POISON_OBJECTREF(sref);  // Yes, then record the
                                                         // One True Implementor
        } else {                // Else going from 1 to 2
          *(intptr_t*)&_implementor = -1;
          break;                // No need to count further, got too many already
        }
      }
      // Walk all children of self, looking for implementors
      sref = GPGC_Collector::perm_remapped_only((heapRef*)sk->adr_next_sibling());
      s = (klassOop)sref.as_oop();
    }
  }
  // Unless we drop to 0, then we did not lower our parent's count of concrete children
  if( nof_implementors() > 0 ) return;

  // We need to account for a 1 ->0 transition, when this class is still live;
  // otherwise the dependencies wont match up to the _implementor.  
  if (nof == 1)
deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl(NULL);

  // Recursively inform parent that it lost an implementor
  heapRef super_ref = GPGC_Collector::perm_remapped_only((heapRef*)adr_super());
  if( super_ref.is_null() ) return; // Object is a root, no parent
  instanceKlass::cast((klassOop)super_ref.as_oop())->GPGC_dec_implementor_impl();
}


#ifndef PRODUCT
static bool is_alive_func(klassOop C_oop, int gc_mode, BoolObjectClosure* is_alive) {
  // gc_mode:
  // 0 - no GC active
  // 1 - Classic (serial, parallel) GC active
  // 3 - GenPauseless GC active
  if( gc_mode == 0 ) return true;
  if( gc_mode == 1 ) return is_alive->do_object_b(C_oop);
  if( gc_mode == 3 ) {
    return ( GPGC_Marks::is_perm_marked_strong_live(C_oop)
      || GPGC_Marks::is_perm_marked_final_live(C_oop) );
  }

  ShouldNotReachHere();
  return false;
}

// --- verify_dependencies
void instanceKlass::verify_dependencies(Klass* K, int gc_mode, BoolObjectClosure* is_alive) {
  while( 1 ) {
    if( !K ) return;
    if( is_alive_func(K->as_klassOop(), gc_mode, is_alive) && // Ignore unloading classes
        K->oop_is_instance() ) { // ignore array klasses?
instanceKlass*ik=(instanceKlass*)K;

      // post-order: verify children first
      verify_dependencies(K->subklass(), gc_mode, is_alive);
      
      // verify K now
      // verify nof_implementors first
      int nof = ik->nof_implementors();
      if( nof == 0 ) {
        // All children must have 0 implementors as well
        assert0( ik->is_abstract() ); // concrete must be at least 1
Klass*C=ik->subklass();
        while( C ) {
klassOop C_oop=C->as_klassOop();
          assert0( !is_alive_func(C_oop, gc_mode, is_alive) || instanceKlass::cast(C_oop)->nof_implementors() == 0 );
          C = C->next_sibling();
        }
      } else if( nof == 1 && !ik->is_interface() ) {
        // No more than 1 child (AND self if concrete) with >0 implementors
        int cnt = ik->is_abstract() ? 0 : 1;
Klass*C=ik->subklass();
        while( C ) {
klassOop C_oop=C->as_klassOop();
          if( is_alive_func(C_oop, gc_mode, is_alive) && instanceKlass::cast(C_oop)->nof_implementors() > 0 ) {
            assert0( cnt == 0 ); // Have not already found an implementor
cnt++;//Found one now!
          }
          C = C->next_sibling();
        }
        assert0( cnt == 1 );    // Exactly 1, not 0, not many
      } else if( !ik->is_interface() && ik->super() /*do not bother with Object root*/ ) {
        int cnt = ik->is_abstract() ? 0 : 1;
Klass*C=ik->subklass();
        while( C ) {
klassOop C_oop=C->as_klassOop();
          if( is_alive_func(C_oop, gc_mode, is_alive) && instanceKlass::cast(C_oop)->nof_implementors() > 0 )
cnt++;//Found one now!
          C = C->next_sibling();
        }
        assert0( cnt >= 2 );    // Must find at least 2 or we should have optimized
      }

      // verify any code dependencies
      if( ik->_dependent_mcoRef.not_null() ) {
        const objArrayOop mcos = ik->dependent_mcos();
for(int i=0;i<mcos->length();i++){
          const methodCodeOop mco = (const methodCodeOop)mcos->obj_at(i);
          if( !mco ) break;
          mco->verify_dependencies(ik);
        }
      }
    }
    // verify siblings last
    K = K->next_sibling();
  }
}
#endif // !PRODUCT
// verify klass-chain: implementors and liveness of klasses 
void instanceKlass::gpgc_verify_klass_chain(Klass* K) {
  assert0(UseGenPauselessGC);
  while( 1 ) {
    if( !K ) return;
    if( K->oop_is_instance() ) { // ignore array klasses
      
instanceKlass*ik=(instanceKlass*)K;

      // post-order: verify children first
      gpgc_verify_klass_chain(K->subklass());
      
      // verify K now
      // verify nof_implementors first
      int nof = ik->nof_implementors();
      if( nof == 0 ) {
        // All children must have 0 implementors as well
        assert0( ik->is_abstract() ); // concrete must be at least 1
Klass*C=ik->subklass();
        while( C ) {
          // assert  liveness
          assert(GPGC_Marks::is_any_marked_strong_live(C->as_klassOop()), "should only find live sub-klasses in the chain" );
klassOop C_oop=C->as_klassOop();
          assert0( instanceKlass::cast(C_oop)->nof_implementors() == 0 );
          C = C->next_sibling();
        }
      } else if( nof == 1 && !ik->is_interface() ) {
        // No more than 1 child (AND self if concrete) with >0 implementors
        int cnt = ik->is_abstract() ? 0 : 1;
Klass*C=ik->subklass();
        while( C ) {
          assert(GPGC_Marks::is_any_marked_strong_live(C->as_klassOop()), "should only find live sub-klasses in the chain" );
klassOop C_oop=C->as_klassOop();
          if( instanceKlass::cast(C_oop)->nof_implementors() > 0 ) {
            assert0( cnt == 0 ); // Have not already found an implementor
cnt++;//Found one now!
          }
          C = C->next_sibling();
        }
        assert0( cnt == 1 );    // Exactly 1, not 0, not many
      } else if( !ik->is_interface() && ik->super() /*do not bother with Object root*/ ) {
        int cnt = ik->is_abstract() ? 0 : 1;
Klass*C=ik->subklass();
        while( C ) {
          assert(GPGC_Marks::is_any_marked_strong_live(C->as_klassOop()), "should only find live sub-klasses in the chain" );
klassOop C_oop=C->as_klassOop();
          if( instanceKlass::cast(C_oop)->nof_implementors() > 0 )
cnt++;//Found one now!
          C = C->next_sibling();
        }
        assert0( cnt >= 2 );    // Must find at least 2 or we should have optimized
      }
    }
    // verify siblings last
    K = K->next_sibling();
  }
}


bool instanceKlass::can_be_primary_super_slow() const {
  if (is_interface())
    return false;
  else
    return Klass::can_be_primary_super_slow();
}

objArrayOop instanceKlass::compute_secondary_supers(int num_extra_slots, TRAPS) {
  // The secondaries are the implemented interfaces.
  instanceKlass* ik = instanceKlass::cast(as_klassOop());
  objArrayHandle interfaces (THREAD, ik->transitive_interfaces());
  int num_secondaries = num_extra_slots + interfaces->length();
  if (num_secondaries == 0) {
    return Universe::the_empty_system_obj_array();
  } else if (num_extra_slots == 0) {
    return interfaces();
  } else {
    // a mix of both
    objArrayOop secondaries = oopFactory::new_system_objArray(num_secondaries, CHECK_NULL);
    for (int i = 0; i < interfaces->length(); i++) {
      secondaries->obj_at_put(num_extra_slots+i, interfaces->obj_at(i));
    }
    return secondaries;
  }
}

bool instanceKlass::compute_is_subtype_of(klassOop k) {
  if (Klass::cast(k)->is_interface()) {
    return implements_interface(k);
  } else {
    return Klass::compute_is_subtype_of(k);
  }
}

bool instanceKlass::implements_interface(klassOop k) const {
  if (as_klassOop() == k) return true;
  assert(Klass::cast(k)->is_interface(), "should be an interface class");
  for (int i = 0; i < transitive_interfaces()->length(); i++) {
    if (transitive_interfaces()->obj_at(i) == k) {
      return true;
    }
  }
  return false;
}

objArrayOop instanceKlass::allocate_objArray(int n, int length, intptr_t sba_hint, TRAPS) {
  if (length < 0) THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  if (length > arrayOopDesc::max_array_length(T_OBJECT)) {
    THROW_OOP_0(Universe::out_of_memory_error_array_size());
  }
  int size = objArrayOopDesc::object_size(length);
klassRef ak=array_klass(klassRef(as_klassOop()),n,CHECK_NULL);
KlassHandle h_ak(ak);
objArrayOop o=NULL;
  assert0( THREAD->is_Java_thread() );
  if( UseSBA )
    o = (objArrayOop)((JavaThread*)THREAD)->sba_area()->allocate(h_ak(), size, length, sba_hint).as_oop();
if(o==NULL)
    o = (objArrayOop)CollectedHeap::array_allocate(h_ak, size, length, CHECK_NULL);
  return o;
}

instanceOop instanceKlass::register_finalizer(instanceOop i, TRAPS) {
  if (TraceFinalizerRegistration) {
    tty->print("Registered ");
    i->print_value_on(tty);
tty->print_cr(" ("PTR_FORMAT") as finalizable",(address)i);
  }
  instanceHandle h_i(THREAD, i);
  // Pass the handle as argument, JavaCalls::call expects oop as jobjects
  JavaValue result(T_VOID);
  JavaCallArguments args(h_i);  
  methodHandle mh (THREAD, Universe::finalizer_register_method());
  JavaCalls::call(&result, mh, &args, CHECK_NULL);
  return h_i();
}

instanceOop instanceKlass::allocate_instance(intptr_t sba_hint, TRAPS) {
  assert0( THREAD->is_Java_thread() );
  bool has_finalizer_flag = has_finalizer(); // Query before possible GC
  int size = size_helper();  // Query before forming handle.

  KlassHandle h_k(THREAD, as_klassOop());

  instanceOop i = NULL;
  if( UseSBA && !has_finalizer_flag )
    i = (instanceOop) ((JavaThread*)THREAD)->sba_area()->allocate(h_k(), size, -1, sba_hint ).as_oop();
if(i==NULL){
((JavaThread*)THREAD)->mmu_start_pause();
    i = (instanceOop)CollectedHeap::obj_allocate(h_k, size, CHECK_NULL);
((JavaThread*)THREAD)->mmu_end_pause();
  }
  if (has_finalizer_flag && !RegisterFinalizersAtInit) {
    i = register_finalizer(i, CHECK_NULL);
  }
  return i;
}

instanceOop instanceKlass::allocate_permanent_instance(TRAPS) {
  // Finalizer registration occurs in the Object.<init> constructor
  // and constructors normally aren't run when allocating perm
  // instances so simply disallow finalizable perm objects.  This can
  // be relaxed if a need for it is found.
  assert(!has_finalizer(), "perm objects not allowed to have finalizers");
  int size = size_helper();  // Query before forming handle.
  KlassHandle h_k(THREAD, as_klassOop());
  instanceOop i = (instanceOop)
    CollectedHeap::permanent_obj_allocate(h_k, size, CHECK_NULL);
  return i;
}

void instanceKlass::check_valid_for_instantiation(bool throwError, TRAPS) {
  if (is_interface() || is_abstract()) {
    ResourceMark rm(THREAD);
    THROW_MSG(throwError ? vmSymbols::java_lang_InstantiationError()
              : vmSymbols::java_lang_InstantiationException(), external_name());
  }
  if (as_klassOop() == SystemDictionary::class_klass()) {
    ResourceMark rm(THREAD);
    THROW_MSG(throwError ? vmSymbols::java_lang_IllegalAccessError()
              : vmSymbols::java_lang_IllegalAccessException(), external_name());
  }
}

klassRef instanceKlass::array_klass_impl(klassRef thsi,bool or_null,int n,TRAPS){
  instanceKlassHandle this_oop(thsi);
  return array_klass_impl(this_oop, or_null, n, THREAD);
}

klassRef instanceKlass::array_klass_impl(instanceKlassHandle this_oop, bool or_null, int n, TRAPS) {    
  if (this_oop->array_klasses() == NULL) {
    if (or_null) return klassRef();

    ResourceMark rm;
    assert0( THREAD->is_Java_thread() );
    JavaThread *jt = (JavaThread *)THREAD;
    {
      // Atomic creation of array_klasses
MutexLockerAllowGC mc(Compile_lock,jt);//for vtables
MutexLockerAllowGC ma(MultiArray_lock,jt);

      // Check if update has already taken place    
      if (this_oop->array_klasses() == NULL) {
        objArrayKlassKlass* oakk =
          (objArrayKlassKlass*)Universe::objArrayKlassKlassObj()->klass_part();

        klassOop  k = oakk->allocate_objArray_klass(1, this_oop, CHECK_(klassRef()));
        this_oop->set_array_klasses(k);
      }
    }
  }
  // _this will always be set at this point
klassRef oak=this_oop->array_klasses_ref();
  if (or_null) {
    return Klass::cast(oak.as_klassOop())->array_klass_or_null(n);
  }
  return Klass::array_klass(oak, n, CHECK_(klassRef()));
}

klassRef instanceKlass::array_klass_impl(klassRef thsi, bool or_null, TRAPS) {
  return array_klass_impl(thsi, or_null, 1, THREAD);
}

void instanceKlass::call_class_initializer(TRAPS) {
  instanceKlassHandle ik (THREAD, as_klassOop());
  call_class_initializer_impl(ik, THREAD);
}

static int call_class_initializer_impl_counter = 0;   // for debugging

methodOop instanceKlass::class_initializer() {
  return find_method(vmSymbols::class_initializer_name(), vmSymbols::void_method_signature()); 
}

void instanceKlass::call_class_initializer_impl(instanceKlassHandle this_oop, TRAPS) {  
  methodHandle h_method(THREAD, this_oop->class_initializer());
  assert(!this_oop->is_initialized(), "we cannot initialize twice");
  if (TraceClassInitialization) {
    tty->print("%d Initializing ", call_class_initializer_impl_counter++);
    this_oop->name()->print_value();
tty->print_cr("%s ("PTR_FORMAT")",h_method()==NULL?"(no method)":"",(address)this_oop());
  }
  if (h_method() != NULL) {  
    JavaCallArguments args; // No arguments
    JavaValue result(T_VOID);
    JavaCalls::call(&result, h_method, &args, CHECK); // Static call (no args)
  }
}


jfieldIDCache *instanceKlass::get_or_create_jfieldid_cache() {
  // Dirty read, then CAS in a new one.
if(_jfieldid_cache==NULL){
    // Otherwise, allocate a new one.  
    jfieldIDCache *cache = new jfieldIDCache();
    Atomic::write_barrier();
    // CAS in the new value.  
    if( Atomic::cmpxchg_ptr((intptr_t)cache, (intptr_t*)&_jfieldid_cache, 0) != 0 )
      delete cache;
  }
  return _jfieldid_cache;
}


bool instanceKlass::find_local_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const {  
  const int n = fields()->length();
  for (int i = 0; i < n; i += next_offset ) {
    int name_index = fields()->ushort_at(i + name_index_offset);
    int sig_index  = fields()->ushort_at(i + signature_index_offset);
    symbolOop f_name = constants()->symbol_at(name_index);
    symbolOop f_sig  = constants()->symbol_at(sig_index);
    if (f_name == name && f_sig == sig) {
      fd->initialize(as_klassOop(), i);
      return true;
    }
  }
  return false;
}


void instanceKlass::field_names_and_sigs_iterate(OopClosure* closure) {  
  Unimplemented();
}


klassOop instanceKlass::find_interface_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const {
  const int n = local_interfaces()->length();
  for (int i = 0; i < n; i++) {
    klassOop intf1 = klassOop(local_interfaces()->obj_at(i));
    assert(Klass::cast(intf1)->is_interface(), "just checking type");
    // search for field in current interface
    if (instanceKlass::cast(intf1)->find_local_field(name, sig, fd)) {
      assert(fd->is_static(), "interface field must be static");
      return intf1;
    }
    // search for field in direct superinterfaces
    klassOop intf2 = instanceKlass::cast(intf1)->find_interface_field(name, sig, fd);
    if (intf2 != NULL) return intf2;
  }
  // otherwise field lookup fails
  return NULL;
}


klassOop instanceKlass::find_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const {
  // search order according to newest JVM spec (5.4.3.2, p.167).
  // 1) search for field in current klass
  if (find_local_field(name, sig, fd)) {
    return as_klassOop();
  }
  // 2) search for field recursively in direct superinterfaces
  { klassOop intf = find_interface_field(name, sig, fd);
    if (intf != NULL) return intf;
  }
  // 3) apply field lookup recursively if superclass exists
  { klassOop supr = super();
    if (supr != NULL) return instanceKlass::cast(supr)->find_field(name, sig, fd);
  }
  // 4) otherwise field lookup fails
  return NULL;
}


klassOop instanceKlass::find_field(symbolOop name, symbolOop sig, bool is_static, fieldDescriptor* fd) const {
  // search order according to newest JVM spec (5.4.3.2, p.167).
  // 1) search for field in current klass
  if (find_local_field(name, sig, fd)) {
    if (fd->is_static() == is_static) return as_klassOop();
  }
  // 2) search for field recursively in direct superinterfaces
  if (is_static) {
    klassOop intf = find_interface_field(name, sig, fd);
    if (intf != NULL) return intf;
  }
  // 3) apply field lookup recursively if superclass exists
  { klassOop supr = super();
    if (supr != NULL) return instanceKlass::cast(supr)->find_field(name, sig, is_static, fd);
  }
  // 4) otherwise field lookup fails
  return NULL;
}


bool instanceKlass::find_local_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const {  
  int length = fields()->length();
  for (int i = 0; i < length; i += next_offset) {
    if (offset_from_fields( i ) == offset) {
      fd->initialize(as_klassOop(), i);      
      if (fd->is_static() == is_static) return true;
    }
  }
  return false;
}


bool instanceKlass::find_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const {
  klassOop klass = as_klassOop();
  while (klass != NULL) {
    if (instanceKlass::cast(klass)->find_local_field_from_offset(offset, is_static, fd)) {
      return true;
    }
    klass = Klass::cast(klass)->super();
  }
  return false;
}


void instanceKlass::methods_do(void f(methodOop method)) {
  int len = methods()->length();
  for (int index = 0; index < len; index++) {
    methodOop m = methodOop(methods()->obj_at(index));
    assert(m->is_method(), "must be method");
    f(m);
  }
}


void instanceKlass::do_local_static_fields(FieldClosure*f,oop obj){
  fieldDescriptor fd;
  int length = fields()->length();
  for (int i = 0; i < length; i += next_offset) {
    fd.initialize(as_klassOop(), i);
    if (fd.is_static()) f->do_field_for(&fd, obj);
  }
}


void instanceKlass::do_local_static_fields(void f(fieldDescriptor*, TRAPS), TRAPS) {
  instanceKlassHandle h_this(THREAD, as_klassOop());
  do_local_static_fields_impl(h_this, f, CHECK);
}
 

void instanceKlass::do_local_static_fields_impl(instanceKlassHandle this_oop, void f(fieldDescriptor* fd, TRAPS), TRAPS) {
  fieldDescriptor fd;
  int length = this_oop->fields()->length();
  for (int i = 0; i < length; i += next_offset) {
    fd.initialize(this_oop(), i);
    if (fd.is_static()) { f(&fd, CHECK); } // Do NOT remove {}! (CHECK macro expands into several statements)
  }
}


void instanceKlass::do_nonstatic_fields(FieldClosure*cl,oop obj){
  instanceKlass* super = superklass();
  if (super != NULL) {
super->do_nonstatic_fields(cl,obj);
  }
  fieldDescriptor fd;
  int length = fields()->length();
  for (int i = 0; i < length; i += next_offset) {
    fd.initialize(as_klassOop(), i);
if(!(fd.is_static()))cl->do_field_for(&fd,obj);
  }  
}


void instanceKlass::array_klasses_do(void f(klassOop k)) {
  if (array_klasses() != NULL)
    arrayKlass::cast(array_klasses())->array_klasses_do(f);
}


void instanceKlass::with_array_klasses_do(void f(klassOop k)) {
  f(as_klassOop());
  array_klasses_do(f);
}

#ifdef ASSERT
static int linear_search(objArrayOop methods, symbolOop name, symbolOop signature) {
  int len = methods->length();
  for (int index = 0; index < len; index++) {
    methodOop m = (methodOop)(methods->obj_at(index));
    assert(m->is_method(), "must be method");
    if (m->signature() == signature && m->name() == name) {
       return index;
    }
  }
  return -1;
}
#endif

methodOop instanceKlass::find_method(symbolOop name, symbolOop signature) const {
  return instanceKlass::find_method(methods(), name, signature);
}

methodOop instanceKlass::find_method(objArrayOop methods, symbolOop name, symbolOop signature) {
  int len = methods->length();
  // methods are sorted, so do binary search
  int l = 0;
  int h = len - 1;
  while (l <= h) {
    int mid = (l + h) >> 1;
    methodOop m = (methodOop)methods->obj_at(mid);
    assert(m->is_method(), "must be method");
    int res = m->name()->fast_compare(name);
    if (res == 0) {
      // found matching name; do linear search to find matching signature
      // first, quick check for common case 
      if (m->signature() == signature) return m;
      // search downwards through overloaded methods
      int i;
      for (i = mid - 1; i >= l; i--) {
        methodOop m = (methodOop)methods->obj_at(i);
        assert(m->is_method(), "must be method");
        if (m->name() != name) break;
        if (m->signature() == signature) return m;
      }
      // search upwards
      for (i = mid + 1; i <= h; i++) {
        methodOop m = (methodOop)methods->obj_at(i);
        assert(m->is_method(), "must be method");
        if (m->name() != name) break;
        if (m->signature() == signature) return m;
      }
      // not found
#ifdef ASSERT
      int index = linear_search(methods, name, signature);
      if (index != -1) fatal1("binary search bug: should have found entry %d", index);
#endif
      return NULL;
    } else if (res < 0) {
      l = mid + 1;
    } else {
      h = mid - 1;
    }
  }
#ifdef ASSERT
  int index = linear_search(methods, name, signature);
  if (index != -1) fatal1("binary search bug: should have found entry %d", index);
#endif
  return NULL;
} 

methodOop instanceKlass::uncached_lookup_method(symbolOop name, symbolOop signature) const {  
  klassOop klass = as_klassOop();
  while (klass != NULL) {
    methodOop method = instanceKlass::cast(klass)->find_method(name, signature);
    if (method != NULL) return method;
    klass = instanceKlass::cast(klass)->super();
  }
  return NULL;
}

// lookup a method in all the interfaces that this class implements
methodOop instanceKlass::lookup_method_in_all_interfaces(symbolOop name, 
                                                         symbolOop signature) const {
  objArrayOop all_ifs = instanceKlass::cast(as_klassOop())->transitive_interfaces();
  int num_ifs = all_ifs->length();
  instanceKlass *ik = NULL;
  for (int i = 0; i < num_ifs; i++) {
    ik = instanceKlass::cast(klassOop(all_ifs->obj_at(i)));
    methodOop m = ik->lookup_method(name, signature);
    if (m != NULL) {
      return m;
    }
  }
  return NULL;
}

/* jni_id_for_impl for jfieldIds only */
JNIid* instanceKlass::jni_id_for_impl(instanceKlassHandle this_oop, int offset) {
MutexLockerAllowGC ml(JfieldIdCreation_lock,JavaThread::current());
  // Retry lookup after we got the lock
  JNIid* probe = this_oop->jni_ids() == NULL ? NULL : this_oop->jni_ids()->find(offset);
  if (probe == NULL) {
    // Slow case, allocate new static field identifier
    probe = new JNIid(this_oop->as_klassOop(), offset, this_oop->jni_ids());
    this_oop->set_jni_ids(probe);
  }
  return probe;
}


/* jni_id_for for jfieldIds only */
JNIid* instanceKlass::jni_id_for(int offset) {
  JNIid* probe = jni_ids() == NULL ? NULL : jni_ids()->find(offset);
  if (probe == NULL) {
    probe = jni_id_for_impl(this->as_klassOop(), offset);
  }
  return probe;
}


// Lookup or create a jmethodID.
// This code can be called by the VM thread.  For this reason it is critical that
// there are no blocking operations (safepoints) while the lock is held -- or a 
// deadlock can occur.
jmethodID instanceKlass::jmethod_id_for_impl(instanceKlassHandle ik_h, methodHandle method_h) {
  size_t idnum = (size_t)method_h->method_idnum();
  jmethodID* jmeths = ik_h->methods_jmethod_ids_acquire();
  size_t length = 0;
  jmethodID id = NULL;
  // array length stored in first element, other elements offset by one
  if (jmeths == NULL ||                         // If there is no jmethodID array,
      (length = (size_t)jmeths[0]) <= idnum ||  // or if it is too short,
      (id = jmeths[idnum+1]) == NULL) {         // or if this jmethodID isn't allocated

    // Do all the safepointing things (allocations) before grabbing the lock.
    // These allocations will have to be freed if they are unused.

    // Allocate a new array of methods.
    jmethodID* to_dealloc_jmeths = NULL;
    jmethodID* new_jmeths = NULL;
    if (length <= idnum) {
      // A new array will be needed (unless some other thread beats us to it)
      size_t size = MAX2(idnum+1, (size_t)ik_h->idnum_allocated_count());
      new_jmeths = NEW_C_HEAP_ARRAY(jmethodID, size+1);
      memset(new_jmeths, 0, (size+1)*sizeof(jmethodID));
      new_jmeths[0] =(jmethodID)size;  // array size held in the first element
    }

    // Allocate a new method ID.
    jmethodID to_dealloc_id = NULL;
    jmethodID new_id = NULL;
    if (method_h->is_old() && !method_h->is_obsolete()) {
      // The method passed in is old (but not obsolete), we need to use the current version
      methodOop current_method = ik_h->method_with_idnum((int)idnum);
      assert(current_method != NULL, "old and but not obsolete, so should exist");
      methodHandle current_method_h(current_method == NULL? method_h() : current_method);
      new_id = JNIHandles::make_jmethod_id(current_method_h);
    } else {
      // It is the current version of the method or an obsolete method, 
      // use the version passed in
      new_id = JNIHandles::make_jmethod_id(method_h);
    }

    {
      MutexLocker ml(JmethodIdCreation_lock);

      // We must not go to a safepoint while holding this lock.
      debug_only(No_Safepoint_Verifier nosafepoints;)

      // Retry lookup after we got the lock
      jmeths = ik_h->methods_jmethod_ids_acquire();
      if (jmeths == NULL || (length = (size_t)jmeths[0]) <= idnum) {
        if (jmeths != NULL) {
          // We have grown the array: copy the existing entries, and delete the old array
          for (size_t index = 0; index < length; index++) {
            new_jmeths[index+1] = jmeths[index+1];
          }
          to_dealloc_jmeths = jmeths; // using the new jmeths, deallocate the old one
        }
        ik_h->release_set_methods_jmethod_ids(jmeths = new_jmeths);
      } else {
        id = jmeths[idnum+1];
        to_dealloc_jmeths = new_jmeths; // using the old jmeths, deallocate the new one
      }
      if (id == NULL) {
        id = new_id;
        jmeths[idnum+1] = id;  // install the new method ID
      } else {
        to_dealloc_id = new_id; // the new id wasn't used, mark it for deallocation
      }
    }

    // Free up unneeded or no longer needed resources
    FreeHeap(to_dealloc_jmeths);
    if (to_dealloc_id != NULL) {
      JNIHandles::destroy_jmethod_id(to_dealloc_id);
    }
  }
  return id;
}


// Lookup a jmethodID, NULL if not found.  Do no blocking, no allocations, no handles
jmethodID instanceKlass::jmethod_id_or_null(methodOop method) {
  size_t idnum = (size_t)method->method_idnum();
  jmethodID* jmeths = methods_jmethod_ids_acquire();
  size_t length;                                // length assigned as debugging crumb
  jmethodID id = NULL;
  if (jmeths != NULL &&                         // If there is a jmethodID array,
      (length = (size_t)jmeths[0]) > idnum) {   // and if it is long enough,
    id = jmeths[idnum+1];                       // Look up the id (may be NULL)
  }
  return id;
}


// Cache an itable index
void instanceKlass::set_cached_itable_index(size_t idnum, int index) {
  int* indices = methods_cached_itable_indices_acquire();
  if (indices == NULL ||                         // If there is no index array,
      ((size_t)indices[0]) <= idnum) {           // or if it is too short
    // Lock before we allocate the array so we don't leak
MutexLockerAllowGC ml(JNICachedItableIndex_lock,JavaThread::current());
    // Retry lookup after we got the lock
    indices = methods_cached_itable_indices_acquire();
    size_t length = 0;
    // array length stored in first element, other elements offset by one
    if (indices == NULL || (length = (size_t)indices[0]) <= idnum) {
      size_t size = MAX2(idnum+1, (size_t)idnum_allocated_count());
      int* new_indices = NEW_C_HEAP_ARRAY(int, size+1);
      // Copy the existing entries, if any
      size_t i;
      for (i = 0; i < length; i++) {
        new_indices[i+1] = indices[i+1];
      }
      // Set all the rest to -1
      for (i = length; i < size; i++) {
        new_indices[i+1] = -1;
      }
      if (indices != NULL) {
        FreeHeap(indices);  // delete any old indices
      }
      release_set_methods_cached_itable_indices(indices = new_indices);
    }
  } else {
    CHECK_UNHANDLED_OOPS_ONLY(Thread::current()->clear_unhandled_oops());
  }
  // This is a cache, if there is a race to set it, it doesn't matter
  indices[idnum+1] = index; 
}


// Retrieve a cached itable index
int instanceKlass::cached_itable_index(size_t idnum) {
  int* indices = methods_cached_itable_indices_acquire();
  if (indices != NULL && ((size_t)indices[0]) > idnum) {
     // indices exist and are long enough, retrieve possible cached
    return indices[idnum+1]; 
  }
  return -1;
}


void instanceKlass::adjust_static_fields() {
  heapRef* start = start_of_static_fields();
  heapRef* end   = start + static_oop_field_size();
  while (start < end) {
    assert(objectRef::is_null_or_heap(start), "should be a heapRef");
    MarkSweep::adjust_pointer(start);
    start++;
  }
}


void instanceKlass::update_static_fields() {
  heapRef* const start = start_of_static_fields();
  heapRef* const beg_oop = start;
  heapRef* const end_oop = start + static_oop_field_size();
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }
}

void
instanceKlass::update_static_fields(HeapWord* beg_addr, HeapWord* end_addr) {
  heapRef* const start = start_of_static_fields();
  heapRef* const beg_oop = MAX2((heapRef*)beg_addr, start);
  heapRef* const end_oop = MIN2((heapRef*)end_addr, start + static_oop_field_size());
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }
}


#define invoke_closure_on(start, closure, nv_suffix) {                          \
  if (!start->is_null()) {                                                      \
    assert(Universe::is_in_allocation_area(ALWAYS_UNPOISON_OBJECTREF(*start).as_oop()), "should be in heap or stack"); \
    (closure)->do_oop##nv_suffix(start);                                        \
  }                                                                             \
}

// closure's do_header() method dicates whether the given closure should be
// applied to the klass ptr in the object header.

#define InstanceKlass_OOP_OOP_ITERATE_DEFN(OopClosureType, nv_suffix)           \
                                                                                \
int instanceKlass::oop_oop_iterate##nv_suffix(oop obj,                          \
                                              OopClosureType* closure) {        \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::ik); \
  /* header */                                                                  \
  if (closure->do_header()) {                                                   \
    obj->oop_iterate_header(closure);                                           \
  }                                                                             \
  /* instance variables */                                                      \
  OopMapBlock* map     = start_of_nonstatic_oop_maps();                         \
  OopMapBlock* const end_map = map + nonstatic_oop_map_size();                  \
  const intx field_offset    = PrefetchFieldsAhead;                             \
  if (field_offset > 0) {                                                       \
    while (map < end_map) {                                                     \
objectRef*start=obj->ref_field_addr(map->offset());\
      objectRef* const end   = start + map->length();                           \
      while (start < end) {                                                     \
prefetch_beyond(start,end,field_offset,\
                        closure->prefetch_style());                             \
        SpecializationStats::                                                   \
          record_do_oop_call##nv_suffix(SpecializationStats::ik);               \
        invoke_closure_on(start, closure, nv_suffix);                           \
        start++;                                                                \
      }                                                                         \
      map++;                                                                    \
    }                                                                           \
  } else {                                                                      \
    while (map < end_map) {                                                     \
objectRef*start=obj->ref_field_addr(map->offset());\
      objectRef* const end   = start + map->length();                           \
      while (start < end) {                                                     \
        SpecializationStats::                                                   \
          record_do_oop_call##nv_suffix(SpecializationStats::ik);               \
        invoke_closure_on(start, closure, nv_suffix);                           \
        start++;                                                                \
      }                                                                         \
      map++;                                                                    \
    }                                                                           \
  }                                                                             \
  return size_helper();                                                         \
}

#define InstanceKlass_OOP_OOP_ITERATE_DEFN_m(OopClosureType, nv_suffix)         \
                                                                                \
int instanceKlass::oop_oop_iterate##nv_suffix##_m(oop obj,                      \
                                                  OopClosureType* closure,      \
                                                  MemRegion mr) {               \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::ik); \
  /* header */                                                                  \
  if (closure->do_header()) {                                                   \
    obj->oop_iterate_header(closure, mr);                                       \
  }                                                                             \
  /* instance variables */                                                      \
  OopMapBlock* map     = start_of_nonstatic_oop_maps();                         \
  OopMapBlock* const end_map = map + nonstatic_oop_map_size();                  \
  HeapWord* bot = mr.start();                                                   \
  HeapWord* top = mr.end();                                                     \
objectRef*start=obj->ref_field_addr(map->offset());\
  HeapWord* end = MIN2((HeapWord*)(start + map->length()), top);                \
  /* Find the first map entry that extends onto mr. */                          \
  while (map < end_map && end <= bot) {                                         \
    map++;                                                                      \
start=obj->ref_field_addr(map->offset());\
    end = MIN2((HeapWord*)(start + map->length()), top);                        \
  }                                                                             \
  if (map != end_map) {                                                         \
    /* The current map's end is past the start of "mr".  Skip up to the first   \
       entry on "mr". */                                                        \
    while ((HeapWord*)start < bot) {                                            \
      start++;                                                                  \
    }                                                                           \
    const intx field_offset = PrefetchFieldsAhead;                              \
    for (;;) {                                                                  \
      if (field_offset > 0) {                                                   \
        while ((HeapWord*)start < end) {                                        \
          prefetch_beyond(start, (objectRef*)end, field_offset,                       \
                          closure->prefetch_style());                           \
          invoke_closure_on(start, closure, nv_suffix);                         \
          start++;                                                              \
        }                                                                       \
      } else {                                                                  \
        while ((HeapWord*)start < end) {                                        \
          invoke_closure_on(start, closure, nv_suffix);                         \
          start++;                                                              \
        }                                                                       \
      }                                                                         \
      /* Go to the next map. */                                                 \
      map++;                                                                    \
      if (map == end_map) {                                                     \
        break;                                                                  \
      }                                                                         \
      /* Otherwise,  */                                                         \
start=obj->ref_field_addr(map->offset());\
      if ((HeapWord*)start >= top) {                                            \
        break;                                                                  \
      }                                                                         \
      end = MIN2((HeapWord*)(start + map->length()), top);                      \
    }                                                                           \
  }                                                                             \
  return size_helper();                                                         \
}

ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_3(InstanceKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceKlass_OOP_OOP_ITERATE_DEFN_m)
ALL_OOP_OOP_ITERATE_CLOSURES_3(InstanceKlass_OOP_OOP_ITERATE_DEFN_m)


void instanceKlass::iterate_static_fields(OopClosure* closure) {
  objectRef* start = start_of_static_fields();
  objectRef* end   = start + static_oop_field_size();
  while (start < end) {
assert(Universe::heap()->is_in_or_null(ALWAYS_UNPOISON_OBJECTREF(*start).as_oop()),"should be in heap");
    closure->do_oop(start);
    start++;
  }
}

void instanceKlass::iterate_static_fields(OopClosure* closure,
                                          MemRegion mr) {
  objectRef* start = start_of_static_fields();
  objectRef* end   = start + static_oop_field_size();
  // I gather that the the static fields of reference types come first,
  // hence the name of "oop_field_size", and that is what makes this safe.
  assert((intptr_t)mr.start() ==
         align_size_up((intptr_t)mr.start(), sizeof(oop)) &&
         (intptr_t)mr.end() == align_size_up((intptr_t)mr.end(), sizeof(oop)),
         "Memregion must be oop-aligned.");
  if ((HeapWord*)start < mr.start()) start = (objectRef*)mr.start();
  if ((HeapWord*)end   > mr.end())   end   = (objectRef*)mr.end();
  while (start < end) {
    invoke_closure_on(start, closure,_v);
    start++;
  }
}


int instanceKlass::oop_adjust_pointers(oop obj) {
  int size = size_helper();

  // Compute oopmap block range. The common case is nonstatic_oop_map_size == 1.
  OopMapBlock* map     = start_of_nonstatic_oop_maps();
  OopMapBlock* const end_map = map + nonstatic_oop_map_size();
  // Iterate over oopmap blocks
  while (map < end_map) {
    // Compute oop range for this block
heapRef*start=(heapRef*)obj->ref_field_addr(map->offset());
    heapRef* end   = start + map->length();
    // Iterate over oops
    while (start < end) {
      assert(objectRef::is_null_or_heap(start), "should be heapRef");
      assert(Universe::heap()->is_in_or_null(ALWAYS_UNPOISON_OBJECTREF(*start).as_oop()), "should be in heap");
      MarkSweep::adjust_pointer(start);
      start++;
    }
    map++;
  }

  return size;
}

void instanceKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(!pm->depth_first(), "invariant");
  // Compute oopmap block range. The common case is nonstatic_oop_map_size == 1.
  OopMapBlock* start_map = start_of_nonstatic_oop_maps();
  OopMapBlock* map       = start_map + nonstatic_oop_map_size();

  // Iterate over oopmap blocks
  while (start_map < map) {
    --map;
    // Compute oop range for this block
heapRef*start=(heapRef*)obj->ref_field_addr(map->offset());
    heapRef* curr  = start + map->length();
    // Iterate over oops
    while (start < curr) {
      --curr;
      assert0(objectRef::is_null_or_heap(curr));
      if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*curr))) {
        assert(Universe::heap()->is_in(ALWAYS_UNPOISON_OBJECTREF(*curr).as_oop()), "should be in heap");
        pm->claim_or_forward_breadth(curr);
      }
    }
  }
}

void instanceKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(pm->depth_first(), "invariant");
  // Compute oopmap block range. The common case is nonstatic_oop_map_size == 1.
  OopMapBlock* start_map = start_of_nonstatic_oop_maps();
  OopMapBlock* map       = start_map + nonstatic_oop_map_size();

  // Iterate over oopmap blocks
  while (start_map < map) {
    --map;
    // Compute oop range for this block
heapRef*start=(heapRef*)obj->ref_field_addr(map->offset());
    heapRef* curr  = start + map->length();
    // Iterate over oops
    while (start < curr) {
      --curr;
if(PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*curr))){
assert(Universe::heap()->is_in(ALWAYS_UNPOISON_OBJECTREF(*curr).as_oop()),"should be in heap");
        pm->claim_or_forward_depth(curr);
      }
    }
  }
}

int instanceKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  // Compute oopmap block range.  The common case is nonstatic_oop_map_size==1.
  OopMapBlock* map           = start_of_nonstatic_oop_maps();
  OopMapBlock* const end_map = map + nonstatic_oop_map_size();
  // Iterate over oopmap blocks
  while (map < end_map) {
    // Compute oop range for this oopmap block.
heapRef*const map_start=(heapRef*)obj->ref_field_addr(map->offset());
    heapRef* const beg_oop = map_start;
    heapRef* const end_oop = map_start + map->length();
    for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
      PSParallelCompact::adjust_pointer(cur_oop);
    }
    ++map;
  }

  return size_helper();
}

int instanceKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				       HeapWord* beg_addr, HeapWord* end_addr) {
  // Compute oopmap block range.  The common case is nonstatic_oop_map_size==1.
  OopMapBlock* map           = start_of_nonstatic_oop_maps();
  OopMapBlock* const end_map = map + nonstatic_oop_map_size();
  // Iterate over oopmap blocks
  while (map < end_map) {
    // Compute oop range for this oopmap block.
heapRef*const map_start=(heapRef*)obj->ref_field_addr(map->offset());
    heapRef* const beg_oop = MAX2((heapRef*)beg_addr, map_start);
    heapRef* const end_oop = MIN2((heapRef*)end_addr, map_start + map->length());
    for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
      PSParallelCompact::adjust_pointer(cur_oop);
    }
    ++map;
  }

  return size_helper();
}

void instanceKlass::copy_static_fields(PSPromotionManager* pm) {
  assert(!pm->depth_first(), "invariant");
  // Compute oop range
heapRef*start=(heapRef*)start_of_static_fields();
  heapRef* end   = start + static_oop_field_size();
  // Iterate over oops
  while (start < end) {
    assert0(objectRef::is_null_or_heap(start));
    if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*start))) {
      assert(Universe::heap()->is_in(ALWAYS_UNPOISON_OBJECTREF(*start).as_oop()), "should be in heap");
      pm->claim_or_forward_breadth(start);
    }
    start++;
  }
}

void instanceKlass::push_static_fields(PSPromotionManager* pm) {
  assert(pm->depth_first(), "invariant");
  // Compute oop range
  heapRef* start = start_of_static_fields();
  heapRef* end   = start + static_oop_field_size();
  // Iterate over oops
  while (start < end) {
if(PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*start))){
assert(Universe::heap()->is_in(ALWAYS_UNPOISON_OBJECTREF(*start).as_oop()),"should be in heap");
      pm->claim_or_forward_depth(start);
    }
    start++;
  }
}

void instanceKlass::copy_static_fields(ParCompactionManager* cm) {
  // Compute oop range
  heapRef* start = start_of_static_fields();
  heapRef* end   = start + static_oop_field_size();
  // Iterate over oops
  while (start < end) {
    heapRef start_ref = UNPOISON_OBJECTREF(*start, start);
    assert0(objectRef::is_null_or_heap(start));
if(start_ref.not_null()){
      assert(Universe::heap()->is_in(start_ref.as_oop()), "should be in heap");
      // *start = (oop) cm->summary_data()->calc_new_pointer(*start);
      PSParallelCompact::adjust_pointer(start);
    }
    start++;
  } 
}

// This klass is alive but the implementor link is not followed/updated.
// Subklass and sibling links are handled by Klass::follow_weak_klass_links

void instanceKlass::follow_weak_klass_links(
  BoolObjectClosure* is_alive, OopClosure* keep_alive) {
  assert(is_alive->do_object_b(as_klassOop()), "this oop should be live");
  // _implementor is corrected during classUNloading, so what's left behind
  // here is always live.
  if (implementor().not_null()) { 
    assert(raw_implementor() != -1 && raw_implementor() != 0, "just checking");
    assert0(is_alive->do_object_b(implementor().as_klassOop()));
    keep_alive->do_oop(&adr_implementor()[0]);
  }
  Klass::follow_weak_klass_links(is_alive, keep_alive);
}

// The following method is used by GenPauselessGC.
void instanceKlass::GPGC_follow_weak_klass_links(){
  assert(GPGC_Marks::is_any_marked_strong_live(as_klassOop()), "just checking");
  // _implementor is corrected during classUNloading, so what's left behind
  // here is always live.
  if (implementor().not_null()) { 
    assert(raw_implementor() != -1 && raw_implementor() != 0, "just checking");
    GPGC_OldCollector::mark_to_live(adr_implementor());
  }
Klass::GPGC_follow_weak_klass_links();
}

void instanceKlass::remove_unshareable_info() {
  Klass::remove_unshareable_info();
}


static void clear_all_breakpoints(methodOop m) {
  m->clear_all_breakpoints();
}


void instanceKlass::release_C_heap_structures() {
  // Deallocate jfieldID cache
if(_jfieldid_cache!=NULL){
    delete _jfieldid_cache;
_jfieldid_cache=NULL;
  }

  // Deallocate JNI identifiers for jfieldIDs
  JNIid::deallocate(jni_ids());
  set_jni_ids(NULL);

  jmethodID* jmeths = methods_jmethod_ids_acquire();
  if (jmeths != (jmethodID*)NULL) {
    release_set_methods_jmethod_ids(NULL);
    FreeHeap(jmeths);
  }

  int* indices = methods_cached_itable_indices_acquire();
  if (indices != (int*)NULL) {
    release_set_methods_cached_itable_indices(NULL);
    FreeHeap(indices);
  }

  // Deallocate breakpoint records
  if (breakpoints() != 0x0) {
    methods_do(clear_all_breakpoints);
    assert(breakpoints() == 0x0, "should have cleared breakpoints");
  }

  // deallocate information about previous versions
  if (_previous_versions != NULL) {
    for (int i = _previous_versions->length() - 1; i >= 0; i--) {
      PreviousVersionNode * pv_node = _previous_versions->at(i);
      delete pv_node;
    }
    delete _previous_versions;
    _previous_versions = NULL;
  }

  // deallocate the cached class file
  if (_cached_class_file_bytes != NULL) {
    os::free(_cached_class_file_bytes);
    _cached_class_file_bytes = NULL;
    _cached_class_file_len = 0;
  }
}

char* instanceKlass::signature_name() const {
  const char* src = (const char*) (name()->as_C_string());
  const int src_length = (int)strlen(src);
  char* dest = NEW_RESOURCE_ARRAY(char, src_length + 3);
  int src_index = 0;
  int dest_index = 0;
  dest[dest_index++] = 'L';
  while (src_index < src_length) {
    dest[dest_index++] = src[src_index++];
  }
  dest[dest_index++] = ';';
  dest[dest_index] = '\0';
  return dest;
}

// different verisons of is_same_class_package
bool instanceKlass::is_same_class_package(klassOop class2) {
  klassOop class1 = as_klassOop();
  oop classloader1 = instanceKlass::cast(class1)->class_loader();
  symbolOop classname1 = Klass::cast(class1)->name();

  if (Klass::cast(class2)->oop_is_objArray()) {
    class2 = objArrayKlass::cast(class2)->bottom_klass();
  }
  oop classloader2;  
  if (Klass::cast(class2)->oop_is_instance()) {
    classloader2 = instanceKlass::cast(class2)->class_loader();
  } else {
    assert(Klass::cast(class2)->oop_is_typeArray(), "should be type array");
    classloader2 = NULL;
  }
  symbolOop classname2 = Klass::cast(class2)->name();

  return instanceKlass::is_same_class_package(classloader1, classname1,
                                              classloader2, classname2);
}

bool instanceKlass::is_same_class_package(oop classloader2, symbolOop classname2) {
  klassOop class1 = as_klassOop();
  oop classloader1 = instanceKlass::cast(class1)->class_loader();
  symbolOop classname1 = Klass::cast(class1)->name();

  return instanceKlass::is_same_class_package(classloader1, classname1,
                                              classloader2, classname2);
}

// return true if two classes are in the same package, classloader 
// and classname information is enough to determine a class's package
bool instanceKlass::is_same_class_package(oop class_loader1, symbolOop class_name1, 
                                          oop class_loader2, symbolOop class_name2) {
  if (class_loader1 != class_loader2) {
    return false;
  } else {
    ResourceMark rm;

    // The symbolOop's are in UTF8 encoding. Since we only need to check explicitly
    // for ASCII characters ('/', 'L', '['), we can keep them in UTF8 encoding.
    // Otherwise, we just compare jbyte values between the strings.
    jbyte *name1 = class_name1->base();
    jbyte *name2 = class_name2->base();

    jbyte *last_slash1 = UTF8::strrchr(name1, class_name1->utf8_length(), '/');
    jbyte *last_slash2 = UTF8::strrchr(name2, class_name2->utf8_length(), '/');
    
    if ((last_slash1 == NULL) || (last_slash2 == NULL)) {
      // One of the two doesn't have a package.  Only return true
      // if the other one also doesn't have a package.
      return last_slash1 == last_slash2; 
    } else {
      // Skip over '['s
      if (*name1 == '[') {
        do {
          name1++;
        } while (*name1 == '[');
        if (*name1 != 'L') {
          // Something is terribly wrong.  Shouldn't be here.
          return false;
        }
      }
      if (*name2 == '[') {
        do {
          name2++;
        } while (*name2 == '[');
        if (*name2 != 'L') {
          // Something is terribly wrong.  Shouldn't be here.
          return false;
        }
      }

      // Check that package part is identical
      int length1 = last_slash1 - name1;
      int length2 = last_slash2 - name2;

      return UTF8::equal(name1, length1, name2, length2);      
    }
  }
}


jint instanceKlass::compute_modifier_flags(TRAPS) const {
  klassOop k = as_klassOop();
  jint access = access_flags().as_int();

  // But check if it happens to be member class.
  typeArrayOop inner_class_list = inner_classes();
  int length = (inner_class_list == NULL) ? 0 : inner_class_list->length();
  assert (length % instanceKlass::inner_class_next_offset == 0, "just checking");
  if (length > 0) {
    typeArrayHandle inner_class_list_h(THREAD, inner_class_list);
    instanceKlassHandle ik(THREAD, k);
    for (int i = 0; i < length; i += instanceKlass::inner_class_next_offset) {
      int ioff = inner_class_list_h->ushort_at(
                      i + instanceKlass::inner_class_inner_class_info_offset);

      // Inner class attribute can be zero, skip it.
      // Strange but true:  JVM spec. allows null inner class refs.
      if (ioff == 0) continue;

      // only look at classes that are already loaded
      // since we are looking for the flags for our self.
      symbolOop inner_name = ik->constants()->klass_name_at(ioff);
      if ((ik->name() == inner_name)) {
        // This is really a member class.
        access = inner_class_list_h->ushort_at(i + instanceKlass::inner_class_access_flags_offset);
        break;
      }
    }
  }
  // Remember to strip ACC_SUPER bit
  return (access & (~JVM_ACC_SUPER)) & JVM_ACC_WRITTEN_FLAGS;
}

jint instanceKlass::jvmti_class_status() const {
  jint result = 0;

  if (is_linked()) {
    result |= JVMTI_CLASS_STATUS_VERIFIED | JVMTI_CLASS_STATUS_PREPARED;
  }

  if (is_initialized()) {
    assert(is_linked(), "Class status is not consistent");
    result |= JVMTI_CLASS_STATUS_INITIALIZED;
  }
  if (is_in_error_state()) {
    result |= JVMTI_CLASS_STATUS_ERROR;
  }
  return result;
}

methodOop instanceKlass::method_at_itable(klassOop holder, int index, TRAPS) {
  itableOffsetEntry* ioe = (itableOffsetEntry*)start_of_itable();
  int method_table_offset_in_words = ioe->offset()/wordSize;
  int nof_interfaces = (method_table_offset_in_words - itable_offset_in_words())
                       / itableOffsetEntry::size();

  for (int cnt = 0 ; ; cnt ++, ioe ++) {
    // If the interface isn't implemented by the reciever class,
    // the VM should throw IncompatibleClassChangeError.
    if (cnt >= nof_interfaces) {
      THROW_OOP_0(vmSymbols::java_lang_IncompatibleClassChangeError());
    }

    klassOop ik = ioe->interface_klass();
    if (ik == holder) break;
  }

  itableMethodEntry* ime = ioe->first_method_entry(as_klassOop());
  methodOop m = ime[index].method();
  if (m == NULL) {
    THROW_OOP_0(vmSymbols::java_lang_AbstractMethodError());
  }
  return m;
}

// -----------------------------------------------------------------------------------------------------

#ifndef PRODUCT

// Printing
class PrintNonStaticFieldClosure:public FieldClosure{
private:
outputStream*_stream;
  
public:
  PrintNonStaticFieldClosure(outputStream* print_stream) : _stream(print_stream) {}

  void do_field_for(fieldDescriptor* fd, oop obj) {
_stream->print("   - ");
fd->print_on_for(_stream,obj);
_stream->cr();
  }
};



void instanceKlass::oop_print_on(oop obj, outputStream* st) {
  Klass::oop_print_on(obj, st);

  if (as_klassOop() == SystemDictionary::string_klass()) {
    typeArrayOop value  = java_lang_String::value(obj);
    juint        offset = java_lang_String::offset(obj);
    juint        length = java_lang_String::length(obj);
    if (value != NULL &&
        value->is_typeArray() &&
        offset          <= (juint) value->length() &&
        offset + length <= (juint) value->length()) {
      st->print("string: ");
java_lang_String::print(obj,st);
      st->cr();
return;//that is enough
    }
  }

  st->print_cr("fields:");
  PrintNonStaticFieldClosure print_nonstatic_fields(st); 
  do_nonstatic_fields(&print_nonstatic_fields, obj);

  if (as_klassOop() == SystemDictionary::class_klass()) {
    klassOop mirrored_klass = java_lang_Class::as_klassOop(obj);
    st->print("   - fake entry for mirror: ");
    mirrored_klass->print_value_on(st);
    st->cr();
    st->print("   - fake entry resolved_constructor: ");
    methodOop ctor = java_lang_Class::resolved_constructor(obj);
    ctor->print_value_on(st);
    klassOop array_klass = java_lang_Class::array_klass(obj);
    st->print("   - fake entry for array: ");
    array_klass->print_value_on(st);
    st->cr();
    st->cr();
  }
}

void instanceKlass::oop_print_value_on(oop obj, outputStream* st) {
  st->print("a ");
  name()->print_value_on(st);
  obj->print_address_on(st);
}

#endif
// Thread unsafe printing, but these routines are only used by the ARTA
// daemon thread:

const char* instanceKlass::internal_name() const {
  return external_name();
}



// Verification

class VerifyFieldClosure2:public OopClosure{
 public:
  void do_oop(objectRef* p) {
    guarantee(Universe::is_in_allocation_area(p), "should be in heap or stack");
    objectRef ref = LVB::lvb_or_relocated(p);
    if (!ref.as_oop()->is_oop_or_null()) {
      tty->print_cr("Failed: %p -> " INTPTR_FORMAT, p, ref.raw_value());
      Universe::print();
      guarantee(false, "boom");
    }
  }
};


void instanceKlass::oop_verify_on(oop obj, outputStream* st) {
  Klass::oop_verify_on(obj, st);
VerifyFieldClosure2 blk;
  oop_oop_iterate(obj, &blk);
}

#ifndef PRODUCT

void instanceKlass::verify_class_klass_nonstatic_oop_maps(klassOop k) {
  // This verification code is disabled.  JDK_Version::is_gte_jdk14x_version()
  // cannot be called since this function is called before the VM is
  // able to determine what JDK version is running with.
  // The check below always is false since 1.4.
  return;
}

#endif


void instanceKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
    xb->name_value_item("name", external_name());
    // for java.lang.String objects, give string value here
    if (java_lang_String::is_instance(obj)) {
int len=java_lang_String::length(obj);
      if (len > ARTAStringPreviewLength) {
len=ARTAStringPreviewLength;
      }
      const char* string_val = len > 0 ? java_lang_String::as_utf8_string(obj, 0, len) : "";
if(string_val!=NULL){
        // Note: This is needed because string may contain non-friendly
        // xml characters.
        int templen = (int)strlen(string_val) + 1;
char*tempstr=new char[templen];
        strcpy(tempstr, string_val);
        for (int index = 0; tempstr[index] != (char)0; index++) {
char c=tempstr[index];
            if ((c < 32) || (c == 34) || (c == 38) || (c == 39) ||
                (c == 60) || (c == 62) || (c >= 127)) {
tempstr[index]='?';
            }
        }
        xb->name_value_item("string_value", tempstr);
        delete [] tempstr; 
      }
    }
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}


class PrintNonStaticFieldClosureXML:public FieldClosure{
private:
xmlBuffer*_stream;
  
public:
  PrintNonStaticFieldClosureXML(xmlBuffer* xml_stream) : _stream(xml_stream) {}
  
  void do_field_for(fieldDescriptor* fd, oop obj) {
fd->print_xml_on_for(_stream,obj);
  }
};


void instanceKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
HandleMark hm;//need hm for do_nonstatic_oops

  xmlElement o(xb, "object");
  {
    { xmlElement xe(xb, "class");
      obj->klass()->print_xml_on(xb, true);
    }
    { xmlElement se(xb, "field_list");
      { PrintNonStaticFieldClosureXML print_field_to_xml(xb);
        do_nonstatic_fields(&print_field_to_xml, obj);
      }
    }
  }
}


/* JNIid class for jfieldIDs only */
 JNIid::JNIid(klassOop holder, int offset, JNIid* next) {
POISON_AND_STORE_REF(&_holder,klassRef(holder));
  _offset = offset;
  _next = next;
   debug_only(_is_static_field_id = false;)
 }
 
 
 JNIid* JNIid::find(int offset) {
   JNIid* current = this;
   while (current != NULL) {
     if (current->offset() == offset) return current;
     current = current->next();
   }
   return NULL;
 }
 
void JNIid::oops_do(OopClosure* f) {
  for (JNIid* cur = this; cur != NULL; cur = cur->next()) {
    f->do_oop(cur->holder_addr());
  }
}
 
void JNIid::deallocate(JNIid* current) {
   while (current != NULL) {
     JNIid* next = current->next();
     delete current;
     current = next;
   }
 }
 
 
 void JNIid::verify(klassOop holder) {
   int first_field_offset  = instanceKlass::cast(holder)->offset_of_static_fields();
   int end_field_offset;
   end_field_offset = first_field_offset + (instanceKlass::cast(holder)->static_field_size() * wordSize);
 
   JNIid* current = this;
   while (current != NULL) {
     guarantee(current->holder() == holder, "Invalid klass in JNIid");
 #ifdef ASSERT
     int o = current->offset();
     if (current->is_static_field_id()) {
       guarantee(o >= first_field_offset  && o < end_field_offset,  "Invalid static field offset in JNIid");
     }
 #endif
     current = current->next();
   }
 }


#ifdef ASSERT
  void instanceKlass::set_init_state(ClassState state) { 
    bool good_state = as_klassOop()->is_shared() ? (_init_state <= state)
                                                 : (_init_state < state);
    assert(good_state || state == allocated, "illegal state transition");
    _init_state = state; 
  }
#endif


// RedefineClasses() support for previous versions:

// Add an information node that contains weak references to the
// interesting parts of the previous version of the_class.
void instanceKlass::add_previous_version(instanceKlassHandle ikh,
       BitMap * emcp_methods, int emcp_method_count) {
  assert(Thread::current()->is_VM_thread(),
    "only VMThread can add previous versions");

  if (_previous_versions == NULL) {
    // This is the first previous version so make some space.
    // Start with 2 elements under the assumption that the class
    // won't be redefined much.
    _previous_versions =  new (ResourceObj::C_HEAP)
                            GrowableArray<PreviousVersionNode *>(2, true);
  }

  constantPoolHandle cp_h(ikh->constants());
  jobject cp_ref;
  if (cp_h->is_shared()) {
    // a shared ConstantPool requires a regular reference; a weak
    // reference would be collectible
    cp_ref = JNIHandles::make_global(cp_h);
  } else {
    cp_ref = JNIHandles::make_weak_global(cp_h);
  }
  PreviousVersionNode * pv_node = NULL;
  objArrayOop old_methods = ikh->methods();

  if (emcp_method_count == 0) {
    // non-shared ConstantPool gets a weak reference
    pv_node = new PreviousVersionNode(cp_ref, !cp_h->is_shared(), NULL);
  } else {
    int local_count = 0;
    GrowableArray<jweak>* method_refs = new (ResourceObj::C_HEAP)
      GrowableArray<jweak>(emcp_method_count, true);
    for (int i = 0; i < old_methods->length(); i++) {
      if (emcp_methods->at(i)) {
        // this old method is EMCP so save a weak ref
        methodOop old_method = (methodOop) old_methods->obj_at(i);
        methodHandle old_method_h(old_method);
        jweak method_ref = JNIHandles::make_weak_global(old_method_h);
        method_refs->append(method_ref);
        if (++local_count >= emcp_method_count) {
          // no more EMCP methods so bail out now
          break;
        }
      }
    }
    // non-shared ConstantPool gets a weak reference
    pv_node = new PreviousVersionNode(cp_ref, !cp_h->is_shared(), method_refs);
  }

  _previous_versions->append(pv_node);

  // Using weak references allows the interesting parts of previous
  // classes to be GC'ed when they are no longer needed. Since the
  // caller is the VMThread and we are at a safepoint, this is a good
  // time to clear out unused weak references.

  // skip the last entry since we just added it
  for (int i = _previous_versions->length() - 2; i >= 0; i--) {
    // check the previous versions array for a GC'ed weak refs
    pv_node = _previous_versions->at(i);
    cp_ref = pv_node->prev_constant_pool();
    assert(cp_ref != NULL, "cp ref was unexpectedly cleared");
    if (cp_ref == NULL) {
      delete pv_node;
      _previous_versions->remove_at(i);
      // Since we are traversing the array backwards, we don't have to
      // do anything special with the index.
      continue;  // robustness
    }

    constantPoolOop cp = (constantPoolOop)JNIHandles::resolve(cp_ref);
    if (cp == NULL) {
      // this entry has been GC'ed so remove it
      delete pv_node;
      _previous_versions->remove_at(i);
      // Since we are traversing the array backwards, we don't have to
      // do anything special with the index.
      continue;
    }

    GrowableArray<jweak>* method_refs = pv_node->prev_EMCP_methods();
    if (method_refs != NULL) {
      for (int j = method_refs->length() - 1; j >= 0; j--) {
        jweak method_ref = method_refs->at(j);
        assert(method_ref != NULL, "weak method ref was unexpectedly cleared");
        if (method_ref == NULL) {
          method_refs->remove_at(j);
          // Since we are traversing the array backwards, we don't have to
          // do anything special with the index.
          continue;  // robustness
        }
      
        methodOop method = (methodOop)JNIHandles::resolve(method_ref);
        if (method == NULL || emcp_method_count == 0) {
          // This method entry has been GC'ed or the current
          // RedefineClasses() call has made all methods obsolete
          // so remove it.
          JNIHandles::destroy_weak_global(method_ref);
          method_refs->remove_at(j);
        }
      }
    }
  }

  int obsolete_method_count = old_methods->length() - emcp_method_count;

  if (emcp_method_count != 0 && obsolete_method_count != 0 &&
      _previous_versions->length() > 1) {
    // We have a mix of obsolete and EMCP methods. If there is more
    // than the previous version that we just added, then we have to
    // clear out any matching EMCP method entries the hard way.
    int local_count = 0;
    for (int i = 0; i < old_methods->length(); i++) {
      if (!emcp_methods->at(i)) {
        // only obsolete methods are interesting
        methodOop old_method = (methodOop) old_methods->obj_at(i);
        symbolOop m_name = old_method->name();
        symbolOop m_signature = old_method->signature();

        // skip the last entry since we just added it
        for (int j = _previous_versions->length() - 2; j >= 0; j--) {
          // check the previous versions array for a GC'ed weak refs
          pv_node = _previous_versions->at(j);
          cp_ref = pv_node->prev_constant_pool();
          assert(cp_ref != NULL, "cp ref was unexpectedly cleared");
          if (cp_ref == NULL) {
            delete pv_node;
            _previous_versions->remove_at(j);
            // Since we are traversing the array backwards, we don't have to
            // do anything special with the index.
            continue;  // robustness
          }

          constantPoolOop cp = (constantPoolOop)JNIHandles::resolve(cp_ref);
          if (cp == NULL) {
            // this entry has been GC'ed so remove it
            delete pv_node;
            _previous_versions->remove_at(j);
            // Since we are traversing the array backwards, we don't have to
            // do anything special with the index.
            continue;
          }

          GrowableArray<jweak>* method_refs = pv_node->prev_EMCP_methods();
          if (method_refs == NULL) {
            // We have run into a PreviousVersion generation where
            // all methods were made obsolete during that generation's
            // RedefineClasses() operation. At the time of that
            // operation, all EMCP methods were flushed so we don't
            // have to go back any further.
            //
            // A NULL method_refs is different than an empty method_refs.
            // We cannot infer any optimizations about older generations
            // from an empty method_refs for the current generation.
            break;
          }

          for (int k = method_refs->length() - 1; k >= 0; k--) {
            jweak method_ref = method_refs->at(k);
            assert(method_ref != NULL,
              "weak method ref was unexpectedly cleared");
            if (method_ref == NULL) {
              method_refs->remove_at(k);
              // Since we are traversing the array backwards, we don't
              // have to do anything special with the index.
              continue;  // robustness
            }
          
            methodOop method = (methodOop)JNIHandles::resolve(method_ref);
            if (method == NULL) {
              // this method entry has been GC'ed so skip it
              JNIHandles::destroy_weak_global(method_ref);
              method_refs->remove_at(k);
              continue;
            }

            if (method->name() == m_name &&
                method->signature() == m_signature) {
              // The current RedefineClasses() call has made all EMCP
              // versions of this method obsolete so mark it as obsolete
              // and remove the weak ref.
              method->set_is_obsolete();
              JNIHandles::destroy_weak_global(method_ref);
              method_refs->remove_at(k);
              break;
            }
          }

          // The previous loop may not find a matching EMCP method, but
          // that doesn't mean that we can optimize and not go any
          // further back in the PreviousVersion generations. The EMCP
          // method for this generation could have already been GC'ed,
          // but there still may be an older EMCP method that has not
          // been GC'ed.
        }

        if (++local_count >= obsolete_method_count) {
          // no more obsolete methods so bail out now
          break;
        }
      }
    }
  }
} // end add_previous_version()


// Determine if instanceKlass has a previous version.
bool instanceKlass::has_previous_version() const {
  if (_previous_versions == NULL) {
    // no previous versions array so answer is easy
    return false;
  }

  for (int i = _previous_versions->length() - 1; i >= 0; i--) {
    // Check the previous versions array for an info node that hasn't
    // been GC'ed
    PreviousVersionNode * pv_node = _previous_versions->at(i);

    jobject cp_ref = pv_node->prev_constant_pool();
    assert(cp_ref != NULL, "cp reference was unexpectedly cleared");
    if (cp_ref == NULL) {
      continue;  // robustness
    }

    constantPoolOop cp = (constantPoolOop)JNIHandles::resolve(cp_ref);
    if (cp != NULL) {
      // we have at least one previous version
      return true;
    }

    // We don't have to check the method refs. If the constant pool has
    // been GC'ed then so have the methods.
  }

  // all of the underlying nodes' info has been GC'ed
  return false;
} // end has_previous_version()

methodOop instanceKlass::method_with_idnum(int idnum) {
  methodOop m = NULL;
  if (idnum < methods()->length()) {
    m = (methodOop) methods()->obj_at(idnum);
  }
  if (m == NULL || m->method_idnum() != idnum) {
    for (int index = 0; index < methods()->length(); ++index) {
      m = (methodOop) methods()->obj_at(index);
      if (m->method_idnum() == idnum) {
        return m;
      }
    }
  }
  return m;
}


// Set the annotation at 'idnum' to 'anno'.
// We don't want to create or extend the array if 'anno' is NULL, since that is the
// default value.  However, if the array exists and is long enough, we must set NULL values.
void instanceKlass::set_methods_annotations_of(int idnum, typeArrayRef anno, objArrayRef* md_p) {
  objArrayOop md = lvb_objArrayRef(md_p).as_objArrayOop();
  if (md != NULL && md->length() > idnum) {
md->obj_at_put(idnum,anno.as_oop());
}else if(anno.not_null()){
    // create the array
    int length = MAX2(idnum+1, (int)_idnum_allocated_count);
objArrayOop md2=oopFactory::new_system_objArray(length,Thread::current());
if(md!=NULL){
      // copy the existing entries
for(int index=0;index<md->length();index++){
md2->obj_at_put(index,md->obj_at(index));
      }
    }
    md = md2;
    set_annotations(md, md_p);
md->obj_at_put(idnum,anno.as_oop());
  } // if no array and idnum isn't included there is nothing to do
}

// Construct a PreviousVersionNode entry for the array hung off
// the instanceKlass.
PreviousVersionNode::PreviousVersionNode(jobject prev_constant_pool,
  bool prev_cp_is_weak, GrowableArray<jweak>* prev_EMCP_methods) {

  _prev_constant_pool = prev_constant_pool;
  _prev_cp_is_weak = prev_cp_is_weak;
  _prev_EMCP_methods = prev_EMCP_methods;
}


// Destroy a PreviousVersionNode
PreviousVersionNode::~PreviousVersionNode() {
  if (_prev_constant_pool != NULL) {
    if (_prev_cp_is_weak) {
      JNIHandles::destroy_weak_global(_prev_constant_pool);
    } else {
      JNIHandles::destroy_global(_prev_constant_pool);
    }
  }

  if (_prev_EMCP_methods != NULL) {
    for (int i = _prev_EMCP_methods->length() - 1; i >= 0; i--) {
      jweak method_ref = _prev_EMCP_methods->at(i);
      if (method_ref != NULL) {
        JNIHandles::destroy_weak_global(method_ref);
      }
    }
    delete _prev_EMCP_methods;
  }
}


// Construct a PreviousVersionInfo entry
PreviousVersionInfo::PreviousVersionInfo(PreviousVersionNode *pv_node) {
  _prev_constant_pool_handle = constantPoolHandle();  // NULL handle
  _prev_EMCP_method_handles = NULL;

  jobject cp_ref = pv_node->prev_constant_pool();
  assert(cp_ref != NULL, "constant pool ref was unexpectedly cleared");
  if (cp_ref == NULL) {
    return;  // robustness
  }

  constantPoolOop cp = (constantPoolOop)JNIHandles::resolve(cp_ref);
  if (cp == NULL) {
    // Weak reference has been GC'ed. Since the constant pool has been
    // GC'ed, the methods have also been GC'ed.
    return;
  }

  // make the constantPoolOop safe to return
  _prev_constant_pool_handle = constantPoolHandle(cp);

  GrowableArray<jweak>* method_refs = pv_node->prev_EMCP_methods();
  if (method_refs == NULL) {
    // the instanceKlass did not have any EMCP methods
    return;
  }
  
  _prev_EMCP_method_handles = new GrowableArray<methodHandle>(10);

  int n_methods = method_refs->length();
  for (int i = 0; i < n_methods; i++) {
    jweak method_ref = method_refs->at(i);
    assert(method_ref != NULL, "weak method ref was unexpectedly cleared");
    if (method_ref == NULL) {
      continue;  // robustness
    }

    methodOop method = (methodOop)JNIHandles::resolve(method_ref);
    if (method == NULL) {
      // this entry has been GC'ed so skip it
      continue;
    }

    // make the methodOop safe to return
    _prev_EMCP_method_handles->append(methodHandle(method));
  }
}


// Destroy a PreviousVersionInfo
PreviousVersionInfo::~PreviousVersionInfo() {
  // Since _prev_EMCP_method_handles is not C-heap allocated, we
  // don't have to delete it.
}


// Construct a helper for walking the previous versions array
PreviousVersionWalker::PreviousVersionWalker(instanceKlass *ik) {
  _previous_versions = ik->previous_versions();
  _current_index = 0;
  // _hm needs no initialization
  _current_p = NULL;
}


// Destroy a PreviousVersionWalker
PreviousVersionWalker::~PreviousVersionWalker() {
  // Delete the current info just in case the caller didn't walk to
  // the end of the previous versions list. No harm if _current_p is
  // already NULL.
  delete _current_p;

  // When _hm is destroyed, all the Handles returned in
  // PreviousVersionInfo objects will be destroyed.
  // Also, after this destructor is finished it will be
  // safe to delete the GrowableArray allocated in the
  // PreviousVersionInfo objects.
}


// Return the interesting information for the next previous version
// of the klass. Returns NULL if there are no more previous versions.
PreviousVersionInfo* PreviousVersionWalker::next_previous_version() {
  if (_previous_versions == NULL) {
    // no previous versions so nothing to return
    return NULL;
  }

  delete _current_p;  // cleanup the previous info for the caller
  _current_p = NULL;  // reset to NULL so we don't delete same object twice

  int length = _previous_versions->length();

  while (_current_index < length) {
    PreviousVersionNode * pv_node = _previous_versions->at(_current_index++);
    PreviousVersionInfo * pv_info = new (ResourceObj::C_HEAP)
                                          PreviousVersionInfo(pv_node);

    constantPoolHandle cp_h = pv_info->prev_constant_pool_handle();
    if (cp_h.is_null()) {
      delete pv_info;

      // The underlying node's info has been GC'ed so try the next one.
      // We don't have to check the methods. If the constant pool has
      // GC'ed then so have the methods.
      continue;
    }

    // Found a node with non GC'ed info so return it. The caller will
    // need to delete pv_info when they are done with it.
    _current_p = pv_info;
    return pv_info;
  }

  // all of the underlying nodes' info has been GC'ed
  return NULL;
} // end next_previous_version()


// --- add_dependent_codeblobref
bool instanceKlass::add_dependent_codeblob(CodeBlob* cb, TRAPS) {
  // Life is easier if this is all done under lock - multiple compiler
  // threads can be trying to simo-insert stuff here, and possibly GC
  // might be trying to unload this class.
  assert0( cb->owner().not_null() );
  assert_lock_strong(Compile_lock);
  // Racey read outside of locking
instanceKlassHandle ik(THREAD,this->as_klassOop());
  objArrayOop mcos = dependent_mcos();
assert(mcos,"Should have been allocated already");
int len=mcos->length();
  if (*(intptr_t*) mcos->obj_at_addr(len - 1)) {
    // We cannot grow the mco array under the Compile_lock because that requires
    // an allocation.  Instead, the caller must re-attempt allocation outside
    // lock and try to add dependencies again.
    return false;
  }

  // Find the space.
  int i;
for(i=len;i>0;i--)//find 1st non-null from packed array
    if( *(intptr_t*)(mcos->obj_at_addr(i-1)) ) // not-null?
       break;
  mcos->ref_at_put(i,cb->owner());

  return true;
}

// --- deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl -------------
// We are about to add a new subclass to this instanceKlass.  Check all
// dependent MethodCodeOops and CodeBlobs to see if we need to deoptimize
// anybody.  The count of implementors has already increased.
//
// This version should be safe for both ParGC and GPGC.  And it's complex,
// so I *really* do not want to maintain two different versions.
void instanceKlass::deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl(const instanceKlass *new_subclass) const {
  if( dependent_mcos_ref().is_null() ) return; // Fast check not under lock
assert_locked_or_safepoint(Compile_lock);
  objArrayOop mcos = dependent_mcos();
int len=mcos->length();
int nidx=len-1;
for(int i=0;i<len;i++){
    methodCodeOop mco = (methodCodeOop)mcos->obj_at(i);
    if( !mco ) break;           // List can have nulls at the end
    bool found=false;
    bool deopt=false;
    objArrayOop dk = mco->dep_klasses().as_objArrayOop();
    objArrayOop dm = mco->dep_methods().as_objArrayOop();
for(int j=0;j<dk->length();j++){
      // Hunt for the dependencies for this klass.  All the dependencies for a
      // single klass are all together, so the 1st miss after any hit means no
      // more dependencies.
      if( dk->obj_at(j) != this->as_klassOop() ) {
        if( !found ) continue;  // Missed, need to keep searching
        break;                // First hit after miss: must have done them all
      }
found=true;//Hit: found the dependencies for 'this' klass
methodOop m=(methodOop)dm->obj_at(j);

      if( m == methodCodeOopDesc::noFinalizers().as_oop() ) {
        // Does this new class have a finalizer?
        if( new_subclass && new_subclass->has_finalizer() ) { deopt=true; break; }

      } else if( m == methodCodeOopDesc::zeroImplementors().as_oop() ) {
        if( nof_implementors() > 0 ) { deopt=true; break; }

      } else if( m == NULL ) {  // Not allowing any concrete subclassing
        if( nof_implementors() != 1 ) { deopt=true; break; }

      } else {
        // test for direct method overriding: does this new class have an instance of 'm'?
        methodOop target = new_subclass ? new_subclass->find_method(m->name(),m->signature()) : NULL;
        if( target && !target->is_abstract() ) { deopt=true; break; }
      }
    } // End of check-all-dependencies-on-this-mco
    assert0( found ); // Better find at least 1 dependency, else why was this mco mentioned as dep on 'this'?

    // Did we find a deoptimizing condition?
    if( deopt ) {
      // Yank 'mco' at index 'i' from the list of dependent mcos.
      // Uses the standard array-compression technique.
      // Yanking the mco will make it go dead at some future GC cycle.
      while( !*(intptr_t*)(mcos->obj_at_addr(nidx)) ) nidx--; // scan for not-null
      mcos->obj_at_put(i,mcos->obj_at(nidx));                // jam last not-null over 'i'
mcos->obj_at_put(nidx,NULL);
      i--; len--;               // Re-run iteration 'i' since mcos[i] has a new value
      mco ->deoptimize(Deoptimization::Reason_unloaded); // Make method unusable in the future
    }
  }
}

void instanceKlass::deoptimize_codeblobrefs_dependent_on_method(methodOop moop) {
  Unimplemented();
}

void instanceKlass::GPGC_sweep_weak_codeblobrefs(GPGC_GCManagerOldStrong* gcm) {
  Unimplemented();
}

void instanceKlass::GPGC_deoptimize_codeblobrefs_dependent_on_method(methodOop moop) const {
  Unimplemented();
}
