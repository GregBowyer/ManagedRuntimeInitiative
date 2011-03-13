/*
 * Copyright 1998-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "callGenerator.hpp"
#include "callnode.hpp"
#include "cfgnode.hpp"
#include "ciStreams.hpp"
#include "compile.hpp"
#include "deoptimization.hpp"
#include "parse.hpp"
#include "runtime.hpp"
#include "sharedRuntime.hpp"
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
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

CallGenerator* Compile::call_generator(ciMethod* call_method, int vtable_index, bool call_is_virtual, JVMState* jvms, bool allow_inline, float prof_factor, CodeProfile *callee_cp, int callee_cp_inloff, CPData_Invoke* c2_caller_cpd, CPData_Invoke *caller_cpd) {
  // Special case the handling of certain common, profitable library
  // methods.  If these methods are replaced with specialized code,
  // then we return it as the inlined version of the call.
  // We do this before the strict f.p. check below because the
  // intrinsics handle strict f.p. correctly.
if(allow_inline&&!caller_cpd->did_fail()){
CallGenerator*cg=find_intrinsic(call_method,call_is_virtual);
if(cg!=NULL){
if(c2_caller_cpd!=NULL)
        c2_caller_cpd->_inlining_failure_id = IF_INLINEDINTRINSIC;
      return cg;
    }
  }

  // Do not inline strict fp into non-strict code, or the reverse
  bool caller_method_is_strict = jvms->method()->is_strict();
  if( caller_method_is_strict ^ call_method->is_strict() ) {
    allow_inline = false;
  }

  // See how many times this site has been invoked.
  float site_count   = UseC1 ? caller_cpd->site_count()  *prof_factor : 1.0;
  float callee_count = UseC1 ? caller_cpd->callee_count()*prof_factor : 0;

  // Attempt to inline...
  if (allow_inline) {
float past_uses=site_count;//computed above based on if C1 is in use or not
    // This is the number of times we expect the call code to be used.
    float expected_uses = past_uses;

    // Try inlining a bytecoded method:
    if (!call_is_virtual) {
      InlineTree* ilt;
      if (UseOldInlining) {
        ilt = InlineTree::find_subtree_from_root(this->ilt(), jvms->caller(), jvms->method());
      } else {
        Unimplemented();
        // Make a disembodied, stateless ILT.
        // TO DO:  When UseOldInlining is removed, copy the ILT code elsewhere.
        float site_invoke_ratio = prof_factor;
        // Note:  ilt is for the root of this parse, not the present call site.
        ilt = new InlineTree(this, jvms->method(), jvms->caller(), site_invoke_ratio);
      }
      WarmCallInfo scratch_ci;
      if (!UseOldInlining)
scratch_ci.init(jvms,call_method,site_count,prof_factor,caller_cpd);
WarmCallInfo*ci=ilt->ok_to_inline(c2_caller_cpd,caller_cpd,call_method,jvms,site_count,&scratch_ci);
      assert(ci != &scratch_ci, "do not let this pointer escape");
      bool allow_inline   = (ci != NULL && !ci->is_cold());
      bool require_inline = (allow_inline && ci->is_hot());

      if (allow_inline) {
CallGenerator*cg=CallGenerator::for_inline(call_method,callee_cp,callee_cp_inloff,expected_uses);
        if (cg == NULL) {
          // Cannot inline.  Fall through & try something else.
        } else if (require_inline || !InlineWarmCalls) {
          if (c2_caller_cpd) c2_caller_cpd->_inlining_failure_id = IF_INLINEHOT;
          return cg;
        } else {
          // Recursively call on self, but with inlining turned off
          CallGenerator* cold_cg = call_generator(call_method, vtable_index, call_is_virtual, jvms, false, prof_factor, callee_cp, callee_cp_inloff, c2_caller_cpd, caller_cpd);
          return CallGenerator::for_warm_call(ci, cold_cg, cg);
        }
      }
    }

    // Try using the type profile.
if(call_is_virtual&&site_count>0&&callee_count>0){
      assert(UseC1 && UseTypeProfile, "Should not be here..");
      uint largest_callee_idx = caller_cpd->get_largest_callee_index();
      float largest_count = caller_cpd->_callee_histogram_count[largest_callee_idx] * prof_factor;
      int most_common_kid = caller_cpd->_callee_histogram_klassids[largest_callee_idx];

      ciKlass* ik = ciKlass::make_from_klassId(most_common_kid);
      if (ik && ik->is_instance_klass() && ik->is_subtype_of(call_method->holder())) {
        ciMethod* receiver_method = call_method->resolve_invoke(jvms->method()->holder(), ik);
        if (receiver_method != NULL) {
          // The single majority receiver sufficiently outweighs the minority.
          // Recursively call on self, but with the named target receiver & inlining encouraged.
          CodeProfile* hit_callee_cp = receiver_method->codeprofile(true);

          // See if any of these main heuristics will fire.  If none will,
          // then do not bother attempting any of the expensive (and noisy
          // printing) inline tests in the call_generators.  This will mostly
          // prevent PrintInlining from printing if we decide to bail out at
          // this top-level anyways.
          if( hit_callee_cp &&  // Have a caller
              // Will we inline as hot-inline/unk_trap?
              ((largest_count >= PROB_ALWAYS * callee_count && !caller_cpd->did_poly_inlining_fail()) ||
               // Will we inline as hot-inline/v-call?
               (largest_count >= .9 * callee_count) ||
               // Will we inline as hot-inline/2nd-hot-inline/v-call?
               (UseBimorphicInlining &&
                largest_count <= .9 * callee_count && 
                caller_cpd->num_callees()==2)) ) {

Compile::current()->record_cloned_cp(hit_callee_cp);
            CallGenerator* hit_cg = call_generator(receiver_method, vtable_index, !call_is_virtual, jvms, /*allow_inline=*/ 1, prof_factor, hit_callee_cp, /*callee_cp_inloff=*/ 0, c2_caller_cpd, caller_cpd);
            if (hit_cg && hit_cg->is_inline()) {
CallGenerator*miss_cg=NULL;
              if (largest_count >= PROB_ALWAYS * callee_count && !caller_cpd->did_poly_inlining_fail()) {
                // Uncommon trap loser
                miss_cg = CallGenerator::for_uncommon_trap(call_method,Deoptimization::Reason_unexpected_klass,"TypeProfile failure",false);
              } 
              else if (largest_count >= .9 * callee_count) {
                // Polymorphicize loser
                miss_cg = CallGenerator::for_virtual_call(call_method, vtable_index);
              }
              // On an 80-20 split between 2 receivers, go ahead an inline both
              else if (UseBimorphicInlining &&
                       largest_count <= .9 * callee_count && 
                       caller_cpd->num_callees()==2) {
                // Attempt bimorphic inlining
                int second_most_common_kid = caller_cpd->_callee_histogram_klassids[1-largest_callee_idx];
assert(second_most_common_kid>0,"Didn't get a valid kid");
                if (second_most_common_kid > 0) {
                  ciKlass* sik = ciKlass::make_from_klassId(second_most_common_kid);
                  if (sik && sik->is_instance_klass() && sik->is_subtype_of(call_method->holder())) {
                    ciMethod* second_receiver_method = call_method->resolve_invoke(jvms->method()->holder(), sik);
                    if (second_receiver_method) {
                      CodeProfile* second_callee_cp = second_receiver_method->codeprofile(true);
                      if (second_callee_cp) {
                        miss_cg = call_generator(second_receiver_method, vtable_index, !call_is_virtual, jvms, /*allow_inline=*/ 1, prof_factor, second_callee_cp, /*callee_cp_inloff=*/ 0, c2_caller_cpd, caller_cpd);
                        if (miss_cg != NULL) {
                          float prob = 1.0 - (float)largest_count/site_count;
                          if (prob > PROB_MAX) prob = PROB_MAX;
                          if (prob < PROB_MIN) prob = PROB_MIN;
                          CallGenerator *cg = CallGenerator::for_bimorphic_predicted_call(ik, sik, miss_cg, hit_cg, prob);
                          //C2OUT->print_cr("***** bimorphic call to %s.%s in %s.%s! %d %d *****", call_method->holder()->name()->as_utf8(), call_method->name()->as_utf8(), method()->holder()->name()->as_utf8(), method()->name()->as_utf8(), caller_cpd->_callee_histogram_count[0], caller_cpd->_callee_histogram_count[1]);
                          if (cg != NULL) return cg;

                          // We failed to generate the call, so back out our miss_cg
miss_cg=NULL;
                        }
                      }
                    }
                  }
                }
              }
              else {
                if (c2_caller_cpd) c2_caller_cpd->_inlining_failure_id = IF_POLYMORPHICNOWINNER;
              }

              if (miss_cg != NULL) {
                float prob = 1.0 - (float)largest_count/site_count;
                if (prob > PROB_MAX) prob = PROB_MAX;
                if (prob < PROB_MIN) prob = PROB_MIN;
                CallGenerator *cg = CallGenerator::for_predicted_call(ik, miss_cg, hit_cg, prob);
                if (cg != NULL) return cg;
              }
            }
          } // End of mega inlining test
        }
      }
    }
  }

  if (c2_caller_cpd && c2_caller_cpd->_inlining_failure_id==0) c2_caller_cpd->_inlining_failure_id = IF_POLYMORPHIC;

  // There was no special inlining tactic, or it bailed out.
  // Use a more generic tactic, like a simple call.
  if (call_is_virtual) {
    return CallGenerator::for_virtual_call(call_method, vtable_index);
  } else {
    // Class Hierarchy Analysis or Type Profile reveals a unique target,
    // or it is a static or special call.
    return CallGenerator::for_direct_call(call_method);
  }
}


