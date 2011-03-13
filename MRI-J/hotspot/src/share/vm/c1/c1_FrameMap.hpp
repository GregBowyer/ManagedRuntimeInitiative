/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_FRAMEMAP_HPP
#define C1_FRAMEMAP_HPP


#include "c1_Compilation.hpp"
#include "c1_Defs_pd.hpp"
#include "c1_LIR.hpp"
#include "c1_MacroAssembler.hpp"
#include "register_pd.hpp"

class ciMethod;
class CallingConvention;
class BasicTypeArray;
class BasicTypeList;

//--------------------------------------------------------
//               FrameMap
//--------------------------------------------------------

//  This class is responsible of mapping items (locals/incoming args,
//  callee saves, monitors, spill slots and registers to their frame location).
//
//  The monitors are specified by a consecutive index. The monitor_index is
//  0.._num_monitors.
//
//  The spill index is similar to local index; it is in range 0..(open)
//
//  The CPU registers are mapped using a fixed table; register with number 0
//  is the most used one.
//
//   stack growth direction -->
//       old SP                                                                  SP
//  +-----------+---+--------------+----------+-------+------------------------+-----+
//  | arguments | x | callee saves | monitors | spill | reserved argument area | ABI |
//  +-----------+---+--------------+----------+-------+------------------------+-----+
//
//  x = ABI area (SPARC) or return adress and link (i486)
//  ABI = ABI area (SPARC) or nothing (i486)
//
//  Sun x86 has no callee saves. Sun SPARC uses locals and ins for callee saves
//  and needs no explicit callee save area. AZUL x86 follows the x86-64 calling
//  conventions.

class LIR_OprDesc;
typedef LIR_OprDesc* LIR_Opr;

class FrameMap : public CompilationResourceObj {
 public:
  enum {
    nof_cpu_regs = pd_nof_cpu_regs_frame_map,

    nof_cpu_regs_reg_alloc = pd_nof_cpu_regs_reg_alloc,

    nof_caller_save_cpu_regs = pd_nof_caller_save_cpu_regs_frame_map,
#ifdef AZ_X86
spill_slot_size_in_bytes=8
#else
    spill_slot_size_in_bytes = 4
#endif
  };

# include "c1_FrameMap_pd.hpp"  // platform dependent declarations

  friend class LIR_OprDesc;

 private:
  static bool         _init_done;
  static Register     _cpu_rnr2reg [nof_cpu_regs];
  static int          _cpu_reg2rnr [nof_cpu_regs];

  static LIR_Opr      _caller_save_cpu_regs [nof_caller_save_cpu_regs];

  int                 _framesize;
  int                 _argcount;               // Number of arguments to method
  int                 _num_monitors;
  int                 _num_spills;
ByteSize _reserved_argument_area_size;
  int                 _oop_map_arg_count;
  int                 _callee_saves;           // Bit map of callee save registers allocated
  int                 _num_callee_saves;       // Finalized count of callee save registers

  CallingConvention*  _incoming_arguments;
intArray*_argument_locations;//Mapping of an argument number to a stack displacement or -1

  void check_spill_index   (int spill_index)   const { assert(spill_index   >= 0, "bad index"); }
  void check_monitor_index (int monitor_index) const { assert(monitor_index >= 0 &&
                                                              monitor_index < _num_monitors, "bad index"); }

  static Register cpu_rnr2reg (int rnr) {
    assert(_init_done, "tables not initialized");
    debug_only(cpu_range_check(rnr);)
#ifdef AZ_X86
    return (Register)rnr;
#else
    return _cpu_rnr2reg[rnr];
#endif
  }

  static int cpu_reg2rnr (Register reg) {
    assert(_init_done, "tables not initialized");
debug_only(cpu_range_check(reg);)
#ifdef AZ_X86
    return (int)reg;
#else
    return _cpu_reg2rnr[reg];
#endif
  }

  static void map_register(int rnr, Register reg) {
    debug_only(cpu_range_check(rnr);)
debug_only(cpu_range_check(reg);)
    _cpu_rnr2reg[rnr] = reg;
_cpu_reg2rnr[reg]=rnr;
  }

 protected:
#ifndef PRODUCT
  static void cpu_range_check (int rnr)          { assert(0 <= rnr && rnr < nof_cpu_regs, "cpu register number is too big"); }
#endif


  Address make_new_address(ByteSize sp_offset) const;

  ByteSize sp_offset_for_slot(const int idx) const;
  ByteSize sp_offset_for_double_slot(const int idx) const;
ByteSize sp_offset_for_argument(const int idx)const;
  ByteSize sp_offset_for_spill(const int idx) const;
  ByteSize sp_offset_for_monitor_object(int monitor_index) const;

  VReg   ::VR sp_offset2dbgvreg(ByteSize offset) const;
  VOopReg::VR sp_offset2oopvreg(ByteSize offset) const;

  // platform dependent hook used to check that frame is properly
  // addressable on the platform.  Used by sparc to verify that all
  // stack addresses are expressable in a simm13.
  bool validate_frame();

  static LIR_Opr map_to_opr(BasicType type, VReg::VR reg, bool incoming);

 public:
  // Opr representing the stack_pointer on this platform
  static LIR_Opr stack_pointer();

  static BasicTypeArray*     signature_type_array_for(const ciMethod* method);
  static BasicTypeArray*     signature_type_array_for(const char * signature);

