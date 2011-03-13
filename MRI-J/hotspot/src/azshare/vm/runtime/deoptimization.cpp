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


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

#include "bytecode.hpp"
#include "sharedRuntime.hpp"
#include "c1_Runtime1.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "compilationPolicy.hpp"
#include "deoptimization.hpp"
#include "frame.hpp"
#include "gcLocker.hpp"
#include "interfaceSupport.hpp"
#include "interpreter_pd.hpp"
#include "jvmtiExport.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "tickProfiler.hpp"
#include "vframe.hpp"
#include "vmTags.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

bool DeoptimizationMarker::_is_active = false;

static int unload_count=0;
static int unload_count_stop = 0;
// ------------------------------------------------------------------
// Record the failure reason and decide if we should recompile the nmethod.
bool Deoptimization::policy_for_recompile( JavaThread *thread, const CodeBlob *cb, int deopt_index ) {
  // Get the CPData where failure happened
  vframe vf(thread);       // The basic Java frame being deopt'd
  CodeProfile *cp;         // CodeProfile holding some C1 inlined data
  CPData *cpd = NULL;      // Place to record failures
  // For JVMTI operation, continous deopts are the expected norm, so we don't
  // do the 'find_nested_cpdata' and don't do the 'did_deopt' assert that
  // entails.  For static fields in <clinit> - it requires a thread-check
  // which C2 will not emit.  Hence we continously deopt & recompile until the
  // class is finally initialized and we can emit the static-field ref code.
  // For Reason_deopt, the code has already been patched due to non-optimization
  // related reasons like stack overflow or class loading so there is nothing
  // interesting to record here.
  if( deopt_index != Reason_unhandled &&
      deopt_index != Reason_jvmti &&
      deopt_index != Reason_static_in_clinit &&
      deopt_index != Reason_deopt )
    // Record a did_deopt attempt, mostly for debugging continous deopt bugs.
    // For *selected* deopt flavors we also record other interesting bits further below.
    cpd = CodeProfile::find_nested_cpdata(thread,vf,cb->owner().as_methodCodeOop(),&cp,true); 

  if( deopt_index < 0 )         // Convert unloaded-classes indexs
    deopt_index = Reason_unloaded; // Do not bother to load the class, the interpreter will do that.

  // Count flavors of deopt
  counters[deopt_index]++;

  // ---------------
  // Make the nmethod not entrant, so next time it is called it gets recompiled.
  bool make_not_entrant = true;
  switch( deopt_index ) {
  case Reason_deopt: // A real deopt in progress; containing nmethod already marked not-entrant
    break;
  case Reason_static_in_clinit:  // Plough ahead in the interpreter
  case Reason_uninitialized:    
  case Reason_unloaded_array_class:
  case Reason_unloaded:          // These are all fine once the interpreter 
    break;                      // loads the class and then recompile.
  case Reason_unhandled:  // Always must uncommon-trap for this (big multi anewarray)
  case Reason_jvmti:             // Cannot compile for this
    make_not_entrant = false;   // Suffer along in the interpreter
    break;                      
  case Reason_stuck_in_loop:
    make_not_entrant = false;
    break;                      
  case Reason_unexpected_klass: {// Had a TypeProfile failure.  After profiling in C1
    CPData_Invoke* cpdi = (CPData_Invoke*)cpd;
    debug_only( assert(cpdi->is_Invoke(), "Not an invoke cpdata structure!") );
    cpdi->_poly_inlining_fail = 1;
    break;                      
  }
  case Reason_unreached: {
assert(cpd,"Unexpected cpd=null.  Are we deopt'ing on an unprofiled bci?");
    if (cpd) {
      CPData_Branch* cpdb = (CPData_Branch*)cpd;
      debug_only( assert(cpdb->is_Branch(), "Not a branch cpdata structure!") );
      if (cpdb->_taken==0) cpdb->_taken++;
      if (cpdb->_nottaken==0) cpdb->_nottaken++;
    }
    break;
  }

  case Reason_null_check:
    // Saw a null, e.g., as an array base.
  case Reason_div0_check: 
  case Reason_unexpected_null_cast:
    // If we see an unexpected null at a check-cast we record it and force a
    // recompile; the offending check-cast will be compiled to handle NULLs.
    ((CPData_Null*)cpd)->_null = 1;
    break;

  case Reason_array_store_check:
    // We come here we tried to cast an oop array to it's declared type and
    // that cast failed.  Flag the bytecode to not attempt the heroic opt
    // again.
  case Reason_intrinsic_check:
    // Math.pow intrinsic returned a NaN, which requires StrictMath.pow to
    // handle.  Recompile without intrinsifying Math.pow.  Or maybe
    // System.arraycopy must go slow; do not intrinsify call at this bci.
    // Intrisinc ops come from calls, which use CPData_Invoke which includes
    // CPData_Null.
  case Reason_athrow:     
    // Actually throwing.  Recompile catching/throwing as needed.
  case Reason_cast_check: 
    // Cast is actually failing.  Recompile catching/throwing as needed.
    ((CPData_Null*)cpd)->_fail = 1;
    break;

  case Reason_range_check: {
    // Method in question is actually throwing range check exceptions.
    // Recompile catching them.
methodOop moop=vf.method();
    Bytecodes::Code bc = (Bytecodes::Code)*moop->bcp_from(vf.bci());
    if (cpd->is_Null(bc)) {
      ((CPData_Null*)cpd)->_rchk = 1;
    }
    if (cpd->is_Branch(bc)) {
      Untested();
      CPData_Branch* cpdb = (CPData_Branch*)cpd;
      debug_only( assert(cpdb->is_Branch(), "Not a branch cpdata structure!") );
      if (cpdb->_taken==0) cpdb->_taken++;
      if (cpdb->_nottaken==0) cpdb->_nottaken++;
    }
    break;
  }

  case Reason_range_check_widened: {
    // We might deopt for a range check 'speculatively', if we've widened some
    // check in the method.  Recompile without the optimization
methodOop moop=vf.method();
    Bytecodes::Code bc = (Bytecodes::Code)*moop->bcp_from(vf.bci());
    if (cpd->is_Null(bc)) {
      ((CPData_Null*)cpd)->_rchk_wide = 1;
    }
    if (cpd->is_Branch(bc)) {
      Untested();
      CPData_Branch* cpdb = (CPData_Branch*)cpd;
      debug_only( assert(cpdb->is_Branch(), "Not a branch cpdata structure!") );
      if (cpdb->_taken==0) cpdb->_taken++;
      if (cpdb->_nottaken==0) cpdb->_nottaken++;
    }
    break;
  }

  default:
    ShouldNotReachHere();
  }
  // Recompile if needed
  return make_not_entrant;
}

