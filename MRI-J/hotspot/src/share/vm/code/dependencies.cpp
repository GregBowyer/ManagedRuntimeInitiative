/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciArrayKlass.hpp"
#include "ciEnv.hpp"
#include "dependencies.hpp"
#include "klassOop.hpp"
#include "instanceKlass.hpp"
#include "methodOop.hpp"
#include "mutexLocker.hpp"
#include "symbolOop.hpp"

#include "orderAccess_os_pd.inline.hpp"

#ifdef ASSERT
static bool must_be_in_vm() {
  Thread* thread = Thread::current();
  Unimplemented();
  return false;
}
#endif //ASSERT

void Dependencies::initialize(ciEnv* env) {
  Arena* arena = env->arena();
  _dep_seen = new(arena) GrowableArray<int>(arena, 500, 0, 0);
  DEBUG_ONLY(_deps[end_marker] = NULL);
  for (int i = (int)FIRST_TYPE; i < (int)TYPE_LIMIT; i++) {
    _deps[i] = new(arena) GrowableArray<ciObject*>(arena, 10, 0, 0);
  }
  _content_bytes = NULL;
  _size_in_bytes = (size_t)-1;

  assert(TYPE_LIMIT <= (1<<LG2_TYPE_LIMIT), "sanity");
}

void Dependencies::assert_evol_method(ciMethod* m) {
  assert_common_1(evol_method, m);
}

void Dependencies::assert_leaf_type(ciKlass* ctxk) {
  if (ctxk->is_array_klass()) {
    // As a special case, support this assertion on an array type,
    // which reduces to an assertion on its element type.
    // Note that this cannot be done with assertions that
    // relate to concreteness or abstractness.
    ciType* elemt = ctxk->as_array_klass()->base_element_type();
    if (!elemt->is_instance_klass())  return;   // Ex:  int[][]
    ctxk = elemt->as_instance_klass();
    //if (ctxk->is_final())  return;            // Ex:  String[][]
  }
  check_ctxk(ctxk);
  assert_common_1(leaf_type, ctxk);
}

void Dependencies::assert_abstract_with_unique_concrete_subtype(ciKlass* ctxk, ciKlass* conck) {
  check_ctxk_abstract(ctxk);
  assert_common_2(abstract_with_unique_concrete_subtype, ctxk, conck);
}

void Dependencies::assert_abstract_with_no_concrete_subtype(ciKlass* ctxk) {
  check_ctxk_abstract(ctxk);
  assert_common_1(abstract_with_no_concrete_subtype, ctxk);
}

void Dependencies::assert_concrete_with_no_concrete_subtype(ciKlass* ctxk) {
  check_ctxk_concrete(ctxk);
  assert_common_1(concrete_with_no_concrete_subtype, ctxk);
}

void Dependencies::assert_unique_concrete_method(ciKlass* ctxk, ciMethod* uniqm) {
  check_ctxk(ctxk);
  assert_common_2(unique_concrete_method, ctxk, uniqm);
}

void Dependencies::assert_abstract_with_exclusive_concrete_subtypes(ciKlass* ctxk, ciKlass* k1, ciKlass* k2) {
  check_ctxk(ctxk);
  assert_common_3(abstract_with_exclusive_concrete_subtypes_2, ctxk, k1, k2);
}

void Dependencies::assert_exclusive_concrete_methods(ciKlass* ctxk, ciMethod* m1, ciMethod* m2) {
  check_ctxk(ctxk);
  assert_common_3(exclusive_concrete_methods_2, ctxk, m1, m2);
}

void Dependencies::assert_has_no_finalizable_subclasses(ciKlass* ctxk) {
  check_ctxk(ctxk);
  assert_common_1(no_finalizable_subclasses, ctxk);
}

// Helper function.  If we are adding a new dep. under ctxk2,
// try to find an old dep. under a broader* ctxk1.  If there is
// 
bool Dependencies::maybe_merge_ctxk(GrowableArray<ciObject*>* deps,
                                    int ctxk_i, ciKlass* ctxk2) {
  ciKlass* ctxk1 = deps->at(ctxk_i)->as_klass();
  if (ctxk2->is_subtype_of(ctxk1)) {
    return true;  // success, and no need to change
  } else if (ctxk1->is_subtype_of(ctxk2)) {
    // new context class fully subsumes previous one
    deps->at_put(ctxk_i, ctxk2);
    return true;
  } else {
    return false;
  }
}

