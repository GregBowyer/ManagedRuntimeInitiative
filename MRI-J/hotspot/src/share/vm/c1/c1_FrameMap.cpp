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


#include "bitOps.hpp"
#include "c1_FrameMap.hpp"
#include "c1_LIR.hpp"
#include "ciMethod.hpp"
#include "sharedRuntime.hpp"
#include "vreg.hpp"

#include "frame.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "register_pd.inline.hpp"

//-----------------------------------------------------

// Convert method signature into an array of BasicTypes for the arguments
BasicTypeArray* FrameMap::signature_type_array_for(const ciMethod* method) {
  ciSignature* sig = method->signature();
  BasicTypeList* sta = new BasicTypeList(method->arg_size());
  // add receiver, if any
  if (!method->is_static()) sta->append(T_OBJECT);
  // add remaining arguments
  for (int i = 0; i < sig->count(); i++) {
    ciType* type = sig->type_at(i);
    BasicType t = type->basic_type();
    if (t == T_ARRAY) {
      t = T_OBJECT;
    }
    sta->append(t);
  }
  // done
  return sta;
}


CallingConvention* FrameMap::java_calling_convention(const BasicTypeArray* signature, bool outgoing) {
  // compute the size of the arguments first.  The signature array
  // that java_calling_convention takes includes a T_VOID after double
  // work items but our signatures do not.
  int i;
  int sizeargs = 0;
  for (i = 0; i < signature->length(); i++) {
    sizeargs += type2size[signature->at(i)];
  }

  BasicType* sig_bt = NEW_RESOURCE_ARRAY(BasicType  , sizeargs);
VReg::VR*regs=NEW_RESOURCE_ARRAY(VReg::VR,sizeargs);
  int sig_index = 0;
  for (i = 0; i < sizeargs; i++, sig_index++) {
    sig_bt[i] = signature->at(sig_index);
    if (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) {
      sig_bt[i + 1] = T_VOID;
      i++;
    }
  }

  ByteSize out_preserve_bytes = SharedRuntime::java_calling_convention(sig_bt, regs, sizeargs, outgoing);
  LIR_OprList* args = new LIR_OprList(signature->length());
  for (i = 0; i < sizeargs;) {
    BasicType t = sig_bt[i];
    assert(t != T_VOID, "should be skipping these");

    LIR_Opr opr = map_to_opr(t, regs[i], outgoing);
    args->append(opr);
    if (opr->is_address()) {
      LIR_Address* addr = opr->as_address_ptr();
      assert(addr->disp() == (int)addr->disp(), "out of range value");
      out_preserve_bytes = MAX2(out_preserve_bytes, in_ByteSize(addr->disp()));
    }
    i += type2size[t];
  }
  assert(args->length() == signature->length(), "size mismatch");

  if (outgoing) {
    // update the space reserved for arguments.
update_reserved_argument_area_size(out_preserve_bytes);
  }
  return new CallingConvention(args, in_bytes(out_preserve_bytes) / spill_slot_size_in_bytes);
}