static BasicType constant_pool_type(methodOop method, int index) {
  constantTag tag = method->constants()->tag_at(index);
       if (tag.is_int              ()) return T_INT;
  else if (tag.is_float            ()) return T_FLOAT;
  else if (tag.is_long             ()) return T_LONG;
  else if (tag.is_double           ()) return T_DOUBLE;
  else if (tag.is_string           ()) return T_OBJECT;
  else if (tag.is_unresolved_string()) return T_OBJECT;
  else if (tag.is_klass		   ()) return T_OBJECT;
  ShouldNotReachHere();
  return T_ILLEGAL;
}

// --- build_repack_buffer ---------------------------------------------------
// Build a IFrame structure to help ASM code repack the 1 compiled frame into
// many interpreter (or C1) frames.  Takes in the current thread and a vframe;
// the vframe is pointing and the virtual Java frame needing to be repacked.
// It takes in the callee (which this frame is busy trying to call in it's
// inlined code), and an array of IFrames.  It returns the updated IFrame
// buffer filled in for this frame.
void Deoptimization::build_repack_buffer( JavaThread *thread, frame fr, IFrame *buf, const DebugMap *dm, const DebugScope *ds, intptr_t *jexstk, objectRef *lckstk, bool is_deopt, bool is_c1, bool is_youngest) {
  assert( thread->_deopt_buffer->contains((char*)(buf+1)), "over-ran large deopt buffer?" );

int bci=ds->bci();
if(bci==InvocationEntryBci){
    // We deoptimized while hanging in prologue code for a synchronized
    // method.  We got the lock (after all, deopt happens after returning
    // from the blocking call).  We want to begin execution in the
    // interpreter at BCI 0, and after taking the lock.
    // Also it is possilble to enter the deopt code through the br_s on method
    // entry before the first byte code.
    bci = 0;
  }

  const methodOop moop = ds->method().as_methodOop();
  if( ds->caller() ) {          // Do I have a caller?  Am I mid-call?
    // Initialize the constant pool entry for caller-parameter size.  It
    // might be the case that we inlined and compiled a callee, and are busy
    // calling it in the compiled code, and get deoptimized with that callee
    // in-progress AND we've never executed it in the interpreter - which
    // would have filled in the constant pool cache before making the call.
    // Fill it in now.
    const methodOop caller = ds->caller()->method().as_methodOop();
    int index = Bytes::get_native_u2(caller->bcp_from(ds->caller()->bci())+1);
    ConstantPoolCacheEntry *cpe = caller->constants()->cache()->entry_at(index);
    // Since we are setting the constant pool entry here, and another thread
    // could be busy resolving here we have a race condition setting the
    // flags.  Use a CAS to only set the flags if they are currently 0.
    intx *flags_adr = (intx*)((intptr_t)cpe + in_bytes(ConstantPoolCacheEntry::flags_offset()));
    if( !*flags_adr ) {         // Flags currently 0?
      // Set the flags, because the interpreter-return-entry points need some
      // info from them.  Not all fields are set, because it's too complex to
      // do it here... and not needed.  The cpCacheEntry is left "unresolved"
      // such that the next real use of it from the interpreter will be forced
      // to do a proper resolve, which will fill in the missing fields.

      // Compute new flags needed by the interpreter-return-entry
      intx flags = 
        (moop->size_of_parameters() & 0xFF) | 
        (1 << ConstantPoolCacheEntry::hotSwapBit) |
        (moop->result_type() << ConstantPoolCacheEntry::tosBits);
      // CAS 'em in, but only if there is currently a 0 flags
      assert0( sizeof(jlong)==sizeof(intx) );
      Atomic::cmpxchg((jlong)flags, (jlong*)flags_adr, 0);
      // We don't care about the result, because the cache is monomorphic.
      // Either our CAS succeeded and jammed    the right parameter count, or
      // another thread succeeded and jammed in the right parameter count.
    } 
  }

  if (TraceDeoptimization) {
    BufferedLoggerMark m(NOTAG, Log::M_DEOPT, TraceDeoptimization, true);
    m.out("DEOPT REPACK c%d: ", is_c1 ? 1 : 2);
    moop->print_short_name(m.stream());
    m.out(" @ bci %d %s", bci, ds->caller() ? "called by...": "   (oldest frame)" );
  }

  // If there was a suitable C1 frame, use it.
  // Otherwise, use an interpreter frame.
  if( 1 ) {
    // Build an interpreter-style IFrame.  Naked oops abound.
    assert0( !objectRef(moop).is_stack() );
    buf->_mref = objectRef(moop);
    buf->_cpc = moop->constants()->cacheRef();

    // Compute monitor list length.  If we have coarsened a lock we will end
    // up unlocking it and the repack buffer will not need to see it.
    uint mons_len = ds->numlocks();
    if( ds->is_extra_lock() ) { mons_len--; assert0( mons_len >= 0 ); }
    assert0( mons_len < (256*sizeof(buf->_numlck)) );
    buf->_numlck = mons_len;
    
    // Set up the return pc for the next frame: the next frame is a younger
    // frame which will return to this older frame.  All middle frames return
    // back into the interpreter, just after a call with proper TOS state.
    // Youngest frames always start in vtos state because the uncommon-trap
    // blob sets them up that way.
    const address bcp = moop->bcp_from(bci);
    Bytecodes::Code c = Bytecodes::java_code(Bytecodes::cast(*bcp));
BasicType return_type=T_VOID;

    bool handle_popframe = is_youngest && JvmtiExport::can_pop_frame() && thread->popframe_forcing_deopt_reexecution();

    int bci_bump = 0;
    if( !is_youngest ) {        // Middle-frame?
      bool from_call = (c == Bytecodes::_invokevirtual ||
c==Bytecodes::_invokespecial||
c==Bytecodes::_invokestatic||
                        c == Bytecodes::_invokeinterface );
assert(from_call,"Middle frame is in the middle of a call");
      bci_bump = Bytecodes::length_at(bcp); // But need to know how much it will be bumped for the return address
      buf->_bci = bci;          // Save bci without bumping it; normal interpreter call returns bump the bci as needed
      buf[-1]._retadr = Interpreter::return_entry(vtos, bci_bump);

    } else if( thread->pending_exception() ) { 
      // Deopt-with-pending.  Throw up on return to interpreter, which is
      // handled by unpack_and_go.
buf->_bci=bci;
      buf[-1]._retadr = Interpreter::unpack_and_go();

    } else if( !is_deopt ) {    // It is a C2-style uncommon-trap.
      // Do NOT increment the BCP!  We are re-executing the current bytecode.
buf->_bci=bci;
      buf[-1]._retadr = Interpreter::unpack_and_go();
      
    } else {                    // It is a plain deopt
      // It is a deopt without exception.  See if we are C1 in mid-patch.
      // If so, we always need to re-execute the bytecode.
      bool is_C1_mid_patch = false;
      if( is_c1 ) {             // C1 codeblob?
address caller_pc=fr.pc();
if(NativeCall::is_call_before(caller_pc)){
          address target = nativeCall_at(caller_pc)->destination();
          is_C1_mid_patch = target == Runtime1::entry_for(Runtime1::load_klass_patching_id);
        }
      }
      if( is_C1_mid_patch ) {
        Untested("");
        // Do NOT increment the BCP!  We are re-executing the current bytecode.
      } else if( ds->bci() == InvocationEntryBci ) {
        // It is deopt while hanging on a method-entry lock.
        // Do not advance BCP, as we have not executed bci 0 yet.
        
      } else {                  // Else C2 or C1-not-mid-patch
        // It is a deopt.  Whether we re-execute the current bytecode or
        // assume it has completed depends on the bytecode.
        switch( c ) {
case Bytecodes::_lookupswitch:
case Bytecodes::_tableswitch:
case Bytecodes::_fast_binaryswitch:
        case Bytecodes::_fast_linearswitch:
          // recompute condtional expression folded into _if<cond>
        case Bytecodes::_lcmp      :
        case Bytecodes::_fcmpl     :
        case Bytecodes::_fcmpg     :
        case Bytecodes::_dcmpl     :
        case Bytecodes::_dcmpg     :
        case Bytecodes::_ifnull    :
        case Bytecodes::_ifnonnull :
        case Bytecodes::_goto      :
        case Bytecodes::_goto_w    :
        case Bytecodes::_ifeq      :
        case Bytecodes::_ifne      :
        case Bytecodes::_iflt      :
        case Bytecodes::_ifge      :
        case Bytecodes::_ifgt      :
        case Bytecodes::_ifle      :
        case Bytecodes::_if_icmpeq :
        case Bytecodes::_if_icmpne :
        case Bytecodes::_if_icmplt :
        case Bytecodes::_if_icmpge :
        case Bytecodes::_if_icmpgt :
        case Bytecodes::_if_icmple :
        case Bytecodes::_if_acmpeq :
        case Bytecodes::_if_acmpne :
          // special cases
case Bytecodes::_aastore:
          // We are re-executing the current bytecode.
          Untested("");
          break;
          // special cases
case Bytecodes::_putstatic:
case Bytecodes::_getstatic:
case Bytecodes::_getfield:
case Bytecodes::_putfield:
          // We are re-executing the current bytecode.
          break;
        case Bytecodes::_athrow    :
          break;                // Must be deopt-w-exception
case Bytecodes::_invokevirtual:
case Bytecodes::_invokespecial:
case Bytecodes::_invokestatic:{
methodHandle mh(thread,moop);
return_type=Bytecode_invoke_at(mh,bci)->result_type(thread);
          if( !handle_popframe &&
              !ds->should_reexecute()) 
            bci_bump = 3; // Increment the BCP to post-call!!!  See below!
          break;
        }
case Bytecodes::_invokeinterface:{
methodHandle mh(thread,moop);
return_type=Bytecode_invoke_at(mh,bci)->result_type(thread);
          if( !handle_popframe &&
              !ds->should_reexecute()) 
            bci_bump = 5; // Increment the BCP to post-call!!!  See below!
          break;
        }
        case Bytecodes::_ldc   : 
          Untested("");
return_type=constant_pool_type(moop,*(bcp+1));
          if( !ds->should_reexecute()) bci_bump = 2; // Increment the BCP to post-call!!!  See below!
          break;
          
        case Bytecodes::_ldc_w : // fall through
        case Bytecodes::_ldc2_w: 
return_type=constant_pool_type(moop,Bytes::get_Java_u2(bcp+1));
          if( !ds->should_reexecute()) bci_bump = 3; // Increment the BCP to post-call!!!  See below!
          break;
          
        default:
return_type=Bytecodes::result_type(c);
          if( !ds->should_reexecute()) bci_bump = Bytecodes::length_at(bcp); // Increment the BCP to post-call!!!  See below!
          break;
        }
        if (ds->should_reexecute()) return_type = T_VOID;
      }
      // Save (possibly advanced) bci
      buf->_bci = bci+bci_bump;
      buf[-1]._retadr = Interpreter::unpack_and_go(); // Interpreter::return_entry(vtos, bci_bump);
    }

    // ---
    // Now all the Java locals.
    // First set the start of locals for the interpreter frame we are building.
    buf->_loc = (intptr_t)jexstk;

    uint loc_len = moop->max_locals();
for(uint i=0;i<loc_len;i++){
      *jexstk++ = dm->get_value(ds->get_local(i),fr);
    }

    // Now that the locals have been unpacked if we have any deferred local writes
    // added by jvmti then we can free up that structure as the data is now in the
    // buffer
    GrowableArray<jvmtiDeferredLocalVariableSet*>* list = thread->deferred_locals();
    if( list ) {
      // Because of inlining we could have multiple vframes for a single frame
      // and several of the vframes could have deferred writes. Find them all.
      Unimplemented();
    }

    // ---
    // Now all the Java Expressions
    uint expr_len = ds->numstk();
for(uint i=0;i<expr_len;i++)
      *jexstk++ = dm->get_value(ds->get_expr(i),fr);

    // If returning from a deoptimized call, we will have return values in
    // registers that need to end up on the Java execution stack.  They are
    // not recorded in the debug info, since they did not exist at the time
    // the call began.
    if( is_youngest && is_deopt ) { 
      if( type2size[return_type] > 0 ) {
        if( type2size[return_type]==2 ) {
          *jexstk++ = (intptr_t)frame::double_slot_primitive_type_empty_slot_id << 32;
        }
        *jexstk++ = pd_fetch_return_values( thread, return_type );
        // Need to adjust the final jexstk_top for the youngest frame
        // returning values.  These returned values are not accounted for in
        // the standard debug info.
        thread->_jexstk_top = jexstk;
      }
    }

    // JVMTI PopFrame support
    // Add the number of words of popframe preserved args to expr_len
    int popframe_preserved_args_size_in_bytes = in_bytes(thread->popframe_preserved_args_size());
    int popframe_preserved_args_size_in_words = in_words(thread->popframe_preserved_args_size_in_words());
    if (handle_popframe) {
      Unimplemented();
      expr_len += popframe_preserved_args_size_in_words;
      // An interpreted frame was popped but it returns to a deoptimized
      // frame. The incoming arguments to the interpreted activation
      // were preserved in thread-local storage by the
      // remove_activation_preserving_args_entry in the interpreter; now
      // we put them back into the just-unpacked interpreter frame.
      // Note that this assumes that the locals arena grows toward lower
      // addresses.
    }

    // Set the JEX stk top
    buf->_stk = (intptr_t)jexstk;

    // --- 
    // Now move locked objects to the interpreters lock-stack.
    // No need to inflate anything, as we're moving standard oops.
    int numlcks = ds->numlocks();
    if( ds->is_extra_lock() ) { // coarsened a lock
      Untested("");
      // The last lock is "coarsened" - kept locked when it should have been
      // unlocked and relocked.  With no deopt, keeping it locked saves the 2
      // sets of back-to-back CAS's and fences.  However, here we need to
      // unlock it to match the proper Java state.
      ObjectSynchronizer::unlock(ALWAYS_POISON_OBJECTREF((objectRef)dm->get_value(ds->get_lock(numlcks-1),fr)).as_oop());
      numlcks--;
    }
for(int i=0;i<numlcks;i++){
      *lckstk++ = ALWAYS_POISON_OBJECTREF((objectRef)dm->get_value(ds->get_lock(i),fr));
    }

  } else {                    // Make a C1 frame
    
    Unimplemented();
    
  }
}