void Dependencies::assert_common_1(Dependencies::DepType dept, ciObject* x) {
  Unimplemented();
}

void Dependencies::assert_common_2(Dependencies::DepType dept,
                                   ciKlass* ctxk, ciObject* x) {
  Unimplemented();
}

void Dependencies::assert_common_3(Dependencies::DepType dept,
                                   ciKlass* ctxk, ciObject* x, ciObject* x2) {
  Unimplemented();
}

/// Support for encoding dependencies into an CodeBlob:

static int sort_dep(ciObject** p1, ciObject** p2, int narg) {
  for (int i = 0; i < narg; i++) {
    int diff = p1[i]->ident() - p2[i]->ident();
    if (diff != 0)  return diff;
  }
  return 0;
}
static int sort_dep_arg_1(ciObject** p1, ciObject** p2)
{ return sort_dep(p1, p2, 1); }
static int sort_dep_arg_2(ciObject** p1, ciObject** p2)
{ return sort_dep(p1, p2, 2); }
static int sort_dep_arg_3(ciObject** p1, ciObject** p2)
{ return sort_dep(p1, p2, 3); }

void Dependencies::sort_all_deps() {
  for (int deptv = (int)FIRST_TYPE; deptv < (int)TYPE_LIMIT; deptv++) {
    DepType dept = (DepType)deptv;
    GrowableArray<ciObject*>* deps = _deps[dept];
    if (deps->length() <= 1)  continue;
    switch (dep_args(dept)) {
    case 1: deps->sort(sort_dep_arg_1, 1); break;
    case 2: deps->sort(sort_dep_arg_2, 2); break;
    case 3: deps->sort(sort_dep_arg_3, 3); break;
    default: ShouldNotReachHere();
    }
  }
}

size_t Dependencies::estimate_size_in_bytes() {
  size_t est_size = 100;
  for (int deptv = (int)FIRST_TYPE; deptv < (int)TYPE_LIMIT; deptv++) {
    DepType dept = (DepType)deptv;
    GrowableArray<ciObject*>* deps = _deps[dept];
    est_size += deps->length()*2;  // tags and argument(s)
  }
  return est_size;
}

ciKlass* Dependencies::ctxk_encoded_as_null(DepType dept, ciObject* x) {
  switch (dept) {
  case abstract_with_exclusive_concrete_subtypes_2:
    return x->as_klass();
  case unique_concrete_method:
  case exclusive_concrete_methods_2:
    return x->as_method()->holder();
  }
  return NULL;  // let NULL be NULL
}

klassOop Dependencies::ctxk_encoded_as_null(DepType dept, oop x) {
  assert(must_be_in_vm(), "raw oops here");
  switch (dept) {
  case abstract_with_exclusive_concrete_subtypes_2:
    assert(x->is_klass(), "sanity");
    return (klassOop) x;
  case unique_concrete_method:
  case exclusive_concrete_methods_2:
    assert(x->is_method(), "sanity");
    return ((methodOop)x)->method_holder();
  }
  return NULL;  // let NULL be NULL
}

const char* Dependencies::_dep_name[TYPE_LIMIT] = {
  "end_marker",
  "evol_method",
  "leaf_type",
  "abstract_with_unique_concrete_subtype",
  "abstract_with_no_concrete_subtype",
  "concrete_with_no_concrete_subtype",
  "unique_concrete_method",
  "abstract_with_exclusive_concrete_subtypes_2",
  "exclusive_concrete_methods_2",
  "no_finalizable_subclasses"
};

int Dependencies::_dep_args[TYPE_LIMIT] = {
  -1,// end_marker
  1, // evol_method m
  1, // leaf_type ctxk
  2, // abstract_with_unique_concrete_subtype ctxk, k
  1, // abstract_with_no_concrete_subtype ctxk
  1, // concrete_with_no_concrete_subtype ctxk
  2, // unique_concrete_method ctxk, m
  3, // unique_concrete_subtypes_2 ctxk, k1, k2
  3, // unique_concrete_methods_2 ctxk, m1, m2
  1  // no_finalizable_subclasses ctxk
};

