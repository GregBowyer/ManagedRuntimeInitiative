/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "jvm.h"
#include "register_pd.hpp"
#include "vm_version_pd.hpp"
#include "stubCodeGenerator.hpp"

int VM_Version::_cpu;
int VM_Version::_model;
int VM_Version::_stepping;
int VM_Version::_cpuFeatures;
const char* VM_Version::_features_str = "";
const char* VM_Version::_architecture_version_str = VM_Version::architecture_version();
VM_Version::CpuidInfo VM_Version::_cpuid_info   = { 0, };

extern "C" {
  typedef void (*getPsrInfo_stub_t)(void*);
}
static getPsrInfo_stub_t getPsrInfo_stub = NULL;

class VM_Version_StubGenerator: public StubCodeGenerator {
 public:

  VM_Version_StubGenerator() : StubCodeGenerator(CodeBlob::vm_version, "Stubs") {}

  address generate_getPsrInfo() {

    StubCodeMark mark(this, "VM_Version", "getPsrInfo_stub", frame::runtime_stub_frame_size);
    Label std_cpuid1, ext_cpuid1, ext_cpuid5, done;

#define __ _masm->
    Label start(_masm);

    //
    // void getPsrInfo(VM_Version::CpuidInfo* cpuid_info);
    //
    // rcx and rdx are first and second argument registers on windows

__ push(RBP);
    __ mov8(RBP, RDI); // cpuid_info address
__ push(RBX);
__ push(RSI);

    //
    // we have a chip which supports the "cpuid" instruction
    //
    __ mov8i(RAX, 0);
    __ cpuid();
__ lea(RSI,RBP,in_bytes(VM_Version::std_cpuid0_offset()));
__ st8(RSI,0,RAX);
    __ st8(RSI,  4, RBX);
    __ st8(RSI,  8, RCX);
    __ st8(RSI, 12, RDX);

__ cmp8i(RAX,3);//Is cpuid(0x4) supported?
__ jbe(std_cpuid1);

    //
    // cpuid(0x4) Deterministic cache params
    //
    __ mov8i(RAX, 4);
    __ mov8i(RCX, 0);   // L1 cache
    __ cpuid();
__ push(RAX);
__ and8i(RAX,0x1f);//Determine if valid cache parameters used
__ or_8(RAX,RAX);//eax[4:0] == 0 indicates invalid cache
__ pop(RAX);
__ jeq(std_cpuid1);

__ lea(RSI,RBP,in_bytes(VM_Version::dcp_cpuid4_offset()));
__ st8(RSI,0,RAX);
__ st8(RSI,4,RBX);
__ st8(RSI,8,RCX);
    __ st8(RSI, 12, RDX);

    //
    // Standard cpuid(0x1)
    //
    __ bind(std_cpuid1);
    __ mov8i(RAX, 1);
    __ cpuid();
__ lea(RSI,RBP,in_bytes(VM_Version::std_cpuid1_offset()));
__ st8(RSI,0,RAX);
    __ st8(RSI,  4, RBX);
    __ st8(RSI,  8, RCX);
    __ st8(RSI, 12, RDX);

    __ mov8i(RAX, (int32_t)0x80000000);
    __ cpuid();
__ cmp8i(RAX,(int32_t)0x80000000);//Is cpuid(0x80000001) supported?
__ jbe(done);
__ cmp8i(RAX,(int32_t)0x80000004);//Is cpuid(0x80000005) supported?
__ jbe(ext_cpuid1);
__ cmp8i(RAX,(int32_t)0x80000007);//Is cpuid(0x80000008) supported?
__ jbe(ext_cpuid5);
    //
    // Extended cpuid(0x80000008)
    //
    __ mov8i(RAX, (int32_t)0x80000008);
    __ cpuid();
__ lea(RSI,RBP,in_bytes(VM_Version::ext_cpuid8_offset()));
__ st8(RSI,0,RAX);
    __ st8(RSI, 4, RBX);
    __ st8(RSI, 8, RCX);
    __ st8(RSI,12, RDX);

    //
    // Extended cpuid(0x80000005)
    //
    __ bind(ext_cpuid5);
    __ mov8i(RAX, (int32_t)0x80000005);
    __ cpuid();
__ lea(RSI,RBP,in_bytes(VM_Version::ext_cpuid5_offset()));
__ st8(RSI,0,RAX);
    __ st8(RSI, 4, RBX);
    __ st8(RSI, 8, RCX);
    __ st8(RSI,12, RDX);

    //
    // Extended cpuid(0x80000001)
    //
    __ bind(ext_cpuid1);
    __ mov8i(RAX, (int32_t)0x80000001);
    __ cpuid();
__ lea(RSI,RBP,in_bytes(VM_Version::ext_cpuid1_offset()));
__ st8(RSI,0,RAX);
    __ st8(RSI, 4, RBX);
    __ st8(RSI, 8, RCX);
    __ st8(RSI,12, RDX);

    //
    // return
    //
    __ bind(done);
__ pop(RSI);
__ pop(RBX);
__ pop(RBP);
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  };
};

void VM_Version::initialize() {
  static volatile bool initialized = false;

  if (!initialized) {
    ResourceMark rm;
    initialized = true;

    VM_Version_StubGenerator g;
    getPsrInfo_stub = CAST_TO_FN_PTR(getPsrInfo_stub_t,
                                     g.generate_getPsrInfo());


    get_processor_features();

    NOT_PRODUCT( if (Verbose) print_features(); );
  }
}

void VM_Version::print_features(){
  // No features being distinguished
}

void VM_Version::get_processor_features() {
  // Get raw processor info
  getPsrInfo_stub(&_cpuid_info);
  _cpuFeatures  = feature_flags();
  _supports_cx8 = supports_cmpxchg8();
}

const char* VM_Version::architecture_version() {
if(_architecture_version_str==NULL){
    uint64_t id;
char buf[32];
    //os_get_control_register("ID", id);
    //uint64_t archId = (id >> 12) & 0xf;
    uint64_t archId = 64;
jio_snprintf(buf,sizeof(buf),"X86_%d",archId);
    _architecture_version_str = strdup(buf);    // no leak:  computed once per run
assert(_architecture_version_str!=NULL,"Out of memory?");
  }

  return _architecture_version_str;
}
