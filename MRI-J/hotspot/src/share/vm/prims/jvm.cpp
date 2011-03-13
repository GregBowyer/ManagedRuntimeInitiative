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


#include "allocation.hpp"
#include "arguments.hpp"
#include "arrayOop.hpp"
#include "attachListener.hpp"
#include "atomic.hpp"
#include "barrierSet.hpp"
#include "classFileStream.hpp"
#include "classLoader.hpp"
#include "collectedHeap.hpp"
#include "copy.hpp"
#include "defaultStream.hpp"
#include "fieldDescriptor.hpp"
#include "gcLocker.hpp"
#include "gcTaskThread.hpp"
#include "globals.hpp"
#include "heapDumper.hpp"
#include "histogram.hpp"
#include "hpi.hpp"
#include "hpi_os.hpp"
#include "interfaceSupport.hpp"
#include "javaAssertions.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jfieldIDWorkaround.hpp"
#include "jni.hpp"
#include "jniHandles.hpp"
#include "jvm.h"
#include "jvm_misc.hpp"
#include "jvmtiExport.hpp"
#include "jvmtiTagMap.hpp"
#include "jvmtiThreadState.hpp"
#include "log.hpp"
#include "management.hpp"
#include "methodOop.hpp"
#include "nativeInst_pd.hpp"
#include "nativeLookup.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "privilegedStack.hpp"
#include "reflection.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "threadCounters.hpp"
#include "threadService.hpp"
#include "tickProfiler.hpp"
#include "vframe.hpp"
#include "vmSymbols.hpp"
#include "vm_operations.hpp"
#include "vm_version_pd.hpp"
#include "vmTags.hpp"
#include "vmThread.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "barrierSet.inline.hpp"
#include "bitMap.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
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
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

/*
  NOTE about use of any ctor or function call that can trigger a safepoint/GC: 
  such ctors and calls MUST NOT come between an oop declaration/init and its 
  usage because if objects are move this may cause various memory stomps, bus 
  errors and segfaults. Here is a cookbook for causing so called "naked oop 
  failures":

      JVM_ENTRY(jobjectArray, JVM_GetClassDeclaredFields<etc> {
          JVMWrapper("JVM_GetClassDeclaredFields");

          // Object address to be held directly in mirror & not visible to GC
          oop mirror = JNIHandles::resolve_non_null(ofClass);

          // If this ctor can hit a safepoint, moving objects around, then
          ComplexConstructor foo;

          // Boom! mirror may point to JUNK instead of the intended object
          (some dereference of mirror)

          // Here's another call that may block for GC, making mirror stale
          MutexLockerAllowGC ml(some_lock);

          // And here's an initializer that can result in a stale oop
          // all in one step.
          oop o = call_that_can_throw_exception(TRAPS);


  The solution is to keep the oop declaration BELOW the ctor or function
  call that might cause a GC, do another resolve to reassign the oop, or
  consider use of a Handle instead of an oop so there is immunity from object 
  motion. But note that the "QUICK" entries below do not have a handlemark
  and thus can only support use of handles passed in.
*/

// Wrapper to trace JVM functions

#ifdef ASSERT
  class JVMTraceWrapper : public StackObj {
   public:
    JVMTraceWrapper(const char* format, ...) {      
      if (TraceJVMCalls) {
        va_list ap;
        va_start(ap, format);
        tty->print("JVM ");
        tty->vprint_cr(format, ap);
        va_end(ap);
      }
    }
  };

  Histogram* JVMHistogram;
volatile jlong JVMHistogram_lock=0;

  class JVMHistogramElement : public HistogramElement {
    public:
     JVMHistogramElement(const char* name);
  };

  JVMHistogramElement::JVMHistogramElement(const char* elementName) {
    _name = elementName;
    uintx count = 0;

    while (Atomic::cmpxchg(1, &JVMHistogram_lock, 0) != 0) {
      while (OrderAccess::load_acquire(&JVMHistogram_lock) != 0) {
        count +=1;
        if ( (WarnOnStalledSpinLock > 0)
          && (count % WarnOnStalledSpinLock == 0)) {
          warning("JVMHistogram_lock seems to be stalled");
        }
      }
     }

    if(JVMHistogram == NULL)
      JVMHistogram = new Histogram("JVM Call Counts",100);

    JVMHistogram->add_element(this);
Atomic::dec_ptr(&JVMHistogram_lock);
  }

  #define JVMCountWrapper(arg) \
    if (CountJVMCalls) { \
      static JVMHistogramElement* e = new JVMHistogramElement(arg); \
if(e!=NULL)e->increment_count();/*Due to bug in VC++, we need a NULL check here eventhough it should never happen!*/\
}

#define JVMWrapper(arg1)const char*FCN_NAME=arg1;JVMCountWrapper(arg1);JVMTraceWrapper(arg1)
#define JVMWrapper2(arg1,arg2)const char*FCN_NAME=arg1;JVMCountWrapper(arg1);JVMTraceWrapper(arg1,arg2)
#define JVMWrapper3(arg1,arg2,arg3)const char*FCN_NAME=arg1;JVMCountWrapper(arg1);JVMTraceWrapper(arg1,arg2,arg3)
#define JVMWrapper4(arg1,arg2,arg3,arg4)const char*FCN_NAME=arg1;JVMCountWrapper(arg1);JVMTraceWrapper(arg1,arg2,arg3,arg4)
#else
  #define JVMWrapper(arg1)                    const char *FCN_NAME=arg1;
  #define JVMWrapper2(arg1, arg2)             const char *FCN_NAME=arg1;
  #define JVMWrapper3(arg1, arg2, arg3)       const char *FCN_NAME=arg1;
  #define JVMWrapper4(arg1, arg2, arg3, arg4) const char *FCN_NAME=arg1;
#endif


// Interface version /////////////////////////////////////////////////////////////////////


JVM_LEAF(jint, JVM_GetInterfaceVersion, ())
  return JVM_INTERFACE_VERSION;
JVM_END


// java.lang.System //////////////////////////////////////////////////////////////////////


JVM_LEAF(jlong, JVM_CurrentTimeMillis, (JNIEnv *env, jclass ignored))
  JVMWrapper("JVM_CurrentTimeMillis");
  return os::javaTimeMillis();
JVM_END

JVM_LEAF(jlong, JVM_NanoTime, (JNIEnv *env, jclass ignored))
  JVMWrapper("JVM_NanoTime");
  return os::javaTimeNanos();
JVM_END


JVM_ENTRY(void, JVM_ArrayCopy, (JNIEnv *env, jclass ignored, jobject src, jint src_pos, 
                               jobject dst, jint dst_pos, jint length))  
  JVMWrapper("JVM_ArrayCopy");
  // Check if we have null pointers
  if (src == NULL || dst == NULL) {
    THROW(vmSymbols::java_lang_NullPointerException());
  }
  arrayOop s = arrayOop(JNIHandles::resolve_non_null(src));
  arrayOop d = arrayOop(JNIHandles::resolve_non_null(dst));
  assert(s->is_oop(), "JVM_ArrayCopy: src not an oop");
  assert(d->is_oop(), "JVM_ArrayCopy: dst not an oop");
  // Do copy
  Klass::cast(s->klass())->copy_array(s, src_pos, d, dst_pos, length, thread);
JVM_END


static void set_property(Handle props, const char* key, const char* value, TRAPS) {
  JavaValue r(T_OBJECT);
  // public synchronized Object put(Object key, Object value);  
  HandleMark hm(THREAD);
  Handle key_str    = java_lang_String::create_from_platform_dependent_str(key, CHECK);
  Handle value_str  = java_lang_String::create_from_platform_dependent_str((value != NULL ? value : ""), CHECK);      
  JavaCalls::call_virtual(&r,                           
                          props, 
                          KlassHandle(THREAD, SystemDictionary::properties_klass()),
                          vmSymbolHandles::put_name(), 
                          vmSymbolHandles::object_object_object_signature(), 
                          key_str, 
                          value_str, 
                          THREAD);  
}


#define PUTPROP(props, name, value) set_property((props), (name), (value), CHECK_(properties)); 


JVM_ENTRY(jobject, JVM_InitProperties, (JNIEnv *env, jobject properties))
  JVMWrapper("JVM_InitProperties");
  ResourceMark rm;

  Handle props(JNIHandles::resolve_as_non_null_ref(properties));

  // System property list includes both user set via -D option and
  // jvm system specific properties.
  for (SystemProperty* p = Arguments::system_properties(); p != NULL; p = p->next()) {
    PUTPROP(props, p->key(), p->value());
  }

  // Convert the -XX:MaxDirectMemorySize= command line flag 
  // to the sun.nio.MaxDirectMemorySize property.
  // Do this after setting user properties to prevent people 
  // from setting the value with a -D option, as requested.
  {
    char as_chars[256];
    jio_snprintf(as_chars, sizeof(as_chars), INTX_FORMAT, MaxDirectMemorySize);
    PUTPROP(props, "sun.nio.MaxDirectMemorySize", as_chars);
  }

  // JVM monitoring and management support
  // Add the sun.management.compiler property for the compiler's name
  {
    const char* compiler_name = Abstract_VM_Version::vm_name();

    if (*compiler_name != '\0' &&
        (Arguments::mode() != Arguments::_int)) {
      PUTPROP(props, "sun.management.compiler", compiler_name);
    }
  }

  return properties;
JVM_END


// java.lang.Runtime /////////////////////////////////////////////////////////////////////////

extern volatile jint vm_created;

JVM_ENTRY_NO_ENV(void, JVM_Exit, (jint code))
  if (vm_created != 0 && (code == 0)) {
    // The VM is about to exit.  We call back into Java to check
    // whether finalizers should be run.
    Universe::run_finalizers_on_exit();
  }
  before_exit(thread);
  vm_exit(code);
JVM_END


JVM_ENTRY_NO_ENV(void, JVM_Halt, (jint code))  
  before_exit(thread);
  vm_exit(code);
JVM_END


JVM_LEAF(void, JVM_OnExit, (void (*func)(void)))
  register_on_exit_function(func);
JVM_END


JVM_ENTRY_NO_ENV(void, JVM_GC, (void))  
  JVMWrapper("JVM_GC");
  if (!DisableExplicitGC) {
    Universe::heap()->collect(GCCause::_java_lang_system_gc);
  }
JVM_END

JVM_ENTRY_NO_ENV(void, JVM_SystemResourceLimit_GC, (void))
JVMWrapper("JVM_SystemResourceLimit_GC");
  Thread* t = Thread::current();
  if (t->is_Java_thread()) {
Universe::heap()->collect(GCCause::_system_resourcelimit_hit);
  }
JVM_END

JVM_LEAF(jlong, JVM_MaxObjectInspectionAge, (void))
  JVMWrapper("JVM_MaxObjectInspectionAge");
  return Universe::heap()->millis_since_last_gc();
JVM_END


JVM_LEAF(void, JVM_TraceInstructions, (jboolean on))
  if (PrintJVMWarnings) warning("JVM_TraceInstructions not supported");
JVM_END


JVM_LEAF(void, JVM_TraceMethodCalls, (jboolean on))
  if (PrintJVMWarnings) warning("JVM_TraceMethodCalls not supported");
JVM_END

static inline jlong convert_size_t_to_jlong(size_t val) {
  // In the 64-bit vm, a size_t can overflow a jlong (which is signed).
return(jlong)MIN2(val,(size_t)max_jlong);
}

JVM_ENTRY_NO_ENV(jlong, JVM_TotalMemory, (void))
  JVMWrapper("JVM_TotalMemory");
  CollectedHeap* ch = Universe::heap();
  size_t n = MAX2(ch->max_capacity(), ch->capacity());
  return convert_size_t_to_jlong(n);
JVM_END


JVM_ENTRY_NO_ENV(jlong, JVM_FreeMemory, (void))
  JVMWrapper("JVM_FreeMemory");
  CollectedHeap* ch = Universe::heap();
  size_t n = (ModifiedFreeMemory != 0) ? ModifiedFreeMemory : (MAX2(ch->max_capacity(), ch->capacity()) - ch->used());
  return convert_size_t_to_jlong(n);
JVM_END


JVM_ENTRY_NO_ENV(jlong, JVM_MaxMemory, (void))
  JVMWrapper("JVM_MaxMemory");
  size_t n = Universe::heap()->max_capacity();
  return convert_size_t_to_jlong(n);
JVM_END


JVM_ENTRY_NO_ENV(jint, JVM_ActiveProcessorCount, (void))
  JVMWrapper("JVM_ActiveProcessorCount");

  if (AvailableProcessors > 0) {
    return AvailableProcessors;
  }

  if (AllowDynamicCPUCount) {
      return os::active_processor_count();
  } else {
return os::processor_count();
  }
JVM_END



// java.lang.Throwable //////////////////////////////////////////////////////


JVM_ENTRY(void, JVM_FillInStackTrace, (JNIEnv *env, jobject receiver))
  JVMWrapper("JVM_FillInStackTrace");    
Handle exception(JNIHandles::resolve_as_non_null_ref(receiver));
  java_lang_Throwable::fill_in_stack_trace(exception);
JVM_END


JVM_ENTRY(void, JVM_PrintStackTrace, (JNIEnv *env, jobject receiver, jobject printable))  
  JVMWrapper("JVM_PrintStackTrace");
  // Note: This is no longer used in Merlin, but we still support it for compatibility.
  oop exception = JNIHandles::resolve_non_null(receiver);
  oop stream    = JNIHandles::resolve_non_null(printable);
  java_lang_Throwable::print_stack_trace(exception, stream);
JVM_END


JVM_ENTRY(jint, JVM_GetStackTraceDepth, (JNIEnv *env, jobject throwable))  
  JVMWrapper("JVM_GetStackTraceDepth");
  oop exception = JNIHandles::resolve(throwable);
  return java_lang_Throwable::get_stack_trace_depth(exception, THREAD);
JVM_END


JVM_ENTRY(jobject, JVM_GetStackTraceElement, (JNIEnv *env, jobject throwable, jint index))  
  JVMWrapper("JVM_GetStackTraceElement");
  JvmtiVMObjectAllocEventCollector oam; // This ctor (throughout this module) may trigger a safepoint/GC
objectRef exception=JNIHandles::resolve_as_ref(throwable);
  objectRef element = java_lang_Throwable::get_stack_trace_element(exception, index, CHECK_NULL);
  return JNIHandles::make_local(env, element);
JVM_END


// java.lang.Object ///////////////////////////////////////////////


JVM_ENTRY(jint, JVM_IHashCode, (JNIEnv* env, jobject handle))
  JVMWrapper("JVM_IHashCode");
  // as implemented in the classic virtual machine; return 0 if object is NULL
return handle==NULL?0:ObjectSynchronizer::FastHashCode(JNIHandles::resolve_as_non_null_ref(handle));
JVM_END


JVM_ENTRY(void, JVM_MonitorWait, (JNIEnv* env, jobject handle, jlong ms))
  JVMWrapper("JVM_MonitorWait");
Handle obj(JNIHandles::resolve_as_non_null_ref(handle));
  assert(obj->is_instance() || obj->is_array(), "JVM_MonitorWait must apply to an object");
  if( obj.as_ref().is_stack() ) {
    StackBasedAllocation::ensure_in_heap(thread,handle,FCN_NAME);
  }
  JavaThreadInObjectWaitState jtiows(thread, ms != 0);
  if (JvmtiExport::should_post_monitor_wait()) {
    JvmtiExport::post_monitor_wait((JavaThread *)THREAD, (oop)obj(), ms);
  }
ObjectSynchronizer::wait(obj.as_ref(),ms,CHECK);
JVM_END


JVM_ENTRY(void, JVM_MonitorNotify, (JNIEnv* env, jobject handle))
  JVMWrapper("JVM_MonitorNotify");
  objectRef ref = JNIHandles::resolve_as_non_null_ref(handle);
  assert(ref.as_oop()->is_instance() || ref.as_oop()->is_array(), "JVM_MonitorNotify must apply to an object");
ObjectSynchronizer::notify(ref,CHECK);
JVM_END


JVM_ENTRY(void, JVM_MonitorNotifyAll, (JNIEnv* env, jobject handle))
  JVMWrapper("JVM_MonitorNotifyAll");
objectRef obj=JNIHandles::resolve_as_ref(handle);
assert(obj.as_oop()->is_instance()||obj.as_oop()->is_array(),"JVM_MonitorNotifyAll must apply to an object");
  ObjectSynchronizer::notifyall(obj, CHECK);
JVM_END


JVM_ENTRY(jobject, JVM_Clone, (JNIEnv* env, jobject handle))
  JVMWrapper("JVM_Clone");
  objectRef h_ref = JNIHandles::resolve_as_ref(handle);
Handle obj(THREAD,JNIHandles::resolve_as_non_null_ref(handle));
  const KlassHandle klass (THREAD, obj->klass());
  JvmtiVMObjectAllocEventCollector oam;

#ifdef ASSERT
  // Just checking that the cloneable flag is set correct
  if (obj->is_javaArray()) {
    guarantee(klass->is_cloneable(), "all arrays are cloneable");
  } else {
    guarantee(obj->is_instance(), "should be instanceOop");
    bool cloneable = klass->is_subtype_of(SystemDictionary::cloneable_klass());
    guarantee(cloneable == klass->is_cloneable(), "incorrect cloneable flag");
  }
#endif

  // Check if class of obj supports the Cloneable interface.
  // All arrays are considered to be cloneable (See JLS 20.1.5)
  if (!klass->is_cloneable()) {
    ResourceMark rm(THREAD);
    THROW_MSG_0(vmSymbols::java_lang_CloneNotSupportedException(), klass->external_name());
  }

  const int size = obj->size();
  objectRef newref = nullRef;

  if (h_ref.is_stack()) {
    // If the existing object is on the stack, we cannot let the clone'd object
    // go to the heap, or else we must stack-escape all his oop fields.
    assert0( stackRef(h_ref).preheader()->fid() <= thread->curr_sbafid() );
    GET_RPC;
if(obj->is_array()){
      const int length = ((arrayOop)obj())->length();
      newref = thread->sba_area()->allocate(klass(), size, length, RPC );
    } else {
      newref = thread->sba_area()->allocate(klass(), size, -1, RPC );
    }
  }
  oop new_obj = newref.as_oop();
if(new_obj==NULL){
if(obj->is_array()){
      new_obj = CollectedHeap::array_allocate_noinit(klass, size, ((arrayOop)obj())->length(), CHECK_NULL);
    } else {
new_obj=CollectedHeap::obj_allocate_noinit(klass,size,CHECK_NULL);
    }
    newref = objectRef(new_obj);
  }


  if (UseLVBs) {
    // If a GC thread were to run this code, you'd end up with a cloned object that might
    // contain bogus objectRefs, since the LVB wouldn't be triggering.
    assert0(!Thread::current()->is_gc_mode());

if(obj->is_typeArray()){
      HeapWord* from      = ((HeapWord*)obj())   + oopDesc::header_size();
      HeapWord* to        = ((HeapWord*)new_obj) + oopDesc::header_size();
      int       copy_size = size - oopDesc::header_size();
      // Make sure the copy will really get each field atomically:
      assert0(HeapWordsPerOop == 1);
      Copy::aligned_disjoint_words(from, to, copy_size);
    } else if (obj->is_objArray()) {
      // copy the array length over, then copy the array elements with LVBs.
      int array_length = arrayOop(obj())->length();
arrayOop(new_obj)->set_length(array_length);
      int ekid = objArrayOop(obj())->ekid();
      objArrayOop(new_obj)->set_ekid(ekid);

      if ( array_length > 0 ) {
        objectRef* from         = objArrayOop(obj()  )->obj_at_addr(0);
        objectRef* to           = objArrayOop(new_obj)->obj_at_addr(0);

        Copy::conjoint_objectRefs_atomic(from, to, array_length);
      }
    } else {
      // Getting here means we're copying some undetermined non-array object, which may contain
      // an odd assortment of objectRefs.  We do the copy by first mem copying everything to the
      // new object, and then we go back again with an oop closure to pickup all the objectRefs,
      // LVB them, and write the safe value into the destination.
      HeapWord* from      = ((HeapWord*)obj())   + oopDesc::header_size();
      HeapWord* to        = ((HeapWord*)new_obj) + oopDesc::header_size();
      int       copy_size = size - oopDesc::header_size();
      // Make sure the copy will really get each field atomically:
      assert0(HeapWordsPerOop == 1);
      if ( copy_size > 0 ) {
        Copy::aligned_disjoint_words(from, to, copy_size);
        LVB::lvb_clone_objectRefs(obj(), new_obj);
      }
    }
  } else {
    // 4839641 (4840070): We must do an oop-atomic copy, because if another thread
    // is modifying a reference field in the clonee, a non-oop-atomic copy might
    // be suspended in the middle of copying the pointer and end up with parts
    // of two different pointers in the field.  Subsequent dereferences will crash.

    // Make sure the copy will really get each field atomically:
    assert0(HeapWordsPerOop == 1);
    Copy::aligned_disjoint_words((HeapWord*)obj(), (HeapWord*)new_obj, size);
  }

  // Clear the header
new_obj->clear_mark();

  if (!newref.is_stack()) {
    // Store check (mark entire object and let gc sort it out)
    BarrierSet* bs = Universe::heap()->barrier_set();
    bs->write_region(MemRegion((HeapWord*)new_obj, size));
  }

  // Caution: this involves a java upcall, so the clone should be
  // "gc-robust" by this stage.
  if (klass->has_finalizer()) {
    assert(obj->is_instance(), "should be instanceOop");
    new_obj = instanceKlass::register_finalizer(instanceOop(new_obj), CHECK_NULL);
  }

return JNIHandles::make_local(env,new_obj);
JVM_END

JVM_ENTRY(jobject, JVM_ShallowCopy, (JNIEnv* env, jobject handle))
JVMWrapper("JVM_ShallowCopy");
  objectRef h_ref = JNIHandles::resolve_as_ref(handle);
  Handle obj(JNIHandles::resolve_as_non_null_ref(handle));
  const KlassHandle klass (*obj->klass_addr());
  JvmtiVMObjectAllocEventCollector oam;

  bool is_stack = false;
  const int size = obj->size();
  oop new_obj = NULL;