const char* Dependencies::dep_name(Dependencies::DepType dept) {
  if (!dept_in_mask(dept, all_types))  return "?bad-dep?";
  return _dep_name[dept];
}

int Dependencies::dep_args(Dependencies::DepType dept) {
  if (!dept_in_mask(dept, all_types))  return -1;
  return _dep_args[dept];
}

/// Dependency stream support (decodes dependencies from an CodeBlob):

/// Checking dependencies:

// This hierarchy walker inspects subtypes of a given type,
// trying to find a "bad" class which breaks a dependency.
// Such a class is called a "witness" to the broken dependency.
// While searching around, we ignore "participants", which
// are already known to the dependency.
class ClassHierarchyWalker {
 public:
  enum { PARTICIPANT_LIMIT = 3 };

 private:
  // optional method descriptor to check for:
  symbolOop _name;
  symbolOop _signature;

  // special classes which are not allowed to be witnesses:
  klassOop  _participants[PARTICIPANT_LIMIT+1];
  int       _num_participants;

  // cache of method lookups
  methodOop _found_methods[PARTICIPANT_LIMIT+1];

  // if non-zero, tells how many witnesses to convert to participants
  int       _record_witnesses;

  void initialize(klassOop participant) {
    _record_witnesses = 0;
    _participants[0]  = participant;
    _found_methods[0] = NULL;
    _num_participants = 0;
    if (participant != NULL) {
      // Terminating NULL.
      _participants[1] = NULL;
      _found_methods[1] = NULL;
      _num_participants = 1;
    }
  }

  void initialize_from_method(methodOop m) {
    assert(m != NULL && m->is_method(), "sanity");
    _name      = m->name();
    _signature = m->signature();
  }

 public:
  // The walker is initialized to recognize certain methods and/or types
  // as friendly participants.
  ClassHierarchyWalker(klassOop participant, methodOop m) {
    initialize_from_method(m);
    initialize(participant);
  }
  ClassHierarchyWalker(methodOop m) {
    initialize_from_method(m);
    initialize(NULL);
  }
  ClassHierarchyWalker(klassOop participant = NULL) {
    _name      = NULL;
    _signature = NULL;
    initialize(participant);
  }

  // This is common code for two searches:  One for concrete subtypes,
  // the other for concrete method implementations and overrides.
  bool doing_subtype_search() {
    return _name == NULL;
  }

  int num_participants() { return _num_participants; }
  klassOop participant(int n) {
    assert((uint)n <= (uint)_num_participants, "oob");
    return _participants[n];
  }

  // Note:  If n==num_participants, returns NULL.
  methodOop found_method(int n) {
    assert((uint)n <= (uint)_num_participants, "oob");
    methodOop fm = _found_methods[n];
    assert(n == _num_participants || fm != NULL, "proper usage");
    assert(fm == NULL || fm->method_holder() == _participants[n], "sanity");
    return fm;
  }

#ifdef ASSERT
  // Assert that m is inherited into ctxk, without intervening overrides.
  // (May return true even if this is not true, in corner cases where we punt.)
  bool check_method_context(klassOop ctxk, methodOop m) {
    if (m->method_holder() == ctxk)
      return true;  // Quick win.
    if (m->is_private())
      return false; // Quick lose.  Should not happen.
    if (!(m->is_public() || m->is_protected()))
      // The override story is complex when packages get involved.
      return true;  // Must punt the assertion to true.
    Klass* k = Klass::cast(ctxk);
    methodOop lm = k->lookup_method(m->name(), m->signature());
    if (lm == NULL && k->oop_is_instance()) {
      // It might be an abstract interface method, devoid of mirandas.
      lm = ((instanceKlass*)k)->lookup_method_in_all_interfaces(m->name(),
                                                                m->signature());
    }
    if (lm == m)
      // Method m is inherited into ctxk.
      return true;
    if (lm != NULL) {
      if (!(lm->is_public() || lm->is_protected()))
        // Method is [package-]private, so the override story is complex.
        return true;  // Must punt the assertion to true.
      if (   !Dependencies::is_concrete_method(lm)
          && !Dependencies::is_concrete_method(m)
          && Klass::cast(lm->method_holder())->is_subtype_of(m->method_holder()))
        // Method m is overridden by lm, but both are non-concrete.
        return true;
    }
    ResourceMark rm;
    tty->print_cr("Dependency method not found in the associated context:");
    tty->print_cr("  context = %s", Klass::cast(ctxk)->external_name());
    tty->print(   "  method = "); m->print_short_name(tty); tty->cr();
    if (lm != NULL) {
      tty->print( "  found = "); lm->print_short_name(tty); tty->cr();
    }
    return false;
  }
#endif