CallingConvention* FrameMap::c_calling_convention(const BasicTypeArray* signature) {
  // compute the size of the arguments first.  The signature array
  // that java_calling_convention takes includes a T_VOID after double
  // work items but our signatures do not.
  int i;
  int sizeargs = 0;
  for (i = 0; i < signature->length(); i++) {
    sizeargs += type2size[signature->at(i)];
  }

  BasicType* sig_bt = NEW_RESOURCE_ARRAY(BasicType, sizeargs);
VReg::VR*regs=NEW_RESOURCE_ARRAY(VReg::VR,sizeargs);
  int sig_index = 0;
  for (i = 0; i < sizeargs; i++, sig_index++) {
    sig_bt[i] = signature->at(sig_index);
    if (sig_bt[i] == T_LONG || sig_bt[i] == T_DOUBLE) {
      sig_bt[i + 1] = T_VOID;
      i++;
    }
  }

  ByteSize out_preserve_bytes = SharedRuntime::c_calling_convention(sig_bt, regs, sizeargs, false);
  LIR_OprList* args = new LIR_OprList(signature->length());
  for (i = 0; i < sizeargs;) {
    BasicType t = sig_bt[i];
    assert(t != T_VOID, "should be skipping these");

    // C calls are always outgoing
    bool outgoing = true;
LIR_Opr opr=map_to_opr(t,*(regs+i),outgoing);
    // they might be of different types if for instance floating point
    // values are passed in cpu registers, but the sizes must match.
    assert(type2size[opr->type()] == type2size[t], "type mismatch");
    args->append(opr);
    if (opr->is_address()) {
      LIR_Address* addr = opr->as_address_ptr();
      out_preserve_bytes = MAX2(out_preserve_bytes, in_ByteSize(addr->disp()));
    }
    i += type2size[t];
  }
  assert(args->length() == signature->length(), "size mismatch");
update_reserved_argument_area_size(out_preserve_bytes);
  return new CallingConvention(args, in_bytes(out_preserve_bytes) / spill_slot_size_in_bytes);
}


//--------------------------------------------------------
//               FrameMap
//--------------------------------------------------------

bool      FrameMap::_init_done = false;
Register  FrameMap::_cpu_rnr2reg [FrameMap::nof_cpu_regs];
int       FrameMap::_cpu_reg2rnr [FrameMap::nof_cpu_regs];


FrameMap::FrameMap(ciMethod* method, int monitors, int reserved_argument_area_size)
    : _reserved_argument_area_size(in_ByteSize(reserved_argument_area_size * BytesPerWord)) {
  if (!_init_done) init();

  assert(reserved_argument_area_size >= 0, "not set");
  _framesize = -1;
  _num_spills = -1;
  assert(monitors >= 0, "not set");
  _num_monitors = monitors;

  _argcount = method->arg_size();
  _argument_locations = new intArray(_argcount, -1);
  _incoming_arguments = java_calling_convention(signature_type_array_for(method), false);
  _oop_map_arg_count = _incoming_arguments->reserved_stack_slots();

  _num_callee_saves = -1;
  _callee_saves = 0;

  int java_index = 0;
  for (int i = 0; i < _incoming_arguments->length(); i++) {
    LIR_Opr opr = _incoming_arguments->at(i);
    if (opr->is_address()) {
      LIR_Address* address = opr->as_address_ptr();
      _argument_locations->at_put(java_index, address->disp() - STACK_BIAS);
      _incoming_arguments->args()->at_put(i, LIR_OprFact::stack(java_index, as_BasicType(as_ValueType(address->type()))));
    }
    java_index += type2size[opr->type()];
  }

}


bool FrameMap::finalize_frame(int nof_slots) {
  assert(nof_slots >= 0, "must be positive");

  assert(_num_spills == -1, "can only be set once");
  _num_spills = nof_slots;

assert(_num_callee_saves==-1,"can only be set once");
  _num_callee_saves = BitOps::number_of_ones((uint32_t)_callee_saves);

  assert(_framesize == -1, "should only be calculated once");
  int frame_size_in_bytes;
  while(1) {
    int bottom_of_stack_space = round_to(first_available_sp_in_frame + in_bytes(_reserved_argument_area_size), sizeof(double));
    int middle_of_stack_space = (_num_spills+_num_monitors+_num_callee_saves) * spill_slot_size_in_bytes;
    int top_of_stack_space    = frame_pad_in_bytes;
    frame_size_in_bytes = bottom_of_stack_space + middle_of_stack_space + top_of_stack_space;
    if (round_to(frame_size_in_bytes, StackAlignmentInBytes) != frame_size_in_bytes) {
      // place padding bytes at the bottom of the frame
      // NB this is a convenience for push/pops of callee saves, if there are no callee saves
      // then we pad at the top and possibly get displacement of 0 for spill slot 0.
      _reserved_argument_area_size = _reserved_argument_area_size + in_ByteSize(spill_slot_size_in_bytes);
      continue;
    }
    break;
  }
  _framesize = frame_size_in_bytes / spill_slot_size_in_bytes;

  int java_index = 0;
  for (int i = 0; i < _incoming_arguments->length(); i++) {
    LIR_Opr opr = _incoming_arguments->at(i);
    if (opr->is_stack()) {
      _argument_locations->at_put(java_index, in_bytes(framesize_in_bytes()) +
                                  _argument_locations->at(java_index));
    }
    java_index += type2size[opr->type()];
  }
  // make sure it's expressible on the platform
  return validate_frame();
}