  // Update reserved argument area as space is required during LIR generation
  void update_reserved_argument_area_size (ByteSize size) {
    assert(in_bytes(size) >= 0, "check");
    _reserved_argument_area_size = MAX2(_reserved_argument_area_size, size);
  }

  // for outgoing calls, these also update the reserved area to
  // include space for arguments and any ABI area.
  CallingConvention* c_calling_convention (const BasicTypeArray* signature);
  CallingConvention* java_calling_convention (const BasicTypeArray* signature, bool outgoing);

  static LIR_Opr as_opr(Register r) {
    return LIR_OprFact::single_cpu(cpu_reg2rnr(r));
  }
  static LIR_Opr as_oop_opr(Register r) {
    return LIR_OprFact::single_cpu_oop(cpu_reg2rnr(r));
  }

  FrameMap(ciMethod* method, int monitors, int reserved_argument_area_size);
  bool finalize_frame(int nof_slots);

  ByteSize reserved_argument_area_size () const { return _reserved_argument_area_size; }
  int      framesize                   () const { assert(_framesize  >= 0, "not set"); return _framesize; }
  ByteSize framesize_in_bytes          () const { return in_ByteSize(framesize() * spill_slot_size_in_bytes); }
  int      num_monitors                () const { return _num_monitors; }
  int      num_spills                  () const { assert(_num_spills >= 0, "not set"); return _num_spills; }
  int      argcount                    () const { assert(_argcount   >= 0, "not set"); return _argcount; }
  int      callee_saves                () const { return _callee_saves; }
  int      num_callee_saves            () const { return _num_callee_saves; }

  int oop_map_arg_count() const { return _oop_map_arg_count; }

  CallingConvention* incoming_arguments() const  { return _incoming_arguments; }

  // convenience routines
  Address address_for_slot(int index, int sp_adjust = 0) const {
    return make_new_address(sp_offset_for_slot(index) + in_ByteSize(sp_adjust));
  }
  Address address_for_double_slot(int index, int sp_adjust = 0) const {
    return make_new_address(sp_offset_for_double_slot(index) + in_ByteSize(sp_adjust));
  }
  Address address_for_monitor_object(int monitor_index) const {
    return make_new_address(sp_offset_for_monitor_object(monitor_index));
  }
  Address address_for_callee_save(int index) const {
    return make_new_address(sp_offset_for_callee_save(index));
  }
  ByteSize sp_offset_for_callee_save(int idx) const;

  void print_frame_layout() const;

  // Creates Location describing desired slot and returns it via pointer
  // to Location object. Returns true if the stack frame offset was legal
  // (as defined by Location::legal_offset_in_bytes()), false otherwise.
  // Do not use the returned location if this returns false.
  //bool location_for_sp_offset(ByteSize byte_offset_from_sp,
  //                            Location::Type loc_type, Location* loc) const;

  VReg::VR location_for_monitor_object(int monitor_index) const {
return sp_offset2dbgvreg(sp_offset_for_monitor_object(monitor_index));
    // return location_for_sp_offset(sp_offset_for_monitor_object(monitor_index), Location::oop, loc);
  }
  //bool locations_for_slot  (int index, Location::Type loc_type,
  //                          Location* loc, Location* second = NULL) const;


void record_callee_save_allocation(int i);

  VReg   ::VR slot_dbgregname(int index) const { return sp_offset2dbgvreg(sp_offset_for_slot(index));  }
  VOopReg::VR slot_oopregname(int index) const { return sp_offset2oopvreg(sp_offset_for_slot(index));  }
  VOopReg::VR max_oopregname() const           {
    return sp_offset2oopvreg(in_ByteSize((_framesize+_oop_map_arg_count)*spill_slot_size_in_bytes));
  }
  VReg   ::VR monitor_object_dbgregname(int monitor_index) const { return sp_offset2dbgvreg(sp_offset_for_monitor_object(monitor_index));  }
  VOopReg::VR monitor_object_oopregname(int monitor_index) const { return sp_offset2oopvreg(sp_offset_for_monitor_object(monitor_index));  }
  VReg   ::VR dbgregname(LIR_Opr opr) const;
  VOopReg::VR oopregname(LIR_Opr opr) const;

  static LIR_Opr caller_save_cpu_reg_at(int i) {
    assert(i >= 0 && i < nof_caller_save_cpu_regs, "out of bounds");
    return _caller_save_cpu_regs[i];
  }

  static void init();
};

//               CallingConvention
//--------------------------------------------------------

class CallingConvention: public ResourceObj {
 private:
  LIR_OprList* _args;
  int          _reserved_stack_slots;

 public:
  CallingConvention (LIR_OprList* args, int reserved_stack_slots)
    : _args(args)
    , _reserved_stack_slots(reserved_stack_slots)  {}

  LIR_OprList* args()       { return _args; }

  LIR_Opr at(int i) const   { return _args->at(i); }
  int length() const        { return _args->length(); }

  // Indicates number of real frame slots used by arguments passed on stack.
  int reserved_stack_slots() const            { return _reserved_stack_slots; }

#ifndef PRODUCT
  void print () const {
    for (int i = 0; i < length(); i++) {
      at(i)->print();
    }
  }
#endif // PRODUCT
};

#endif // C1_FRAMEMAP_HPP