  void add_participant(klassOop participant) {
    assert(_num_participants + _record_witnesses < PARTICIPANT_LIMIT, "oob");
    int np = _num_participants++;
    _participants[np] = participant;
    _participants[np+1] = NULL;
    _found_methods[np+1] = NULL;
  }

  void record_witnesses(int add) {
    if (add > PARTICIPANT_LIMIT)  add = PARTICIPANT_LIMIT;
    assert(_num_participants + add < PARTICIPANT_LIMIT, "oob");
    _record_witnesses = add;
  }

  bool is_witness(klassOop k) {
    if (doing_subtype_search()) {
      return Dependencies::is_concrete_klass(k);
    } else {
      methodOop m = instanceKlass::cast(k)->find_method(_name, _signature);
      if (m == NULL || !Dependencies::is_concrete_method(m))  return false;
      _found_methods[_num_participants] = m;
      // Note:  If add_participant(k) is called,
      // the method m will already be memoized for it.
      return true;
    }
  }

  bool is_participant(klassOop k) {
    if (k == _participants[0]) {
      return true;
    } else if (_num_participants <= 1) {
      return false;
    } else {
      return in_list(k, &_participants[1]);
    }
  }
  bool ignore_witness(klassOop witness) {
    if (_record_witnesses == 0) {
      return false;
    } else {
      --_record_witnesses;
      add_participant(witness);
      return true;
    }
  }
  static bool in_list(klassOop x, klassOop* list) {
    for (int i = 0; ; i++) {
      klassOop y = list[i];
      if (y == NULL)  break;
      if (y == x)  return true;
    }
    return false;  // not in list
  }

 private:
  // the actual search method:
  klassOop find_witness_anywhere(klassOop context_type,
                                 bool participants_hide_witnesses,
                                 bool top_level_call = true);
  // the spot-checking version:
  klassOop find_witness_in(DepChange& changes,
                           klassOop context_type,
                           bool participants_hide_witnesses);
 public:
  klassOop find_witness_subtype(klassOop context_type, DepChange* changes = NULL) {
    assert(doing_subtype_search(), "must set up a subtype search");
    // When looking for unexpected concrete types,
    // do not look beneath expected ones.
    const bool participants_hide_witnesses = true;
    // CX > CC > C' is OK, even if C' is new.
    // CX > { CC,  C' } is not OK if C' is new, and C' is the witness.
    if (changes != NULL) {
      return find_witness_in(*changes, context_type, participants_hide_witnesses);
    } else {
      return find_witness_anywhere(context_type, participants_hide_witnesses);
    }
  }
  klassOop find_witness_definer(klassOop context_type, DepChange* changes = NULL) {
    assert(!doing_subtype_search(), "must set up a method definer search");
    // When looking for unexpected concrete methods,
    // look beneath expected ones, to see if there are overrides.
    const bool participants_hide_witnesses = true;
    // CX.m > CC.m > C'.m is not OK, if C'.m is new, and C' is the witness.
    if (changes != NULL) {
      return find_witness_in(*changes, context_type, !participants_hide_witnesses);
    } else {
      return find_witness_anywhere(context_type, !participants_hide_witnesses);
    }
  }
};