void FrameMap::record_callee_save_allocation(int i) {
assert(i<32,"Supported architectures have fewer registers than this");
  _callee_saves |= 1<<i;
}

VReg::VR FrameMap::sp_offset2dbgvreg(ByteSize offset) const {
  int offset_in_bytes = in_bytes(offset);
  assert(offset_in_bytes % spill_slot_size_in_bytes == 0, "must be multiple of spill slot size in bytes");
  assert(offset_in_bytes / spill_slot_size_in_bytes < framesize() + oop_map_arg_count(), "out of range");
  return VReg::stk2reg(offset_in_bytes);
}

VOopReg::VR FrameMap::sp_offset2oopvreg(ByteSize offset) const {
  int offset_in_bytes = in_bytes(offset);
  assert(offset_in_bytes % spill_slot_size_in_bytes == 0, "must be multiple of spill slot size in bytes");
  assert(offset_in_bytes / spill_slot_size_in_bytes <= framesize() + oop_map_arg_count(), "out of range");
  return VOopReg::stk2reg(offset_in_bytes);
}

//////////////////////
// Public accessors //
//////////////////////

ByteSize FrameMap::sp_offset_for_slot(const int slot)const{
if(slot<argcount()){
ByteSize offset=sp_offset_for_argument(slot);
    assert(in_bytes(offset) >= framesize() * spill_slot_size_in_bytes, "argument inside of frame");
    return offset;
  } else {
ByteSize offset=sp_offset_for_spill(slot-argcount());
    assert(in_bytes(offset) <  framesize() * spill_slot_size_in_bytes, "spill outside of frame");
    return offset;
  }
}

ByteSize FrameMap::sp_offset_for_double_slot(const int slot)const{
return sp_offset_for_slot(slot);
}

ByteSize FrameMap::sp_offset_for_argument(const int arg)const{
  assert((arg == 0 && _argcount == 0) ||
         (arg >= 0 && arg < _argcount), "out of range");
int offset=_argument_locations->at(arg);
  assert(offset != -1, "not a memory argument");
  assert(offset >= framesize() * spill_slot_size_in_bytes, "argument inside of frame");
  return in_ByteSize(offset);
}

ByteSize FrameMap::sp_offset_for_spill(const int index) const {
assert((index==0&&_num_spills==0)||
(index>=0&&index<_num_spills),"out of range");
int offset=round_to(first_available_sp_in_frame+in_bytes(_reserved_argument_area_size),sizeof(double))+
               index * spill_slot_size_in_bytes;
  return in_ByteSize(offset);
}

ByteSize FrameMap::sp_offset_for_monitor_object(const int index)const{
  assert((index == 0 && _num_monitors == 0) ||
         (index >= 0 && index < _num_monitors), "out of range");
assert(_num_spills!=-1,"number of spills must be known");
  int end_of_spills = round_to(first_available_sp_in_frame + in_bytes(_reserved_argument_area_size), sizeof(double)) +
                      _num_spills * spill_slot_size_in_bytes;
int offset=end_of_spills+index*spill_slot_size_in_bytes;
  return in_ByteSize(offset);
}