if(obj->is_array()){
    const int length = ((arrayOop)obj())->length();
new_obj=CollectedHeap::array_allocate_noinit(klass,size,length,CHECK_NULL);
  } else {
new_obj=CollectedHeap::obj_allocate_noinit(klass,size,CHECK_NULL);
  }


  if (UseLVBs) {
    // If a GC thread were to run this code, you'd end up with a cloned object that might
    // contain bogus objectRefs, since the LVB wouldn't be triggering.
    assert0(!Thread::current()->is_gc_mode());

if(obj->is_typeArray()){
      HeapWord* from      = ((HeapWord*)obj())   + oopDesc::header_size();
      HeapWord* to        = ((HeapWord*)new_obj) + oopDesc::header_size();
      int       copy_size = size - oopDesc::header_size();
      // Make sure the copy will really get each field atomically:
      assert0(HeapWordsPerOop == 1);
      Copy::aligned_disjoint_words(from, to, copy_size);
    } else if (obj->is_objArray()) {
      // copy the array length and ekid over, then copy the array elements with LVBs.
      int array_length = arrayOop(obj())->length();
arrayOop(new_obj)->set_length(array_length);
      int ekid = objArrayOop(obj())->ekid();
      objArrayOop(new_obj)->set_ekid(ekid);

      if ( array_length > 0 ) {
        objectRef* from         = objArrayOop(obj())->obj_at_addr(0);
        objectRef* to           = objArrayOop(new_obj)->obj_at_addr(0);
        
        Copy::conjoint_objectRefs_atomic(from, to, array_length);
      }
    } else {
      // Getting here means we're copying some undetermined non-array object, which may contain
      // an odd assortment of objectRefs.  We do the copy by first mem copying everything to the
      // new object, and then we go back again with an oop closure to pickup all the objectRefs,
      // LVB them, and write the safe value into the destination.
      HeapWord* from      = ((HeapWord*)obj())   + oopDesc::header_size();
      HeapWord* to        = ((HeapWord*)new_obj) + oopDesc::header_size();
      int       copy_size = size - oopDesc::header_size();
      // Make sure the copy will really get each field atomically:
      assert0(HeapWordsPerOop == 1);
      if ( copy_size > 0 ) {
        Copy::aligned_disjoint_words(from, to, copy_size);
        LVB::lvb_clone_objectRefs(obj(), new_obj);
      }
    }
  } else {
    // 4839641 (4840070): We must do an oop-atomic copy, because if another thread
    // is modifying a reference field in the clonee, a non-oop-atomic copy might
    // be suspended in the middle of copying the pointer and end up with parts
    // of two different pointers in the field.  Subsequent dereferences will crash.
    
    // Make sure the copy will really get each field atomically:
    assert0(HeapWordsPerOop == 1);
    Copy::aligned_disjoint_words((HeapWord*)obj(), (HeapWord*)new_obj, size);
  }

  // Clear the header
  intptr_t id_hash = ObjectSynchronizer::FastHashCode(obj());
  new_obj->set_mark(obj->mark()->clear_and_save_kid()->copy_set_hash(id_hash));


  if (!is_stack) {
    // Store check (mark entire object and let gc sort it out)
    BarrierSet* bs = Universe::heap()->barrier_set();
    assert(bs->has_write_region_opt(), "Barrier set does not have write_region");
    bs->write_region(MemRegion((HeapWord*)new_obj, size));
  }

  // Caution: this involves a java upcall, so the clone should be
  // "gc-robust" by this stage.
  if (klass->has_finalizer()) {
    assert(obj->is_instance(), "should be instanceOop");
    new_obj = instanceKlass::register_finalizer(instanceOop(new_obj), CHECK_NULL);
  }

return JNIHandles::make_local(env,new_obj);
JVM_END

JVM_ENTRY(jboolean, JVM_ReferenceEquality, (JNIEnv* env, jobject ref_handle, jobject obj_handle))
JVMWrapper("JVM_ReferenceEquality");
  Handle ref(JNIHandles::resolve_as_ref(ref_handle));
  Handle obj(JNIHandles::resolve_as_ref(obj_handle));

  oop ref_oop = LVB::mutator_relocate_only(java_lang_ref_Reference::referent_addr(ref())).as_oop();
  if (ref_oop == obj()) {
    return JNI_TRUE;
  }

  return JNI_FALSE;
JVM_END


// java.lang.Compiler ////////////////////////////////////////////////////

// The initial cuts of the HotSpot VM will not support JITs, and all existing
// JITs would need extensive changes to work with HotSpot.  The JIT-related JVM
// functions are all silently ignored unless JVM warnings are printed.

JVM_LEAF(void, JVM_InitializeCompiler, (JNIEnv *env, jclass compCls))
  if (PrintJVMWarnings) warning("JVM_InitializeCompiler not supported");
JVM_END


JVM_LEAF(jboolean, JVM_IsSilentCompiler, (JNIEnv *env, jclass compCls))
  if (PrintJVMWarnings) warning("JVM_IsSilentCompiler not supported");
  return JNI_FALSE;
JVM_END


JVM_LEAF(jboolean, JVM_CompileClass, (JNIEnv *env, jclass compCls, jclass cls))
  if (PrintJVMWarnings) warning("JVM_CompileClass not supported");
  return JNI_FALSE;
JVM_END


JVM_LEAF(jboolean, JVM_CompileClasses, (JNIEnv *env, jclass cls, jstring jname))
  if (PrintJVMWarnings) warning("JVM_CompileClasses not supported");
  return JNI_FALSE;
JVM_END


JVM_LEAF(jobject, JVM_CompilerCommand, (JNIEnv *env, jclass compCls, jobject arg))
  if (PrintJVMWarnings) warning("JVM_CompilerCommand not supported");
  return NULL;
JVM_END


JVM_LEAF(void, JVM_EnableCompiler, (JNIEnv *env, jclass compCls))
  if (PrintJVMWarnings) warning("JVM_EnableCompiler not supported");
JVM_END


JVM_LEAF(void, JVM_DisableCompiler, (JNIEnv *env, jclass compCls))
  if (PrintJVMWarnings) warning("JVM_DisableCompiler not supported");
JVM_END



// Error message support //////////////////////////////////////////////////////

JVM_LEAF(jint, JVM_GetLastErrorString, (char *buf, int len))
  JVMWrapper("JVM_GetLastErrorString");
  return hpi::lasterror(buf, len);
JVM_END


// java.io.File ///////////////////////////////////////////////////////////////

JVM_LEAF(char*, JVM_NativePath, (char* path))
  JVMWrapper2("JVM_NativePath (%s)", path);
  return hpi::native_path(path);
JVM_END


// Misc. class handling ///////////////////////////////////////////////////////////


JVM_ENTRY(jclass, JVM_GetCallerClass, (JNIEnv* env, int depth))
  JVMWrapper("JVM_GetCallerClass");
  klassOop k = thread->security_get_caller_class(depth);
return(k==NULL)?NULL:(jclass)JNIHandles::make_local(env,Klass::cast(k)->java_mirror_ref());
JVM_END


JVM_ENTRY(jclass, JVM_FindPrimitiveClass, (JNIEnv* env, const char* utf))  
  JVMWrapper("JVM_FindPrimitiveClass");
  oop mirror = NULL;
  BasicType t = name2type(utf);
  if (t != T_ILLEGAL && t != T_OBJECT && t != T_ARRAY) {
    mirror = Universe::java_mirror(t);
  }
  if (mirror == NULL) {
    THROW_MSG_0(vmSymbols::java_lang_ClassNotFoundException(), (char*) utf);
  } else {
    return (jclass) JNIHandles::make_local(env, mirror);
  }
JVM_END


JVM_ENTRY(void, JVM_ResolveClass, (JNIEnv* env, jclass cls))
  JVMWrapper("JVM_ResolveClass");
  if (PrintJVMWarnings) warning("JVM_ResolveClass not implemented");
JVM_END


JVM_ENTRY(jclass, JVM_FindClassFromClassLoader, (JNIEnv* env, const char* name, 
                                               jboolean init, jobject loader, 
                                               jboolean throwError))
  JVMWrapper3("JVM_FindClassFromClassLoader %s throw %s", name, 
               throwError ? "error" : "exception");
  // Java libraries should ensure that name is never null...
  if (name == NULL || (int)strlen(name) > symbolOopDesc::max_length()) {
    // It's impossible to create this class;  the name cannot fit
    // into the constant pool.
    if (throwError) {
      THROW_MSG_0(vmSymbols::java_lang_NoClassDefFoundError(), name);
    } else {
      THROW_MSG_0(vmSymbols::java_lang_ClassNotFoundException(), name);
    }
  }
  symbolHandle h_name = oopFactory::new_symbol_handle(name, CHECK_NULL);
Handle h_loader(JNIHandles::resolve_as_ref(loader));
  jclass result = find_class_from_class_loader(env, h_name, init, h_loader, 
                                               Handle(), throwError, thread);

  return result;
JVM_END


JVM_ENTRY(jclass, JVM_FindClassFromClass, (JNIEnv *env, const char *name, 
                                         jboolean init, jclass from))
  JVMWrapper2("JVM_FindClassFromClass %s", name);
  if (name == NULL || (int)strlen(name) > symbolOopDesc::max_length()) {
    // It's impossible to create this class;  the name cannot fit
    // into the constant pool.
    THROW_MSG_0(vmSymbols::java_lang_NoClassDefFoundError(), name);
  }
  symbolHandle h_name = oopFactory::new_symbol_handle(name, CHECK_NULL);
objectRef from_class_ref=JNIHandles::resolve(from);
objectRef class_loader;
objectRef protection_domain;
klassRef from_class;
if(from_class_ref.not_null()){
from_class=java_lang_Class::as_klassRef(from_class_ref);
    class_loader = Klass::cast(from_class.as_klassOop())->class_loader();
    protection_domain = Klass::cast(from_class.as_klassOop())->protection_domain();
  }
Handle h_loader(class_loader);
Handle h_prot(protection_domain);
  jclass result = find_class_from_class_loader(env, h_name, init, h_loader, 
                                               h_prot, true, thread);
  
  return result;
JVM_END

static void is_lock_held_by_thread(oop loader,PerfCounter*counter,TRAPS){
if(loader==NULL)return;
  // check whether the current caller thread holds the lock or not.
  // If not, increment the corresponding counter
  if( !loader->is_self_locked() ) {
    counter->inc();
  }
}

// common code for JVM_DefineClass() and JVM_DefineClassWithSource()
static jclass jvm_define_class_common(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len, jobject pd, const char *source, TRAPS) {

  // Since exceptions can be thrown, class initialization can take place  
  // if name is NULL no check for class name in .class stream has to be made.
  symbolHandle class_name;
  if (name != NULL) {
    const int str_len = (int)strlen(name);
    if (str_len > symbolOopDesc::max_length()) {
      // It's impossible to create this class;  the name cannot fit
      // into the constant pool.
      THROW_MSG_0(vmSymbols::java_lang_NoClassDefFoundError(), name);
    }
    class_name = oopFactory::new_symbol_handle(name, str_len, CHECK_NULL);
  }

  ResourceMark rm(THREAD);
  ClassFileStream st((u1*) buf, len, (char *)source);

  StackBasedAllocation::ensure_in_heap((JavaThread*) THREAD, loader, source);

oop class_loader=JNIHandles::resolve(loader);
  if (UsePerfData) {
    is_lock_held_by_thread(class_loader, 
                           ClassLoader::sync_JVMDefineClassLockFreeCounter(),
                           THREAD);
  }
oop protection_domain=JNIHandles::resolve(pd);
  klassOop k = SystemDictionary::resolve_from_stream(class_name, class_loader, 
                                                     protection_domain, &st, 
                                                     CHECK_NULL);

return(jclass)JNIHandles::make_local(env,Klass::cast(k)->java_mirror_ref());
}


JVM_ENTRY(jclass, JVM_DefineClass, (JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len, jobject pd))
  JVMWrapper2("JVM_DefineClass %s", name);

  return jvm_define_class_common(env, name, loader, buf, len, pd, "__JVM_DefineClass__", THREAD);
JVM_END


JVM_ENTRY(jclass, JVM_DefineClassWithSource, (JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len, jobject pd, const char *source))
  JVMWrapper2("JVM_DefineClassWithSource %s", name);

  return jvm_define_class_common(env, name, loader, buf, len, pd, source, THREAD);
JVM_END


JVM_ENTRY(jclass, JVM_FindLoadedClass, (JNIEnv *env, jobject loader, jstring name))
  JVMWrapper("JVM_FindLoadedClass");
  ResourceMark rm(THREAD);

  Handle h_name (JNIHandles::resolve_as_non_null_ref(name));
  Handle string = java_lang_String::internalize_classname(h_name, CHECK_NULL);

  const char* str   = java_lang_String::as_utf8_string(string());
  // Sanity check, don't expect null
  if (str == NULL) return NULL;

  const int str_len = (int)strlen(str);
if(str_len==0||str_len>symbolOopDesc::max_length()){
    // It's impossible to create this class;  the name cannot fit
    // into the constant pool.
    return NULL;
  }
  symbolHandle klass_name = oopFactory::new_symbol_handle(str, str_len,CHECK_NULL);

  // Security Note:
  //   The Java level wrapper will perform the necessary security check allowing
  //   us to pass the NULL as the initiating class loader. 
oop oop_loader=JNIHandles::resolve(loader);
  if (UsePerfData) {
is_lock_held_by_thread(oop_loader,
                           ClassLoader::sync_JVMFindLoadedClassLockFreeCounter(),
                           THREAD);
  }
 
  klassOop k = SystemDictionary::find_instance_or_array_klass(klass_name, 
                                                              oop_loader, 
                                                              Handle(),
                                                              CHECK_NULL);

  return (k == NULL) ? NULL :
(jclass)JNIHandles::make_local(env,Klass::cast(k)->java_mirror_ref());
JVM_END


// Reflection support //////////////////////////////////////////////////////////////////////////////

JVM_ENTRY(jstring, JVM_GetClassName, (JNIEnv *env, jclass cls))
  assert (cls != NULL, "illegal class");
  JVMWrapper("JVM_GetClassName");
  JvmtiVMObjectAllocEventCollector oam;
  ResourceMark rm(THREAD);
  const char* name;
  if (java_lang_Class::is_primitive(JNIHandles::resolve(cls))) {    
    name = type2name(java_lang_Class::primitive_type(JNIHandles::resolve(cls)));
  } else {
    // Consider caching interned string in Klass
    klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve(cls));
    assert(k->is_klass(), "just checking");
    name = Klass::cast(k)->external_name();
  } 
  oop result = StringTable::intern((char*) name, CHECK_NULL);
  return (jstring) JNIHandles::make_local(env, result);
JVM_END


JVM_ENTRY(jobjectArray, JVM_GetClassInterfaces, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassInterfaces");
  JvmtiVMObjectAllocEventCollector oam;  
  oop mirror = JNIHandles::resolve_non_null(cls);

  // Special handling for primitive objects
  if (java_lang_Class::is_primitive(mirror)) {    
    // Primitive objects does not have any interfaces
objArrayOop r=oopFactory::new_objArray(SystemDictionary::class_klass(),0,true/*in stack*/,CHECK_NULL);
    return (jobjectArray) JNIHandles::make_local(env, r);
  }

  KlassHandle klass(java_lang_Class::as_klassRef(mirror));
  // Figure size of result array    
  int size;
  if (klass->oop_is_instance()) {
    size = instanceKlass::cast(klass())->local_interfaces()->length();
  } else {
    assert(klass->oop_is_objArray() || klass->oop_is_typeArray(), "Illegal mirror klass");
    size = 2;
  }

  // Allocate result array
objArrayOop r=oopFactory::new_objArray(SystemDictionary::class_klass(),size,true/*in stack*/,CHECK_NULL);
  objArrayHandle result (THREAD, r);
  // Fill in result
  if (klass->oop_is_instance()) {
    // Regular instance klass, fill in all local interfaces
    for (int index = 0; index < size; index++) {
      klassOop k = klassOop(instanceKlass::cast(klass())->local_interfaces()->obj_at(index));
result->ref_at_put(index,Klass::cast(k)->java_mirror_ref());
    }
  } else {
    // All arrays implement java.lang.Cloneable and java.io.Serializable
result->ref_at_put(0,Klass::cast(SystemDictionary::cloneable_klass())->java_mirror_ref());
result->ref_at_put(1,Klass::cast(SystemDictionary::serializable_klass())->java_mirror_ref());
  }
return(jobjectArray)JNIHandles::make_local(env,result.as_ref());
JVM_END


JVM_ENTRY(jobject, JVM_GetClassLoader, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassLoader");
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {
    return NULL;
  }
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));  
  oop loader = Klass::cast(k)->class_loader();
  return JNIHandles::make_local(env, loader);
JVM_END


JVM_QUICK_ENTRY(jboolean, JVM_IsInterface, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_IsInterface");
  oop mirror = JNIHandles::resolve_non_null(cls);
  if (java_lang_Class::is_primitive(mirror)) {
    return JNI_FALSE;
  }
  klassOop k = java_lang_Class::as_klassOop(mirror);
  jboolean result = Klass::cast(k)->is_interface();
  assert(!result || Klass::cast(k)->oop_is_instance(),
         "all interfaces are instance types");
  // The compiler intrinsic for isInterface tests the
  // Klass::_access_flags bits in the same way.
  return result;
JVM_END


JVM_ENTRY(jobjectArray, JVM_GetClassSigners, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassSigners");
  JvmtiVMObjectAllocEventCollector oam;
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {
    // There are no signers for primitive types 
    return NULL;    
  }

  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  objArrayOop signers = NULL;
  if (Klass::cast(k)->oop_is_instance()) {
    signers = instanceKlass::cast(k)->signers();
  }

  // If there are no signers set in the class, or if the class
  // is an array, return NULL.
  if (signers == NULL) return NULL;

  // copy of the signers array
  klassOop element = objArrayKlass::cast(signers->klass())->element_klass();
objArrayOop signers_copy=oopFactory::new_objArray(element,signers->length(),true/*in stack*/,CHECK_NULL);
  for (int index = 0; index < signers->length(); index++) {
    signers_copy->obj_at_put(index, signers->obj_at(index));
  }

  // return the copy
  return (jobjectArray) JNIHandles::make_local(env, signers_copy);
JVM_END


JVM_ENTRY(void, JVM_SetClassSigners, (JNIEnv *env, jclass cls, jobjectArray signers))
  JVMWrapper("JVM_SetClassSigners");
  if (!java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {          
    // This call is ignored for primitive types and arrays.
    // Signers are only set once, ClassLoader.java, and thus shouldn't
    // be called with an array.  Only the bootstrap loader creates arrays.
    klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
    if (Klass::cast(k)->oop_is_instance()) {
      instanceKlass::cast(k)->set_signers(objArrayOop(JNIHandles::resolve(signers)));
    }
  }
JVM_END


JVM_ENTRY(jobject, JVM_GetProtectionDomain, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetProtectionDomain");
  if (JNIHandles::resolve(cls) == NULL) {
    THROW_(vmSymbols::java_lang_NullPointerException(), NULL);
  }

  if (java_lang_Class::is_primitive(JNIHandles::resolve(cls))) {
    // Primitive types does not have a protection domain.
    return NULL;
  }

  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve(cls));  
  if (!Klass::cast(k)->oop_is_instance()) return NULL;
  return (jobject) JNIHandles::make_local(env, Klass::cast(k)->protection_domain());
JVM_END


// Obsolete since 1.2 (Class.setProtectionDomain removed), although
// still defined in core libraries as of 1.5.
JVM_ENTRY(void,JVM_SetProtectionDomain,(JNIEnv*env,jclass cls,jobject protection_domain))
  JVMWrapper("JVM_SetProtectionDomain");
  if (JNIHandles::resolve(cls) == NULL) {
    THROW(vmSymbols::java_lang_NullPointerException());
  }
  if (!java_lang_Class::is_primitive(JNIHandles::resolve(cls))) {
    // Call is ignored for primitive types
    klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve(cls));    

    // cls won't be an array, as this called only from ClassLoader.defineClass
    if (Klass::cast(k)->oop_is_instance()) {
      oop pd = JNIHandles::resolve(protection_domain);
      assert(pd == NULL || pd->is_oop(), "just checking");
      instanceKlass::cast(k)->set_protection_domain(pd);    
    }
  }
JVM_END


JVM_ENTRY(jobject, JVM_DoPrivileged, (JNIEnv *env, jclass cls, jobject action, jobject context, jboolean wrapException))
  JVMWrapper("JVM_DoPrivileged");

  if (action == NULL) {
    THROW_MSG_0(vmSymbols::java_lang_NullPointerException(), "Null action");
  }

  // Stack allocated list of privileged stack elements
  PrivilegedElement pi;

  // Check that action object understands "Object run()"
Handle object(JNIHandles::resolve_as_ref(action));

  // get run() method
  methodOop m_oop = Klass::cast(object->klass())->uncached_lookup_method(
                                           vmSymbols::run_method_name(), 
                                           vmSymbols::void_object_signature());  
  methodHandle m (THREAD, m_oop);
  if (m.is_null() || !m->is_method() || !methodOop(m())->is_public() || methodOop(m())->is_static()) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(), "No run method");
  }

  // Compute the frame initiating the do privileged operation and setup the privileged stack
  ResourceMark rm;
  bool vfdone;
  { vframe vf(thread);
vf.security_get_caller_frame(1);
  vfdone = vf.done();

  if (!vfdone) {    
    assert0( THREAD->is_Java_thread() );
    StackBasedAllocation::ensure_in_heap(((JavaThread*)THREAD), context, FCN_NAME); // context must be escaped
pi.initialize(&vf,JNIHandles::resolve(context),thread->privileged_stack_top(),CHECK_NULL);
    thread->set_privileged_stack_top(&pi);      
  }
  }

    
  // invoke the Object run() in the action object. We cannot use call_interface here, since the static type
  // is not really known - it is either java.security.PrivilegedAction or java.security.PrivilegedExceptionAction
  Handle pending_exception;
  JavaValue result(T_OBJECT);
  JavaCallArguments args(object);
  JavaCalls::call(&result, m, &args, THREAD);

  // done with action, remove ourselves from the list
  if (!vfdone) {
    assert(thread->privileged_stack_top() != NULL && thread->privileged_stack_top() == &pi, "wrong top element");
    thread->set_privileged_stack_top(thread->privileged_stack_top()->next());	
  }  

  if (HAS_PENDING_EXCEPTION) {      
    pending_exception = Handle(THREAD, PENDING_EXCEPTION);
    CLEAR_PENDING_EXCEPTION;
            
    if ( pending_exception->is_a(SystemDictionary::exception_klass()) && 
        !pending_exception->is_a(SystemDictionary::runtime_exception_klass())) {      
      // Throw a java.security.PrivilegedActionException(Exception e) exception
      JavaCallArguments args(pending_exception);
      THROW_ARG_0(vmSymbolHandles::java_security_PrivilegedActionException(),
                  vmSymbolHandles::exception_void_signature(),
                  &args);
    }
  }

  if (pending_exception.not_null()) THROW_OOP_0(pending_exception());
return JNIHandles::make_local(env,(*(objectRef*)result.get_value_addr()).as_oop());
JVM_END


// Returns the inherited_access_control_context field of the running thread.
JVM_ENTRY(jobject,JVM_GetInheritedAccessControlContext,(JNIEnv*env,jclass cls))
  JVMWrapper("JVM_GetInheritedAccessControlContext");
  oop result = java_lang_Thread::inherited_access_control_context(thread->threadObj());
  return JNIHandles::make_local(env, result);
JVM_END

define_array(handleArray, Handle)     define_stack(handleStack, handleArray)

