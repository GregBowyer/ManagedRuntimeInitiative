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

/* 
 * stubilicious.cpp
 *
 * We're removing methods that aren't used in the Azul version 
 * of OpenJDK/Hotspot.  In most cases, we've deleted the associated
 * files and removed all references to the missing methods.  In a 
 * few cases, we've removed the files, but haven't finished removing
 * all referencs to the missing methods.  
 *
 * In order to link without error *and* to detect if we ever reference 
 * any of these missing methods at runtime, we have redefined the 
 * methods here and implemented them as breakpoint instructions.
 *
 */
extern "C" { 
  void _ZNK28OneContigSpaceCardGeneration11used_regionEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation12max_capacityEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration8print_onEP12outputStream() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap18attempt_allocationEmbb() { __asm__ __volatile__("int $0x03"); }
  void _ZNK13ParMarkBitMap19live_words_in_rangeEP8HeapWordP7oopDesc() { __asm__ __volatile__("int $0x03"); }
  void _ZTV10OopClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN15ContiguousSpace35set_concurrent_iteration_safe_limitEP8HeapWord() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation16space_containingEPKv() { __asm__ __volatile__("int $0x03"); }
  void _ZN27BlockOffsetArrayContigSpace16alloc_block_workEP8HeapWordS1_() { __asm__ __volatile__("int $0x03"); }
  void _ZN14HeapInspection27find_instances_at_safepointEP12klassOopDescP13GrowableArrayIP7oopDescE() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager14_manager_arrayE() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration4usedEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK13ParMarkBitMap7iterateEP20ParMarkBitMapClosuremm() { __asm__ __volatile__("int $0x03"); }
  void _ZN21UpdateDensePrefixTaskC1EN17PSParallelCompact7SpaceIdEmm() { __asm__ __volatile__("int $0x03"); }
  void _ZN24NativeAllocationTemplate8set_heapEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation21print_summary_info_onEP12outputStream() { __asm__ __volatile__("int $0x03"); }
  void _ZN16HistogramElement5countEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN14HeapInspection15heap_inspectionEP12outputStream() { __asm__ __volatile__("int $0x03"); }
  void _ZN21InstructionTraceArrayC1EPh() { __asm__ __volatile__("int $0x03"); }
  void _ZTV11VoidClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN7GCCause9to_stringENS_5CauseE() { __asm__ __volatile__("int $0x03"); }
  void _ZN23PermanentGenerationSpecC1EN7PermGen4NameEmm() { __asm__ __volatile__("int $0x03"); }
  void _ZN2os8perfcnt1Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration20contiguous_availableEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField28generate_fast_get_byte_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration11gc_epilogueEb() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation22prepare_for_compactionEP12CompactPoint() { __asm__ __volatile__("int $0x03"); }
  void _ZN27BlockOffsetArrayContigSpace20initialize_thresholdEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN7oopDesc18relock_recursivelyEi() { __asm__ __volatile__("int $0x03"); }
  void _ZN12ASPSYoungGenC1EP14PSVirtualSpacemmm() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration21unsafe_max_alloc_nogcEv() { __asm__ __volatile__("int $0x03"); }
  void _ZTV28OneContigSpaceCardGeneration() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField28generate_fast_get_char_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN13CollectedHeap27reset_promotion_should_failEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManagerD1Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation11oop_iterateE9MemRegionP10OopClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN18ObjectSynchronizer16monitors_iterateEP14MonitorClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation5printEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK16GenCollectedHeap14is_in_youngestEPKv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap24must_clear_all_soft_refsEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration28object_iterate_since_last_GCEP13ObjectClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN18GenerationCountersC1EPKciiP12VirtualSpace() { __asm__ __volatile__("int $0x03"); }
  void _ZN10OopClosure14do_derived_oopEP9objectRefS1_() { __asm__ __volatile__("int $0x03"); }
  void _ZN24NativeAllocationTemplate9set_stackEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration20younger_refs_iterateEP16OopsInGenClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN16BlockOffsetArrayC2EP22BlockOffsetSharedArray9MemRegionb() { __asm__ __volatile__("int $0x03"); }
  void _ZN16StealMarkingTaskC1EP22ParallelTaskTerminator() { __asm__ __volatile__("int $0x03"); }
  void _ZNK13ParMarkBitMap7iterateEP20ParMarkBitMapClosureS1_mmm() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap13do_collectionEbbmbi() { __asm__ __volatile__("int $0x03"); }
  void _ZN10PerfMemory20create_memory_regionEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration19expand_and_allocateEmbb() { __asm__ __volatile__("int $0x03"); }
  void _ZN23InstructionTraceManager12addFullTraceEP21InstructionTraceArray() { __asm__ __volatile__("int $0x03"); }
  void _ZNK16BlockOffsetArray6verifyEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation11par_promoteEiP7oopDescP8markWordm() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField29generate_fast_get_short_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN21InstructionTraceArray5clearEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN2os14perfcnt1_eventEv() { __asm__ __volatile__("int $0x03"); }
  void _ZTV27BlockOffsetArrayContigSpace() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration8allocateEmb() { __asm__ __volatile__("int $0x03"); }
  void _ZN25InstructionTraceRecording10deactivateEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration7collectEbbmb() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation12print_xml_onEP9xmlBufferb() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation7promoteEP7oopDescmP9objectRef() { __asm__ __volatile__("int $0x03"); }
  void _ZTV19RefProcTaskExecutor() { __asm__ __volatile__("int $0x03"); }
  void _ZN16MutableNUMASpaceC1Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZNK22BlockOffsetSharedArray9index_forEPKv() { __asm__ __volatile__("int $0x03"); }
  void _ZN22OffsetTableContigSpace12par_allocateEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN11OrderAccess8loadloadEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK22BlockOffsetSharedArray16is_card_boundaryEP8HeapWord() { __asm__ __volatile__("int $0x03"); }
  void _ZN6Atomic7cmpxchgEaPVaa() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager12_chunk_arrayE() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation17attempt_promotionEP7oopDescmP9objectRef() { __asm__ __volatile__("int $0x03"); }
  //void _ZN18AllocationProfiler9disengageEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration22first_compaction_spaceEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManagerC1Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation25promotion_attempt_is_safeEmb() { __asm__ __volatile__("int $0x03"); }
  void _ZN16HistogramElement7compareEPS_S0_() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField29generate_fast_get_float_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager17save_for_scanningEP7oopDesc() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField30generate_fast_get_double_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation11block_startEPKv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK6BitMap17find_next_one_bitEmm() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField28generate_fast_get_long_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16HistogramElement4nameEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN22OffsetTableContigSpace8allocateEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN11CardTableRSC1E9MemRegioni() { __asm__ __volatile__("int $0x03"); }
  void _ZN7oopDesc15update_contentsEP20ParCompactionManager() { __asm__ __volatile__("int $0x03"); }
  void _ZN23InstructionTraceManager12getFullTraceEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration14object_iterateEP13ObjectClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap25satisfy_failed_allocationEmb() { __asm__ __volatile__("int $0x03"); }
  //void _ZN18AllocationProfiler6engageEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN7oopDesc15follow_contentsEP20ParCompactionManager() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration18prepare_for_verifyEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN21InstructionTraceArrayC1Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZN23InstructionTraceManager13getEmptyTraceEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager5resetEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN7oopDesc13follow_headerEP20ParCompactionManager() { __asm__ __volatile__("int $0x03"); }
  void _ZTV18ObjectToOopClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager18should_verify_onlyEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager10initializeEP13ParMarkBitMap() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation18print_summary_infoEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation22par_promote_alloc_undoEiP8HeapWordm() { __asm__ __volatile__("int $0x03"); }
  void _ZN13ParMarkBitMap8mark_objEP8HeapWordm() { __asm__ __volatile__("int $0x03"); }
  void _ZNK22OffsetTableContigSpace11block_startEPKv() { __asm__ __volatile__("int $0x03"); }
  //void _ZN18AllocationProfiler5printEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN14CSpaceCountersC1EPKcimP15ContiguousSpaceP18GenerationCounters() { __asm__ __volatile__("int $0x03"); }
  void _ZTV17MarkFromRootsTask() { __asm__ __volatile__("int $0x03"); }
  void _ZNK10Generation24max_contiguous_availableEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN13ParMarkBitMap14reset_countersEv() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration8capacityEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN15FastScanClosure9do_oop_nvEP9objectRef() { __asm__ __volatile__("int $0x03"); }
  void _ZTV14ImmutableSpace() { __asm__ __volatile__("int $0x03"); }
  void _ZN13ParMarkBitMap10initializeE9MemRegion() { __asm__ __volatile__("int $0x03"); }
  void _ZTV22ThreadRootsMarkingTask() { __asm__ __volatile__("int $0x03"); }
  void _ZN11MemProfiler9disengageEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10SharedHeap3_shE() { __asm__ __volatile__("int $0x03"); }
  //void _ZN18AllocationProfiler7destroyEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN18ObjectSynchronizer20summarize_sma_statusEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration10save_marksEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10ASPSOldGenC1EP14PSVirtualSpacemmmPKci() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager19save_for_processingEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN20ParCompactionManager17should_reset_onlyEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration7grow_byEm() { __asm__ __volatile__("int $0x03"); }
  void _ZN11MemProfiler6engageEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN7WLMuxer9print_xmlEPN6azprof7RequestEPNS0_8ResponseE() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration12par_allocateEmb() { __asm__ __volatile__("int $0x03"); }
  void _ZNK16HistogramElement8print_onEP12outputStream() { __asm__ __volatile__("int $0x03"); }
  void _ZNK13ParMarkBitMap12verify_clearEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16HistogramElement15increment_countEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation18ref_processor_initEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN25InstructionTraceRecording8activateEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN13LIR_Assembler17emit_profile_callEP17LIR_OpProfileCall() { __asm__ __volatile__("int $0x03"); }
  void _ZN25InstructionTraceRecording7_activeE() { __asm__ __volatile__("int $0x03"); }
  void _ZN9HistogramC1EPKci() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration31oop_since_save_marks_iterate_nvEP15FastScanClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration26no_allocs_since_save_marksEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN2os26set_abort_on_out_of_memoryEb() { __asm__ __volatile__("int $0x03"); }
  void _ZN24StealChunkCompactionTaskC1EP22ParallelTaskTerminator() { __asm__ __volatile__("int $0x03"); }
  void _ZN15ContiguousSpace31concurrent_iteration_safe_limitEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN23InstructionTraceManager17addIDToThreadNameElPKc() { __asm__ __volatile__("int $0x03"); }
  void _ZN10Generation11oop_iterateEP10OopClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeapC1EP18GenCollectorPolicy() { __asm__ __volatile__("int $0x03"); }
  void _ZN22SequentialSubTasksDone5clearEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN14CardGenerationC2E13ReservedSpacemiP9GenRemSet() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap18do_full_collectionEbi() { __asm__ __volatile__("int $0x03"); }
  void _ZNK28OneContigSpaceCardGeneration4freeEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16GenCollectedHeap4heapEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN18JniPeriodicChecker6engageEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField27generate_fast_get_int_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN11ScanClosure9do_oop_nvEP9objectRef() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration13space_iterateEP12SpaceClosureb() { __asm__ __volatile__("int $0x03"); }
  void _ZN16JNI_FastGetField31generate_fast_get_boolean_fieldEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN10PerfMemory20delete_memory_regionEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN23InstructionTraceManager13addEmptyTraceEP21InstructionTraceArray() { __asm__ __volatile__("int $0x03"); }
  void _ZN7WLMuxer5resetEPN6azprof7RequestEPNS0_8ResponseE() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration31oop_since_save_marks_iterate_nvEP11ScanClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZN2os9timeofdayEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN27BlockOffsetArrayContigSpace9serializeEP19SerializeOopClosure() { __asm__ __volatile__("int $0x03"); }
  void _ZTV25DrainStacksCompactionTask() { __asm__ __volatile__("int $0x03"); }
  void _ZN16HistogramElementC2Ev() { __asm__ __volatile__("int $0x03"); }
  void _ZN13LIR_Assembler15monitor_addressEiP11LIR_OprDesc() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration17reset_saved_marksEv() { __asm__ __volatile__("int $0x03"); }
  void _ZN28OneContigSpaceCardGeneration30oop_since_save_marks_iterate_vEP16OopsInGenClosure() { __asm__ __volatile__("int $0x03"); }
}