ByteSize FrameMap::sp_offset_for_callee_save(int index)const{
  assert((index == 0 && _num_callee_saves == 0) ||
         (index >= 0 && index < _num_callee_saves), "out of range");
assert(_num_spills!=-1,"number of spills must be known");
assert(_num_monitors!=-1,"number of monitors must be known");
assert(index>=0,"out of range");
  int end_of_monitors = round_to(first_available_sp_in_frame + in_bytes(_reserved_argument_area_size), sizeof(double)) +
                      (_num_spills + _num_monitors) * spill_slot_size_in_bytes;
  int offset = end_of_monitors + index * spill_slot_size_in_bytes;
  return in_ByteSize(offset);
}

void FrameMap::print_frame_layout() const {
  int svar;
  tty->print_cr("#####################################");
  tty->print_cr("Frame size in words %d", framesize());

  if( _num_callee_saves > 0) {
tty->print_cr("callee save [0]:%d | [%2d]:%d",
                  in_bytes(sp_offset_for_callee_save(0)), _callee_saves-1,
                  in_bytes(sp_offset_for_monitor_object(_callee_saves-1)));
  }
  if( _num_monitors > 0) {
    tty->print_cr("monitor [0]:%d | [%2d]:%d",
                  in_bytes(sp_offset_for_monitor_object(0)), _num_monitors-1,
                  in_bytes(sp_offset_for_monitor_object(_num_monitors-1)));
  }
  if( _num_spills > 0) {
    svar = _num_spills - 1;
    if(svar == 0)
      tty->print_cr("spill   [0]:%d", in_bytes(sp_offset_for_spill(0)));
    else
      tty->print_cr("spill   [0]:%d | [%2d]:%d", in_bytes(sp_offset_for_spill(0)),
                    svar,
                    in_bytes(sp_offset_for_spill(svar)));
  }
}


// This is the offset from sp() in the frame of the slot for the index,
// skewed by REG_COUNT to indicate a stack location (vs.a register.)
//
//         C ABI size +
//         framesize +     framesize +
//         REG_COUNT       REG_COUNT      REG_COUNT       0 <- VReg->value()
//            |              |              | <registers> |
//  ..........|..............|..............|.............|
//    0 1 2 3 | <C ABI area> | 4 5 6 ...... |               <- local indices
//    ^                        ^          sp()
//    |                        |
//  arguments            non-argument locals


VReg::VR FrameMap::dbgregname(LIR_Opr opr) const {
if(opr->is_cpu_register()){
    assert(!opr->is_virtual(), "should not see virtual registers here");
    return gpr2reg(opr->as_register());
}else if(opr->is_xmm_register()){
    assert(!opr->is_virtual(), "should not see virtual registers here");
    return fpr2reg(opr->as_xmm_float_reg());
  } else if (opr->is_single_stack()) {
return sp_offset2dbgvreg(sp_offset_for_slot(opr->single_stack_ix()));
  } else if (opr->is_address()) {
    LIR_Address* addr = opr->as_address_ptr();
    assert(addr->base() == stack_pointer(), "sp based addressing only");
return sp_offset2dbgvreg(in_ByteSize(addr->index()->as_jint()));
  }
  ShouldNotReachHere();
return VReg::Bad;
}

VOopReg::VR FrameMap::oopregname(LIR_Opr opr) const {
  if (opr->is_single_cpu()) {
    assert(!opr->is_virtual(), "should not see virtual registers here");
return gpr2oopreg(opr->as_register());
  } else if (opr->is_single_stack()) {
return sp_offset2oopvreg(sp_offset_for_slot(opr->single_stack_ix()));
  } else if (opr->is_address()) {
    LIR_Address* addr = opr->as_address_ptr();
    assert(addr->base() == stack_pointer(), "sp based addressing only");
    return sp_offset2oopvreg(in_ByteSize(addr->disp()));
  }
  ShouldNotReachHere();
return VOopReg::Bad;
}




// ------------ extra spill slots ---------------