JVM_ENTRY(jobject, JVM_GetStackAccessControlContext, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetStackAccessControlContext");  
  if (!UsePrivilegedStack) return NULL;

  ResourceMark rm(THREAD);
  handleStack local_array(12);  assert(THREAD==thread && thread==Thread::current(), "local_array uses local rm" );
  JvmtiVMObjectAllocEventCollector oam;

  // count the protection domains on the execution stack. We collapse
  // duplicate consecutive protection domains into a single one, as
  // well as stopping when we hit a privileged frame.

  oop previous_protection_domain = NULL;
  Handle privileged_context(thread, NULL);  
  bool is_privileged = false;
  oop protection_domain = NULL;  
  
  for( vframe vf(thread); !vf.done(); vf.next()) {
    // get method of frame
methodOop method=vf.method();
    intptr_t* frame_id   = vf.get_frame().id();
    
    // check the privileged frames to see if we have a match
    if (thread->privileged_stack_top() && thread->privileged_stack_top()->frame_id() == frame_id) {
      // this frame is privileged 
      is_privileged = true;
      privileged_context = Handle(thread, thread->privileged_stack_top()->privileged_context());
      protection_domain  = thread->privileged_stack_top()->protection_domain();
    } else {
      protection_domain = instanceKlass::cast(method->method_holder())->protection_domain();
    }		
     
    if ((previous_protection_domain != protection_domain) && (protection_domain != NULL)) {
Handle h(thread,protection_domain);
      local_array.push(h);
      previous_protection_domain = protection_domain;
    }

    if (is_privileged) break;
  } 


  // either all the domains on the stack were system domains, or
  // we had a privileged system domain
objArrayOop context=NULL;
if(local_array.length()==0){
    if (is_privileged && privileged_context.is_null()) return NULL;
  } else {
    // May cause GC - good thing local_array is an array of Handles
    GET_RPC;
    context = oopFactory::new_objArray(SystemDictionary::protectionDomain_klass(),local_array.length(),RPC/*in stack*/, CHECK_NULL);
    for (int index = 0; index < local_array.length(); index++) {
      assert( local_array.at(index)()->is_oop(), "did not survive gc?" );
      context->obj_at_put(index, local_array.at(index)());
    }
  }
oop result=java_security_AccessControlContext::create(context,is_privileged,privileged_context,CHECK_NULL);
  return JNIHandles::make_local(env, result);
JVM_END


JVM_QUICK_ENTRY(jboolean, JVM_IsArrayClass, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_IsArrayClass");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  return (k != NULL) && Klass::cast(k)->oop_is_javaArray() ? true : false;
JVM_END


JVM_QUICK_ENTRY(jboolean, JVM_IsPrimitiveClass, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_IsPrimitiveClass");
  oop mirror = JNIHandles::resolve_non_null(cls);
  return (jboolean) java_lang_Class::is_primitive(mirror);
JVM_END


JVM_ENTRY(jclass, JVM_GetComponentType, (JNIEnv *env, jclass cls))  
  JVMWrapper("JVM_GetComponentType");
  oop mirror = JNIHandles::resolve_non_null(cls);
  oop result = Reflection::array_component_type(mirror, CHECK_NULL);
  return (jclass) JNIHandles::make_local(env, result);
JVM_END


JVM_ENTRY(jint, JVM_GetClassModifiers, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassModifiers");
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {
    // Primitive type
    return JVM_ACC_ABSTRACT | JVM_ACC_FINAL | JVM_ACC_PUBLIC;
  }

  Klass* k = Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls)));
  debug_only(int computed_modifiers = k->compute_modifier_flags(CHECK_0));
  assert(k->modifier_flags() == computed_modifiers, "modifiers cache is OK");
  return k->modifier_flags();
JVM_END


// Inner class reflection ///////////////////////////////////////////////////////////////////////////////

JVM_ENTRY(jobjectArray, JVM_GetDeclaredClasses, (JNIEnv *env, jclass ofClass))
  const int inner_class_info_index = 0;
  const int outer_class_info_index = 1;

  JvmtiVMObjectAllocEventCollector oam;
  // ofClass is a reference to a java_lang_Class object. The mirror object
  // of an instanceKlass

  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(ofClass)) || 
      ! Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(ofClass)))->oop_is_instance()) {
oop result=oopFactory::new_objArray(SystemDictionary::class_klass(),0,true/*in stack*/,CHECK_NULL);
    return (jobjectArray)JNIHandles::make_local(env, result);
  }

  instanceKlassHandle k(java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(ofClass)));

  if (k->inner_classes()->length() == 0) {
    // Neither an inner nor outer class
oop result=oopFactory::new_objArray(SystemDictionary::class_klass(),0,true/*in stack*/,CHECK_NULL);
    return (jobjectArray)JNIHandles::make_local(env, result);
  }

  // find inner class info
  typeArrayHandle    icls(thread, k->inner_classes());
constantPoolHandle cp(k->constantsRef());
  int length = icls->length();

  // Allocate temp. result array
objArrayOop r=oopFactory::new_objArray(SystemDictionary::class_klass(),length/4,true/*in stack*/,CHECK_NULL);
  objArrayHandle result (THREAD, r);
  int members = 0;
  
  for(int i = 0; i < length; i += 4) {
    int ioff = icls->ushort_at(i + inner_class_info_index);
    int ooff = icls->ushort_at(i + outer_class_info_index);         

    if (ioff != 0 && ooff != 0) {
      // Check to see if the name matches the class we're looking for
      // before attempting to find the class.
      if (cp->klass_name_at_matches(k, ooff)) { 
        klassOop outer_klass = cp->klass_at(ooff, CHECK_NULL);
        if (outer_klass == k()) {
           klassOop ik = cp->klass_at(ioff, CHECK_NULL);
           instanceKlassHandle inner_klass (THREAD, ik);

           // Throws an exception if outer klass has not declared k as 
           // an inner klass
           Reflection::check_for_inner_class(k, inner_klass, CHECK_NULL);

result->ref_at_put(members,inner_klass->java_mirror_ref());
           members++;
        }
      }
    }
  }

  if (members != length) {
    // Return array of right length
objArrayOop res=oopFactory::new_objArray(SystemDictionary::class_klass(),members,true/*in stack*/,CHECK_NULL);
    for(int i = 0; i < members; i++) {
      res->obj_at_put(i, result->obj_at(i));
    }
    return (jobjectArray)JNIHandles::make_local(env, res);
  } 

return(jobjectArray)JNIHandles::make_local(env,result.as_ref());
JVM_END


JVM_ENTRY(jclass, JVM_GetDeclaringClass, (JNIEnv *env, jclass ofClass))
  const int inner_class_info_index = 0;
  const int outer_class_info_index = 1;
  
  // ofClass is a reference to a java_lang_Class object.
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(ofClass)) || 
      ! Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(ofClass)))->oop_is_instance()) {
    return NULL;
  }

  instanceKlassHandle k(java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(ofClass)));

  if (k->inner_classes()->length() == 0) {
    // No inner class info => no declaring class
    return NULL;
  }

  typeArrayHandle i_icls(thread, k->inner_classes());
constantPoolHandle i_cp(k->constantsRef());
  int i_length = i_icls->length();
  
  bool found = false;
  klassOop ok;
  instanceKlassHandle outer_klass;
  
  // Find inner_klass attribute 
  for(int i = 0; i < i_length && !found; i+= 4) {
    int ioff = i_icls->ushort_at(i + inner_class_info_index);
    int ooff = i_icls->ushort_at(i + outer_class_info_index);         

    if (ioff != 0 && ooff != 0) {
      // Check to see if the name matches the class we're looking for
      // before attempting to find the class.
      if (i_cp->klass_name_at_matches(k, ioff)) { 
        klassOop inner_klass = i_cp->klass_at(ioff, CHECK_NULL);
        if (k() == inner_klass) {
          found = true;
          ok = i_cp->klass_at(ooff, CHECK_NULL);
          outer_klass = instanceKlassHandle(thread, ok);
        }
      }        
    }
  }

  // If no inner class attribute found for this class. 
  if (!found) return NULL;

  // Throws an exception if outer klass has not declared k as an inner klass
  Reflection::check_for_inner_class(outer_klass, k, CHECK_NULL);

return(jclass)JNIHandles::make_local(env,outer_klass->java_mirror_ref());
JVM_END


JVM_ENTRY(jstring, JVM_GetClassSignature, (JNIEnv *env, jclass cls))
  assert (cls != NULL, "illegal class");
  JVMWrapper("JVM_GetClassSignature");
  JvmtiVMObjectAllocEventCollector oam;
  ResourceMark rm(THREAD);
  // Return null for arrays and primatives
  if (!java_lang_Class::is_primitive(JNIHandles::resolve(cls))) {    
    klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve(cls));
    if (Klass::cast(k)->oop_is_instance()) {
      symbolHandle sym = symbolHandle(THREAD, instanceKlass::cast(k)->generic_signature());
      if (sym.is_null()) return NULL;
Handle str=java_lang_String::create_from_symbol(sym,true/*SBA*/,CHECK_NULL);
      return (jstring) JNIHandles::make_local(env, str());
    }
  }
  return NULL;
JVM_END


JVM_ENTRY(jbyteArray, JVM_GetClassAnnotations, (JNIEnv *env, jclass cls))
  assert (cls != NULL, "illegal class");
  JVMWrapper("JVM_GetClassAnnotations");
  ResourceMark rm(THREAD);
  // Return null for arrays and primitives
  if (!java_lang_Class::is_primitive(JNIHandles::resolve(cls))) {    
    klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve(cls));
    if (Klass::cast(k)->oop_is_instance()) {
      return (jbyteArray) JNIHandles::make_local(env, 
                                  instanceKlass::cast(k)->class_annotations());
    }
  }
  return NULL;
JVM_END


JVM_ENTRY(jbyteArray, JVM_GetFieldAnnotations, (JNIEnv *env, jobject field))
  assert(field != NULL, "illegal field");
  JVMWrapper("JVM_GetFieldAnnotations");

  // some of this code was adapted from from jni_FromReflectedField

  // field is a handle to a java.lang.reflect.Field object
  oop reflected = JNIHandles::resolve_non_null(field);
  oop mirror    = java_lang_reflect_Field::clazz(reflected);
  klassOop k    = java_lang_Class::as_klassOop(mirror);
  int slot      = java_lang_reflect_Field::slot(reflected);
  int modifiers = java_lang_reflect_Field::modifiers(reflected);

  fieldDescriptor fd;
  KlassHandle kh(THREAD, k);
  intptr_t offset = instanceKlass::cast(kh())->offset_from_fields(slot);

  if (modifiers & JVM_ACC_STATIC) {
    // for static fields we only look in the current class
    if (!instanceKlass::cast(kh())->find_local_field_from_offset(offset,
                                                                 true, &fd)) {
      assert(false, "cannot find static field");
      return NULL;  // robustness
    }
  } else {
    // for instance fields we start with the current class and work
    // our way up through the superclass chain
    if (!instanceKlass::cast(kh())->find_field_from_offset(offset, false,
                                                           &fd)) {
      assert(false, "cannot find instance field");
      return NULL;  // robustness
    }
  }

  return (jbyteArray) JNIHandles::make_local(env, fd.annotations());
JVM_END


static methodOop jvm_get_method_common(jobject method, TRAPS) {
  // some of this code was adapted from from jni_FromReflectedMethod

  oop reflected = JNIHandles::resolve_non_null(method);
  oop mirror    = NULL;
  int slot      = 0;

  if (reflected->klass() == SystemDictionary::reflect_constructor_klass()) {
    mirror = java_lang_reflect_Constructor::clazz(reflected);
    slot   = java_lang_reflect_Constructor::slot(reflected);
  } else {
    assert(reflected->klass() == SystemDictionary::reflect_method_klass(),
           "wrong type");
    mirror = java_lang_reflect_Method::clazz(reflected);
    slot   = java_lang_reflect_Method::slot(reflected);
  }
  klassOop k = java_lang_Class::as_klassOop(mirror);

  KlassHandle kh(THREAD, k);
  methodOop m = instanceKlass::cast(kh())->method_with_idnum(slot);
  if (m == NULL) {
    assert(false, "cannot find method");
    return NULL;  // robustness
  }

  return m;
}


JVM_ENTRY(jbyteArray, JVM_GetMethodAnnotations, (JNIEnv *env, jobject method))
  JVMWrapper("JVM_GetMethodAnnotations");

  // method is a handle to a java.lang.reflect.Method object
  methodOop m = jvm_get_method_common(method, CHECK_NULL);
  return (jbyteArray) JNIHandles::make_local(env, m->annotations());
JVM_END


JVM_ENTRY(jbyteArray, JVM_GetMethodDefaultAnnotationValue, (JNIEnv *env, jobject method))
  JVMWrapper("JVM_GetMethodDefaultAnnotationValue");

  // method is a handle to a java.lang.reflect.Method object
  methodOop m = jvm_get_method_common(method, CHECK_NULL);
  return (jbyteArray) JNIHandles::make_local(env, m->annotation_default());
JVM_END


JVM_ENTRY(jbyteArray, JVM_GetMethodParameterAnnotations, (JNIEnv *env, jobject method))
  JVMWrapper("JVM_GetMethodParameterAnnotations");

  // method is a handle to a java.lang.reflect.Method object
  methodOop m = jvm_get_method_common(method, CHECK_NULL);
  return (jbyteArray) JNIHandles::make_local(env, m->parameter_annotations());
JVM_END


// New (JDK 1.4) reflection implementation /////////////////////////////////////

JVM_ENTRY(jobjectArray, JVM_GetClassDeclaredFields, (JNIEnv *env, jclass ofClass, jboolean publicOnly))
{
  JVMWrapper("JVM_GetClassDeclaredFields");
  JvmtiVMObjectAllocEventCollector oam;
  GET_RPC;

  // Exclude primitive types and array types
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(ofClass)) ||
      Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(ofClass)))->oop_is_javaArray()) {
    // Return empty array
oop res=oopFactory::new_objArray(SystemDictionary::reflect_field_klass(),0,RPC,CHECK_NULL);
    return (jobjectArray) JNIHandles::make_local(env, res);
  }

  instanceKlassHandle k(java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(ofClass)));
  constantPoolHandle cp(k->constantsRef());
  
  // Ensure class is linked
  k->link_class(CHECK_NULL);

  typeArrayHandle fields(THREAD, k->fields());
  int fields_len = fields->length();

  // 4496456 We need to filter out java.lang.Throwable.backtrace
  bool skip_backtrace = false;
  // We need to filter out from java.lang.ref.Reference discovered and referent
  bool skip_reference_fields = false;

  // Allocate result
  int num_fields;

  if (publicOnly) {
    num_fields = 0;
    for (int i = 0, j = 0; i < fields_len; i += instanceKlass::next_offset, j++) {
      int mods = fields->ushort_at(i + instanceKlass::access_flags_offset) & JVM_RECOGNIZED_FIELD_MODIFIERS;
      if (mods & JVM_ACC_PUBLIC) ++num_fields;
    }
  } else {
    num_fields = fields_len / instanceKlass::next_offset;

    if (k() == SystemDictionary::throwable_klass()) {
      num_fields--;
      skip_backtrace = true;
    }

if(k()==SystemDictionary::reference_klass()){
      num_fields -= 2;
      skip_reference_fields = true;
    }
  }

  objArrayOop r = oopFactory::new_objArray(SystemDictionary::reflect_field_klass(), num_fields, RPC, CHECK_NULL);
  objArrayHandle result (THREAD, r);

  int out_idx = 0;
  fieldDescriptor fd;
  for (int i = 0; i < fields_len; i += instanceKlass::next_offset) {
    if (skip_backtrace) {
      // 4496456 skip java.lang.Throwable.backtrace
      int offset = k->offset_from_fields(i);
      if (offset == java_lang_Throwable::get_backtrace_offset()) continue;
    }

    if (skip_reference_fields) {
      int offset = k->offset_from_fields(i);
      if (offset == java_lang_ref_Reference::referent_offset) continue;
      if (offset == java_lang_ref_Reference::discovered_offset) continue;
    }

    int mods = fields->ushort_at(i + instanceKlass::access_flags_offset) & JVM_RECOGNIZED_FIELD_MODIFIERS;
    if (!publicOnly || (mods & JVM_ACC_PUBLIC)) {
      fd.initialize(k(), i);
oop field=Reflection::new_field(&fd,RPC,CHECK_NULL);
      result->obj_at_put(out_idx, field);
      ++out_idx;
    }
  }
  assert(out_idx == num_fields, "just checking");
  return (jobjectArray) JNIHandles::make_local(env, result());
}
JVM_END

JVM_ENTRY(jobjectArray, JVM_GetClassDeclaredMethods, (JNIEnv *env, jclass ofClass, jboolean publicOnly))
{
  JVMWrapper("JVM_GetClassDeclaredMethods");
  JvmtiVMObjectAllocEventCollector oam;
  GET_RPC;

  // Exclude primitive types and array types
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(ofClass))
      || Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(ofClass)))->oop_is_javaArray()) {
    // Return empty array
oop res=oopFactory::new_objArray(SystemDictionary::reflect_method_klass(),0,RPC,CHECK_NULL);
    return (jobjectArray) JNIHandles::make_local(env, res);
  }

  instanceKlassHandle k(java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(ofClass)));
  
  // Ensure class is linked
  k->link_class(CHECK_NULL);

  objArrayHandle methods (THREAD, k->methods());
  int methods_length = methods->length();
  int num_methods = 0;

  int i;
  for (i = 0; i < methods_length; i++) {
    methodHandle method(THREAD, (methodOop) methods->obj_at(i));
    if (!method->is_initializer()) {
      if (!publicOnly || method->is_public()) {
        ++num_methods;
      }
    }
  }

  // Allocate result
objArrayOop r=oopFactory::new_objArray(SystemDictionary::reflect_method_klass(),num_methods,RPC,CHECK_NULL);
  objArrayHandle result (THREAD, r);

  int out_idx = 0;
  for (i = 0; i < methods_length; i++) {
    methodHandle method(THREAD, (methodOop) methods->obj_at(i));
    if (!method->is_initializer()) {
      if (!publicOnly || method->is_public()) {
oop m=Reflection::new_method(method,false,RPC,CHECK_NULL);
        result->obj_at_put(out_idx, m);
        ++out_idx;
      }
    }
  }
  assert(out_idx == num_methods, "just checking");
  return (jobjectArray) JNIHandles::make_local(env, result());
}
JVM_END

JVM_ENTRY(jobjectArray, JVM_GetClassDeclaredConstructors, (JNIEnv *env, jclass ofClass, jboolean publicOnly))
{
  JVMWrapper("JVM_GetClassDeclaredConstructors");
  JvmtiVMObjectAllocEventCollector oam;
  GET_RPC;

  // Exclude primitive types and array types
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(ofClass))
      || Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(ofClass)))->oop_is_javaArray()) {
    // Return empty array
oop res=oopFactory::new_objArray(SystemDictionary::reflect_constructor_klass(),0,RPC,CHECK_NULL);
    return (jobjectArray) JNIHandles::make_local(env, res);
  }

  instanceKlassHandle k(java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(ofClass)));
  
  // Ensure class is linked
  k->link_class(CHECK_NULL);

  objArrayHandle methods (THREAD, k->methods());
  int methods_length = methods->length();
  int num_constructors = 0;

  int i;
  for (i = 0; i < methods_length; i++) {
    methodHandle method(THREAD, (methodOop) methods->obj_at(i));
    if (method->is_initializer() && !method->is_static()) {
      if (!publicOnly || method->is_public()) {
        ++num_constructors;
      }
    }
  }

  // Allocate result
objArrayOop r=oopFactory::new_objArray(SystemDictionary::reflect_constructor_klass(),num_constructors,RPC,CHECK_NULL);
  objArrayHandle result(THREAD, r);

  int out_idx = 0;
  for (i = 0; i < methods_length; i++) {
    methodHandle method(THREAD, (methodOop) methods->obj_at(i));
    if (method->is_initializer() && !method->is_static()) {
      if (!publicOnly || method->is_public()) {
oop m=Reflection::new_constructor(method,RPC,CHECK_NULL);
        result->obj_at_put(out_idx, m);
        ++out_idx;
      }
    }
  }
  assert(out_idx == num_constructors, "just checking");
  return (jobjectArray) JNIHandles::make_local(env, result());
}
JVM_END

JVM_ENTRY(jint, JVM_GetClassAccessFlags, (JNIEnv *env, jclass cls))
{
  JVMWrapper("JVM_GetClassAccessFlags");
if(cls==NULL){
    THROW_(vmSymbols::java_lang_NullPointerException(), JNI_FALSE);
  }
  if (java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {
    // Primitive type
    return JVM_ACC_ABSTRACT | JVM_ACC_FINAL | JVM_ACC_PUBLIC;
  }

  Klass* k = Klass::cast(java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls)));
  return k->access_flags().as_int() & JVM_ACC_WRITTEN_FLAGS;
}
JVM_END


// Constant pool access //////////////////////////////////////////////////////////

JVM_ENTRY(jobject, JVM_GetClassConstantPool, (JNIEnv *env, jclass cls))
{
  JVMWrapper("JVM_GetClassConstantPool");
  JvmtiVMObjectAllocEventCollector oam;

  // Return null for primitives and arrays
  if (!java_lang_Class::is_primitive(JNIHandles::resolve_non_null(cls))) {
klassRef k=java_lang_Class::as_klassRef(JNIHandles::resolve_as_non_null_ref(cls));
    if (Klass::cast(k.as_klassOop())->oop_is_instance()) {
      instanceKlassHandle k_h(k);
      Handle jcp = sun_reflect_ConstantPool::create(CHECK_NULL);
      sun_reflect_ConstantPool::set_cp_oop(jcp(), k_h->constants());
      return JNIHandles::make_local(jcp());
    }
  }
  return NULL;
}
JVM_END


JVM_ENTRY(jint, JVM_ConstantPoolGetSize, (JNIEnv *env, jobject unused, jobject jcpool))
{
  JVMWrapper("JVM_ConstantPoolGetSize");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  return cp->length();
}
JVM_END


static void bounds_check(constantPoolHandle cp, jint index, TRAPS) {
  if (!cp->is_within_bounds(index)) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "Constant pool index out of bounds");
  }
}


JVM_ENTRY(jclass, JVM_ConstantPoolGetClassAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetClassAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_klass() && !tag.is_unresolved_klass()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  klassOop k = cp->klass_at(index, CHECK_NULL);
return(jclass)JNIHandles::make_local(k->klass_part()->java_mirror_ref());
}
JVM_END


JVM_ENTRY(jclass, JVM_ConstantPoolGetClassAtIfLoaded, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetClassAtIfLoaded");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_klass() && !tag.is_unresolved_klass()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  klassOop k = constantPoolOopDesc::klass_at_if_loaded(cp, index);
  if (k == NULL) return NULL;
return(jclass)JNIHandles::make_local(k->klass_part()->java_mirror_ref());
}
JVM_END