#ifndef PRODUCT
static int deps_find_witness_calls = 0;
static int deps_find_witness_steps = 0;
static int deps_find_witness_recursions = 0;
static int deps_find_witness_singles = 0;
static int deps_find_witness_print = 0; // set to -1 to force a final print
static bool count_find_witness_calls() {
  return false;
}
#else
#define count_find_witness_calls() (0)
#endif //PRODUCT


klassOop ClassHierarchyWalker::find_witness_in(DepChange& changes,
                                               klassOop context_type,
                                               bool participants_hide_witnesses) {
  Unimplemented();
  return NULL;
}


// Walk hierarchy under a context type, looking for unexpected types.
// Do not report participant types, and recursively walk beneath
// them only if participants_hide_witnesses is false.
// If top_level_call is false, skip testing the context type,
// because the caller has already considered it.
klassOop ClassHierarchyWalker::find_witness_anywhere(klassOop context_type,
                                                     bool participants_hide_witnesses,
                                                     bool top_level_call) {
  // Current thread must be in VM (not native mode, as in CI):
JavaThread*thread=JavaThread::current();
assert(thread->jvm_locked_by_self(),"raw oops here");
  // Must not move the class hierarchy during this check:
  assert_locked_or_safepoint(Compile_lock);

  bool do_counts = count_find_witness_calls();

  // Check the root of the sub-hierarchy first.
  if (top_level_call) {
    if (do_counts) {
      NOT_PRODUCT(deps_find_witness_calls++);
      NOT_PRODUCT(deps_find_witness_steps++);
    }
    if (is_participant(context_type)) {
      if (participants_hide_witnesses)  return NULL;
      // else fall through to search loop...
    } else if (is_witness(context_type) && !ignore_witness(context_type)) {
      // The context is an abstract class or interface, to start with.
      return context_type;
    }
  }

  // Now we must check each implementor and each subclass.
  // Use a short worklist to avoid blowing the stack.
  // Each worklist entry is a *chain* of subklass siblings to process.
  const int CHAINMAX = 100;  // >= 1 + instanceKlass::implementors_limit
  Klass* chains[CHAINMAX];
  int    chaini = 0;  // index into worklist
  Klass* chain;       // scratch variable
#define ADD_SUBCLASS_CHAIN(k)                     {  \
    assert(chaini < CHAINMAX, "oob");                \
    chain = instanceKlass::cast(k)->subklass();      \
    if (chain != NULL)  chains[chaini++] = chain;    }

  // Look for non-abstract subclasses.
  // (Note:  Interfaces do not have subclasses.)
  ADD_SUBCLASS_CHAIN(context_type);

  // If it is an interface, search its direct implementors.
  // (Their subclasses are additional indirect implementors.
  // See instanceKlass::add_implementor.)
  // (Note:  nof_implementors is always zero for non-interfaces.)
  int nof_impls = instanceKlass::cast(context_type)->nof_implementors();
  if (nof_impls > 1) {
    // Avoid this case: *I.m > { A.m, C }; B.m > C
    // Here, I.m has 2 concrete implementations, but m appears unique
    // as A.m, because the search misses B.m when checking C.
    // The inherited method B.m was getting missed by the walker
    // when interface 'I' was the starting point.
    // %%% Until this is fixed more systematically, bail out.
    // (Old CHA had the same limitation.)
    return context_type;
  }
  for (int i = 0; i < nof_impls; i++) {
klassOop impl=instanceKlass::cast(context_type)->implementor(i).as_klassOop();
    if (impl == NULL) {
      // implementors array overflowed => no exact info.
      return context_type;  // report an inexact witness to this sad affair
    }
    if (do_counts)
      { NOT_PRODUCT(deps_find_witness_steps++); }
    if (is_participant(impl)) {
      if (participants_hide_witnesses)  continue;
      // else fall through to process this guy's subclasses
    } else if (is_witness(impl) && !ignore_witness(impl)) {
      return impl;
    }
    ADD_SUBCLASS_CHAIN(impl);
  }

  // Recursively process each non-trivial sibling chain.
  while (chaini > 0) {
    Klass* chain = chains[--chaini];
    for (Klass* subk = chain; subk != NULL; subk = subk->next_sibling()) {
      klassOop sub = subk->as_klassOop();
      if (do_counts) { NOT_PRODUCT(deps_find_witness_steps++); }
      if (is_participant(sub)) {
        if (participants_hide_witnesses)  continue;
        // else fall through to process this guy's subclasses
      } else if (is_witness(sub) && !ignore_witness(sub)) {
        return sub;
      }
if(chaini<CHAINMAX){
        // Fast path.  (Partially disabled if VerifyDependencies.)
        ADD_SUBCLASS_CHAIN(sub);
      } else {
        // Worklist overflow.  Do a recursive call.  Should be rare.
        // The recursive call will have its own worklist, of course.
        // (Note that sub has already been tested, so that there is
        // no need for the recursive call to re-test.  That's handy,
        // since the recursive call sees sub as the context_type.)
        if (do_counts) { NOT_PRODUCT(deps_find_witness_recursions++); }
        klassOop witness = find_witness_anywhere(sub,
                                                 participants_hide_witnesses,
                                                 /*top_level_call=*/ false);
        if (witness != NULL)  return witness;
      }
    }
  }

  // No witness found.  The dependency remains unbroken.
  return NULL;