// uncommon-trap call-sites where callee is unloaded, uninitialized or will not link
bool Parse::can_not_compile_call_site(ciMethod *dest_method, ciInstanceKlass* klass) {
  // Additional inputs to consider...
  // bc      = bc()
  // caller  = method()
  // iter().get_method_holder_index()
  assert( dest_method->is_loaded(), "ciTypeFlow should not let us get here" );
  // Interface classes can be loaded & linked and never get around to
  // being initialized.  Uncommon-trap for not-initialized static or
  // v-calls.  Let interface calls happen.
  ciInstanceKlass* holder_klass  = dest_method->holder();
  if (!holder_klass->is_initialized() &&
      !holder_klass->is_interface()) {

    if( method()->is_static() && method()->name() == ciSymbol::class_initializer_name() )
      return false;             // OK to inline inside of <clinit>
    if( method()->name() == ciSymbol::object_initializer_name() )
      // because any thread calling the constructor must first have
      // synchronized on the class by executing a '_new' bytecode.
      return false;

    // Here we have decided that we cannot make the call because the method
    // holder is not initialized.  We can still check for a null-receiver at
    // runtime and throw the NPE - which avoids a deopt if the user is
    // expecting the NPE.
if(!dest_method->is_static()){
int nargs=1+dest_method->signature()->size();
assert(sp()>=nargs,"stack accepts only positive values");
Node*receiver=stack(sp()-nargs);
      receiver = do_null_check(receiver, T_OBJECT, "nullchk receiver");
    }
    uncommon_trap(Deoptimization::Reason_uninitialized,holder_klass,"call site where called method holder is not initialized",false);
    return true;
  }

  assert(dest_method->will_link(method()->holder(), klass, bc()), "dest_method: typeflow responsibility");
  return false;
}