static jobject get_method_at_helper(constantPoolHandle cp, jint index, bool force_resolution, TRAPS) {
  constantTag tag = cp->tag_at(index);
  if (!tag.is_method() && !tag.is_interface_method()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  int klass_ref  = cp->uncached_klass_ref_index_at(index);
  klassOop k_o;
  if (force_resolution) {
    k_o = cp->klass_at(klass_ref, CHECK_NULL);
  } else {
    k_o = constantPoolOopDesc::klass_at_if_loaded(cp, klass_ref);
    if (k_o == NULL) return NULL;
  }
  instanceKlassHandle k(THREAD, k_o);
  symbolOop name = cp->uncached_name_ref_at(index);
  symbolOop sig  = cp->uncached_signature_ref_at(index);
  methodHandle m (THREAD, k->find_method(name, sig));
  if (m.is_null()) {
    THROW_MSG_0(vmSymbols::java_lang_RuntimeException(), "Unable to look up method in target class");
  }
  oop method;
  if (!m->is_initializer() || m->is_static()) {
method=Reflection::new_method(m,true,true/*in stack*/,CHECK_NULL);
  } else {
method=Reflection::new_constructor(m,true/*in stack*/,CHECK_NULL);
  }
  return JNIHandles::make_local(method);
}

JVM_ENTRY(jobject, JVM_ConstantPoolGetMethodAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetMethodAt");
  JvmtiVMObjectAllocEventCollector oam;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  jobject res = get_method_at_helper(cp, index, true, CHECK_NULL);
  return res;
}
JVM_END

JVM_ENTRY(jobject, JVM_ConstantPoolGetMethodAtIfLoaded, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetMethodAtIfLoaded");
  JvmtiVMObjectAllocEventCollector oam;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  jobject res = get_method_at_helper(cp, index, false, CHECK_NULL);
  return res;
}
JVM_END

static jobject get_field_at_helper(constantPoolHandle cp, jint index, bool force_resolution, intptr_t sba_hint, TRAPS) {
  constantTag tag = cp->tag_at(index);
  if (!tag.is_field()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  int klass_ref  = cp->uncached_klass_ref_index_at(index);
  klassOop k_o;
  if (force_resolution) {
    k_o = cp->klass_at(klass_ref, CHECK_NULL);
  } else {
    k_o = constantPoolOopDesc::klass_at_if_loaded(cp, klass_ref);
    if (k_o == NULL) return NULL;
  }
  instanceKlassHandle k(THREAD, k_o);
  symbolOop name = cp->uncached_name_ref_at(index);
  symbolOop sig  = cp->uncached_signature_ref_at(index);
  fieldDescriptor fd;
  klassOop target_klass = k->find_field(name, sig, &fd);
  if (target_klass == NULL) {
    THROW_MSG_0(vmSymbols::java_lang_RuntimeException(), "Unable to look up field in target class");
  }
oop field=Reflection::new_field(&fd,sba_hint,CHECK_NULL);
  return JNIHandles::make_local(field);
}

JVM_ENTRY(jobject, JVM_ConstantPoolGetFieldAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetFieldAt");
  JvmtiVMObjectAllocEventCollector oam;
  GET_RPC;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
jobject res=get_field_at_helper(cp,index,true,RPC,CHECK_NULL);
  return res;
}
JVM_END

JVM_ENTRY(jobject, JVM_ConstantPoolGetFieldAtIfLoaded, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetFieldAtIfLoaded");
  JvmtiVMObjectAllocEventCollector oam;
  GET_RPC;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
jobject res=get_field_at_helper(cp,index,false,RPC,CHECK_NULL);
  return res;
}
JVM_END

JVM_ENTRY(jobjectArray, JVM_ConstantPoolGetMemberRefInfoAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetMemberRefInfoAt");
  JvmtiVMObjectAllocEventCollector oam;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_field_or_method()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  int klass_ref = cp->uncached_klass_ref_index_at(index);
  symbolHandle klass_name (THREAD, cp->klass_name_at(klass_ref));
  symbolHandle member_name(THREAD, cp->uncached_name_ref_at(index));
  symbolHandle member_sig (THREAD, cp->uncached_signature_ref_at(index));
objArrayOop dest_o=oopFactory::new_objArray(SystemDictionary::string_klass(),3,true/*SBA*/,CHECK_NULL);
  objArrayHandle dest(THREAD, dest_o);
Handle str=java_lang_String::create_from_symbol(klass_name,true/*SBA*/,CHECK_NULL);
  dest->obj_at_put(0, str());
str=java_lang_String::create_from_symbol(member_name,true/*SBA*/,CHECK_NULL);
  dest->obj_at_put(1, str());
str=java_lang_String::create_from_symbol(member_sig,true/*SBA*/,CHECK_NULL);
  dest->obj_at_put(2, str());
  return (jobjectArray) JNIHandles::make_local(dest());
}
JVM_END

JVM_ENTRY(jint, JVM_ConstantPoolGetIntAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetIntAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_0);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_int()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  return cp->int_at(index);
}
JVM_END

JVM_ENTRY(jlong, JVM_ConstantPoolGetLongAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetLongAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_(0L));
  constantTag tag = cp->tag_at(index);
  if (!tag.is_long()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  return cp->long_at(index);
}
JVM_END

JVM_ENTRY(jfloat, JVM_ConstantPoolGetFloatAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetFloatAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_(0.0f));
  constantTag tag = cp->tag_at(index);
  if (!tag.is_float()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  return cp->float_at(index);
}
JVM_END

JVM_ENTRY(jdouble, JVM_ConstantPoolGetDoubleAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetDoubleAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_(0.0));
  constantTag tag = cp->tag_at(index);
  if (!tag.is_double()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  return cp->double_at(index);
}
JVM_END

JVM_ENTRY(jstring, JVM_ConstantPoolGetStringAt, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetStringAt");
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_string() && !tag.is_unresolved_string()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  oop str = cp->string_at(index, CHECK_NULL);
  return (jstring) JNIHandles::make_local(str);
}
JVM_END

JVM_ENTRY(jstring, JVM_ConstantPoolGetUTF8At, (JNIEnv *env, jobject unused, jobject jcpool, jint index))
{
  JVMWrapper("JVM_ConstantPoolGetUTF8At");
  JvmtiVMObjectAllocEventCollector oam;
  constantPoolHandle cp = constantPoolHandle(THREAD, constantPoolOop(JNIHandles::resolve_non_null(jcpool)));
  bounds_check(cp, index, CHECK_NULL);
  constantTag tag = cp->tag_at(index);
  if (!tag.is_symbol()) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Wrong type at constant pool index");
  }
  symbolOop sym_o = cp->symbol_at(index);
  symbolHandle sym(THREAD, sym_o);
Handle str=java_lang_String::create_from_symbol(sym,true/*SBA*/,CHECK_NULL);
  return (jstring) JNIHandles::make_local(str());
}
JVM_END


// Assertion support. //////////////////////////////////////////////////////////

JVM_ENTRY(jboolean, JVM_DesiredAssertionStatus, (JNIEnv *env, jclass unused, jclass cls))
  JVMWrapper("JVM_DesiredAssertionStatus");
  assert(cls != NULL, "bad class");

  oop r = JNIHandles::resolve(cls);
  assert(! java_lang_Class::is_primitive(r), "primitive classes not allowed");
  if (java_lang_Class::is_primitive(r)) return false;

  klassOop k = java_lang_Class::as_klassOop(r);
  if (! Klass::cast(k)->oop_is_instance()) return false;

  ResourceMark rm(THREAD);
  const char* name = Klass::cast(k)->name()->as_C_string();
  bool system_class = Klass::cast(k)->class_loader() == NULL;
  return JavaAssertions::enabled(name, system_class);

JVM_END


// Return a new AssertionStatusDirectives object with the fields filled in with
// command-line assertion arguments (i.e., -ea, -da).
JVM_ENTRY(jobject,JVM_AssertionStatusDirectives,(JNIEnv*env,jclass unused))
  JVMWrapper("JVM_AssertionStatusDirectives");
  JvmtiVMObjectAllocEventCollector oam;
  oop asd = JavaAssertions::createAssertionStatusDirectives(CHECK_NULL);
  return JNIHandles::make_local(env, asd);
JVM_END

// Verification ////////////////////////////////////////////////////////////////////////////////

// Reflection for the verifier /////////////////////////////////////////////////////////////////

// RedefineClasses support: bug 6214132 caused verification to fail.
// All functions from this section should call the jvmtiThreadSate function:
//   klassOop class_to_verify_considering_redefinition(klassOop klass).
// The function returns a klassOop of the _scratch_class if the verifier
// was invoked in the middle of the class redefinition.
// Otherwise it returns its argument value which is the _the_class klassOop.
// Please, refer to the description in the jvmtiThreadSate.hpp.

JVM_ENTRY(const char*, JVM_GetClassNameUTF, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  return Klass::cast(k)->name()->as_utf8();
JVM_END


JVM_QUICK_ENTRY(void, JVM_GetClassCPTypes, (JNIEnv *env, jclass cls, unsigned char *types))
  JVMWrapper("JVM_GetClassCPTypes");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  // types will have length zero if this is not an instanceKlass
  // (length is determined by call to JVM_GetClassCPEntriesCount)
  if (Klass::cast(k)->oop_is_instance()) {
    constantPoolOop cp = instanceKlass::cast(k)->constants();
    for (int index = cp->length() - 1; index >= 0; index--) {
      constantTag tag = cp->tag_at(index);
      types[index] = (tag.is_unresolved_klass()) ? JVM_CONSTANT_Class :
                     (tag.is_unresolved_string()) ? JVM_CONSTANT_String : tag.value();
  }
  }
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetClassCPEntriesCount, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassCPEntriesCount");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  if (!Klass::cast(k)->oop_is_instance())
    return 0;
  return instanceKlass::cast(k)->constants()->length();
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetClassFieldsCount, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassFieldsCount");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  if (!Klass::cast(k)->oop_is_instance())
    return 0;
  return instanceKlass::cast(k)->fields()->length() / instanceKlass::next_offset;
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetClassMethodsCount, (JNIEnv *env, jclass cls))
  JVMWrapper("JVM_GetClassMethodsCount");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  if (!Klass::cast(k)->oop_is_instance())
    return 0;
  return instanceKlass::cast(k)->methods()->length();
JVM_END


// The following methods, used for the verifier, are never called with
// array klasses, so a direct cast to instanceKlass is safe.
// Typically, these methods are called in a loop with bounds determined
// by the results of JVM_GetClass{Fields,Methods}Count, which return
// zero for arrays.
JVM_QUICK_ENTRY(void,JVM_GetMethodIxExceptionIndexes,(JNIEnv*env,jclass cls,jint method_index,unsigned short*exceptions))
  JVMWrapper("JVM_GetMethodIxExceptionIndexes");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  int length = methodOop(method)->checked_exceptions_length();
  if (length > 0) {
    CheckedExceptionElement* table= methodOop(method)->checked_exceptions_start();
    for (int i = 0; i < length; i++) {
      exceptions[i] = table[i].class_cp_index;
    }
  }
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxExceptionsCount, (JNIEnv *env, jclass cls, jint method_index))
  JVMWrapper("JVM_GetMethodIxExceptionsCount");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->checked_exceptions_length();
JVM_END


JVM_QUICK_ENTRY(void, JVM_GetMethodIxByteCode, (JNIEnv *env, jclass cls, jint method_index, unsigned char *code))
  JVMWrapper("JVM_GetMethodIxByteCode");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  memcpy(code, methodOop(method)->code_base(), methodOop(method)->code_size());
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxByteCodeLength, (JNIEnv *env, jclass cls, jint method_index))
  JVMWrapper("JVM_GetMethodIxByteCodeLength");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->code_size();
JVM_END


JVM_QUICK_ENTRY(void, JVM_GetMethodIxExceptionTableEntry, (JNIEnv *env, jclass cls, jint method_index, jint entry_index, JVM_ExceptionTableEntryType *entry))
  JVMWrapper("JVM_GetMethodIxExceptionTableEntry");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  typeArrayOop extable = methodOop(method)->exception_table();
  entry->start_pc   = extable->int_at(entry_index * 4);
  entry->end_pc     = extable->int_at(entry_index * 4 + 1);
  entry->handler_pc = extable->int_at(entry_index * 4 + 2);
  entry->catchType  = extable->int_at(entry_index * 4 + 3);
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxExceptionTableLength, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_GetMethodIxExceptionTableLength");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->exception_table()->length() / 4;
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxModifiers, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_GetMethodIxModifiers");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->access_flags().as_int() & JVM_RECOGNIZED_METHOD_MODIFIERS; 
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetFieldIxModifiers, (JNIEnv *env, jclass cls, int field_index))
  JVMWrapper("JVM_GetFieldIxModifiers");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  typeArrayOop fields = instanceKlass::cast(k)->fields();
  return fields->ushort_at(field_index * instanceKlass::next_offset + instanceKlass::access_flags_offset) & JVM_RECOGNIZED_FIELD_MODIFIERS;
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxLocalsCount, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_GetMethodIxLocalsCount");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->max_locals();
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxArgsSize, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_GetMethodIxArgsSize");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->size_of_parameters();
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetMethodIxMaxStack, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_GetMethodIxMaxStack");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->max_stack();
JVM_END


JVM_QUICK_ENTRY(jboolean, JVM_IsConstructorIx, (JNIEnv *env, jclass cls, int method_index))
  JVMWrapper("JVM_IsConstructorIx");
  ResourceMark rm(THREAD);
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->name() == vmSymbols::object_initializer_name();
JVM_END