#undef ADD_SUBCLASS_CHAIN
}


bool Dependencies::is_concrete_klass(klassOop k) {
  if (Klass::cast(k)->is_abstract())  return false;
  // %%% We could treat classes which are concrete but
  // have not yet been instantiated as virtually abstract.
  // This would require a deoptimization barrier on first instantiation.
  //if (k->is_not_instantiated())  return false;
  return true;
}

bool Dependencies::is_concrete_method(methodOop m) {
  if (m->is_abstract())  return false;
  // %%% We could treat unexecuted methods as virtually abstract also.
  // This would require a deoptimization barrier on first execution.
  return !m->is_abstract();
}


Klass* Dependencies::find_finalizable_subclass(Klass* k) {
  if (k->is_interface())  return NULL;
  if (k->has_finalizer()) return k;
  k = k->subklass();
  while (k != NULL) {
    Klass* result = find_finalizable_subclass(k);
    if (result != NULL) return result;
    k = k->next_sibling();
  }
  return NULL;
}


bool Dependencies::is_concrete_klass(ciInstanceKlass* k) {
  if (k->is_abstract())  return false;
  // We could return also false if k does not yet appear to be
  // instantiated, if the VM version supports this distinction also.
  //if (k->is_not_instantiated())  return false;
  return true;
}

bool Dependencies::is_concrete_method(ciMethod* m) {
  // Statics are irrelevant to virtual call sites.
  if (m->is_static())  return false;

  // We could return also false if m does not yet appear to be
  // executed, if the VM version supports this distinction also.
  return !m->is_abstract();
}


bool Dependencies::has_finalizable_subclass(ciInstanceKlass* k) {
return k->has_finalizable_subclass_query();
}


// Any use of the contents (bytecodes) of a method must be
// marked by an "evol_method" dependency, if those contents
// can change.  (Note: A method is always dependent on itself.)
klassOop Dependencies::check_evol_method(methodOop m) {
  assert(must_be_in_vm(), "raw oops here");
  // Did somebody do a JVMTI RedefineClasses while our backs were turned?
  // Or is there a now a breakpoint?
  // (Assumes compiled code cannot handle bkpts; change if UseFastBreakpoints.)
  if (m->is_old()
      || m->number_of_breakpoints() > 0) {
    return m->method_holder();
  } else {
    return NULL;
  }
}

// This is a strong assertion:  It is that the given type
// has no subtypes whatever.  It is most useful for
// optimizing checks on reflected types or on array types.
// (Checks on types which are derived from real instances
// can be optimized more strongly than this, because we
// know that the checked type comes from a concrete type,
// and therefore we can disregard abstract types.)
klassOop Dependencies::check_leaf_type(klassOop ctxk) {
  assert(must_be_in_vm(), "raw oops here");
  assert_locked_or_safepoint(Compile_lock);
  instanceKlass* ctx = instanceKlass::cast(ctxk);
  Klass* sub = ctx->subklass();
  if (sub != NULL) {
    return sub->as_klassOop();
  } else if (ctx->nof_implementors() != 0) {
    // if it is an interface, it must be unimplemented
    // (if it is not an interface, nof_implementors is always zero)
klassOop impl=ctx->implementor(0).as_klassOop();
    return (impl != NULL)? impl: ctxk;
  } else {
    return NULL;
  }
}