//------------------------------do_call----------------------------------------
// Handle your basic call.  Inline if we can & want to, else just setup call.
void Parse::do_call() {
  // It's likely we are going to add debug info soon.
  // Also, if we inline a guy who eventually needs debug info for this JVMS,
  // our contribution to it is cleaned up right here.
  kill_dead_locals();

  // Set frequently used booleans
  bool is_virtual = bc() == Bytecodes::_invokevirtual;
  bool is_virtual_or_interface = is_virtual || bc() == Bytecodes::_invokeinterface;
  bool has_receiver = is_virtual_or_interface || bc() == Bytecodes::_invokespecial;

  // Find target being called
  bool             will_link;
  ciMethod*        dest_method   = iter().get_method(will_link);
  ciInstanceKlass* holder_klass  = dest_method->holder();
  ciKlass* holder = iter().get_declared_method_holder();
  ciInstanceKlass* klass = ciEnv::get_instance_klass_for_declared_method_holder(holder);

  int   nargs    = dest_method->arg_size();
  // See if the receiver (if any) is NULL, hence we always throw BEFORE
  // attempting to resolve the call or initialize the holder class.  Doing so
  // out of order opens a window where we can endlessly deopt because the call
  // holder is not initialized, but the call never actually happens (forcing
  // class initialization) because we only see NULL receivers.
  CPData_Invoke *caller_cpdi = cpdata()->as_Invoke(bc());
  debug_only( assert(caller_cpdi->is_Invoke(), "Not invoke!") );
  if( is_virtual_or_interface &&
      _gvn.type(stack(sp() - nargs))->higher_equal(TypePtr::NULL_PTR) ) {
    builtin_throw( Deoptimization::Reason_null_check, "null receiver", caller_cpdi, caller_cpdi->saw_null(), /*must_throw=*/true );
    return;
  }

  // uncommon-trap when callee is unloaded, uninitialized or will not link
  // bailout when too many arguments for register representation
  if (!will_link || can_not_compile_call_site(dest_method, klass)) {
    return;
  }
assert(FAM||holder_klass->is_loaded(),"");
  assert(dest_method->is_static() == !has_receiver, "must match bc");
  // Note: this takes into account invokeinterface of methods declared in java/lang/Object,
  // which should be invokevirtuals but according to the VM spec may be invokeinterfaces
  assert(holder_klass->is_interface() || holder_klass->super() == NULL || (bc() != Bytecodes::_invokeinterface), "must match bc");
  // Note:  In the absence of miranda methods, an abstract class K can perform
  // an invokevirtual directly on an interface method I.m if K implements I.

  // ---------------------
  // Does Class Hierarchy Analysis reveal only a single target of a v-call?
  // Then we may inline or make a static call, but become dependent on there being only 1 target.
  // Does the call-site type profile reveal only one receiver?
  // Then we may introduce a run-time check and inline on the path where it succeeds.
  // The other path may uncommon_trap, check for another receiver, or do a v-call.

  // Choose call strategy.
  bool call_is_virtual = is_virtual_or_interface;
  int vtable_index = methodOopDesc::invalid_vtable_index;
  ciMethod* call_method = dest_method;

  // Try to get the most accurate receiver type
  if (is_virtual_or_interface) {
    Node*             receiver_node = stack(sp() - nargs);
const TypeInstPtr*inst_type=_gvn.type(receiver_node)->isa_instptr();
    if( inst_type ) {
ciInstanceKlass*ikl=inst_type->klass()->as_instance_klass();
      // If the receiver is not yet linked then: (1) we never can make this
      // call because no objects can be created until linkage, and (2) CHA
      // reports incorrect answers... so do not bother with making the call
      // until after the klass gets linked.
      ciInstanceKlass *ikl2 = ikl->is_subtype_of(klass) ? ikl : klass;
if(!ikl->is_linked()){
        uncommon_trap(Deoptimization::Reason_uninitialized,klass,"call site where receiver is not linked",false);
        return;
      }
    }
    const TypeOopPtr* receiver_type = _gvn.type(receiver_node)->isa_oopptr();
    ciMethod* optimized_virtual_method = optimize_inlining(method(), bci(), klass, dest_method, receiver_type);

    // Have the call been sufficiently improved such that it is no longer a virtual?
    if (optimized_virtual_method != NULL) {
      call_method     = optimized_virtual_method;
      call_is_virtual = false;
    } else if (false) {
      // We can make a vtable call at this site
      vtable_index = call_method->resolve_vtable_index(method()->holder(), klass);
    }
  }

  // Note:  It's OK to try to inline a virtual call.
  // The call generator will not attempt to inline a polymorphic call
  // unless it knows how to optimize the receiver dispatch.
bool try_inline=(C->do_inlining()||InlineAccessors)&&
                    (!C->method()->should_disable_inlining()) &&
                    (call_method->number_of_breakpoints() == 0);

  // Get profile data for the *callee*.  First see if we have precise
  // CodeProfile for this exact inline because C1 inlined it already.
  CodeProfile *callee_cp;
  int callee_cp_inloff;

  if( caller_cpdi->inlined_method_oid() == call_method->objectId() ) {
    callee_cp = c1_cp();        // Use same CodeProfile as current
    callee_cp_inloff = caller_cpdi->cpd_offset(); // But use inlined portion
  } else {
    // If callee has a cp, clone it and use
    callee_cp = call_method->codeprofile(true);
    callee_cp_inloff = 0;

    if (callee_cp || FAM) {
      // The cloned cp needs to be freed later
      Compile* C = Compile::current();
      C->record_cloned_cp(callee_cp);
    } else { // Had profile info at top level, but not for this call site?
      // callee_cp will hold the just created cp, or whatever cp allocated by
      // other thread which wins the race in set_codeprofile
      callee_cp = call_method->set_codeprofile(CodeProfile::make(call_method));
    }
  }

  CPData_Invoke *c2_caller_cpdi = UseC1 ? c2cpdata()->as_Invoke(bc()) : NULL;

  // ---------------------
  inc_sp(- nargs);              // Temporarily pop args for JVM state of call
  JVMState* jvms = sync_jvms();

  // ---------------------
  // Decide call tactic.
  // This call checks with CHA, the interpreter profile, intrinsics table, etc.
  // It decides whether inlining is desirable or not.
CallGenerator*cg=C->call_generator(call_method,vtable_index,call_is_virtual,jvms,try_inline,prof_factor(),callee_cp,callee_cp_inloff,c2_caller_cpdi,caller_cpdi);

  // ---------------------
  // Round double arguments before call
  round_double_arguments(dest_method);

#ifndef PRODUCT
  // Record first part of parsing work for this call
  parse_histogram()->record_change();
#endif // not PRODUCT

  assert(jvms == this->jvms(), "still operating on the right JVMS");
  assert(jvms_in_sync(),       "jvms must carry full info into CG");

  // save across call, for a subsequent cast_not_null.
  Node* receiver = has_receiver ? argument(0) : NULL;

  JVMState* new_jvms = cg->generate(jvms, caller_cpdi, is_private_copy());
  if( new_jvms == NULL ) {      // Did it work?
    // When inlining attempt fails (e.g., too many arguments),
    // it may contaminate the current compile state, making it
    // impossible to pull back and try again.  Once we call
    // cg->generate(), we are committed.  If it fails, the whole
    // compilation task is compromised.
    if (failing())  return;
    if (PrintOpto || PrintInlining || PrintC2Inlining) {
      // Only one fall-back, so if an intrinsic fails, ignore any bytecodes.
      if (cg->is_intrinsic() && call_method->code_size() > 0) {
C2OUT->print("Bailed out of intrinsic, will not inline: ");
        call_method->print_name(C2OUT); C2OUT->cr();
      }
    }
    // This can happen if a library intrinsic is available, but refuses
    // the call site, perhaps because it did not match a pattern the
    // intrinsic was expecting to optimize.  The fallback position is
    // to call out-of-line.
    try_inline = false;  // Inline tactic bailed out.
cg=C->call_generator(call_method,vtable_index,call_is_virtual,jvms,try_inline,prof_factor(),c1_cp(),c1_cp_inloff(),c2_caller_cpdi,caller_cpdi);
new_jvms=cg->generate(jvms,caller_cpdi,is_private_copy());
assert(new_jvms!=NULL,"call failed to generate:  calls should work");
    if (c2_caller_cpdi) c2_caller_cpdi->_inlining_failure_id = IF_GENERALFAILURE;
  }

  if (cg->is_inline()) {
    C->env()->notice_inlined_method(call_method);
  }

  // Reset parser state from [new_]jvms, which now carries results of the call.
  // Return value (if any) is already pushed on the stack by the cg.
  add_exception_states_from(new_jvms);
  if (new_jvms->map()->control() == top()) {
    stop_and_kill_map();
  } else {
    assert(new_jvms->same_calls_as(jvms), "method/bci left unchanged");
    set_jvms(new_jvms);
  }

  if (!stopped()) {
    // This was some sort of virtual call, which did a null check for us.
    // Now we can assert receiver-not-null, on the normal return path.
    if (receiver != NULL && cg->is_virtual()) {
Node*cast=cast_not_null(receiver,true);
      // %%% assert(receiver == cast, "should already have cast the receiver");
    }

    // Round double result after a call from strict to non-strict code
    round_double_result(dest_method);

    // If the return type of the method is not loaded, assert that the
    // value we got is a null.  Otherwise, we need to recompile.
    if (!dest_method->return_type()->is_loaded()) {
      // If there is going to be a trap, put it at the next bytecode:
      set_bci(iter().next_bci());
      do_null_assert(peek(), T_OBJECT);
      set_bci(iter().cur_bci()); // put it back
    } else {
      assert0( call_method->return_type()->is_loaded() );
      BasicType result_type = dest_method->return_type()->basic_type();
if(result_type==T_OBJECT||result_type==T_ARRAY){
        const Type *t = peek()->bottom_type();
        assert0( t == TypePtr::NULL_PTR || t->is_oopptr()->klass()->is_loaded() );
      }
    }
  }

  // Restart record of parsing work after possible inlining of call
#ifndef PRODUCT
  parse_histogram()->set_initial_state(bc());
#endif
}