JVM_ENTRY(const char*, JVM_GetMethodIxNameUTF, (JNIEnv *env, jclass cls, jint method_index))
  JVMWrapper("JVM_GetMethodIxIxUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->name()->as_utf8();
JVM_END


JVM_ENTRY(const char*, JVM_GetMethodIxSignatureUTF, (JNIEnv *env, jclass cls, jint method_index))
  JVMWrapper("JVM_GetMethodIxSignatureUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  oop method = instanceKlass::cast(k)->methods()->obj_at(method_index);
  return methodOop(method)->signature()->as_utf8();
JVM_END

/**
 * All of these JVM_GetCP-xxx methods are used by the old verifier to 
 * read entries in the constant pool.  Since the old verifier always 
 * works on a copy of the code, it will not see any rewriting that 
 * may possibly occur in the middle of verification.  So it is important
 * that nothing it calls tries to use the cpCache instead of the raw 
 * constant pool, so we must use cp->uncached_x methods when appropriate.
 */
JVM_ENTRY(const char*,JVM_GetCPFieldNameUTF,(JNIEnv*env,jclass cls,jint cp_index))
  JVMWrapper("JVM_GetCPFieldNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Fieldref:
      return cp->uncached_name_ref_at(cp_index)->as_utf8();
    default:
      fatal("JVM_GetCPFieldNameUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_ENTRY(const char*, JVM_GetCPMethodNameUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPMethodNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_InterfaceMethodref:
    case JVM_CONSTANT_Methodref:
      return cp->uncached_name_ref_at(cp_index)->as_utf8();
    default:
      fatal("JVM_GetCPMethodNameUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_ENTRY(const char*, JVM_GetCPMethodSignatureUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPMethodSignatureUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_InterfaceMethodref:
    case JVM_CONSTANT_Methodref:
      return cp->uncached_signature_ref_at(cp_index)->as_utf8();
    default:
      fatal("JVM_GetCPMethodSignatureUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_ENTRY(const char*, JVM_GetCPFieldSignatureUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPFieldSignatureUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Fieldref: 
      return cp->uncached_signature_ref_at(cp_index)->as_utf8();
    default:
      fatal("JVM_GetCPFieldSignatureUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_ENTRY(const char*, JVM_GetCPClassNameUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPClassNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  symbolOop classname = cp->klass_name_at(cp_index);
  return classname->as_utf8();
JVM_END


JVM_ENTRY(const char*, JVM_GetCPFieldClassNameUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPFieldClassNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Fieldref: {
      int class_index = cp->uncached_klass_ref_index_at(cp_index);
      symbolOop classname = cp->klass_name_at(class_index);
      return classname->as_utf8();
    }
    default:
      fatal("JVM_GetCPFieldClassNameUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_ENTRY(const char*, JVM_GetCPMethodClassNameUTF, (JNIEnv *env, jclass cls, jint cp_index))
  JVMWrapper("JVM_GetCPMethodClassNameUTF");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  k = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Methodref: 
    case JVM_CONSTANT_InterfaceMethodref: {
      int class_index = cp->uncached_klass_ref_index_at(cp_index);
      symbolOop classname = cp->klass_name_at(class_index);
      return classname->as_utf8();
    }
    default:
      fatal("JVM_GetCPMethodClassNameUTF: illegal constant");
  }
  ShouldNotReachHere();
  return NULL;
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetCPFieldModifiers, (JNIEnv *env, jclass cls, int cp_index, jclass called_cls))
  JVMWrapper("JVM_GetCPFieldModifiers");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  klassOop k_called = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(called_cls));
  k        = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  k_called = JvmtiThreadState::class_to_verify_considering_redefinition(k_called, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  constantPoolOop cp_called = instanceKlass::cast(k_called)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Fieldref: {
      symbolOop name      = cp->uncached_name_ref_at(cp_index);
      symbolOop signature = cp->uncached_signature_ref_at(cp_index);
      typeArrayOop fields = instanceKlass::cast(k_called)->fields();
      int fields_count = fields->length();
      for (int i = 0; i < fields_count; i += instanceKlass::next_offset) {
        if (cp_called->symbol_at(fields->ushort_at(i + instanceKlass::name_index_offset)) == name &&
            cp_called->symbol_at(fields->ushort_at(i + instanceKlass::signature_index_offset)) == signature) {
          return fields->ushort_at(i + instanceKlass::access_flags_offset) & JVM_RECOGNIZED_FIELD_MODIFIERS;
        }
      }
      return -1;
    }
    default:
      fatal("JVM_GetCPFieldModifiers: illegal constant");
  }
  ShouldNotReachHere();
  return 0;
JVM_END


JVM_QUICK_ENTRY(jint, JVM_GetCPMethodModifiers, (JNIEnv *env, jclass cls, int cp_index, jclass called_cls))
  JVMWrapper("JVM_GetCPMethodModifiers");
  klassOop k = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(cls));
  klassOop k_called = java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(called_cls));
  k        = JvmtiThreadState::class_to_verify_considering_redefinition(k, thread);
  k_called = JvmtiThreadState::class_to_verify_considering_redefinition(k_called, thread);
  constantPoolOop cp = instanceKlass::cast(k)->constants();
  switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_Methodref:
    case JVM_CONSTANT_InterfaceMethodref: {
      symbolOop name      = cp->uncached_name_ref_at(cp_index);
      symbolOop signature = cp->uncached_signature_ref_at(cp_index);
      objArrayOop methods = instanceKlass::cast(k_called)->methods();
      int methods_count = methods->length();
      for (int i = 0; i < methods_count; i++) {
        methodOop method = methodOop(methods->obj_at(i));
        if (method->name() == name && method->signature() == signature) {
            return method->access_flags().as_int() & JVM_RECOGNIZED_METHOD_MODIFIERS;
        }
      }
      return -1;
    }
    default:
      fatal("JVM_GetCPMethodModifiers: illegal constant");
  }
  ShouldNotReachHere();
  return 0;
JVM_END


// Misc //////////////////////////////////////////////////////////////////////////////////////////////

JVM_LEAF(void, JVM_ReleaseUTF, (const char *utf))
  // So long as UTF8::convert_to_utf8 returns resource strings, we don't have to do anything
JVM_END


JVM_ENTRY(jboolean, JVM_IsSameClassPackage, (JNIEnv *env, jclass class1, jclass class2))
  JVMWrapper("JVM_IsSameClassPackage");
  oop class1_mirror = JNIHandles::resolve_non_null(class1);
  oop class2_mirror = JNIHandles::resolve_non_null(class2);
  klassOop klass1 = java_lang_Class::as_klassOop(class1_mirror);
  klassOop klass2 = java_lang_Class::as_klassOop(class2_mirror);
  return (jboolean) Reflection::is_same_class_package(klass1, klass2);
JVM_END


// IO functions ////////////////////////////////////////////////////////////////////////////////////////

JVM_LEAF(jint, JVM_Open, (const char *fname, jint flags, jint mode))
  JVMWrapper2("JVM_Open (%s)", fname);  

  //%note jvm_r6
  int result = hpi::open(fname, flags, mode);    
  if (result >= 0) {
    return result;
  } else {
    switch(errno) {
      case EEXIST:
        return JVM_EEXIST;
      default:
        return -1;
    }
  }  
JVM_END


JVM_LEAF(jint, JVM_Close, (jint fd))
  JVMWrapper2("JVM_Close (0x%x)", fd);
  //%note jvm_r6
  return hpi::close(fd);
JVM_END


JVM_LEAF(jint, JVM_Read, (jint fd, char *buf, jint nbytes))
  JVMWrapper2("JVM_Read (0x%x)", fd);

  //%note jvm_r6
  return (jint)hpi::read(fd, buf, nbytes);
JVM_END


JVM_LEAF(jint, JVM_Write, (jint fd, char *buf, jint nbytes))
  JVMWrapper2("JVM_Write (0x%x)", fd);

  //%note jvm_r6
  return (jint)hpi::write(fd, buf, nbytes);
JVM_END


JVM_LEAF(jint, JVM_Available, (jint fd, jlong *pbytes))
  JVMWrapper2("JVM_Available (0x%x)", fd);
  //%note jvm_r6
  return hpi::available(fd, pbytes);
JVM_END


JVM_LEAF(jlong, JVM_Lseek, (jint fd, jlong offset, jint whence))
  JVMWrapper4("JVM_Lseek (0x%x, %Ld, %d)", fd, offset, whence);
  //%note jvm_r6
  return hpi::lseek(fd, offset, whence);  
JVM_END


JVM_LEAF(jint, JVM_SetLength, (jint fd, jlong length))
  JVMWrapper3("JVM_SetLength (0x%x, %Ld)", fd, length);
  return hpi::ftruncate(fd, length);  
JVM_END


JVM_LEAF(jint, JVM_Sync, (jint fd))
  JVMWrapper2("JVM_Sync (0x%x)", fd);
  //%note jvm_r6
  return hpi::fsync(fd);
JVM_END


// Printing support //////////////////////////////////////////////////
extern "C" {

int jio_vsnprintf(char *str, size_t count, const char *fmt, va_list args) {
  // see bug 4399518, 4417214
  if ((intptr_t)count <= 0) return -1;
  return vsnprintf(str, count, fmt, args);
}


int jio_snprintf(char *str, size_t count, const char *fmt, ...) {
  va_list args;
  int len;
  va_start(args, fmt);
  len = jio_vsnprintf(str, count, fmt, args);
  va_end(args);
  return len;
}


int jio_fprintf(FILE* f, const char *fmt, ...) {
  int len;
  va_list args;
  va_start(args, fmt);
  len = jio_vfprintf(f, fmt, args);
  va_end(args);
  return len;
}


int jio_vfprintf(FILE* f, const char *fmt, va_list args) {  
  if (Arguments::vfprintf_hook() != NULL) {
     return Arguments::vfprintf_hook()(f, fmt, args);
  } else {	
    return vfprintf(f, fmt, args);
  }
}


int jio_printf(const char *fmt, ...) {
  int len;
  va_list args;
  va_start(args, fmt);
  len = jio_vfprintf(defaultStream::output_stream(), fmt, args);
  va_end(args);
  return len;
}


// HotSpot specific jio method
void jio_print(const char* s) {
  // Try to make this function as atomic as possible.
  if (Arguments::vfprintf_hook() != NULL) {
    jio_fprintf(defaultStream::output_stream(), "%s", s);    
  } else {
    ::write(defaultStream::output_fd(), s, (int)strlen(s));
  }  
}

} // Extern C

// java.lang.Thread //////////////////////////////////////////////////////////////////////////////

// In most of the JVM Thread support functions we need to be sure to lock the Threads_lock
// to prevent the target thread from exiting after we have a pointer to the C++ Thread or
// OSThread objects.  The exception to this rule is when the target object is the thread
// doing the operation, in which case we know that the thread won't exit until the
// operation is done (all exits being voluntary).  There are a few cases where it is
// rather silly to do operations on yourself, like resuming yourself or asking whether
// you are alive.  While these can still happen, they are not subject to deadlocks if
// the lock is held while the operation occurs (this is not the case for suspend, for
// instance), and are very unlikely.  Because IsAlive needs to be fast and its
// implementation is local to this file, we always lock Threads_lock for that one.

static void thread_entry(JavaThread* thread, TRAPS) {
  HandleMark hm(THREAD);
  Handle obj(THREAD, thread->threadObj());    
  JavaValue result(T_VOID);
  JavaCalls::call_virtual(&result, 
                          obj, 
                          KlassHandle(THREAD, SystemDictionary::thread_klass()),
                          vmSymbolHandles::run_method_name(), 
                          vmSymbolHandles::void_method_signature(), 
                          THREAD);
}


JVM_ENTRY(void, JVM_StartThread, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_StartThread");
  JavaThread *native_thread = NULL;

  // We cannot hold the Threads_lock when we throw an exception,
  // due to rank ordering issues. Example:  we might need to grab the
  // Heap_lock while we construct the exception.
  bool throw_illegal_thread_state = false;

  Handle thread_handle(JNIHandles::resolve_as_non_null_ref(jthread));
  jlong size = java_lang_Thread::stackSize(thread_handle());
  // Allocate the C++ Thread structure and create the native thread.  The
  // stack size retrieved from java is signed, but the constructor takes
  // size_t (an unsigned type), so avoid passing negative values which would
  // result in really large stacks.
  size_t sz = size > 0 ? (size_t) size : 0;

  // We must release the Threads_lock before we can post a jvmti event
  // in Thread::start.
  { 
    // Ensure that the C++ Thread and OSThread structures aren't freed before 
    // we operate.
MutexLockerAllowGC mu(Threads_lock,thread);

    // Check to see if we're running a thread that's already exited or was
    // stopped (is_stillborn) or is still active (thread is not NULL).
if(java_lang_Thread::is_stillborn(thread_handle())||
java_lang_Thread::thread(thread_handle())!=NULL){
      throw_illegal_thread_state = true;
    } else {
      // The OS call to thread_stack_allocate can get really slow, so I tried it
      // outside the Threads_lock.  This failed for an unknown datarace in
      // the thread constructor that is guarded by the Threads_lock.  If we find
      // the race it should be labeled with a assert_lock_strong(Threads_lock).
      native_thread = new (ttype::java_thread) JavaThread(&thread_entry, sz);

      // At this point it may be possible that no osthread was created for the
      // JavaThread due to lack of memory. Check for this situation and throw
      // an exception if necessary. Eventually we may want to change this so
      // that we only grab the lock if the thread was created successfully -
      // then we can also do this check and throw the exception in the 
      // JavaThread constructor.
if(native_thread!=NULL){
        if (native_thread->osthread() != NULL) {
          // Note: the current thread is not being used within "prepare".
native_thread->prepare(thread_handle);
        }
      }
    }
  }

  if (throw_illegal_thread_state) {
    THROW(vmSymbols::java_lang_IllegalThreadStateException());
  }

if(native_thread!=NULL){
    if (native_thread->osthread() != NULL) {
      Thread::start(native_thread);
    } else {
      // No one should hold a reference to the 'native_thread'.
      delete native_thread;
      
      if (JvmtiExport::should_post_resource_exhausted()) {
        JvmtiExport::post_resource_exhausted(
          JVMTI_RESOURCE_EXHAUSTED_OOM_ERROR | JVMTI_RESOURCE_EXHAUSTED_THREADS, 
          "unable to create new native thread");
      }
      char buf[128];
      jio_snprintf(buf, sizeof buf, "Threads limit (current=%d, max=%d) for the process reached. (jvm.cpp)", os::_os_thread_count, os::_os_thread_limit);
THROW_MSG(vmSymbols::java_lang_OutOfMemoryError(),buf);
::abort();//Temporary for catching the state of the universe
    }
  } else {
THROW_MSG(vmSymbols::java_lang_OutOfMemoryError(),"unable to create new java thread");
  }

JVM_END

JVM_ENTRY(void, JVM_StopThread, (JNIEnv* env, jobject jthread, jobject throwable))
  JVMWrapper("JVM_StopThread");

  // async exception cannot be stack allocated
  StackBasedAllocation::ensure_in_heap(thread, throwable, FCN_NAME);
  
  objectRef java_throwable = JNIHandles::resolve(throwable);
if(java_throwable.is_null()){
    // Must throw potential NPE before taking the Threads_lock, since
    // construction creates an oop, which needs initialization which calls
    // Java and it Would Be Bad to hold the Threads_lock while calling Java.
    THROW(vmSymbols::java_lang_NullPointerException());
  }

  // Since the jthread pointer is likely not the current thread, it might be
  // in the process of exiting or other such gruesome acts.  Prevent it from
  // going away by taking the threads-lock.  (Before taking the threads lock
  // it is in the jthread handle; a GC might happen before we can take the
  // threads lock).
MutexLockerAllowGC mu(Threads_lock,thread);
  
  oop java_thread = JNIHandles::resolve_non_null(jthread);
  JavaThread* receiver = java_lang_Thread::thread(java_thread);
Log::log4(NOTAG,0,"JVM_StopThread thread JavaThread "INTPTR_FORMAT" as oop "INTPTR_FORMAT" [exception "INTPTR_FORMAT"] ",receiver,(address)java_thread,throwable);
  // First check if thread already exited  
  if (receiver != NULL) {
java_throwable=JNIHandles::resolve(throwable);//re-resolve post GC
//Post the async exception to the JavaThread.  It will be polled for
    // and inspected at the receiver's leisure.  No Safepoint.
    receiver->install_async_exception(java_throwable,true/*overwrite*/, true/*remote thread*/);
  }

JVM_END


JVM_ENTRY(jboolean, JVM_IsThreadAlive, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_IsThreadAlive");

  oop thread_oop = JNIHandles::resolve_non_null(jthread);
  return java_lang_Thread::is_alive(thread_oop);
JVM_END


JVM_ENTRY(void, JVM_SuspendThread, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_SuspendThread");  
  oop java_thread = JNIHandles::resolve_non_null(jthread);
  JavaThread* receiver = java_lang_Thread::thread(java_thread); 

  if (receiver != NULL) {
    // Post a java.lang.Thread.suspend suspend request.
    // Thread will self-suspend at it's earliest convenience.
    receiver->set_suspend_request(JavaThread::jlang_suspend);
    // Wait a little bit for the other thread to settle out
    if( receiver != JavaThread::current() ) 
receiver->wait_for_suspend_completion();
  }
JVM_END


JVM_ENTRY(void, JVM_ResumeThread, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_ResumeThread");  
  JavaThread* thr = java_lang_Thread::thread(JNIHandles::resolve_non_null(jthread));
if(thr!=NULL)
thr->java_resume(JavaThread::jlang_suspend);

JVM_END


JVM_ENTRY(void, JVM_SetThreadPriority, (JNIEnv* env, jobject jthread, jint prio))
  JVMWrapper("JVM_SetThreadPriority");
  // Ensure that the C++ Thread and OSThread structures aren't freed before we operate
MutexLockerAllowGC ml(Threads_lock,thread);
  oop java_thread = JNIHandles::resolve_non_null(jthread);  
  JavaThread* thr = java_lang_Thread::thread(java_thread);
  if (thr != NULL) {                  // Thread not yet started; priority pushed down when it is
    thr->set_priority((JavaThreadPriority)prio);
  } else {
java_lang_Thread::set_priority(java_thread,(JavaThreadPriority)prio);
  }
JVM_END


JVM_ENTRY(void, JVM_Yield, (JNIEnv *env, jclass threadClass))
  JVMWrapper("JVM_Yield");
  if (os::dont_yield()) return;
  os::yield();
JVM_END


JVM_ENTRY(void, JVM_Sleep, (JNIEnv* env, jclass threadClass, jlong millis))
  JVMWrapper("JVM_Sleep");  
  // Save current thread state and restore it at the end of this block.
  // And set new thread state to SLEEPING.
  JavaThreadSleepState jtss(thread);

  if (millis < 0) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "timeout value is negative");    
  } 
  if (millis == 0) millis = 1;
  if (thread->sleep(millis) == OS_INTRPT) {
    // An asynchronous exception (e.g., ThreadDeathException) could have been
    // thrown on us while we were sleeping.  We do not overwrite those.
    if (!HAS_PENDING_EXCEPTION) {
      THROW_MSG(vmSymbols::java_lang_InterruptedException(), "sleep interrupted");
    }  
  }  
JVM_END

JVM_ENTRY(jobject, JVM_CurrentThread, (JNIEnv* env, jclass threadClass))
  JVMWrapper("JVM_CurrentThread");
  return JNIHandles::make_local(env, thread->threadRef());
JVM_END


JVM_ENTRY(jint, JVM_CountStackFrames, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_CountStackFrames");
  ResourceMark rm(THREAD);
  
  // Ensure that the C++ Thread and OSThread structures aren't freed before we operate
  oop java_thread = JNIHandles::resolve_non_null(jthread); 
  bool throw_illegal_thread_state = false;
  int count = 0;
 
  {
MutexLockerAllowGC ml(thread->threadObj()==java_thread?NULL:&Threads_lock,1);
    // We need to re-resolve the java_thread, since a GC might have happened during the
    // acquire of the lock
    JavaThread* thr = java_lang_Thread::thread(JNIHandles::resolve_non_null(jthread));
   
    if (thr == NULL) { 
      // do nothing 
    } else if (!thr->is_being_ext_suspended()) {
      // Check whether this java thread has been suspended already. If not, throws  
      // IllegalThreadStateException. We defer to throw that exception until 
      // Threads_lock is released since loading exception class has to leave VM.
      // The correct way to test a thread is actually suspended is 
      // wait_for_ext_suspend_completion(), but we can't call that while holding 
      // the Threads_lock. The above tests are sufficient for our purposes 
      // provided the walkability of the stack is stable - which it isn't 
      // 100% but close enough for most practical purposes.
      throw_illegal_thread_state = true; 
    } else { 
      // Count all java activation, i.e., number of vframes
      for(vframe vf(thr); !vf.done(); vf.next()) {    
        // Native frames are not counted
if(!vf.method()->is_native())count++;
       }
    }
  }

  // Outside of Thread_lock (so we can GC) make and throw an exception object 
  if (throw_illegal_thread_state) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalThreadStateException(),
                "this thread is not suspended");
  }
  return count; 
JVM_END

// Consider: A better way to implement JVM_Interrupt() is to acquire
// Threads_lock to resolve the jthread into a Thread pointer, fetch
// Thread->platformevent, Thread->native_thr, Thread->parker, etc.,
// drop Threads_lock, and the perform the unpark() and thr_kill() operations
// outside the critical section.  Threads_lock is hot so we want to minimize
// the hold-time.  A cleaner interface would be to decompose interrupt into
// two steps.  The 1st phase, performed under Threads_lock, would return
// a closure that'd be invoked after Threads_lock was dropped.   
// This tactic is safe as PlatformEvent and Parkers are type-stable (TSM) and
// admit spurious wakeups.  

JVM_ENTRY(void, JVM_Interrupt, (JNIEnv* env, jobject jthread))
  JVMWrapper("JVM_Interrupt");

  // Ensure that the C++ Thread and OSThread structures aren't freed before we operate
  oop java_thread = JNIHandles::resolve_non_null(jthread);  
MutexLockerAllowGC ml(thread->threadObj()==java_thread?NULL:&Threads_lock,1);
  // We need to re-resolve the java_thread, since a GC might have happened during the
  // acquire of the lock
  JavaThread* thr = java_lang_Thread::thread(JNIHandles::resolve_non_null(jthread));
  if (thr != NULL) {    
    Thread::interrupt(thr);
  }
JVM_END


JVM_QUICK_ENTRY(jboolean, JVM_IsInterrupted, (JNIEnv* env, jobject jthread, jboolean clear_interrupted))
  JVMWrapper("JVM_IsInterrupted");

  // Ensure that the C++ Thread and OSThread structures aren't freed before we operate
  oop java_thread = JNIHandles::resolve_non_null(jthread);  
MutexLockerAllowGC ml(thread->threadObj()==java_thread?NULL:&Threads_lock,1);
  // We need to re-resolve the java_thread, since a GC might have happened during the
  // acquire of the lock
  JavaThread* thr = java_lang_Thread::thread(JNIHandles::resolve_non_null(jthread));
  if (thr == NULL) {
    return JNI_FALSE;
  } else {
return(jboolean)(clear_interrupted
                       ? os::is_interrupted_and_clear(thr)
                       : os::is_interrupted          (thr));
  }
JVM_END


// Return true iff the current thread has locked the object passed in

JVM_ENTRY(jboolean, JVM_HoldsLock, (JNIEnv* env, jclass threadClass, jobject obj))
  JVMWrapper("JVM_HoldsLock");
  assert(THREAD->is_Java_thread(), "sanity check");
  if (obj == NULL) {
    THROW_(vmSymbols::java_lang_NullPointerException(), JNI_FALSE);
  }
return ObjectSynchronizer::current_thread_holds_lock((JavaThread*)THREAD,JNIHandles::resolve(obj));
JVM_END


JVM_ENTRY(void, JVM_DumpAllStacks, (JNIEnv* env, jclass))
  JVMWrapper("JVM_DumpAllStacks");
  VM_PrintThreads op;
  VMThread::execute(&op);
  if (JvmtiExport::should_post_data_dump()) {
    JvmtiExport::post_data_dump();
  }
JVM_END


// java.lang.SecurityManager ///////////////////////////////////////////////////////////////////////

static bool is_trusted_frame(JavaThread*jthread,vframe*vf){
  assert(jthread->is_Java_thread(), "must be a Java thread");
  if (jthread->privileged_stack_top() == NULL) return false;   
if(jthread->privileged_stack_top()->frame_id()==vf->get_frame().id()){
    oop loader = jthread->privileged_stack_top()->class_loader();
    if (loader == NULL) return true;
    bool trusted = java_lang_ClassLoader::is_trusted_loader(loader);
    if (trusted) return true;      
  }  
  return false;
}

JVM_ENTRY(jclass, JVM_CurrentLoadedClass, (JNIEnv *env))
  JVMWrapper("JVM_CurrentLoadedClass");
  ResourceMark rm(THREAD);
  
  for (vframe vf(thread); !vf.done(); vf.next()) {
    // if a method in a class in a trusted loader is in a doPrivileged, return NULL    
bool trusted=is_trusted_frame(thread,&vf);
    if (trusted) return NULL;    
    
methodOop m=vf.method();
    if (!m->is_native()) {      
      klassOop holder = m->method_holder();    
      oop      loader = instanceKlass::cast(holder)->class_loader();
      if (loader != NULL && !java_lang_ClassLoader::is_trusted_loader(loader)) { 
return(jclass)JNIHandles::make_local(env,Klass::cast(holder)->java_mirror_ref());
      }
    }
  }
  return NULL;
JVM_END


JVM_ENTRY(jobject, JVM_CurrentClassLoader, (JNIEnv *env))
  JVMWrapper("JVM_CurrentClassLoader");
  ResourceMark rm(THREAD);
  
  for (vframe vf(thread); !vf.done(); vf.next()) {
  
    // if a method in a class in a trusted loader is in a doPrivileged, return NULL    
bool trusted=is_trusted_frame(thread,&vf);
    if (trusted) return NULL;    

methodOop m=vf.method();
    if (!m->is_native()) {
      klassOop holder = m->method_holder();
      assert(holder->is_klass(), "just checking");
      oop loader = instanceKlass::cast(holder)->class_loader();
      if (loader != NULL && !java_lang_ClassLoader::is_trusted_loader(loader)) {
        return JNIHandles::make_local(env, loader);
      }
    }
  }
  return NULL;
JVM_END


// Utility object for collecting method holders walking down the stack
class KlassLink: public ResourceObj {
 public:
  KlassHandle klass;
  KlassLink*  next;

  KlassLink(KlassHandle k) { klass = k; next = NULL; }
};


JVM_ENTRY(jobjectArray, JVM_GetClassContext, (JNIEnv *env))
  JVMWrapper("JVM_GetClassContext");
  ResourceMark rm(THREAD);
  JvmtiVMObjectAllocEventCollector oam;
  // Collect linked list of (handles to) method holders
  KlassLink* first = NULL;
  KlassLink* last  = NULL;
  int depth = 0;
    
  for(vframe vf(thread); !vf.done(); vf.security_get_caller_frame(1)) {  
    // Native frames are not returned
if(!vf.method()->is_native()){
klassOop holder=vf.method()->method_holder();
      assert(holder->is_klass(), "just checking");        
      depth++;
      KlassLink* l = new KlassLink(KlassHandle(thread, holder));    
      if (first == NULL) {
        first = last = l;
      } else {
        last->next = l;
        last = l;
      }
    }
  }

  // Create result array of type [Ljava/lang/Class;
objArrayOop result=oopFactory::new_objArray(SystemDictionary::class_klass(),depth,true/*SBA*/,CHECK_NULL);
  // Fill in mirrors corresponding to method holders
  int index = 0;
  while (first != NULL) {
result->ref_at_put(index++,Klass::cast(first->klass())->java_mirror_ref());
    first = first->next;
  }
  assert(index == depth, "just checking");

  return (jobjectArray) JNIHandles::make_local(env, result);
JVM_END


JVM_ENTRY(jint, JVM_ClassDepth, (JNIEnv *env, jstring name))
  JVMWrapper("JVM_ClassDepth");
  ResourceMark rm(THREAD);
Handle h_name(THREAD,JNIHandles::resolve_as_non_null_ref(name));
  Handle class_name_str = java_lang_String::internalize_classname(h_name, CHECK_0);

  const char* str = java_lang_String::as_utf8_string(class_name_str());
  symbolHandle class_name_sym = 
                symbolHandle(THREAD, SymbolTable::probe(str, (int)strlen(str)));
  if (class_name_sym.is_null()) {
    return -1;
  }

  int depth = 0;
  
  for(vframe vf(thread); !vf.done(); vf.next()) {    
if(!vf.method()->is_native()){
klassOop holder=vf.method()->method_holder();
      assert(holder->is_klass(), "just checking");
      if (instanceKlass::cast(holder)->name() == class_name_sym()) {
        return depth;
      }
      depth++;
    }
  }
  return -1;
JVM_END


JVM_ENTRY(jint, JVM_ClassLoaderDepth, (JNIEnv *env))
  JVMWrapper("JVM_ClassLoaderDepth");
  ResourceMark rm(THREAD);
  int depth = 0;  
  for (vframe vf(thread); !vf.done(); vf.next()) {
    // if a method in a class in a trusted loader is in a doPrivileged, return -1    
bool trusted=is_trusted_frame(thread,&vf);
    if (trusted) return -1;    
    
methodOop m=vf.method();
    if (!m->is_native()) {
      klassOop holder = m->method_holder();
      assert(holder->is_klass(), "just checking");
      oop loader = instanceKlass::cast(holder)->class_loader();
      if (loader != NULL && !java_lang_ClassLoader::is_trusted_loader(loader)) {
        return depth;
      }
      depth++;
    }
  }
  return -1;
JVM_END


// java.lang.Package ////////////////////////////////////////////////////////////////


JVM_ENTRY(jstring, JVM_GetSystemPackage, (JNIEnv *env, jstring name))
  JVMWrapper("JVM_GetSystemPackage");
  ResourceMark rm(THREAD);
  JvmtiVMObjectAllocEventCollector oam;
  char* str = java_lang_String::as_utf8_string(JNIHandles::resolve_non_null(name));
  oop result = ClassLoader::get_system_package(str, CHECK_NULL);
  return (jstring) JNIHandles::make_local(result);
JVM_END


JVM_ENTRY(jobjectArray, JVM_GetSystemPackages, (JNIEnv *env))
  JVMWrapper("JVM_GetSystemPackages");
  JvmtiVMObjectAllocEventCollector oam;
  objArrayOop result = ClassLoader::get_system_packages(CHECK_NULL);
  return (jobjectArray) JNIHandles::make_local(result);
JVM_END


// ObjectInputStream ///////////////////////////////////////////////////////////////

bool force_verify_field_access(klassOop current_class, klassOop field_class, AccessFlags access, bool classloader_only) {  
  if (current_class == NULL) {
    return true;
  }
  if ((current_class == field_class) || access.is_public()) {
    return true;
  }

  if (access.is_protected()) {
    // See if current_class is a subclass of field_class 
    if (Klass::cast(current_class)->is_subclass_of(field_class)) {
      return true;
    }
  }

  return (!access.is_private() && instanceKlass::cast(current_class)->is_same_class_package(field_class));
}


// JVM_AllocateNewObject and JVM_AllocateNewArray are unused as of 1.4
JVM_ENTRY(jobject,JVM_AllocateNewObject,(JNIEnv*env,jobject receiver,jclass currClass,jclass initClass))
  JVMWrapper("JVM_AllocateNewObject");
  JvmtiVMObjectAllocEventCollector oam;
  // Receiver is not used
  oop curr_mirror = JNIHandles::resolve_non_null(currClass);
  oop init_mirror = JNIHandles::resolve_non_null(initClass);

  // Cannot instantiate primitive types
  if (java_lang_Class::is_primitive(curr_mirror) || java_lang_Class::is_primitive(init_mirror)) {
    ResourceMark rm(THREAD);    
    THROW_0(vmSymbols::java_lang_InvalidClassException());
  }
     
  // Arrays not allowed here, must use JVM_AllocateNewArray 
  if (Klass::cast(java_lang_Class::as_klassOop(curr_mirror))->oop_is_javaArray() ||
      Klass::cast(java_lang_Class::as_klassOop(init_mirror))->oop_is_javaArray()) {
    ResourceMark rm(THREAD);    
    THROW_0(vmSymbols::java_lang_InvalidClassException());
  }

  instanceKlassHandle curr_klass (java_lang_Class::as_klassRef(curr_mirror));
  instanceKlassHandle init_klass (java_lang_Class::as_klassRef(init_mirror));

  assert(curr_klass->is_subclass_of(init_klass()), "just checking");

  // Interfaces, abstract classes, and java.lang.Class classes cannot be instantiated directly.
  curr_klass->check_valid_for_instantiation(false, CHECK_NULL);

  // Make sure klass is initialized, since we are about to instantiate one of them.
  curr_klass->initialize(CHECK_NULL);

 methodHandle m (THREAD, 
                 init_klass->find_method(vmSymbols::object_initializer_name(), 
                                         vmSymbols::void_method_signature()));
  if (m.is_null()) {
    ResourceMark rm(THREAD);    
    THROW_MSG_0(vmSymbols::java_lang_NoSuchMethodError(),
                methodOopDesc::name_and_sig_as_C_string(Klass::cast(init_klass()),
                                          vmSymbols::object_initializer_name(),
                                          vmSymbols::void_method_signature()));
  }
  
  if (curr_klass ==  init_klass && !m->is_public()) {
    // Calling the constructor for class 'curr_klass'. 
    // Only allow calls to a public no-arg constructor.
    // This path corresponds to creating an Externalizable object.    
    THROW_0(vmSymbols::java_lang_IllegalAccessException());  
  } 
  
  if (!force_verify_field_access(curr_klass(), init_klass(), m->access_flags(), false)) {
    // subclass 'curr_klass' does not have access to no-arg constructor of 'initcb'
    THROW_0(vmSymbols::java_lang_IllegalAccessException());      
  }

  Handle obj = curr_klass->allocate_instance_handle(true/*in stack*/, CHECK_NULL);
  // Call constructor m. This might call a constructor higher up in the hierachy
  JavaCalls::call_default_constructor(thread, m, obj, CHECK_NULL);
  
  return JNIHandles::make_local(obj());
JVM_END


JVM_ENTRY(jobject, JVM_AllocateNewArray, (JNIEnv *env, jobject obj, jclass currClass, jint length))
  JVMWrapper("JVM_AllocateNewArray");  
  JvmtiVMObjectAllocEventCollector oam;
  oop mirror = JNIHandles::resolve_non_null(currClass);

  if (java_lang_Class::is_primitive(mirror)) {    
    THROW_0(vmSymbols::java_lang_InvalidClassException());
  }
  klassOop k = java_lang_Class::as_klassOop(mirror);
  oop result;

  if (k->klass_part()->oop_is_typeArray()) {
    // typeArray
result=typeArrayKlass::cast(k)->allocate(length,true/*in stack*/,CHECK_NULL);
  } else if (k->klass_part()->oop_is_objArray()) {
    // objArray
    objArrayKlassHandle oak(THREAD, k);
    oak->initialize(CHECK_NULL); // make sure class is initialized (matches Classic VM behavior)
result=oak->allocate(length,true/*in stack*/,CHECK_NULL);
  } else {
    THROW_0(vmSymbols::java_lang_InvalidClassException());
  }
  return JNIHandles::make_local(env, result);
JVM_END


// Return the first non-null class loader up the execution stack, or null 
// if only code from the null class loader is on the stack.

JVM_ENTRY(jobject, JVM_LatestUserDefinedLoader, (JNIEnv *env))
  ResourceMark rm(THREAD);
  for (vframe vf(thread); !vf.done(); vf.next()) {
    // UseNewReflection
vf.skip_reflection_related_frames();//Only needed for 1.4 reflection
klassOop holder=vf.method()->method_holder();
    oop loader = instanceKlass::cast(holder)->class_loader();
    if (loader != NULL) {
      return JNIHandles::make_local(env, loader);
    }
  }
  return NULL;
JVM_END


// Load a class relative to the most recent class on the stack  with a non-null 
// classloader.
// This function has been deprecated and should not be considered part of the 
// specified JVM interface.

JVM_ENTRY(jclass, JVM_LoadClass0, (JNIEnv *env, jobject receiver, 
                                 jclass currClass, jstring currClassName))
  JVMWrapper("JVM_LoadClass0");  
  // Receiver is not used
  ResourceMark rm(THREAD);

  // Class name argument is not guaranteed to be in internal format
Handle classname(THREAD,JNIHandles::resolve_as_non_null_ref(currClassName));
  Handle string = java_lang_String::internalize_classname(classname, CHECK_NULL);
  
  const char* str = java_lang_String::as_utf8_string(string());

  if (str == NULL || (int)strlen(str) > symbolOopDesc::max_length()) {
    // It's impossible to create this class;  the name cannot fit
    // into the constant pool.
    THROW_MSG_0(vmSymbols::java_lang_NoClassDefFoundError(), str);
  }

  symbolHandle name = oopFactory::new_symbol_handle(str, CHECK_NULL);
Handle curr_klass(THREAD,JNIHandles::resolve_as_ref(currClass));
  // Find the most recent class on the stack with a non-null classloader  
  oop loader = NULL;
  oop protection_domain = NULL;
  if (curr_klass.is_null()) {
for(vframe vf(thread);
!vf.done()&&loader==NULL;
vf.next()){
if(!vf.method()->is_native()){
klassOop holder=vf.method()->method_holder();
        loader             = instanceKlass::cast(holder)->class_loader();
        protection_domain  = instanceKlass::cast(holder)->protection_domain();
      }
    }
  } else {
    klassOop curr_klass_oop = java_lang_Class::as_klassOop(curr_klass());
    loader            = instanceKlass::cast(curr_klass_oop)->class_loader();
    protection_domain = instanceKlass::cast(curr_klass_oop)->protection_domain();
  }
  Handle h_loader(THREAD, loader);
  Handle h_prot  (THREAD, protection_domain);
  return find_class_from_class_loader(env, name, true, h_loader, h_prot, 
                                      false, thread);
JVM_END


// Array ///////////////////////////////////////////////////////////////////////////////////////////
 

// resolve array handle and check arguments
static inline arrayOop check_array(JNIEnv *env, jobject arr, bool type_array_only, TRAPS) {
  if (arr == NULL) {
    THROW_0(vmSymbols::java_lang_NullPointerException());
  }
  oop a = JNIHandles::resolve_non_null(arr);
  if (!a->is_javaArray() || (type_array_only && !a->is_typeArray())) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "Argument is not an array");
  }
  return arrayOop(a); 
}


JVM_ENTRY(jint, JVM_GetArrayLength, (JNIEnv *env, jobject arr))
  JVMWrapper("JVM_GetArrayLength");
  arrayOop a = check_array(env, arr, false, CHECK_0);
  return a->length();
JVM_END


JVM_ENTRY(jobject, JVM_GetArrayElement, (JNIEnv *env, jobject arr, jint index))
  JVMWrapper("JVM_Array_Get");
  JvmtiVMObjectAllocEventCollector oam;
  arrayOop a = check_array(env, arr, false, CHECK_NULL);
  jvalue value;
  BasicType type = Reflection::array_get(&value, a, index, CHECK_NULL);
  oop box = Reflection::box(&value, type, CHECK_NULL);
  return JNIHandles::make_local(env, box);
JVM_END


JVM_ENTRY(jvalue, JVM_GetPrimitiveArrayElement, (JNIEnv *env, jobject arr, jint index, jint wCode))
  JVMWrapper("JVM_GetPrimitiveArrayElement");
  jvalue value;
  value.i = 0; // to initialize value before getting used in CHECK
  arrayOop a = check_array(env, arr, true, CHECK_(value));
  assert(a->is_typeArray(), "just checking");
  BasicType type = Reflection::array_get(&value, a, index, CHECK_(value));
  BasicType wide_type = (BasicType) wCode;
  if (type != wide_type) {
    Reflection::widen(&value, type, wide_type, CHECK_(value));
  }
  return value;
JVM_END 


JVM_ENTRY(void, JVM_SetArrayElement, (JNIEnv *env, jobject arr, jint index, jobject val))
  JVMWrapper("JVM_SetArrayElement");
  arrayOop a = check_array(env, arr, false, CHECK);
  objectRef box = JNIHandles::resolve(val);
  jvalue value;
  value.i = 0; // to initialize value before getting used in CHECK
  BasicType value_type;
  if (a->is_objArray()) {
    // Make sure we do no unbox e.g. java/lang/Integer instances when storing into an object array
    value_type = Reflection::unbox_for_regular_object(box, &value);
  } else {
    value_type = Reflection::unbox_for_primitive(box, &value, CHECK);
  }
  Reflection::array_set(&value, a, index, value_type, CHECK);
JVM_END


JVM_ENTRY(void, JVM_SetPrimitiveArrayElement, (JNIEnv *env, jobject arr, jint index, jvalue v, unsigned char vCode))
  JVMWrapper("JVM_SetPrimitiveArrayElement");
  arrayOop a = check_array(env, arr, true, CHECK);
  assert(a->is_typeArray(), "just checking");
  BasicType value_type = (BasicType) vCode;
  Reflection::array_set(&v, a, index, value_type, CHECK);
JVM_END


JVM_ENTRY(jobject, JVM_NewArray, (JNIEnv *env, jclass eltClass, jint length))
  JVMWrapper("JVM_NewArray");
  JvmtiVMObjectAllocEventCollector oam;
  oop element_mirror = JNIHandles::resolve(eltClass);
  oop result = Reflection::reflect_new_array(element_mirror, length, CHECK_NULL);
  return JNIHandles::make_local(env, result);
JVM_END


JVM_ENTRY(jobject, JVM_NewMultiArray, (JNIEnv *env, jclass eltClass, jintArray dim))
  JVMWrapper("JVM_NewMultiArray");  
  JvmtiVMObjectAllocEventCollector oam;
  arrayOop dim_array = check_array(env, dim, true, CHECK_NULL);
  oop element_mirror = JNIHandles::resolve(eltClass);
  assert(dim_array->is_typeArray(), "just checking");
  oop result = Reflection::reflect_new_multi_array(element_mirror, typeArrayOop(dim_array), CHECK_NULL);
  return JNIHandles::make_local(env, result);
JVM_END


// Networking library support ////////////////////////////////////////////////////////////////////

JVM_LEAF(jint, JVM_InitializeSocketLibrary, ())
  JVMWrapper("JVM_InitializeSocketLibrary");
  return 0;
JVM_END


JVM_LEAF(jint, JVM_Socket, (jint domain, jint type, jint protocol))
  JVMWrapper("JVM_Socket");
  return hpi::socket(domain, type, protocol);
JVM_END


JVM_LEAF(jint, JVM_SocketClose, (jint fd))
  JVMWrapper2("JVM_SocketClose (0x%x)", fd);
  //%note jvm_r6
  return hpi::socket_close(fd);
JVM_END


JVM_LEAF(jint, JVM_SocketShutdown, (jint fd, jint howto))
  JVMWrapper2("JVM_SocketShutdown (0x%x)", fd);
  //%note jvm_r6
  return hpi::socket_shutdown(fd, howto);
JVM_END


JVM_LEAF(jint, JVM_Recv, (jint fd, char *buf, jint nBytes, jint flags))
  JVMWrapper2("JVM_Recv (0x%x)", fd);
  //%note jvm_r6
  return hpi::recv(fd, buf, nBytes, flags);
JVM_END


JVM_LEAF(jint, JVM_Send, (jint fd, char *buf, jint nBytes, jint flags))
  JVMWrapper2("JVM_Send (0x%x)", fd);
  //%note jvm_r6
  return hpi::send(fd, buf, nBytes, flags);
JVM_END


JVM_LEAF(jint, JVM_Timeout, (int fd, long timeout))
  JVMWrapper2("JVM_Timeout (0x%x)", fd);
  //%note jvm_r6
  return hpi::timeout(fd, timeout);
JVM_END


JVM_LEAF(jint, JVM_Listen, (jint fd, jint count))
  JVMWrapper2("JVM_Listen (0x%x)", fd);
  //%note jvm_r6
  return hpi::listen(fd, count);
JVM_END


JVM_LEAF(jint, JVM_Connect, (jint fd, struct sockaddr *him, jint len))
  JVMWrapper2("JVM_Connect (0x%x)", fd);
  //%note jvm_r6
  return hpi::connect(fd, him, len);
JVM_END


JVM_LEAF(jint, JVM_Bind, (jint fd, struct sockaddr *him, jint len))
  JVMWrapper2("JVM_Bind (0x%x)", fd);
  //%note jvm_r6
  return hpi::bind(fd, him, len);
JVM_END


JVM_LEAF(jint, JVM_Accept, (jint fd, struct sockaddr *him, jint *len))
  JVMWrapper2("JVM_Accept (0x%x)", fd);
  //%note jvm_r6
  return hpi::accept(fd, him, (int *)len);
JVM_END


JVM_LEAF(jint, JVM_RecvFrom, (jint fd, char *buf, int nBytes, int flags, struct sockaddr *from, int *fromlen))
  JVMWrapper2("JVM_RecvFrom (0x%x)", fd);
  //%note jvm_r6
  return hpi::recvfrom(fd, buf, nBytes, flags, from, fromlen); 
JVM_END


JVM_LEAF(jint, JVM_GetSockName, (jint fd, struct sockaddr *him, int *len))
  JVMWrapper2("JVM_GetSockName (0x%x)", fd);
  //%note jvm_r6
  return hpi::get_sock_name(fd, him, len);
JVM_END


JVM_LEAF(jint, JVM_SendTo, (jint fd, char *buf, int len, int flags, struct sockaddr *to, int tolen))
  JVMWrapper2("JVM_SendTo (0x%x)", fd);
  //%note jvm_r6
  return hpi::sendto(fd, buf, len, flags, to, tolen);
JVM_END


JVM_LEAF(jint, JVM_SocketAvailable, (jint fd, jint *pbytes))
  JVMWrapper2("JVM_SocketAvailable (0x%x)", fd);
  //%note jvm_r6
  return hpi::socket_available(fd, pbytes);
JVM_END


JVM_LEAF(jint, JVM_GetSockOpt, (jint fd, int level, int optname, char *optval, int *optlen))
  JVMWrapper2("JVM_GetSockOpt (0x%x)", fd);
  //%note jvm_r6
  return hpi::get_sock_opt(fd, level, optname, optval, optlen);
JVM_END


JVM_LEAF(jint, JVM_SetSockOpt, (jint fd, int level, int optname, const char *optval, int optlen))
  JVMWrapper2("JVM_GetSockOpt (0x%x)", fd);
  //%note jvm_r6
  return hpi::set_sock_opt(fd, level, optname, optval, optlen);
JVM_END

JVM_LEAF(int, JVM_GetHostName, (char* name, int namelen))
  JVMWrapper("JVM_GetHostName");
  return hpi::get_host_name(name, namelen);
JVM_END

#ifdef _WINDOWS

JVM_LEAF(struct hostent*, JVM_GetHostByAddr, (const char* name, int len, int type))
  JVMWrapper("JVM_GetHostByAddr");
  return hpi::get_host_by_addr(name, len, type);
JVM_END


JVM_LEAF(struct hostent*, JVM_GetHostByName, (char* name))
  JVMWrapper("JVM_GetHostByName");
  return hpi::get_host_by_name(name);
JVM_END


JVM_LEAF(struct protoent*, JVM_GetProtoByName, (char* name))
  JVMWrapper("JVM_GetProtoByName");
  return hpi::get_proto_by_name(name);
JVM_END

#endif

// Library support ///////////////////////////////////////////////////////////////////////////

JVM_ENTRY_NO_ENV(void*, JVM_LoadLibrary, (const char* name))
  //%note jvm_ct  
  JVMWrapper2("JVM_LoadLibrary (%s)", name);  
  char ebuf[1024];
  void *load_result;
  {
    ThreadToNativeFromVM ttnfvm(thread);
    load_result = hpi::dll_load(name, ebuf, sizeof ebuf);
  }
  if (load_result == NULL) {
    char msg[1024];
    jio_snprintf(msg, sizeof msg, "%s: %s", name, ebuf);
    // Since 'ebuf' may contain a string encoded using
    // platform encoding scheme, we need to pass 
    // Exceptions::unsafe_to_utf8 to the new_exception method 
    // as the last argument. See bug 6367357.
    Handle h_exception = 
      Exceptions::new_exception(thread,
                                vmSymbols::java_lang_UnsatisfiedLinkError(),
                                msg, Exceptions::unsafe_to_utf8);

    THROW_HANDLE_0(h_exception);
  }
  return load_result;
JVM_END


JVM_LEAF(void, JVM_UnloadLibrary, (void* handle))  
  JVMWrapper("JVM_UnloadLibrary");  
  hpi::dll_unload(handle);
JVM_END


JVM_LEAF(void*, JVM_FindLibraryEntry, (void* handle, const char* name))  
  JVMWrapper2("JVM_FindLibraryEntry (%s)", name);  
  return hpi::dll_lookup(handle, name);
JVM_END

// Floating point support ////////////////////////////////////////////////////////////////////

JVM_LEAF(jboolean, JVM_IsNaN, (jdouble a))
  JVMWrapper("JVM_IsNaN");
  return g_isnan(a);
JVM_END



// JNI version ///////////////////////////////////////////////////////////////////////////////

JVM_LEAF(jboolean, JVM_IsSupportedJNIVersion, (jint version))
  JVMWrapper2("JVM_IsSupportedJNIVersion (%d)", version);
  return Threads::is_supported_jni_version_including_1_1(version);
JVM_END


// String support ///////////////////////////////////////////////////////////////////////////

JVM_ENTRY(jstring, JVM_InternString, (JNIEnv *env, jstring str))
  JVMWrapper("JVM_InternString");
  JvmtiVMObjectAllocEventCollector oam;
  if (str == NULL) return NULL;
  oop string = JNIHandles::resolve_non_null(str);
  oop result = StringTable::intern(string, CHECK_NULL);
  return (jstring) JNIHandles::make_local(env, result);
JVM_END


// Raw monitor support //////////////////////////////////////////////////////////////////////

// The lock routine below calls lock_without_safepoint_check in order to get a raw lock
// without interfering with the safepoint mechanism. The routines are not JVM_LEAF because
// they might be called by non-java threads. The JVM_LEAF installs a NoHandleMark check
// that only works with java threads.


JNIEXPORT void* JNICALL JVM_RawMonitorCreate(void) {
  VM_Exit::block_if_vm_exited();
  JVMWrapper("JVM_RawMonitorCreate");
  return new AzLock(AzLock::JVM_RAW_rank, "JVM_RawMonitorCreate", false);
}


JNIEXPORT void JNICALL  JVM_RawMonitorDestroy(void *mon) {
  VM_Exit::block_if_vm_exited();
  JVMWrapper("JVM_RawMonitorDestroy");
delete((AzLock*)mon);
}


JNIEXPORT jint JNICALL JVM_RawMonitorEnter(void *mon) {
  VM_Exit::block_if_vm_exited();
  JVMWrapper("JVM_RawMonitorEnter");  
((AzLock*)mon)->jvm_raw_lock();
  GET_RPC;
  ((AzLock*) mon)->_rpc = RPC;
  return 0;
}


JNIEXPORT void JNICALL JVM_RawMonitorExit(void *mon) { 
  VM_Exit::block_if_vm_exited();
  JVMWrapper("JVM_RawMonitorExit");  
((AzLock*)mon)->jvm_raw_unlock();
}


// Support for Serialization

void initialize_converter_functions() {
  // These functions only exist for compatibility with 1.3.1 and earlier
  return;
}


// Shared JNI/JVM entry points //////////////////////////////////////////////////////////////

jclass find_class_from_class_loader(JNIEnv* env, symbolHandle name, jboolean init, Handle loader, Handle protection_domain, jboolean throwError, TRAPS) {
  // Security Note:
  //   The Java level wrapper will perform the necessary security check allowing 
  //   us to pass the NULL as the initiating class loader.
  klassOop klass = SystemDictionary::resolve_or_fail(name, loader, protection_domain, throwError != 0, CHECK_NULL);
  KlassHandle klass_handle(THREAD, klass);
  // Check if we should initialize the class
  if (init && klass_handle->oop_is_instance()) {
    klass_handle->initialize(CHECK_NULL);
  }
return(jclass)JNIHandles::make_local(env,klass_handle->java_mirror_ref());
}


// Internal SQE debugging support ///////////////////////////////////////////////////////////

#ifndef PRODUCT

extern "C" {
  JNIEXPORT jboolean JNICALL JVM_AccessVMBooleanFlag(const char* name, jboolean* value, jboolean is_get);
  JNIEXPORT jboolean JNICALL JVM_AccessVMIntFlag(const char* name, jint* value, jboolean is_get);
  JNIEXPORT void JNICALL JVM_VMBreakPoint(JNIEnv *env, jobject obj);
}

JVM_LEAF(jboolean, JVM_AccessVMBooleanFlag, (const char* name, jboolean* value, jboolean is_get))
  JVMWrapper("JVM_AccessBoolVMFlag");
  return is_get ? CommandLineFlags::boolAt((char*) name, (bool*) value) : CommandLineFlags::boolAtPut((char*) name, (bool*) value, INTERNAL);
JVM_END

JVM_LEAF(jboolean, JVM_AccessVMIntFlag, (const char* name, jint* value, jboolean is_get))
  JVMWrapper("JVM_AccessVMIntFlag");
  intx v;
  jboolean result = is_get ? CommandLineFlags::intxAt((char*) name, &v) : CommandLineFlags::intxAtPut((char*) name, &v, INTERNAL);
  *value = (jint)v;
  return result;
JVM_END


JVM_ENTRY(void, JVM_VMBreakPoint, (JNIEnv *env, jobject obj))
  JVMWrapper("JVM_VMBreakPoint");
  oop the_obj = JNIHandles::resolve(obj);
  BREAKPOINT;
JVM_END


#endif


JVM_ENTRY(jobject, JVM_InvokeMethod, (JNIEnv *env, jobject method, jobject obj, jobjectArray args0))  
  JVMWrapper("JVM_InvokeMethod");
  Handle method_handle;
if(thread->stack_available((intptr_t)&method_handle)>=JVMInvokeMethodSlack){
    method_handle = Handle(THREAD, JNIHandles::resolve(method));
Handle receiver(THREAD,JNIHandles::resolve_as_ref(obj));
    objArrayHandle args(THREAD, objArrayOop(JNIHandles::resolve(args0)));
    oop result = Reflection::invoke_method(method_handle(), receiver, args, CHECK_NULL);
    jobject res = JNIHandles::make_local(env, result);
    if (JvmtiExport::should_post_vm_object_alloc()) {
      oop ret_type = java_lang_reflect_Method::return_type(method_handle());
      assert(ret_type != NULL, "sanity check: ret_type oop must not be NULL!");
      if (java_lang_Class::is_primitive(ret_type)) {
        // Only for primitive type vm allocates memory for java object.
        // See box() method.
        JvmtiExport::post_vm_object_alloc(JavaThread::current(), result);
      }
    }
    return res; 
  } else {
    THROW_0(vmSymbols::java_lang_StackOverflowError());
  }
JVM_END

JVM_ENTRY(jobject, JVM_NewInstanceFromConstructor, (JNIEnv *env, jobject c, jobjectArray args0))  
  JVMWrapper("JVM_NewInstanceFromConstructor");
  oop constructor_mirror = JNIHandles::resolve(c);
  objArrayHandle args(THREAD, objArrayOop(JNIHandles::resolve(args0)));
GET_RPC;
oop result=Reflection::invoke_constructor(constructor_mirror,args,RPC,CHECK_NULL);
  if (JvmtiExport::should_post_vm_object_alloc()) {
    JvmtiExport::post_vm_object_alloc(JavaThread::current(), result);
  }
  return JNIHandles::make_local(env, result);
JVM_END


// Atomic ///////////////////////////////////////////////////////////////////////////////////////////

JVM_LEAF(jboolean, JVM_SupportsCX8, ())
  JVMWrapper("JVM_SupportsCX8");
  return VM_Version::supports_cx8();
JVM_END


JVM_ENTRY(jboolean, JVM_CX8Field, (JNIEnv *env, jobject obj, jfieldID fid, jlong oldVal, jlong newVal))
  JVMWrapper("JVM_CX8Field");
  jlong res;
  oop             o       = JNIHandles::resolve(obj);
  intptr_t        fldOffs = jfieldIDWorkaround::from_instance_jfieldID(o->klass(), fid);
  volatile jlong* addr    = (volatile jlong*)((address)o + fldOffs);

  assert(VM_Version::supports_cx8(), "cx8 not supported");
  res = Atomic::cmpxchg(newVal, addr, oldVal);

  return res == oldVal;
JVM_END

// Returns an array of all live Thread objects (VM internal JavaThreads,
// jvmti agent threads, and JNI attaching threads  are skipped)
// See CR 6404306 regarding JNI attaching threads
JVM_ENTRY(jobjectArray,JVM_GetAllThreads,(JNIEnv*env,jclass dummy))
  ResourceMark rm(THREAD);
  ThreadsListEnumerator tle(THREAD, false, false);
  JvmtiVMObjectAllocEventCollector oam;

  int num_threads = tle.num_threads();
objArrayOop r=oopFactory::new_objArray(SystemDictionary::thread_klass(),num_threads,true/*SBA*/,CHECK_NULL);
  objArrayHandle threads_ah(THREAD, r);

  for (int i = 0; i < num_threads; i++) {
    Handle h = tle.get_threadObj(i);
    threads_ah->obj_at_put(i, h());
  }

  return (jobjectArray) JNIHandles::make_local(env, threads_ah());
JVM_END


// Support for java.lang.Thread.getStackTrace() and getAllStackTraces() methods
// Return StackTraceElement[][], each element is the stack trace of a thread in
// the corresponding entry in the given threads array
JVM_ENTRY(jobjectArray,JVM_DumpThreads,(JNIEnv*env,jclass threadClass,jobjectArray threads))
  JVMWrapper("JVM_DumpThreads");
  JvmtiVMObjectAllocEventCollector oam;

  // Check if threads is null
  if (threads == NULL) {
    THROW_(vmSymbols::java_lang_NullPointerException(), 0);
  }

  objArrayOop a = objArrayOop(JNIHandles::resolve_non_null(threads));
  objArrayHandle ah(THREAD, a);
  int num_threads = ah->length();
  // check if threads is non-empty array
  if (num_threads == 0) {
    THROW_(vmSymbols::java_lang_IllegalArgumentException(), 0);
  }

  // check if threads is not an array of objects of Thread class
  klassOop k = objArrayKlass::cast(ah->klass())->element_klass();
  if (k != SystemDictionary::thread_klass()) {
    THROW_(vmSymbols::java_lang_IllegalArgumentException(), 0);
  }

  ResourceMark rm(THREAD);

  GrowableArray<instanceHandle>* thread_handle_array = new GrowableArray<instanceHandle>(num_threads);
  for (int i = 0; i < num_threads; i++) {
    oop thread_obj = ah->obj_at(i);
    instanceHandle h(THREAD, (instanceOop) thread_obj);
    thread_handle_array->append(h);
  }

  Handle stacktraces = ThreadService::dump_stack_traces(thread_handle_array, num_threads, CHECK_NULL);
  return (jobjectArray)JNIHandles::make_local(env, stacktraces());

JVM_END

// JVM monitoring and management support
JVM_ENTRY_NO_ENV(void*,JVM_GetManagement,(jint version))
  return Management::get_jmm_interface(version);
JVM_END

// com.sun.tools.attach.VirtualMachine agent properties support
// 
// Initialize the agent properties with the properties maintained in the VM
JVM_ENTRY(jobject,JVM_InitAgentProperties,(JNIEnv*env,jobject properties))
  JVMWrapper("JVM_InitAgentProperties");
  ResourceMark rm;

Handle props(THREAD,JNIHandles::resolve_as_non_null_ref(properties));

  PUTPROP(props, "sun.java.command", Arguments::java_command());
  PUTPROP(props, "sun.jvm.flags", Arguments::jvm_flags());
  PUTPROP(props, "sun.jvm.args", Arguments::jvm_args());
  return properties;
JVM_END

JVM_ENTRY(jobjectArray, JVM_GetEnclosingMethodInfo, (JNIEnv *env, jclass ofClass))
{
  JVMWrapper("JVM_GetEnclosingMethodInfo");
  JvmtiVMObjectAllocEventCollector oam;

  if (ofClass == NULL) {
    return NULL;
  }
Handle mirror(THREAD,JNIHandles::resolve_as_non_null_ref(ofClass));
  // Special handling for primitive objects
  if (java_lang_Class::is_primitive(mirror())) {
    return NULL;
  }
  klassOop k = java_lang_Class::as_klassOop(mirror());
  if (!Klass::cast(k)->oop_is_instance()) {
    return NULL;
  }
  instanceKlassHandle ik_h(THREAD, k);
  int encl_method_class_idx = ik_h->enclosing_method_class_index();
  if (encl_method_class_idx == 0) {
    return NULL;
  }
objArrayOop dest_o=oopFactory::new_objArray(SystemDictionary::object_klass(),3,true/*SBA*/,CHECK_NULL);
  objArrayHandle dest(THREAD, dest_o);
  klassOop enc_k = ik_h->constants()->klass_at(encl_method_class_idx, CHECK_NULL);
dest->ref_at_put(0,Klass::cast(enc_k)->java_mirror_ref());
  int encl_method_method_idx = ik_h->enclosing_method_method_index();
  if (encl_method_method_idx != 0) {
    symbolOop sym_o = ik_h->constants()->symbol_at(
                        extract_low_short_from_int(
                          ik_h->constants()->name_and_type_at(encl_method_method_idx)));
    symbolHandle sym(THREAD, sym_o);
Handle str=java_lang_String::create_from_symbol(sym,true/*SBA*/,CHECK_NULL);
    dest->obj_at_put(1, str());
    sym_o = ik_h->constants()->symbol_at(
              extract_high_short_from_int(
                ik_h->constants()->name_and_type_at(encl_method_method_idx)));
    sym = symbolHandle(THREAD, sym_o);
str=java_lang_String::create_from_symbol(sym,true/*SBA*/,CHECK_NULL);
    dest->obj_at_put(2, str());
  }
  return (jobjectArray) JNIHandles::make_local(dest());
}
JVM_END

JVM_ENTRY(jintArray, JVM_GetThreadStateValues, (JNIEnv* env,
                                              jint javaThreadState))
{
  // If new thread states are added in future JDK and VM versions,
  // this should check if the JDK version is compatible with thread
  // states supported by the VM.  Return NULL if not compatible.
  // 
  // This function must map the VM java_lang_Thread::ThreadStatus
  // to the Java thread state that the JDK supports.
  //
 
  typeArrayHandle values_h;
  switch (javaThreadState) {
    case JAVA_THREAD_STATE_NEW : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,1,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::NEW); 
      break; 
    }
    case JAVA_THREAD_STATE_RUNNABLE : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,1,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::RUNNABLE); 
      break; 
    }
    case JAVA_THREAD_STATE_BLOCKED : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,1,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::BLOCKED_ON_MONITOR_ENTER); 
      break; 
    }
    case JAVA_THREAD_STATE_WAITING : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,2,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::IN_OBJECT_WAIT); 
      values_h->int_at_put(1, java_lang_Thread::PARKED); 
      break; 
    }
    case JAVA_THREAD_STATE_TIMED_WAITING : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,3,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::SLEEPING); 
      values_h->int_at_put(1, java_lang_Thread::IN_OBJECT_WAIT_TIMED); 
      values_h->int_at_put(2, java_lang_Thread::PARKED_TIMED); 
      break; 
    }
    case JAVA_THREAD_STATE_TERMINATED : {
typeArrayOop r=oopFactory::new_typeArray(T_INT,1,true/*SBA*/,CHECK_NULL);
      values_h = typeArrayHandle(THREAD, r); 
      values_h->int_at_put(0, java_lang_Thread::TERMINATED); 
      break; 
    }
    default:
      // Unknown state - probably incompatible JDK version
      return NULL;
  }

  return (jintArray) JNIHandles::make_local(env, values_h());
}
JVM_END


