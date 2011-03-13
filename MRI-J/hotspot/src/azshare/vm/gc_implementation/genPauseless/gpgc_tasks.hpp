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

#ifndef GPGC_TASKS_HPP
#define GPGC_TASKS_HPP

#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_population.hpp"
#include "pgcTaskManager.hpp"


// gpgc_tasks.hpp is a collection of GCTasks used by the GenPauselessGC.
//

//
// GPGC_OldGC_MarkRootsTaskConcurrent
//
// This task scans roots concurrently
//
// 

class GPGC_OldGC_MarkRootsTaskConcurrent: public PGCTask {
public:
  GPGC_OldGC_MarkRootsTaskConcurrent()  {}
  
  const char* name();

  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_MarkRootsTask
//
// This task scans all the roots of a given type.
// 

class GPGC_NewGC_MarkRootsTask : public PGCTask {
 public:
  enum RootType {
    universe                        =  1,
    jni_handles                     =  2,
object_synchronizer=3,
system_dictionary=4,
vm_symbols=5,
jvmti=6,
    arta_objects                    =  7, // Only strong roots during NewGC!
    management                      =  8
  }; 
 private:
  RootType _root_type;
 public:
GPGC_NewGC_MarkRootsTask(RootType value):_root_type(value){}
  const char* name();
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_VMThreadMarkTask
//
// This task scans the roots of the VMThread.
// 

class GPGC_NewGC_VMThreadMarkTask : public PGCTask {
 public:
const char*name(){return"gpgc-newgc-vmthread-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_VMThreadMarkTask
//
// This task scans the roots of the VMThread.
// 

class GPGC_OldGC_VMThreadMarkTask : public PGCTask {
 public:
const char*name(){return"gpgc-oldgc-vmthread-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_InitReadTrapArrayTask
//
// This task initializes the readbarrier array
// 

class GPGC_InitReadTrapArrayTask : public PGCTask {
  private:
    PageNum _task_start_page;
    PageNum _task_end_page;
    long    _block_size;
    bool    _in_large_space;

  public:
    GPGC_InitReadTrapArrayTask(PageNum task_start_page, PageNum task_end_page, long block_size, bool in_large_space)
                              : _task_start_page (task_start_page),
                                _task_end_page (task_end_page),
                                _block_size (block_size), 
                                _in_large_space (in_large_space) { }
const char*name(){return"gpgc-init-read-trap-array-task";}
    virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_UpdateRelocateStatesTask
//
// Prep the GPGC_PageInfo's of pages about to be relocated.
//

class GPGC_NewGC_UpdateRelocateStatesTask: public PGCTask {
 private:
  uint64_t _section;
  uint64_t _sections;
 public:
  GPGC_NewGC_UpdateRelocateStatesTask(uint64_t section,uint64_t sections) : _section(section), _sections(sections) { }
const char*name(){return"gpgc-newgc-update-relocate-states-task";}
  virtual void do_it(uint64_t which);

  void do_page_array   (GPGC_PopulationArray* relocation_array);
  void do_block_array  (GPGC_PopulationArray* relocation_array);
};


//
// GPGC_OldGC_UpdateRelocateStatesTask
//
// Prep the GPGC_PageInfo's of pages about to be relocated.
//

class GPGC_OldGC_UpdateRelocateStatesTask: public PGCTask {
 private:
  uint64_t            _section;
  uint64_t            _sections;
  GPGC_PageInfo::Gens _gen;

 public:
  GPGC_OldGC_UpdateRelocateStatesTask(uint64_t section, uint64_t sections, GPGC_PageInfo::Gens gen)
                                     : _section(section), _sections(sections), _gen(gen) { }
  const char* name() {
    if (_gen == GPGC_PageInfo::OldGen) {
return"gpgc-oldgc-update-old-relocate-states-task";
    } else  {
return"gpgc-oldgc-update-perm-relocate-states-task";
    }
  }
  virtual void do_it(uint64_t which);

  void do_page_array (GPGC_PopulationArray* relocation_array);
  void do_block_array(GPGC_PopulationArray* relocation_array);
};


//
// GPGC_OnePageScanCardMarksTask
//
// This task scans a page's (or block's) cardmarks looking for old-to-new refs
//

class GPGC_OnePageScanCardMarksTask : public PGCTask {
  private:
    long _section;
    long _sections;
  public:
    GPGC_OnePageScanCardMarksTask(long section,long sections) : _section(section), _sections(sections) { }
const char*name(){return"gpgc-one-page-scan-cardmarks-task";}
    virtual void do_it(uint64_t which);
};


//
// GPGC_MultiPageScanCardMarksTask
//
// This task scans a page's (or block's) cardmarks looking for old-to-new refs
//

class GPGC_MultiPageScanCardMarksTask: public PGCTask {
  private:
    PageNum _page;
    long    _chunk;
    long    _chunks;
 public:
  GPGC_MultiPageScanCardMarksTask(PageNum page, long chunk, long chunks) : _page(page), _chunk(chunk), _chunks(chunks) {}
const char*name(){return"gpgc-multi-page-scan-cardmarks-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OnePageHeapIterateTask
//
// This task iterates through all objects in a page or block
//

class GPGC_OnePageHeapIterateTask : public PGCTask {
  private:
    long _section;
    long _sections;
    GPGC_OnePageSpace* _one_space; // mid or small space
ObjectClosure*_closure;
  public:
    GPGC_OnePageHeapIterateTask(GPGC_OnePageSpace* one_space, long section, long sections, ObjectClosure* closure) : 
                                _one_space(one_space), _section(section), _sections(sections), _closure(closure) { }
const char*name(){return"gpgc-one-page-heap-iterate-task";}
    virtual void do_it(uint64_t which);
};


//
// GPGC_MultiPageHeapIterateTask
//
// This task iterates through all objects in a page or block
//

class GPGC_MultiPageHeapIterateTask: public PGCTask {
  private:
    PageNum _page;
ObjectClosure*_closure;
  public:
    GPGC_MultiPageHeapIterateTask(PageNum page, ObjectClosure* closure) : _page(page), _closure(closure) {}
const char*name(){return"gpgc-multi-page-heap-iterate-task";}
    virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_StrongMarkTask
//
// This task is used to drain the NewGC StrongLive marking stacks, with lock-free work
// stealing to distribute work to idle threads.
// 

class GPGC_NewGC_StrongMarkTask : public PGCTask {
 public:
  GPGC_NewGC_StrongMarkTask();
const char*name(){return"gpgc-newgc-strong-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_StrongMarkTask
//
// This task is used to drain the OldGC StrongLive marking stacks, with lock-free work
// stealing to distribute work to idle threads.
// 

class GPGC_OldGC_StrongMarkTask : public PGCTask {
 public:
  GPGC_OldGC_StrongMarkTask();
const char*name(){return"gpgc-oldgc-strong-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_FinalMarkTask
//
// This task is used to drain the NewGC FinalLive marking stacks, with lock-free work
// stealing to distribute work to idle threads.
// 

class GPGC_NewGC_FinalMarkTask : public PGCTask {
 public:
  GPGC_NewGC_FinalMarkTask();
const char*name(){return"gpgc-newgc-final-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_FinalMarkTask
//
// This task is used to drain the OldGC FinalLive marking stacks, with lock-free work
// stealing to distribute work to idle threads.
// 

class GPGC_OldGC_FinalMarkTask : public PGCTask {
 public:
  GPGC_OldGC_FinalMarkTask();
const char*name(){return"gpgc-oldgc-final-mark-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_SidebandForwardingInitTask
//
// This task initializes sideband forwarding arrays for pages about to be relocated.
// 

class GPGC_NewGC_SidebandForwardingInitTask : public PGCTask {
 private:
  long _work_unit;
 public:
  GPGC_NewGC_SidebandForwardingInitTask(long work_unit) : _work_unit(work_unit) {}
const char*name(){return"gpgc-newgc-sideband-forwarding-init-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_SidebandForwardingInitTask
//
// This task initializes sideband forwarding arrays for pages about to be relocated.
// 

class GPGC_OldGC_SidebandForwardingInitTask : public PGCTask {
 private:
  long _work_unit;
 public:
  GPGC_OldGC_SidebandForwardingInitTask(long work_unit) : _work_unit(work_unit) {}
const char*name(){return"gpgc-oldgc-sideband-forwarding-init-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_UnloadDictionarySectionTask
//
// This task unloads unmarked classes during second safepoint.
//

class GPGC_OldGC_UnloadDictionarySectionTask : public PGCTask {
 private:
  long     _sections;
  long     _section;
 public:
  GPGC_OldGC_UnloadDictionarySectionTask(long section, long sections)
          : _section(section), _sections(sections)
          {}
const char*name(){return"gpgc-oldgc-unload-dictionary-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_UnloadLoaderConstraintSectionTask
//
// This task cleans the loader constraints during second safepoint.
//

class GPGC_OldGC_UnloadLoaderConstraintSectionTask : public PGCTask {
 private:
  long     _sections;
  long     _section;
 public:
  GPGC_OldGC_UnloadLoaderConstraintSectionTask(long section, long sections)
          : _section(section), _sections(sections)
          {}
const char*name(){return"gpgc-oldgc-loader-constraint-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_StealRevisitKlassTask
//
// This task adjusts the live classes' subklass info after class unloading during second safepoint.
// 

class GPGC_OldGC_StealRevisitKlassTask : public PGCTask {
 public:
  GPGC_OldGC_StealRevisitKlassTask();
const char*name(){return"gpgc-oldgc-steal-revisit-klass-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_UnlinkKlassTableTask
//
// This task unlinks the KlassTable in stripes.
//

class GPGC_OldGC_UnlinkKlassTableTask : public PGCTask {
 private:
  const long _sections;
  const long _section;
 public:
  GPGC_OldGC_UnlinkKlassTableTask(long section, long sections)
          : _section(section), _sections(sections)
          {}
const char*name(){return"gpgc-oldgc-unlink-klass-table-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_UnlinkCodeCacheOopTableTask
//
// This task unlinks the CodeCacheOopTable in stripes 
//

class GPGC_OldGC_UnlinkCodeCacheOopTableTask: public PGCTask {
 private:
  const long  _sections;
  const long  _section;
 public:
  GPGC_OldGC_UnlinkCodeCacheOopTableTask(long section, long sections)
          : _section(section), _sections(sections)
          {}
const char*name(){return"gpgc-oldgc-unlink-code-cache-oop-table-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_UnlinkWeakSymbolsTask
//
// This task concurrently scans weak symbols and strings, and clears the ones that aren't used anymore.
//

class GPGC_OldGC_UnlinkWeakRootSectionTask : public PGCTask {
 public:
  enum RootType {
    symbol_table = 1,
    string_table = 2
  };
 private:
  RootType _root_type;
  long     _sections;
  long     _section;
 public:
  GPGC_OldGC_UnlinkWeakRootSectionTask(RootType type, long section, long sections)
          : _root_type(type), _section(section), _sections(sections)
          {}
const char*name(){return"gpgc-oldgc-unlink-weak-root-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_RelocateMidPagesTask
//
// This task remaps live objects from mid space pages during phase 2 of a NewGC.
//

class GPGC_NewGC_RelocateMidPagesTask : public PGCTask {
 private:
  long    _work_unit;
  int64_t _stripe;
 public:
  GPGC_NewGC_RelocateMidPagesTask(long work_unit, int64_t stripe) : _work_unit(work_unit), _stripe(stripe) {}
const char*name(){return"gpgc-newgc-relocate-mid-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_HealMidPagesTask
//
// This task heals remapped mid space objects into 2 MB pages during phase 2 of a NewGC.
//

class GPGC_NewGC_HealMidPagesTask : public PGCTask {
 private:
  int64_t _stripe;
 public:
  GPGC_NewGC_HealMidPagesTask(int64_t stripe) : _stripe(stripe) {}
const char*name(){return"gpgc-newgc-heal-mid-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_RelocateSmallPagesTask
//
// This task relocates small space pages during phase 2 of a NewGC.
//

class GPGC_NewGC_RelocateSmallPagesTask : public PGCTask {
 private:
  long _work_unit;
 public:
  GPGC_NewGC_RelocateSmallPagesTask(long work_unit) : _work_unit(work_unit) {}
const char*name(){return"gpgc-newgc-relocate-small-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_RelocateMidPagesTask
//
// This task remaps live objects from mid space pages during phase 2 of a NewGC.
//

class GPGC_OldGC_RelocateMidPagesTask : public PGCTask {
 private:
  long                  _work_unit;
  GPGC_PopulationArray* _relocation_array;
  int64_t               _stripe;
 public:
  GPGC_OldGC_RelocateMidPagesTask(long work_unit, GPGC_PopulationArray* array, int64_t stripe)
                                 : _work_unit(work_unit), _relocation_array(array), _stripe(stripe) {}
const char*name(){return"gpgc-oldgc-relocate-mid-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_HealMidPagesTask
//
// This task heals remapped mid space objects into 2 MB pages during phase 2 of an OldGC.
//

class GPGC_OldGC_HealMidPagesTask : public PGCTask {
 private:
  int64_t _stripe;
 public:
  GPGC_OldGC_HealMidPagesTask(int64_t stripe) : _stripe(stripe) {}
const char*name(){return"gpgc-oldgc-heal-mid-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_RelocateSmallPagesTask
//
// This task relocates pages during phase 2 of an OldGC.
//

class GPGC_OldGC_RelocateSmallPagesTask : public PGCTask {
 private:
  long                  _work_unit;
  GPGC_PopulationArray* _relocation_array;

 public:
  GPGC_OldGC_RelocateSmallPagesTask(long work_unit, GPGC_PopulationArray* relocation_array)
     : _work_unit(work_unit), _relocation_array(relocation_array) {}
const char*name(){return"gpgc-oldgc-relocate-small-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_RelocatePageStripeTask
//
// This task relocates pages during phase 2 of an OldGC.
//

/*
class GPGC_OldGC_RelocatePageStripeTask : public PGCTask {
 private:
  long _stripe_start;
  long _gen;
 public:
  GPGC_OldGC_RelocatePageStripeTask(long index, long gen) : _stripe_start(index), _gen(gen) {}
  const char* name() { return "gpgc-oldgc-relocate-page-stripe-task"; }
  virtual void do_it(uint64_t which);
};
*/


//
// GPGC_NewGC_CardMarkBlockTask
//
// This task card-marks multi-page objects promoted to OldGen during phase 2 of a NewGC.
// 

class GPGC_NewGC_CardMarkBlockTask : public PGCTask {
 private:
  PageNum _block;
 public:
  GPGC_NewGC_CardMarkBlockTask(PageNum block) : _block(block) {}
const char*name(){return"gpgc-newgc-card-mark-block-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_CardMarkPagesTask
//
// This task card-marks objects promoted to OldGen during phase 2 of a NewGC.
// 

class GPGC_NewGC_CardMarkPagesTask : public PGCTask {
 private:
  long _work_unit;
 public:
  GPGC_NewGC_CardMarkPagesTask(long work_unit) : _work_unit(work_unit) {}
const char*name(){return"gpgc-newgc-card-mark-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_CardMarkPagesTask
//
// This task card-marks objects relocated from Old Space phase 2 of an OldGC.
// 

class GPGC_OldGC_CardMarkPagesTask : public PGCTask {
 private:
GPGC_PopulationArray*_array;
  long                  _work_unit;
 public:
  GPGC_OldGC_CardMarkPagesTask(GPGC_PopulationArray* array, long work_unit) : _array(array), _work_unit(work_unit) {}
const char*name(){return"gpgc-oldgc-card-mark-pages-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_RefsTask
//
// This task handles soft/weak/final/phantom/jni refs.
//

class GPGC_NewGC_RefsTask : public PGCTask {
 private:
GPGC_ReferenceList*_list;
 public:
  GPGC_NewGC_RefsTask(GPGC_ReferenceList* list) : _list(list) {}
  
  const char* name();

  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_RefsTask
//
// This task handles soft/weak/final/phantom/jni refs.
//

class GPGC_OldGC_RefsTask : public PGCTask {
 private:
GPGC_ReferenceList*_list;
 public:
  GPGC_OldGC_RefsTask(GPGC_ReferenceList* list) : _list(list) {}
  
  const char* name();

  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_ClearOneSpaceMarksTask
//
// clears page marks in GPGC_OneSpace NewGen pages
//

class GPGC_NewGC_ClearOneSpaceMarksTask: public PGCTask {
 private:
  long _section;
  long _sections;
 public:
  GPGC_NewGC_ClearOneSpaceMarksTask(long section,long sections) : _section(section), _sections(sections) { }
const char*name(){return"gpgc-newgc-clear-one-space-marks-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_NewGC_ClearMultiSpaceMarksTask
//
// clears page marks in GPGC_MultiSpace NewGen pages
//

class GPGC_NewGC_ClearMultiSpaceMarksTask: public PGCTask {
 public:
  GPGC_NewGC_ClearMultiSpaceMarksTask() { }
const char*name(){return"gpgc-newgc-clear-multi-space-marks-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_ClearOneSpaceMarksTask
//
// clears page marks in GPGC_OneSpace OldGen & PermGen pages
//

class GPGC_OldGC_ClearOneSpaceMarksTask: public PGCTask {
 private:
  long _section;
  long _sections;
 public:
  GPGC_OldGC_ClearOneSpaceMarksTask(long section,long sections) : _section(section), _sections(sections) { }
const char*name(){return"gpgc-oldgc-clear-one-space-marks-task";}
  virtual void do_it(uint64_t which);
};


//
// GPGC_OldGC_ClearMultiSpaceMarksTask
//
// clears page marks in GPGC_MultiSpace OldGen & PermGen pages
//

class GPGC_OldGC_ClearMultiSpaceMarksTask: public PGCTask {
 public:
  GPGC_OldGC_ClearMultiSpaceMarksTask() { }
const char*name(){return"gpgc-oldgc-clear-multi-space-marks-task";}
  virtual void do_it(uint64_t which);
};


#endif // GPGC_TASKS_HPP