//---------------------------catch_call_exceptions-----------------------------
// Put a Catch and CatchProj nodes behind a just-created call.
// Send their caught exceptions to the proper handler.
// This may be used after a call to the rethrow VM stub,
// when it is needed to process unloaded exception classes.
void Parse::catch_call_exceptions(ciExceptionHandlerStream& handlers) {
  // Exceptions are delivered through this channel:
  Node* i_o = this->i_o();

  // Add a CatchNode.
  GrowableArray<int>* bcis = new (C->node_arena()) GrowableArray<int>(C->node_arena(), 8, 0, -1);
  GrowableArray<const Type*>* extypes = new (C->node_arena()) GrowableArray<const Type*>(C->node_arena(), 8, 0, NULL);
  GrowableArray<int>* saw_unloaded = new (C->node_arena()) GrowableArray<int>(C->node_arena(), 8, 0, 0);

  for (; !handlers.is_done(); handlers.next()) {
    ciExceptionHandler* h        = handlers.handler();
    int                 h_bci    = h->handler_bci();
    ciInstanceKlass*    h_klass  = h->is_catch_all() ? env()->Throwable_klass() : h->catch_klass();
    const TypePtr* h_extype = TypeOopPtr::make_from_klass_unique(h_klass)->cast_away_null();
    // Ignore exceptions with no implementors.  These cannot be thrown
    // (without class loading anyways, which will deopt this code).
    if( h_extype->empty() ) continue;

    // Do not introduce unloaded exception types into the graph:
    if (!h_klass->is_loaded()) {
      if (saw_unloaded->contains(h_bci)) {
        /* We've already seen an unloaded exception with h_bci, 
           so don't duplicate. Duplication will cause the CatchNode to be
           unnecessarily large. See 4713716. */
        continue;
      } else {
        saw_unloaded->append(h_bci);
      }
    }
    // Note:  It's OK if the BCIs repeat themselves.
    bcis->append(h_bci);
    extypes->append(h_extype);
  }

  int len = bcis->length();
  CatchNode *cn = new (C, 2) CatchNode(control(), i_o, len+1);
  Node *catch_ = _gvn.transform(cn);

  // now branch with the exception state to each of the (potential)
  // handlers
  for(int i=0; i < len; i++) {
    // Setup JVM state to enter the handler.
    PreserveJVMState pjvms(this);
    // Locals are just copied from before the call.
    // Get control from the CatchNode.
    int handler_bci = bcis->at(i);
    Node* ctrl = _gvn.transform( new (C, 1) CatchProjNode(catch_, i+1,handler_bci));
    // This handler cannot happen?
    if (ctrl == top())  continue;
    set_control(ctrl);

    // Create exception oop
    const TypeInstPtr* extype = extypes->at(i)->is_instptr();
    
    Node *thread = _gvn.transform( new (C, 1) ThreadLocalNode() );
Node*ex_adr=basic_plus_adr(top(),thread,in_bytes(JavaThread::pending_exception_offset()));
    int pending_ex_alias_idx = C->get_alias_index(ex_adr->bottom_type()->is_ptr());
    Node *ex_oop = make_load( NULL, ex_adr, extype, T_OBJECT, pending_ex_alias_idx );
    Node *ex_st  = store_to_memory( ctrl, ex_adr, null(), T_OBJECT, pending_ex_alias_idx );
record_for_igvn(ex_st);

    // Handle unloaded exception classes.
    if (saw_unloaded->contains(handler_bci)) {
      // An unloaded exception type is coming here.  Do an uncommon trap.
      // We do not expect the same handler bci to take both cold unloaded
      // and hot loaded exceptions.  But, watch for it.
if(PrintOpto&&extype->is_loaded()){
C2OUT->print_cr("Warning: Handler @%d takes mixed loaded/unloaded exceptions in ",handler_bci);
method()->print_name(C2OUT);C2OUT->cr();
      }
      // Emit an uncommon trap instead of processing the block.
      set_bci(handler_bci);
      push_ex_oop(ex_oop);
uncommon_trap(Deoptimization::Reason_unloaded,extype->klass(),"not loaded exception",false);
      set_bci(iter().cur_bci()); // put it back
      continue;
    }

    // go to the exception handler
    if (handler_bci < 0) {     // merge with corresponding rethrow node
      throw_to_exit(make_exception_state(ex_oop));
    } else {                      // Else jump to corresponding handle
      push_ex_oop(ex_oop);        // Clear stack and push just the oop.
      merge_exception(handler_bci);
    }
  }

  // The first CatchProj is for the normal return.
  // (Note:  If this is a call to rethrow_Java, this node goes dead.)
  set_control(_gvn.transform( new (C, 1) CatchProjNode(catch_, CatchProjNode::fall_through_index, CatchProjNode::no_handler_bci)));
}