JVM_ENTRY(jobjectArray, JVM_GetThreadStateNames, (JNIEnv* env,
                                                jint javaThreadState,
                                                jintArray values))
{
  // If new thread states are added in future JDK and VM versions,
  // this should check if the JDK version is compatible with thread
  // states supported by the VM.  Return NULL if not compatible.
  // 
  // This function must map the VM java_lang_Thread::ThreadStatus
  // to the Java thread state that the JDK supports.
  //

  ResourceMark rm;

  // Check if threads is null
  if (values == NULL) {
    THROW_(vmSymbols::java_lang_NullPointerException(), 0);
  }

  typeArrayOop v = typeArrayOop(JNIHandles::resolve_non_null(values));
  typeArrayHandle values_h(THREAD, v);

  objArrayHandle names_h;
  switch (javaThreadState) {
    case JAVA_THREAD_STATE_NEW : {
      assert(values_h->length() == 1 && 
               values_h->int_at(0) == java_lang_Thread::NEW,
             "Invalid threadStatus value");

      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
1/*only 1 substate*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
Handle name=java_lang_String::create_from_str("NEW",true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name());
      break; 
    }
    case JAVA_THREAD_STATE_RUNNABLE : {
      assert(values_h->length() == 1 && 
               values_h->int_at(0) == java_lang_Thread::RUNNABLE,
             "Invalid threadStatus value");

      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
1/*only 1 substate*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
Handle name=java_lang_String::create_from_str("RUNNABLE",true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name()); 
      break; 
    }
    case JAVA_THREAD_STATE_BLOCKED : {
      assert(values_h->length() == 1 && 
               values_h->int_at(0) == java_lang_Thread::BLOCKED_ON_MONITOR_ENTER,
             "Invalid threadStatus value");

      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
1/*only 1 substate*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
Handle name=java_lang_String::create_from_str("BLOCKED",true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name()); 
      break; 
    }
    case JAVA_THREAD_STATE_WAITING : {
      assert(values_h->length() == 2 && 
               values_h->int_at(0) == java_lang_Thread::IN_OBJECT_WAIT &&
               values_h->int_at(1) == java_lang_Thread::PARKED,
             "Invalid threadStatus value");
      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
2/*number of substates*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
      Handle name0 = java_lang_String::create_from_str("WAITING.OBJECT_WAIT",
true/*SBA*/,CHECK_NULL);
      Handle name1 = java_lang_String::create_from_str("WAITING.PARKED",
true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name0()); 
      names_h->obj_at_put(1, name1()); 
      break; 
    }
    case JAVA_THREAD_STATE_TIMED_WAITING : {
      assert(values_h->length() == 3 && 
               values_h->int_at(0) == java_lang_Thread::SLEEPING &&
               values_h->int_at(1) == java_lang_Thread::IN_OBJECT_WAIT_TIMED &&
               values_h->int_at(2) == java_lang_Thread::PARKED_TIMED,
             "Invalid threadStatus value");
      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
3/*number of substates*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
      Handle name0 = java_lang_String::create_from_str("TIMED_WAITING.SLEEPING",
true/*SBA*/,CHECK_NULL);
      Handle name1 = java_lang_String::create_from_str("TIMED_WAITING.OBJECT_WAIT",
true/*SBA*/,CHECK_NULL);
      Handle name2 = java_lang_String::create_from_str("TIMED_WAITING.PARKED",
true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name0()); 
      names_h->obj_at_put(1, name1()); 
      names_h->obj_at_put(2, name2()); 
      break; 
    }
    case JAVA_THREAD_STATE_TERMINATED : {
      assert(values_h->length() == 1 && 
               values_h->int_at(0) == java_lang_Thread::TERMINATED,
             "Invalid threadStatus value");
      objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
1/*only 1 substate*/,true/*SBA*/,
                                               CHECK_NULL);
      names_h = objArrayHandle(THREAD, r); 