// Test the assertion that conck is the only concrete subtype* of ctxk.
// The type conck itself is allowed to have have further concrete subtypes.
// This allows the compiler to narrow occurrences of ctxk by conck,
// when dealing with the types of actual instances.
klassOop Dependencies::check_abstract_with_unique_concrete_subtype(klassOop ctxk,
                                                                   klassOop conck,
                                                                   DepChange* changes) {
  ClassHierarchyWalker wf(conck);
  return wf.find_witness_subtype(ctxk, changes);
}

// If a non-concrete class has no concrete subtypes, it is not (yet)
// instantiatable.  This can allow the compiler to make some paths go
// dead, if they are gated by a test of the type.
klassOop Dependencies::check_abstract_with_no_concrete_subtype(klassOop ctxk,
                                                               DepChange* changes) {
  // Find any concrete subtype, with no participants:
  ClassHierarchyWalker wf;
  return wf.find_witness_subtype(ctxk, changes);
}


// If a concrete class has no concrete subtypes, it can always be
// exactly typed.  This allows the use of a cheaper type test.
klassOop Dependencies::check_concrete_with_no_concrete_subtype(klassOop ctxk,
                                                               DepChange* changes) {
  // Find any concrete subtype, with only the ctxk as participant:
  ClassHierarchyWalker wf(ctxk);
  return wf.find_witness_subtype(ctxk, changes);
}


// Find the unique concrete proper subtype of ctxk, or NULL if there
// is more than one concrete proper subtype.  If there are no concrete
// proper subtypes, return ctxk itself, whether it is concrete or not.
// The returned subtype is allowed to have have further concrete subtypes.
// That is, return CC1 for CX > CC1 > CC2, but NULL for CX > { CC1, CC2 }.
klassOop Dependencies::find_unique_concrete_subtype(klassOop ctxk) {
  ClassHierarchyWalker wf(ctxk);   // Ignore ctxk when walking.
  wf.record_witnesses(1);          // Record one other witness when walking.
  klassOop wit = wf.find_witness_subtype(ctxk);
  if (wit != NULL)  return NULL;   // Too many witnesses.
  klassOop conck = wf.participant(0);
  if (conck == NULL) {
    return ctxk;                   // Return ctxk as a flag for "no subtypes".
  } else {
    return conck;
  }
}

// Test the assertion that the k[12] are the only concrete subtypes of ctxk,
// except possibly for further subtypes of k[12] themselves.
// The context type must be abstract.  The types k1 and k2 are themselves
// allowed to have further concrete subtypes.
klassOop Dependencies::check_abstract_with_exclusive_concrete_subtypes(
                                                klassOop ctxk,
                                                klassOop k1,
                                                klassOop k2,
                                                DepChange* changes) {
  ClassHierarchyWalker wf;
  wf.add_participant(k1);
  wf.add_participant(k2);
  return wf.find_witness_subtype(ctxk, changes);
}

// Search ctxk for concrete implementations.  If there are klen or fewer,
// pack them into the given array and return the number.
// Otherwise, return -1, meaning the given array would overflow.
// (Note that a return of 0 means there are exactly no concrete subtypes.)
// In this search, if ctxk is concrete, it will be reported alone.
// For any type CC reported, no proper subtypes of CC will be reported.
int Dependencies::find_exclusive_concrete_subtypes(klassOop ctxk,
                                                   int klen,
                                                   klassOop karray[]) {
  ClassHierarchyWalker wf;
  wf.record_witnesses(klen);
  klassOop wit = wf.find_witness_subtype(ctxk);
  if (wit != NULL)  return -1;  // Too many witnesses.
  int num = wf.num_participants();
  assert(num <= klen, "oob");
  // Pack the result array with the good news.
  for (int i = 0; i < num; i++)
    karray[i] = wf.participant(i);
  return num;
}