//----------------------------catch_inline_exceptions--------------------------
// Handle all exceptions thrown by an inlined method or individual bytecode.
// Common case 1: we have no handler, so all exceptions merge right into
// the rethrow case.
// Case 2: we have some handlers, with loaded exception klasses that have
// no subklasses.  We do a Deutsch-Shiffman style type-check on the incoming
// exception oop and branch to the handler directly.
// Case 3: We have some handlers with subklasses or are not loaded at
// compile-time.  We have to call the runtime to resolve the exception.
// So we insert a RethrowCall and all the logic that goes with it.
void Parse::catch_inline_exceptions(SafePointNode* ex_map) {
  // Caller is responsible for saving away the map for normal control flow!
  assert(stopped(), "call set_map(NULL) first");
  assert(method()->has_exception_handlers(), "don't come here w/o work to do");

  Node* ex_node = saved_ex_oop(ex_map);
  if (ex_node == top()) {
    // No action needed.
    return;
  }
const TypeInstPtr*ex_type=_gvn.type(ex_node)->is_instptr();

  // determine potential exception handlers
  ciExceptionHandlerStream handlers(method(), bci(),
                                    ex_type->klass()->as_instance_klass(),
                                    ex_type->klass_is_exact());

  // Start executing from the given throw state.  (Keep its stack, for now.)
  // Get the exception oop as known at compile time.
  ex_node = use_exception_state(ex_map);

  // Get the exception oop klass from its header
  const TypeOopPtr *toop = ex_node->bottom_type()->is_oopptr();
  const TypeKlassPtr *tkid = TypeKlassPtr::make_kid(toop->klass(),toop->klass_is_exact());
Node*ex_kid_node=_gvn.transform(new(C,2)GetKIDNode(control(),ex_node,tkid));
  // Have handlers and the exception klass is not exact?  It might be the
  // merging of many exact exception klasses (happens alot with nested inlined
  // throw/catch blocks).  
  if (has_ex_handler() && !ex_type->klass_is_exact()) {
    // Compute the exception klass a little more cleverly.
    // Obvious solution is to simple do a GetKlass from the 'ex_node'.
    // However, if the ex_node is a PhiNode, I'm going to do a GetKlass for
    // each arm of the Phi.  If I know something clever about the exceptions
    // I'm loading the class from, I can replace the GetKlass with the
    // klass constant for the exception oop.
    if( ex_node->is_Phi() ) {
ex_kid_node=new(C,ex_node->req())PhiNode(ex_node->in(0),TypeKlassPtr::KID);
      for( uint i = 1; i < ex_node->req(); i++ ) {
        const TypeOopPtr *toopi = ex_node->in(i)->bottom_type()->is_oopptr();
        const TypeKlassPtr *tkidi = TypeKlassPtr::make_kid(toop->klass(),toop->klass_is_exact());
        Node *kid = _gvn.transform(new (C, 2) GetKIDNode(ex_node->in(0)->in(i), ex_node->in(i),tkidi));
ex_kid_node->init_req(i,kid);
      }
_gvn.set_type(ex_kid_node,TypeKlassPtr::KID);
      
    }
  }

  // Scan the exception table for applicable handlers.
  // If none, we can call rethrow() and be done!
  // If precise (loaded with no subklasses), insert a D.S. style
  // pointer compare to the correct handler and loop back.
  // If imprecise, switch to the Rethrow VM-call style handling.

  int remaining = handlers.count_remaining();

  // iterate through all entries sequentially
ciInstanceKlass*handler_catch_klass=NULL;
  for (;!handlers.is_done(); handlers.next()) {
    // Do nothing if turned off
    if( !DeutschShiffmanExceptions ) break;
    ciExceptionHandler* handler = handlers.handler();

    if (handler->is_rethrow()) {
      // If we fell off the end of the table without finding an imprecise
      // exception klass (and without finding a generic handler) then we
      // know this exception is not handled in this method.  We just rethrow
      // the exception into the caller.
      throw_to_exit(make_exception_state(ex_node));
      return;
    }

    // exception handler bci range covers throw_bci => investigate further
    int handler_bci = handler->handler_bci();

    if (remaining == 1) {
      push_ex_oop(ex_node);        // Push exception oop for handler
      merge_exception(handler_bci); // jump to handler
      return;                   // No more handling to be done here!
    }

handler_catch_klass=handler->catch_klass();
if(!handler_catch_klass->is_loaded())//klass is not loaded?
      break;                    // Must call Rethrow!
    // Sharpen handler klass.  Some klasses cannot have any oops
    // (e.g. interface with no implementations).
    const TypePtr* tpx = TypeOopPtr::make_from_klass_unique(handler_catch_klass);
    const TypeOopPtr *tp = tpx->isa_oopptr(); // Oop of this klass is possible?
    Node *handler_klass = tp ? _gvn.makecon( TypeKlassPtr::make_kid(tp->klass(),true) ) : NULL;

    Node *failure = gen_subtype_check( ex_kid_node, handler_klass, _gvn.type(ex_node) );
    { PreserveJVMState pjvms(this);
Node*ex_oop=_gvn.transform(new(C,2)CheckCastPPNode(control(),ex_node,tpx));
      push_ex_oop(ex_oop);      // Push exception oop for handler
      merge_exception(handler_bci);
    }

    // Come here if exception does not match handler.
    // Carry on with more handler checks.
set_control(failure);
    --remaining;
  }

  assert(!stopped(), "you should return if you finish the chain");

  if (remaining == 1) {
    // Further checks do not matter.
  }

  if (can_rerun_bytecode()) {
    // Do not push_ex_oop here!
    // Re-executing the bytecode will reproduce the throwing condition.
    bool must_throw = true;
    uncommon_trap(Deoptimization::Reason_unloaded,handler_catch_klass,"matching handler klass not loaded",
                  must_throw);
    return;
  }

  // Oops, need to call into the VM to resolve the klasses at runtime.
  // Note:  This call must not deoptimize, since it is not a real at this bci!
  kill_dead_locals();

  make_runtime_call(RC_NO_LEAF | RC_MUST_THROW,
                    false /* !must_callruntimenode */,
OptoRuntime::forward_exception2_Type(),
StubRoutines::forward_exception_entry2(),
"forward_exception2",
                    TypeRawPtr::BOTTOM, // sets the exception oop back into thr->_pending_ex
                    ex_node);

  // Rethrow is a pure call, no side effects, only a result.
  // The result cannot be allocated, so we use I_O

  // Catch exceptions from the rethrow
  catch_call_exceptions(handlers);
}