Handle name=java_lang_String::create_from_str("TERMINATED",true/*SBA*/,CHECK_NULL);
      names_h->obj_at_put(0, name()); 
      break; 
    }
    default:
      // Unknown state - probably incompatible JDK version
      return NULL;
  }
  return (jobjectArray) JNIHandles::make_local(env, names_h());
}
JVM_END

JVM_ENTRY(void, JVM_GetVersionInfo, (JNIEnv* env, jvm_version_info* info, size_t info_size))
{
  memset(info, 0, sizeof(info_size));
                                                                                
  info->jvm_version = Abstract_VM_Version::jvm_version();
  info->update_version = 0;          /* 0 in HotSpot Express VM */
  info->special_update_version = 0;  /* 0 in HotSpot Express VM */

  // when we add a new capability in the jvm_version_info struct, we should also
  // consider to expose this new capability in the sun.rt.jvmCapabilities jvmstat
  // counter defined in runtimeService.cpp.
  info->is_attachable = AttachListener::is_attach_supported();
}
JVM_END

// ------------------------------------ Azul use -----------------------------------------

// Allow for ticks to be generated from Java or native code
JVM_LEAF(void, JVM_MetaTick, (jint meta, jlong info, jlong tag))
  TickProfiler::meta_tick(meta, (intptr_t)info, (intptr_t)tag);
JVM_END


// Convert local JNIEnv to the proxy JNIEnv
JVM_LEAF(uint64_t, JVM_Avm2ProxyJNIEnv, (JNIEnv* avm_env))
JVMWrapper("JVM_Avm2ProxyJNIEnv");
#ifdef AZ_PROXIED
JavaThread*jt=JavaThread::thread_from_jni_environment(avm_env);
  uint64_t penv = jt->proxy_jni_environment();
  if (penv == 0LL) {
      penv = proxy_createJNIEnv((uint64_t)avm_env);
      jt->set_proxy_jni_environment(penv);
  }
assert(penv!=0,"invalid proxy JNIEnv");
  return penv;
#else
  Unimplemented();
  return 0;
#endif // AZ_PROXIED
JVM_END


// caller must free the char* when done with it
JVM_ENTRY_NO_ENV(char*, JVM_GetMethodSignature, (jmethodID methodID))
{
  ResourceMark rm;
  char* sig = (char *)(JNIHandles::resolve_jmethod_id(methodID).as_methodOop()->signature()->as_C_string());
  return strdup(sig); // caller must free this
}
JVM_END


// This method can be called by a GC task thread, VMThread or JavaThread
JVMTI_ENTRY(char*, JVM_GetMethodSignatureFromUnknownThread, (jmethodID methodID))
{
  Thread* thread = Thread::current();

assert(thread->is_Complete_Thread(),"JVM_GetMethodSignatureFromUnknownThread can be called only by a thread of type 'Thread'");

if(thread->is_GC_task_thread()||thread->is_VM_thread()){
    ResourceMark rm;
    char* sig = (char *)(JNIHandles::resolve_jmethod_id(methodID).as_methodOop()->signature()->as_C_string());
    return strdup(sig); // caller must free this
  } else {
    return JVM_GetMethodSignature(methodID);
  }
}
JVMTI_END


JVMTI_ENTRY(void, JVM_GetTagAndThreadId, (jlong* callback_wrapper_ptr, jlong* tag_addr, jint* thread_id_addr))
{
  Thread* thread = Thread::current();

  *tag_addr = *((CallbackWrapper*) callback_wrapper_ptr)->obj_tag_p();

if(thread->is_GC_task_thread()){
    *thread_id_addr = ((GCTaskThread*) thread)->which() + 1; 
}else if(thread->is_VM_thread()){
    *thread_id_addr = 0;
  } else {
    ShouldNotReachHere();
  }
}
JVMTI_END


JVMTI_ENTRY(jint, JVM_GetThreadId, ())
{
  Thread* thread = Thread::current();

if(thread->is_GC_task_thread()){
    return ((GCTaskThread*) thread)->which() + 1; 
}else if(thread->is_VM_thread()){
    return 0;
  } else {
    ShouldNotReachHere();
    return 0;
  }
}
JVMTI_END


JVMTI_ENTRY(void, JVM_SetCallbackWrapperTag, (jboolean update_tag, jlong callback_wrapper_ptr, jlong tag))
{
  if (update_tag) {
    ((CallbackWrapper*) callback_wrapper_ptr)->set_tag(tag);
  }

  delete ((CallbackWrapper*) callback_wrapper_ptr);
}
JVMTI_END


JVMTI_ENTRY(void, JVM_SetHeapIterationControl, (jvmtiIterationControl value, jvmti_heap_iteration_type type))
{
if(type==JVMTI_ITERATE_OVER_HEAP){
    VM_HeapIterateOperation::set_heap_iteration_control(value);
}else if(type==JVMTI_ITERATE_OVER_REACHABLE_OBJECT){
    VM_HeapWalkOperation::set_heap_iteration_control(value);
  } else {
    ShouldNotReachHere();
  }
}
JVMTI_END


JVMTI_ENTRY(jboolean, JVM_ShouldUseSynchronousHeapCallbacks, ())
{
  // If HeapIteratorTaskThreads is the default value of 1,
  // use synchronous callbacks.
  // This may be desired for apps that require an immediate JVMTI_ITERATION_ABORT
  return (HeapIteratorTaskThreads == 1);
}
JVMTI_END


JVM_ENTRY_NO_ENV(jchar, JVM_GetMethodReturnType, (jmethodID methodID))
  BasicType type = JNIHandles::resolve_jmethod_id(methodID).as_methodOop()->result_type();

  switch (type) {
    case T_OBJECT  :
    case T_ARRAY   : return 'L';
    case T_BOOLEAN : return 'Z';
    case T_BYTE    : return 'B';
    case T_CHAR    : return 'C';
    case T_SHORT   : return 'S';
    case T_INT	   : return 'I';
    case T_LONG    : return 'J';
    case T_FLOAT   : return 'F';
    case T_DOUBLE  : return 'D';
    case T_VOID    : return 'V';
    default        : ShouldNotReachHere(); 
  }
  return 0;
JVM_END


JVM_LEAF(jboolean, JVM_GetUseDebugLibrarySuffix, (JNIEnv* env))
{
  return UseDebugLibrarySuffix;
}
JVM_END

JVM_LEAF(jboolean, JVM_GetUseLockedCollections, ())
{
  return UseLockedCollections;
}
JVM_END


JVM_LEAF(jboolean, JVM_GetUseCheckedCollections, ())
{
  return UseCheckedCollections;
}
JVM_END

JVM_ENTRY(void, JVM_BlockingHint_set, (JNIEnv* env, jobject str, jobject lock, jobject sync))
{
  thread->set_hint_blocking_concurrency_msg (JNIHandles::resolve_as_ref(str ));
  thread->set_hint_blocking_concurrency_lock(JNIHandles::resolve_as_ref(lock));
  thread->set_hint_blocking_concurrency_sync(JNIHandles::resolve_as_ref(sync));
thread->set_time_blocked();
}
JVM_END

JVM_LEAF(jint, JVM_GetJavaThreadLocalMapInitialCapacity, ())
{
  return JavaThreadLocalMapInitialCapacity;
}
JVM_END

// ThreadCounters
JVM_LEAF(jlong, JVM_TicksToNanos, (JNIEnv* env, jlong ticks))
{
  return os::ticks_to_nanos(ticks);
}
JVM_END

JVM_LEAF(jlong, JVM_CollectCpuTicks, (JNIEnv* env))
{
  ThreadCounters* tc = thread->thread_counters();

  return os::thread_cpu_time_millis(thread);
}
JVM_END

JVM_LEAF(jlong, JVM_CollectWallTicks, (JNIEnv* env))
{
  ThreadCounters* tc = thread->thread_counters();

  return tc->wall_ticks();
}
JVM_END

JVM_LEAF(jlong, JVM_CollectBlockedTicks, (JNIEnv* env))
{
  ThreadCounters* tc = thread->thread_counters();

  return tc->object_blocked_ticks();
}
JVM_END

JVM_LEAF(jlong, JVM_CollectObjWaitTicks, (JNIEnv* env))
{
  ThreadCounters* tc = thread->thread_counters();

  return tc->object_wait_ticks();
}
JVM_END

JVM_ENTRY(jlong, JVM_CollectNetWaitTicks, (JNIEnv* env))
{
THROW_MSG_0(vmSymbols::java_lang_UnsupportedOperationException(),
"CollectNetWaitTicks not supported.");
}
JVM_END

JVM_ENTRY(jlong, JVM_CollectFileWaitTicks, (JNIEnv* env))
{
THROW_MSG_0(vmSymbols::java_lang_UnsupportedOperationException(),
"CollectFileWaitTicks not supported.");
}
JVM_END

JVM_ENTRY(jlong, JVM_CollectGCWaitTicks, (JNIEnv* env))
{
THROW_MSG_0(vmSymbols::java_lang_UnsupportedOperationException(),
"CollectGCWaitTicks not supported.");
}
JVM_END

JVM_ENTRY(jlong, JVM_CollectSafepointWaitTicks, (JNIEnv* env))
{
THROW_MSG_0(vmSymbols::java_lang_UnsupportedOperationException(),
"CollectSafepointTicks not supported.");
}
JVM_END

JVM_LEAF(jlong, JVM_CollectCpuWaitTicks, (JNIEnv* env))
{
  ThreadCounters* tc = thread->thread_counters();

  return tc->cpu_wait_ticks();
}
JVM_END

//  To request TLAB parking from j2se or proxy
JVM_PROXY_SAFE_LEAF(void, JVM_ParkTLAB, ())
{
Thread*t=Thread::current();
  if (t != NULL) {
    if (ParkTLAB && (t->is_Java_thread())) {
      if (!(((JavaThread *)t)->jvm_locked_by_self())) {
        ((JavaThread *)t)->hint_blocked("I/O wait");
      }
      t->tlab().park();
    } 
    t->thread_counters()->set_temp_tick(os::elapsed_counter());
  } 
}
JVM_END

JVM_PROXY_SAFE_LEAF(void, JVM_UnparkTLAB, ())
{
Thread*t=Thread::current();
  if (t != NULL) {
ThreadCounters*tc=t->thread_counters();
    tc->add_rpc_wait_ticks(os::elapsed_counter() - tc->temp_tick());
    if (ParkTLAB && (t->is_Java_thread())) {
      if (!(((JavaThread *)t)->jvm_locked_by_self())) {
        ((JavaThread *)t)->hint_unblocked();
      }
      t->tlab().unpark();
    } 
  } 
}
JVM_END

#ifdef AZ_PROXIED
#include <proxy/v2dhandle.h>
#endif // AZ_PROXIED