// Do nothing special for stack derived pointers, they will follow their base
// during the evacuation, if needed.
static void do_nothing_derived(objectRef* base, objectRef* derived) { /*nothing*/ }

XRT_ENTRY_EX_NO_GC_ON_EXIT(address, Deoptimization, deoptimize, (JavaThread* thread)) {
  return uncommon_trap(thread,Reason_deopt);
} JRT_END

XRT_ENTRY_EX_NO_GC_ON_EXIT(address, Deoptimization, uncommon_trap, (JavaThread* thread, jint deopt_index)) {
  DeoptimizationMarker doptmrk; // Flag deopt-in-progress for profiler
ResourceMark rm;//Allocation and handles are used at least
HandleMark hm;//by the ScopeDesc in the vframes.
  assert( deopt_index < Reason_max, "" );
  const bool is_deopt = (deopt_index == Reason_deopt);

  assert0( !thread->has_pending_exception() || is_deopt );

  // Figure out some basic stuff
  frame stub_frame = thread->last_frame();
  frame deopt_frame = stub_frame.sender();
  const CodeBlob* cb = CodeCache::find_blob(deopt_frame.pc());
  const bool is_c1 = cb->is_c1_method();

  Log::log4(NOTAG, Log::M_DEOPT, "Uncommon trap occurred @" INTPTR_FORMAT " deopt_index = %d", deopt_frame.pc(), deopt_index);
  if (TraceDeoptimization) {  
    BufferedLoggerMark m(NOTAG, Log::M_DEOPT, TraceDeoptimization, true);
    m.out("Uncommon trap #%d occurred in ", (++unload_count)+0);
    cb->method().as_methodOop()->print_short_name(m.stream());
    m.out(" (@0x%lx) thread = %d, deopt_index = %s", deopt_frame.pc(), thread->osthread()->thread_id(), 
          names[deopt_index<0?Reason_unloaded:deopt_index]);
#ifndef PRODUCT
    if ((unload_count_stop != 0) && (unload_count == unload_count_stop)) os::breakpoint();
#endif // PRODUCT 
  }

  if (UseSBA && VerifySBA) thread->sba_area()->verify();

  // For long running C1 loops, C1 bails out with a stuck_in_loop.  The goal
  // is to get the thread running in a C2 OSR method as soon as possible,
  // also start a C2 normal compile.  However, starting a compile takes
  // the compile lock and can deopt.
  if ( deopt_index == Reason_stuck_in_loop ) {
    CompilationPolicy::method_invocation_event(cb->method(), 2, NULL);
  }

  // Record the failure reason and decide if we should recompile the CodeBlob
  if( policy_for_recompile(thread, cb, deopt_index) ) {
MutexLockerAllowGC ml(Compile_lock,thread);
    cb->owner().as_methodCodeOop()->not_entrant();
  }

  // We stuff naked oops into the deopt buffer, so no more GCs allowed
  No_Safepoint_Verifier nsv; // Do not expect a Safepoint anywhere after here

  // Get a big buffer to play in from the ChunkPool.  
  // It's so big I do not need to re-size it dynamically.
  if( !thread->_deopt_buffer ) 
    thread->_deopt_buffer = new (Chunk::medium_size) Chunk(Chunk::medium_size);
  char *buf = thread->_deopt_buffer->bottom();
  ((int*)buf)[0] = cb->framesize_bytes();
  ((int*)buf)[1] = 0; // num_iframes*sizeof(IFrame);
  ((intptr_t*)buf)[1] = (intptr_t)0xdeadbeef;

  // Compute the max JEXSTK needed here.  IFrames are built in the
  // order they go on the C-stack from youngest to oldest.  But JEXSTK
  // grows from low to high memory so the youngest frame (processed
  // first by build_repack_buffer) needs the highest memory addresses.
  // Compute that high address to pass in to build_repack_buffer.
  intptr_t *jexstk = thread->_jexstk_top;
  const DebugMap *dm = cb->debuginfo();
  const DebugScope *ds0 = cb->debuginfo(deopt_frame.pc());
  int numlcks = 0;              // locks also need to be inverted, count them all
  for( const DebugScope *ds = ds0; ds; ds = ds->caller() ) {
    jexstk += ds->method().as_methodOop()->max_locals()+ds->numstk();
    numlcks += ds->numlocks();
  }
  thread->_jexstk_top = jexstk;

  // extend lock-stack to handle all locks for all inline levels
  objectRef *lckstk = thread->_lckstk_top+numlcks;
  while( lckstk > thread->_lckstk_max ) {
JavaThread::extend_lckstk(thread);
    lckstk = thread->_lckstk_top+numlcks;
  }
  thread->_lckstk_top = lckstk; // set to the proper value with all locks pushed on

  // Build a description of how the ASM code should re-pack the stack
  IFrame *ifr0 = (IFrame*)(buf+2*sizeof(intptr_t));
  IFrame *ifr  = ifr0;
  bool is_youngest = true;
  for( const DebugScope *ds = ds0; ds; ds = ds->caller() ) {
    jexstk -= ds->method().as_methodOop()->max_locals()+ds->numstk();
    lckstk -= ds->numlocks();   // push locks on frame-by-frame
    build_repack_buffer( thread, deopt_frame, ifr++, dm, ds, jexstk, lckstk, is_deopt, is_c1, is_youngest );
    is_youngest = false;
  }
#ifdef ASSERT // Verify that interpreter stack looks sane following deopt
  for( intptr_t *optr = JavaThread::jexstk_base_for_sp(jexstk); optr < thread->_jexstk_top; optr++ ) {
    switch (frame::tag_at_address(optr)) {
    case frame::single_slot_primitive_type:         break;
    case frame::double_slot_primitive_type: optr++; break;
    case frame::single_slot_ref_type:       assert( SharedRuntime::verify_oop(objectRef(*optr)), "bad oop!" ); break;
    default:  ShouldNotReachHere();
    }
  }
#endif
  // Set final return_pc in the IFrame
  ifr[-1]._retadr = deopt_frame.sender().pc();
  ifr[ 0]._retadr = (address)0xf00ff00fcafebabeLL; // No-IFrame-Here crash-n-burn marker
  ((int*)buf)[1] = (char*)ifr - (char*)ifr0;

  // Load out the callee-save registers from the disappearing 'deopt_frame'
  // and jam them into the stub_frame's stack.  When the stub unwinds, the
  // callee-save registers will be restored to their callee-save values.
  ds0->restore_callee_saves(deopt_frame, stub_frame );

  if (JvmtiExport::can_pop_frame()) {
    // Regardless of whether we entered this routine with the pending
    // popframe condition bit set, we should always clear it now
    thread->clear_popframe_condition();
  }
  
  return (address)thread->_deopt_buffer->bottom();
  
} JRT_END