// (Note:  Moved add_debug_info into GraphKit::add_safepoint_edges.)


// Identify possible target method and inlining style
ciMethod* Parse::optimize_inlining(ciMethod* caller, int bci, ciInstanceKlass* klass, 
                                   ciMethod *dest_method, const TypeOopPtr* receiver_type) {
  // only use for virtual or interface calls

  // If it is obviously final, do not bother to call find_monomorphic_target,
  // because the class hierarchy checks are not needed, and may fail due to
  // incompletely loaded classes.  Since we do our own class loading checks
  // in this module, we may confidently bind to any method.
  if (dest_method->can_be_statically_bound()) {
    return dest_method;
  }

  // Attempt to improve the receiver
  bool actual_receiver_is_exact = false;
  ciInstanceKlass* actual_receiver = klass;
  if (receiver_type != NULL) {
    // Array methods are all inherited from Object, and are monomorphic.
    if (receiver_type->isa_aryptr() &&
        dest_method->holder() == env()->Object_klass()) {
      return dest_method;
    }

    // All other interesting cases are instance klasses.
    if (!receiver_type->isa_instptr()) {
      return NULL;
    }

    ciInstanceKlass *ikl = receiver_type->klass()->as_instance_klass();
    if (ikl->is_loaded() && ikl->is_initialized() && !ikl->is_interface() &&
(ikl==actual_receiver||ikl->is_subtype_of(actual_receiver))){
      // ikl is a same or better type than the original actual_receiver, 
      // e.g. static receiver from bytecodes. 
      actual_receiver = ikl;
      // Is the actual_receiver exact?
      actual_receiver_is_exact = receiver_type->klass_is_exact();
    }
  }

  ciInstanceKlass*   calling_klass = caller->holder();
  ciMethod* cha_monomorphic_target = dest_method->find_monomorphic_target(calling_klass, klass, actual_receiver);
  if (cha_monomorphic_target != NULL) {
    // Look at the method-receiver type.  Does it add "too much information"?

    // If the actual_receiver is loaded but not linked, then CHA "knows" not
    // to try a lookup on the method (no vtable entry yet) and thus does not
    // acknowledge a matching method in the parent-chain of actual_receiver.
    // If the actual_receiver also has a child klass (it also must be loaded
    // but not linked), then CHA will do name/sig matching on the child and
    // can report back the single child class & method as the only target.
    // This is WRONG, in that if the child ever gets linked (and so you could
    // possibly allocate an object of that type and make this call), then also
    // actual_receiver must be linked first - and at that point CHA will
    // report the correct answer of two possible targets.  Example:
    //
    // JPassword extends JTextField extends JTextComponent.
    // JPassword and JTextField are both loaded-not-linked.
    // JPassword and JTextComponent implement getText.
    // actual_receiver is JTextField (inexact, subclasses allowed).
    // Then CHA reports back only the getText in JPassword - but if ever
    // a JPassword is created, then also a JTextField could be created
    // and so JTextComponent.getText is also a legitamite target.

    ciKlass*    mr_klass = cha_monomorphic_target->holder();
    const Type* mr_type  = TypeInstPtr::make(TypePtr::BotPTR, mr_klass);
    if (receiver_type == NULL || !receiver_type->higher_equal(mr_type)) {
      // Calling this method would include an implicit cast to its holder.
      // %%% Not yet implemented.  Would throw minor asserts at present.
      // %%% The most common wins are already gained by +UseUniqueSubclasses.
      // To fix, put the higher_equal check at the call of this routine,
      // and add a CheckCastPP to the receiver.
      cha_monomorphic_target = NULL;
    }
    if (cha_monomorphic_target && 
        (cha_monomorphic_target->is_abstract() || // single target is abstract, about to throw AME
         cha_monomorphic_target->number_of_breakpoints())) {
      // We do not care about abstract methods and those with break points here.
      cha_monomorphic_target = NULL;
    }
  }
  if (cha_monomorphic_target != NULL) {
    // Hardwiring a virtual.
    // If we inlined because CHA revealed only a single target method,
    // then we are dependent on that target method not getting overridden
    // by dynamic class loading.  Be sure to test the "static" receiver
    // dest_method here, as opposed to the actual receiver, which may
    // falsely lead us to believe that the receiver is final or private.
C->_masm.assert_unique_concrete_method(actual_receiver,cha_monomorphic_target);
    return cha_monomorphic_target;
  }

  // If the type is exact, we can still bind the method w/o a vcall.
  // (This case comes after CHA so we can see how much extra work it does.)
  if (actual_receiver_is_exact) {
    // In case of evolution, there is a dependence on every inlined method, since each
    // such method can be changed when its class is redefined.
    ciMethod* exact_method = dest_method->resolve_invoke(calling_klass, actual_receiver);
    if (exact_method != NULL) {
      if (PrintOpto) {
C2OUT->print("  Calling method via exact type @%d --- ",bci);
        exact_method->print_name(C2OUT);
C2OUT->cr();
      }
      return exact_method;
    }
  }

  return NULL;
}