// If a class (or interface) has a unique concrete method uniqm, return NULL.
// Otherwise, return a class that contains an interfering method.
klassOop Dependencies::check_unique_concrete_method(klassOop ctxk, methodOop uniqm,
                                                    DepChange* changes) {
  // Here is a missing optimization:  If uniqm->is_final(),
  // we don't really need to search beneath it for overrides.
  // This is probably not important, since we don't use dependencies
  // to track final methods.  (They can't be "definalized".)
  ClassHierarchyWalker wf(uniqm->method_holder(), uniqm);
  return wf.find_witness_definer(ctxk, changes);
}

// Find the set of all non-abstract methods under ctxk that match m.
// (The method m must be defined or inherited in ctxk.)
// Include m itself in the set, unless it is abstract.
// If this set has exactly one element, return that element.
methodOop Dependencies::find_unique_concrete_method(klassOop ctxk, methodOop m) {
  ClassHierarchyWalker wf(m);
  assert(wf.check_method_context(ctxk, m), "proper context");
  wf.record_witnesses(1);
  klassOop wit = wf.find_witness_definer(ctxk);
  if (wit != NULL)  return NULL;  // Too many witnesses.
  methodOop fm = wf.found_method(0);  // Will be NULL if num_parts == 0.
  if (Dependencies::is_concrete_method(m)) {
    if (fm == NULL) {
      // It turns out that m was always the only implementation.
      fm = m;
    } else if (fm != m) {
      // Two conflicting implementations after all.
      // (This can happen if m is inherited into ctxk and fm overrides it.)
      return NULL;
    }
  }
  return fm;
}

klassOop Dependencies::check_exclusive_concrete_methods(klassOop ctxk,
                                                        methodOop m1,
                                                        methodOop m2,
                                                        DepChange* changes) {
  ClassHierarchyWalker wf(m1);
  wf.add_participant(m1->method_holder());
  wf.add_participant(m2->method_holder());
  return wf.find_witness_definer(ctxk, changes);
}

// Find the set of all non-abstract methods under ctxk that match m[0].
// (The method m[0] must be defined or inherited in ctxk.)
// Include m itself in the set, unless it is abstract.
// Fill the given array m[0..(mlen-1)] with this set, and return the length.
// (The length may be zero if no concrete methods are found anywhere.)
// If there are too many concrete methods to fit in marray, return -1.
int Dependencies::find_exclusive_concrete_methods(klassOop ctxk,
                                                  int mlen,
                                                  methodOop marray[]) {
  methodOop m0 = marray[0];
  ClassHierarchyWalker wf(m0);
  assert(wf.check_method_context(ctxk, m0), "proper context");
  wf.record_witnesses(mlen);
  bool participants_hide_witnesses = true;
  klassOop wit = wf.find_witness_definer(ctxk);
  if (wit != NULL)  return -1;  // Too many witnesses.
  int num = wf.num_participants();
  assert(num <= mlen, "oob");
  // Keep track of whether m is also part of the result set.
  int mfill = 0;
  assert(marray[mfill] == m0, "sanity");
  if (Dependencies::is_concrete_method(m0))
    mfill++;  // keep m0 as marray[0], the first result
  for (int i = 0; i < num; i++) {
    methodOop fm = wf.found_method(i);
    if (fm == m0)  continue;  // Already put this guy in the list.
    if (mfill == mlen) {
      return -1;              // Oops.  Too many methods after all!
    }
    marray[mfill++] = fm;
  }
  return mfill;
}


klassOop Dependencies::check_has_no_finalizable_subclasses(klassOop ctxk, DepChange* changes) {
  Klass* search_at = ctxk->klass_part();
  if (changes != NULL)
    search_at = changes->new_type()->klass_part(); // just look at the new bit
  Klass* result = find_finalizable_subclass(search_at);
  if (result == NULL) {
    return NULL;
  }
  return result->as_klassOop();
}

#ifndef PRODUCT
void Dependencies::print_statistics() {
  if (deps_find_witness_print != 0) {
    // Call one final time, to flush out the data.
    deps_find_witness_print = -1;
    count_find_witness_calls();
  }
}
#endif