JVM_ENTRY(int, JVM_ReadByteArrayRegion, (JNIEnv *env, jbyteArray array, jsize start,
                                         jsize len, jint fd))
{
JVMWrapper("JVM_ReadByteArrayRegion");
typeArrayOop dst=typeArrayOop(JNIHandles::resolve_non_null(array));
    int nread = 0;
    int datalen;

assert(dst->is_array(),"must be array");
    datalen = dst->length();
    if ((start < 0) || (start > datalen) ||
        (len < 0) || ((start + len) > datalen) || ((start + len) < 0)) {
THROW_(vmSymbols::java_lang_ArrayIndexOutOfBoundsException(),-1);
    } else {
        if (len > 0) {
            int l2es = typeArrayKlass::cast(dst->klass())->log2_element_size();
#ifdef AZ_PROXIED
            nread = (int)::v2dhandle_read(fd, (void*) dst->byte_at_addr(start), (size_t)(len << l2es), 1);
#else
            nread = (int)::read          (fd, (void*) dst->byte_at_addr(start), (size_t)(len << l2es));
#endif // AZ_PROXIED
        }
    }
    return nread;
}
JVM_END

JVM_ENTRY(int, JVM_WriteByteArrayRegion, (JNIEnv *env, jbyteArray array, jsize start,
                                          jsize len, jint fd))
{
JVMWrapper("JVM_WriteByteArrayRegion");
typeArrayOop dst=typeArrayOop(JNIHandles::resolve_non_null(array));
    int nwrite = 0;
    int datalen;

assert(dst->is_array(),"must be array");
    datalen = dst->length();
    if ((start < 0) || (start > datalen) ||
        (len < 0) || ((start + len) > datalen) || ((start + len) < 0)) {
THROW_(vmSymbols::java_lang_ArrayIndexOutOfBoundsException(),-1);
    } else {
        int l2es = typeArrayKlass::cast(dst->klass())->log2_element_size();
        while (len > 0) {
#ifdef AZ_PROXIED
            nwrite = (int)::v2dhandle_write(fd, (void*) dst->byte_at_addr(start), (size_t)(len << l2es), 1);
#else
            nwrite = (int)::write          (fd, (void*) dst->byte_at_addr(start), (size_t)(len << l2es));
#endif // AZ_PROXIED
            if (nwrite < 0) {
                break;
            }
            start += nwrite;
len-=nwrite;
        }
    }
    return nwrite;
}
JVM_END

static jclass fileDescriptorClass=NULL;
static jfieldID fdFieldId=NULL;

JVM_ENTRY(void, JVM_InitFileDescriptorField, (JNIEnv *env, jclass fdClass, jfieldID fdid))
{
JVMWrapper("JVM_InitFileDescriptorField");

    // Save global references for file descriptor class
    fileDescriptorClass = fdClass;

    // Save the fd field id
    fdFieldId = fdid;
}
JVM_END

JVM_ENTRY(jboolean, JVM_IsFileDescriptorField, (JNIEnv *env, jobject obj, jfieldID id))
{
JVMWrapper("JVM_IsFileDescriptorField");

    if (obj == NULL) return JNI_FALSE;
klassOop kls=java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(fileDescriptorClass));
    if (kls == NULL) return JNI_FALSE;
    if ((JNIHandles::resolve_non_null(obj)->is_a(kls)) && (id == fdFieldId)) {
        return JNI_TRUE;
    } else {
        return JNI_FALSE;
    }
}
JVM_END


JVM_ENTRY(jboolean, JVM_IsJVMPIEnabled, (JNIEnv* env))
{
JVMWrapper("JVM_IsJVMPIEnabled");
  return false;
}
JVM_END


JVM_ENTRY(void, JVM_ExternallyDisableJVMPIObjectAllocEvent, (JNIEnv* env, jboolean which)) 
{
JVMWrapper("JVM_ExternallyDisableJVMPIObjectAllocEvent");
  ShouldNotReachHere();
}
JVM_END


JVM_ENTRY(void, JVM_GetObjectArrayRegionIntField, (JNIEnv *env, jobjectArray array, jsize start, jsize len, jint *buf, jfieldID fieldID))
{
JVMWrapper("JVM_GetObjectArrayRegionIntField");
if(array==NULL)THROW(vmSymbols::java_lang_NullPointerException());
objArrayOop a=objArrayOop(JNIHandles::resolve_non_null(array));
  if (!((start >= 0) && (len >= 0) && ((start + len) <= a->length()))) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  if (len > 0) {
    oop o = a->obj_at(start);
if(o==NULL)THROW(vmSymbols::java_lang_NullPointerException());
    klassOop k = o->klass();
    int offset = jfieldIDWorkaround::from_instance_jfieldID(k, fieldID);
    buf[0] = o->int_field(offset);
for(int i=1;i<len;i++){
      oop o = a->obj_at(start+i);
if(o==NULL)THROW(vmSymbols::java_lang_NullPointerException());
      buf[i] = o->int_field(offset);
    }
  }
}
JVM_END

JVM_ENTRY(void, JVM_SetObjectArrayRegionIntField, (JNIEnv *env, jobjectArray array, jsize start, jsize len, const jint *buf, jfieldID fieldID))
{
JVMWrapper("JVM_SetObjectArrayRegionIntField");
if(array==NULL)THROW(vmSymbols::java_lang_NullPointerException());
objArrayOop a=objArrayOop(JNIHandles::resolve_non_null(array));
  if (!((start >= 0) && (len >= 0) && ((start + len) <= a->length()))) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  if (len > 0) {
    oop o = a->obj_at(start);
if(o==NULL)THROW(vmSymbols::java_lang_NullPointerException());
    klassOop k = o->klass();
    int offset = jfieldIDWorkaround::from_instance_jfieldID(k, fieldID);
    o->int_field_put(offset, buf[0]);
for(int i=1;i<len;i++){
      oop o = a->obj_at(start+i);
if(o==NULL)THROW(vmSymbols::java_lang_NullPointerException());
      o->int_field_put(offset, buf[i]);
    }
  }
}
JVM_END

#define GET_ARRAY_STARTADDR(arraytype, a, srcP)             \
  switch (arraytype) {                                      \
    case T_BOOLEAN:                                         \
      srcP = (u_char *)a->bool_at_addr(0);                  \
      break;                                                \
    case T_BYTE:                                            \
      srcP = (u_char *)a->byte_at_addr(0);                  \
      break;                                                \
    case T_CHAR:                                            \
      srcP = (u_char *)a->char_at_addr(0);                  \
      break;                                                \
    case T_SHORT:                                           \
      srcP = (u_char *)a->short_at_addr(0);                 \
      break;                                                \
    case T_INT:                                             \
      srcP = (u_char *)a->int_at_addr(0);                   \
      break;                                                \
    case T_DOUBLE:                                          \
      srcP = (u_char *)a->double_at_addr(0);                \
      break;                                                \
    case T_FLOAT:                                           \
      srcP = (u_char *)a->float_at_addr(0);                 \
      break;                                                \
    case T_LONG:                                            \
      srcP = (u_char *)a->long_at_addr(0);                  \
      break;                                                \
    default:                                                \
      srcP = NULL;                                          \
  }                                                         \

JVM_ENTRY(int, JVM_GetObjectFields, (JNIEnv *env, jobject obj, void *objserializer))
{
JVMWrapper("JVM_GetObjectFields");
#ifndef AZ_PROXIED
  // TODO: Verify that we're not using this method in unproxied builds.
  Unimplemented();
#else // AZ_PROXIED:
  if (obj == NULL) {
    return -1;
  }
oop o=JNIHandles::resolve_non_null(obj);
  klassOop k = o->klass();
if(k==NULL){
    return -1;                                    // Unable to find class
  }

  /*
   * First check if object is a string, if so get the length, utf8 length,
   * char array and utf8 string so that we can field
   * jni_GetStringLength, jni_GetStringChars, jni_GetStringUTFLength,
   * jni_GetStringUTFChars etc.
   * locally on the proxy without making round trips
   * Then check if object is a primitive array, if so we get the
   * array contents so that we can field all the array access calls
   */
  if (java_lang_String::is_instance(o)) {         // Check if object is a string
typeArrayOop s_value=java_lang_String::value(o);
int s_offset=java_lang_String::offset(o);
int s_len=java_lang_String::length(o);
    ResourceMark rm;

    if (s_len > 0) {
      if (s_len <= MAX_JNICACHING_STRING_SIZE) {
        proxy_serialize_jstring(objserializer,
                                s_len, s_value->char_at_addr(s_offset),
                                java_lang_String::utf8_length(o), java_lang_String::as_utf8_string(o));
      } else {
        return -1;
      }
    } else {
      proxy_serialize_jstring(objserializer, s_len, NULL, 0, NULL);
    }
    return 0;
  } else if (o->is_objArray()) {                  // Object is an object array
    return -1;                                    // We don't try and serialize an array of objects
  } else if (o->is_array()) {                     // Object is primitive array
typeArrayOop a=typeArrayOop(o);
BasicType arraytype=typeArrayKlass::cast(o->klass())->element_type();
    int alen = a->length();
    int l2es = typeArrayKlass::cast(o->klass())->log2_element_size();

    if (alen > 0) {
        u_char *srcP;

        GET_ARRAY_STARTADDR(arraytype, a, srcP);
if(srcP!=NULL){
            proxy_serialize_jarray(objserializer, alen, 1 << l2es, arraytype, srcP);
            return 0;
        }
    }
    return -1;
  }

  /*
   * Object is not a string/array it is regular object so get all non static/volatile fields
   * of the object so that field access functions can be handled locally without making
   * round trips from the proxy
   * Get fieldid, values of the various non static and non volatile fields
   * in the object (handles jni_GetFieldID, jni_Get**Field)
   */

  KlassHandle kh(THREAD, k);
  jfieldIDCache *fieldid_cache = instanceKlass::cast(kh())->jfieldid_cache();
  jfieldIDEntry *cached_fieldid_list;
  int offset;
  jfieldID fieldid;
BasicType fieldtype;

  // Loop through all the fields
if(fieldid_cache==NULL){
    return -1;
  }
  int count = fieldid_cache->get_fieldids(&cached_fieldid_list);
  if (count == 0 || cached_fieldid_list == NULL) {
    return -1;
  }
  proxy_serialize_jobject(objserializer, count);
  while (cached_fieldid_list != NULL && count > 0) {
offset=cached_fieldid_list->offset();
    fieldid = cached_fieldid_list->fieldid();
    fieldtype = cached_fieldid_list->fieldtype();
    switch (fieldtype) {
      case T_BOOLEAN:
        proxy_serialize_jint_field(objserializer, fieldid, fieldtype, o->bool_field(offset));
        break;
      case T_CHAR:
        proxy_serialize_jint_field(objserializer, fieldid, fieldtype, o->char_field(offset));
        break;
      case T_FLOAT:
        proxy_serialize_jfloat_field(objserializer, fieldid, o->float_field(offset));
        break;
      case T_DOUBLE:
        proxy_serialize_jdouble_field(objserializer, fieldid, o->double_field(offset));
        break;
      case T_BYTE:
        proxy_serialize_jint_field(objserializer, fieldid, fieldtype, o->byte_field(offset));
        break;
      case T_SHORT:
        proxy_serialize_jint_field(objserializer, fieldid, fieldtype, o->short_field(offset));
        break;
      case T_INT:
        proxy_serialize_jint_field(objserializer, fieldid, fieldtype, o->int_field(offset));
        break;
      case T_LONG:
        proxy_serialize_jlong_field(objserializer, fieldid, o->long_field(offset));
        break;
      case T_OBJECT:
        {
oop fldoop=o->obj_field(offset);

if(fldoop!=NULL){
            if (java_lang_String::is_instance(fldoop)) {// Check if object is a string
jobject fldobj=JNIHandles::make_local(env,fldoop);
typeArrayOop s_value=java_lang_String::value(fldoop);
int s_offset=java_lang_String::offset(fldoop);
int s_len=java_lang_String::length(fldoop);
              ResourceMark rm;

              if (s_len > 0) {
                if (s_len <= MAX_JNICACHING_STRING_SIZE) {
                  proxy_serialize_jstring_field(objserializer,
                                                fieldid, fldobj,
                                                s_len, s_value->char_at_addr(s_offset),
                                                java_lang_String::utf8_length(fldoop), java_lang_String::as_utf8_string(fldoop));
                }
              } else {
                proxy_serialize_jstring_field(objserializer, fieldid, fldobj, s_len, NULL, 0, NULL);
              }
            }
          }
        }
        break;
      case T_ARRAY:
        {
oop array=o->obj_field(offset);

if(array!=NULL){
            if (!array->is_objArray() && array->is_array()) { // Object is primitive array
typeArrayOop a=typeArrayOop(array);
BasicType arraytype=typeArrayKlass::cast(array->klass())->element_type();
              int alen = a->length();
              int l2es = typeArrayKlass::cast(array->klass())->log2_element_size();
              u_char *srcP = NULL;

              if (alen != 0) {
                GET_ARRAY_STARTADDR(arraytype, a, srcP);
              }
if(srcP!=NULL){
jobject arrayobj=JNIHandles::make_local(env,array);
                proxy_serialize_jarray_field(objserializer, fieldid, arrayobj, alen, 1 << l2es, arraytype, srcP);
              }
            }
          }
        }
        break;
      default:
        break;
    }
    cached_fieldid_list = cached_fieldid_list->next();
    count -= 1;
  }
  proxy_serialize_jobject_field(objserializer, 0, NULL);
#endif // AZ_PROXIED
  return 0;
}
JVM_END

JVM_ENTRY(void, JVM_CachejfieldID, (JNIEnv *env, jclass clazz, jfieldID id, jfieldID mapid))
{
JVMWrapper("JVM_CachejfieldID");
if(clazz!=NULL){
klassOop k=java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(clazz));
klassOop fileFld=java_lang_Class::as_klassOop(JNIHandles::resolve_non_null(fileDescriptorClass));

    // Don't try to cache the fileID field of FileDescriptor objects as we need to
    // track access to the field and do some special processing
    if (k == fileFld && id == fdFieldId) {
        return;
    }
    if (Klass::cast(k)->oop_is_instance()) {
      jfieldIDCache *fieldid_cache = instanceKlass::cast(k)->get_or_create_jfieldid_cache();
      if (fieldid_cache != NULL && !fieldid_cache->lookup_fieldid(mapid)) {
        fieldDescriptor fd;
int offset=jfieldIDWorkaround::from_instance_jfieldID(k,id);

        if (instanceKlass::cast(k)->find_field_from_offset(offset, false, &fd) && !fd.is_volatile()) {
          fieldid_cache->add_fieldid(offset, mapid, fd.field_type());
        }
      }
    }
  }
}
JVM_END

JVM_ENTRY(void, JVM_SetFinalizerThread, (JNIEnv *env, jclass ignored, jboolean flag))
{
    thread->set_finalizer_thread((bool)flag);
}
JVM_END

JVM_ENTRY(jobject, JVM_ReferenceGetReferent, (JNIEnv* env, jobject reference))
{
JVMWrapper("JVM_ReferenceGetReferent");
  // GPGC concurrent ref processing depends on having no safepoints during referent lookup.
  No_Safepoint_Verifier nsv;
  objectRef ref       = JNIHandles::resolve_as_ref(reference);
  assert0(!UseGenPauselessGC || ref.is_heap());  // SBA not yet supported
  return JNIHandles::make_local(env, UseGenPauselessGC 
                                ? GPGC_Collector::java_lang_ref_Reference_get(ref)
                                : lvb_ref(java_lang_ref_Reference::referent_addr(ref.as_oop())));
}
JVM_END

JVM_ENTRY(jboolean, JVM_ReferencePendingInNext, (JNIEnv* env))
{
jboolean pendingInNext;

  if ( UseGenPauselessGC ) {
    pendingInNext = false;
  } else {
    pendingInNext = true;
  }

  return pendingInNext;
}
JVM_END

#ifdef AZ_PROXIED

#define MAX_BUFFER_LEN 16384
#define MAX_PACKET_LEN 65536
#define IPv4 1

JVM_ENTRY(int, JVM_DatagramSendTo, (JNIEnv *env, jobject socket, jobject packet, jfieldID *idarray,
                                    jint *fd, jboolean *connected, jint *packetBufferLen, char **buffer,
                                    jint *family, jint *rport, uint32_t *raddr))
{
    // JVMWrapper("JVM_DatagramSendTo");
klassOop k;
oop o;
oop packetBufferOop;
typeArrayOop packetBuffer;
oop packetAddress;
  jint packetBufferOffset;
  proxy_error_t retval;
  uint64_t num_sent;

o=JNIHandles::resolve_non_null(socket);
k=o->klass();
  *connected = o->bool_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_PDSI_CONNECTED_FIELDID]));
  o = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_PDSI_FD_FIELDID]));
if(o==NULL){
    *fd = -1;
    return -1;                                    // Null fd object
  }
k=o->klass();
  *fd = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_IO_FD_FD_FIELDID]));
  if (*fd < 0) {
    return -1;                                    // Bad file descriptor
  }
o=JNIHandles::resolve_non_null(packet);
k=o->klass();
  packetBufferOop = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPBUFFER_FIELDID]));
  packetAddress = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPADDRESS_FIELDID]));
if(packetBufferOop==NULL||packetAddress==NULL){
    return AZPR_EINVAL;                           // Null packet buffer or address
  }
  packetBuffer = typeArrayOop(packetBufferOop);
  packetBufferOffset = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPOFFSET_FIELDID]));
  *packetBufferLen = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPBUFLENGTH_FIELDID]));
  if (*packetBufferLen > 0) {
    if (*connected) {
#if 0
      retval = v2dhandle_sendto(*fd, packetBuffer->byte_at_addr(packetBufferOffset), *packetBufferLen, PROXY_MSG_NONBLOCK, NULL, &num_sent);
#else
      retval = AZPR_EAGAIN;
#endif

    } else {
      *family = packetAddress->int_field(jfieldIDWorkaround::from_instance_jfieldID(packetAddress->klass(), idarray[DATAGRAM_IAFAMILY_FIELDID]));
      *rport = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPPORT_FIELDID]));
      *raddr = packetAddress->int_field(jfieldIDWorkaround::from_instance_jfieldID(packetAddress->klass(), idarray[DATAGRAM_IAADDRESS_FIELDID]));
#if 0
      if (*family == IPv4) {
        proxy_sockaddr_t rmtaddr;

        rmtaddr.sin_port = htons((short) *rport);
        rmtaddr.sin_addr.s_addr = htonl(*raddr);
        Unimplemented();
        //rmtaddr.sin_addr.family = AF_INET;
        retval = v2dhandle_sendto(*fd, packetBuffer->byte_at_addr(packetBufferOffset), *packetBufferLen, PROXY_MSG_NONBLOCK, &rmtaddr, &num_sent);
      } else {
        retval = AZPR_EAGAIN;
      }
#else
      retval = AZPR_EAGAIN;
#endif
    }
    if (retval == AZPR_EAGAIN) {
      if (*packetBufferLen > MAX_BUFFER_LEN) {
        if (*packetBufferLen > MAX_PACKET_LEN) {
          *packetBufferLen = MAX_PACKET_LEN;
        }
        *buffer = (char *)malloc(*packetBufferLen);
        if (!*buffer) {
          return -1;                                // OOM, Unable to allocate buffer
        }
      }
      memcpy(*buffer, packetBuffer->byte_at_addr(packetBufferOffset), *packetBufferLen);
    }
  } else {
    retval = AZPR_EAGAIN;
  }
  return retval;
}
JVM_END

JVM_ENTRY(int, JVM_DatagramRecvFrom, (JNIEnv *env, jobject socket, jobject packet, jfieldID *idarray,
                                      jint *fd, jint *bufoffset, size_t *buflen, unsigned int *timeout,
                                      jint *family, uint32_t *caddr, uint32_t *raddr))
{
JVMWrapper("JVM_DatagramRecvFrom");
klassOop k;
oop o;
oop packetBufferOop;
oop packetAddress;
  jint socketAddress;
proxy_error_t ret;
typeArrayOop packetBuffer;

o=JNIHandles::resolve_non_null(socket);
k=o->klass();

  // Get timeout value
  *timeout = (unsigned int)o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_PDSI_TIMEOUT_FIELDID]));

  // Get filedescriptor value
  o = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_PDSI_FD_FIELDID]));
if(o==NULL){
    *fd = -1;
    return -1;                                    // Null fd object
  }
k=o->klass();
  *fd = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_IO_FD_FD_FIELDID]));
  if (*fd < 0) {
    return -1;                                    // Bad file descriptor
  }

  // Get packet buffer offset and length
o=JNIHandles::resolve_non_null(packet);
k=o->klass();
  packetBufferOop = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPBUFFER_FIELDID]));
if(packetBufferOop==NULL){
    return AZPR_EINVAL;                           // Null packet buffer
  }
  packetBuffer = typeArrayOop(packetBufferOop);
  *bufoffset = o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPOFFSET_FIELDID]));
  *buflen = (size_t)o->int_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPBUFLENGTH_FIELDID]));

  // Get socket address
  packetAddress = o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPADDRESS_FIELDID]));
if(packetAddress!=NULL){
    *family = packetAddress->int_field(jfieldIDWorkaround::from_instance_jfieldID(packetAddress->klass(), idarray[DATAGRAM_IAFAMILY_FIELDID]));
    *caddr = packetAddress->int_field(jfieldIDWorkaround::from_instance_jfieldID(packetAddress->klass(), idarray[DATAGRAM_IAADDRESS_FIELDID]));
  } else {
    *family = 0;
  }
#if 0
  // Do a non blocking read into the packet buffer directly (will save a memcpy if data is available)
  if (*family == IPv4 && *buflen > 0) {
      proxy_sockaddr_t rmtaddr;
      uint64_t len;
char buf[1];
      size_t recvlen = *buflen;

      if (recvlen > MAX_PACKET_LEN) {
          recvlen = MAX_PACKET_LEN;
      }
len=recvlen;
      ret = v2dhandle_recvfrom(*fd, packetBuffer->byte_at_addr(*bufoffset), recvlen, PROXY_MSG_NONBLOCK, &rmtaddr, *timeout, &len);
      if (ret == PROXY_ERROR_NONE) {
          *raddr = rmtaddr.sin_addr.s_addr;
          o->int_field_put(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPPORT_FIELDID]), (jint)ntohs(rmtaddr.sin_port));
          o->int_field_put(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPLENGTH_FIELDID]), (jint)len);
      }
  } else {
ret=AZPR_EAGAIN;
  }
#else
ret=AZPR_EAGAIN;
#endif
  return ret;
}
JVM_END

JVM_ENTRY(void, JVM_DatagramRecvFromSet, (JNIEnv *env, jobject packet, jfieldID *idarray,
                                          jint bufoffset, jint buflen, char *buffer, jint rport))
{
JVMWrapper("JVM_DatagramRecvFromSet");
klassOop k;
oop o;
typeArrayOop packetBuffer;

  // Get packet buffer offset and length
o=JNIHandles::resolve_non_null(packet);
k=o->klass();
  if (buflen > 0) {
    packetBuffer = typeArrayOop(o->obj_field(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPBUFFER_FIELDID])));
    memcpy(packetBuffer->byte_at_addr(bufoffset), buffer, buflen);
  }
  o->int_field_put(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPPORT_FIELDID]), rport);
  o->int_field_put(jfieldIDWorkaround::from_instance_jfieldID(k, idarray[DATAGRAM_DPLENGTH_FIELDID]), buflen);
}
JVM_END
#endif // AZ_PROXIED