// JVMTI PopFrame support
JRT_LEAF(void, Deoptimization, popframe_preserve_args, (JavaThread* thread, int bytes_to_save, void* start_address))

  thread->popframe_preserve_args(in_ByteSize(bytes_to_save), start_address);

JRT_END


//-----------------------------------------------------------------------------

const char *Deoptimization::names[Reason_max] = {
"BAD",
"deoptimization",
"unloaded",
"unreached",
"athrow",
"null_check",
"div0_check",
"range_check",
"range_check_widened",
"cast_check",
  "unhandled",
"array_store_check",
"uninitialized",
"unloaded_array_class",
"unexpected_klass",
"unexpected_null_cast",
"intrinsic_check",
"install_async_ex",
"new_exception",
"jvmti",
"static_in_clinit",
"stuck_in_loop",
"c1_patch_volatile_field"
};

long Deoptimization::counters[Reason_max];

jint Deoptimization::deoptimization_count(DeoptReason reason) {
assert(reason>=0&&reason<Reason_max,"oob");
return counters[reason];
}

void Deoptimization::increment_deoptimization_count(DeoptReason reason) {
assert(reason>=0&&reason<Reason_max,"oob");
  counters[reason]++;
}

void Deoptimization::print_stat_line(int i, long total) {
  if( counters[i] == 0 ) return;
  tty->print_cr("  %s: %ld (%ld%%)", names[i], counters[i], (counters[i] * 100) / total);
}

void Deoptimization::print_statistics() {
  long total = 0;
for(int i=0;i<Reason_max;i++)
    total += counters[i];
  if( total == 0 ) return;
  ttyLocker ttyl;
tty->print_cr("Total number of deoptimizations: %ld",total);
for(int i=0;i<Reason_max;i++)
    print_stat_line(i,total);
}

void Deoptimization::print_statistics(xmlBuffer *xb) {
  xmlElement xe(xb, "deoptimization_causes");
  xmlElement xf(xb, "name_value_table");
for(int i=0;i<Reason_max;i++)
    xb->name_value_item(names[i], counters[i]);
}

