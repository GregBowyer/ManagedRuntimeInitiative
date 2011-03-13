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


#include "arguments.hpp"
#include "jvm.h"
#include "ostream.hpp"
#include "vm_version.hpp"
#include "vm_version_pd.hpp"

const char* Abstract_VM_Version::_s_vm_release = Abstract_VM_Version::vm_release();
const char* Abstract_VM_Version::_s_internal_vm_info_string = Abstract_VM_Version::internal_vm_info_string();
bool Abstract_VM_Version::_supports_cx8 = false;
unsigned int Abstract_VM_Version::_logical_processors_per_package = 1U;

#ifndef AVM_RELEASE_VERSION
#error AVM_RELEASE_VERSION must be defined
#endif
#ifndef HOTSPOT_RELEASE_VERSION
  #error HOTSPOT_RELEASE_VERSION must be defined
#endif
#ifndef HOTSPOT_BUILD_USER
#error HOTSPOT_BUILD_USER must be defined
#endif

#ifdef PRODUCT
#define  HOTSPOT_BUILD_TARGET "product"
#elif defined(FASTDEBUG)
#define  HOTSPOT_BUILD_TARGET "fastdebug"
#elif defined(DEBUG)
#define  HOTSPOT_BUILD_TARGET "debug"
#else
#define  HOTSPOT_BUILD_TARGET "optimized"
#endif


// HOTSPOT_RELEASE_VERSION must follow the release version naming convention 
// <major_ver>.<minor_ver>-b<nn>[-<identifier>][-<debug_target>]
int Abstract_VM_Version::_vm_major_version = 0;
int Abstract_VM_Version::_vm_minor_version = 0;
int Abstract_VM_Version::_vm_build_number = 0;
bool Abstract_VM_Version::_initialized = false;

void Abstract_VM_Version::initialize() {
  if (_initialized) {
    return;
  }
char*vm_version=os::strdup(XSTR(HOTSPOT_RELEASE_VERSION));

  // Expecting the next vm_version format: 
  // <major_ver>.<minor_ver>-b<nn>[-<identifier>]
  char* vm_major_ver = vm_version;
  assert(isdigit(vm_major_ver[0]),"wrong vm major version number");
  char* vm_minor_ver = strchr(vm_major_ver, '.');
  assert(vm_minor_ver != NULL && isdigit(vm_minor_ver[1]),"wrong vm minor version number");
  vm_minor_ver[0] = '\0'; // terminate vm_major_ver
  vm_minor_ver += 1;
  char* vm_build_num = strchr(vm_minor_ver, '-');
  assert(vm_build_num != NULL && vm_build_num[1] == 'b' && isdigit(vm_build_num[2]),"wrong vm build number");
  vm_build_num[0] = '\0'; // terminate vm_minor_ver
  vm_build_num += 2;

  _vm_major_version = atoi(vm_major_ver); 
  _vm_minor_version = atoi(vm_minor_ver); 
  _vm_build_number  = atoi(vm_build_num);
 
  os::free(vm_version);
  _initialized = true;
}

const char* avm_release_version  = XSTR(AVM_RELEASE_VERSION);
const char* hotspot_build_user   = XSTR(HOTSPOT_BUILD_USER);
const char* hotspot_build_target = HOTSPOT_BUILD_TARGET;

char vm_platform_str[32];
char vm_release_str[256];
char internal_vmname[1024];


const char* Abstract_VM_Version::vm_name() {
const char*VMNAME=NULL;
  if (UseC1) {
    if (UseC2) {
      VMNAME = "OpenJDK 64-Bit Tiered VM";
    } else {
      VMNAME = "OpenJDK 64-Bit Client VM";
    }
  } else {
    if (UseC2) {
      VMNAME = "OpenJDK 64-Bit Server VM";
    } else {
      VMNAME = "OpenJDK 64-Bit Interpreted VM";
    }
  }
  return VMNAME;
}


const char* Abstract_VM_Version::vm_vendor() {
#ifdef VENDOR
  return XSTR(VENDOR);
#else
return"Azul Systems\054 Inc.";
#endif
}


const char* Abstract_VM_Version::vm_info_string() {
  switch (Arguments::mode()) {
    case Arguments::_int:
return"interpreted mode";
    case Arguments::_mixed:
return"mixed mode";
    case Arguments::_comp:
return"compiled mode";
  };
  ShouldNotReachHere();
  return "";
}

// NOTE: do *not* use stringStream. this function is called by 
//       fatal error handler. if the crash is in native thread,
//       stringStream cannot get resource allocated and will SEGV.
const char* Abstract_VM_Version::vm_release() {
  if (strcmp(hotspot_build_user, "buildmaster") != 0) {
    jio_snprintf(vm_release_str, sizeof(vm_release_str), "%s-%s-%s-%s", avm_release_version, hotspot_build_target, vm_platform_string(), hotspot_build_user);
  } else {
    jio_snprintf(vm_release_str, sizeof(vm_release_str), "%s-%s-%s", avm_release_version, hotspot_build_target, vm_platform_string());
  }

  return vm_release_str;
}

const char *Abstract_VM_Version::vm_platform_string() {
  const char* osname = "linux";

#ifdef AZ_PROXIED
    osname = "azproxied";
#else // !AZ_PROXIED
if(os::using_az_kernel_modules()){
    osname = "azlinux";
  }
#endif // !AZ_PROXIED

  jio_snprintf(vm_platform_str, sizeof(vm_platform_str), "%s-%s", osname, os::arch_version());

  return vm_platform_str;
}

const char* Abstract_VM_Version::internal_vm_info_string() {
  #ifndef HOTSPOT_BUILD_COMPILER
    #ifdef _MSC_VER
      #if   _MSC_VER == 1100
        #define HOTSPOT_BUILD_COMPILER "MS VC++ 5.0"
      #elif _MSC_VER == 1200
        #define HOTSPOT_BUILD_COMPILER "MS VC++ 6.0"
      #elif _MSC_VER == 1310
        #define HOTSPOT_BUILD_COMPILER "MS VC++ 7.1"
      #elif _MSC_VER == 1400
        #define HOTSPOT_BUILD_COMPILER "MS VC++ 8.0"
      #else
        #define HOTSPOT_BUILD_COMPILER "unknown MS VC++:" XSTR(_MSC_VER)
      #endif
    #elif defined(__SUNPRO_CC)
      #if   __SUNPRO_CC == 0x420
        #define HOTSPOT_BUILD_COMPILER "Workshop 4.2"
      #elif __SUNPRO_CC == 0x500
        #define HOTSPOT_BUILD_COMPILER "Workshop 5.0 compat=" XSTR(__SUNPRO_CC_COMPAT)
      #elif __SUNPRO_CC == 0x520
        #define HOTSPOT_BUILD_COMPILER "Workshop 5.2 compat=" XSTR(__SUNPRO_CC_COMPAT)
      #elif __SUNPRO_CC == 0x580
        #define HOTSPOT_BUILD_COMPILER "Workshop 5.8"
      #elif __SUNPRO_CC == 0x590
        #define HOTSPOT_BUILD_COMPILER "Workshop 5.9"
      #else
        #define HOTSPOT_BUILD_COMPILER "unknown Workshop:" XSTR(__SUNPRO_CC)
      #endif
    #elif defined(__GNUC__)
      #define HOTSPOT_BUILD_COMPILER "gcc-" XSTR(__GNUC__) "." XSTR(__GNUC_MINOR__) "." XSTR(__GNUC_PATCHLEVEL__)
    #else
      #define HOTSPOT_BUILD_COMPILER "unknown compiler"
    #endif
  #endif

  jio_snprintf(internal_vmname, sizeof(internal_vmname), "%s (%s-%s) for %s, built on "
               __DATE__ " " __TIME__ " by %s with %s", vm_name(), avm_release_version,
               hotspot_build_target , vm_platform_string(), hotspot_build_user,
               HOTSPOT_BUILD_COMPILER);

  return internal_vmname;
}

unsigned int Abstract_VM_Version::jvm_version() {
  return ((Abstract_VM_Version::vm_major_version() & 0xFF) << 24) |
         ((Abstract_VM_Version::vm_minor_version() & 0xFF) << 16) |
         (Abstract_VM_Version::vm_build_number() & 0xFF);
}


void VM_Version_init() {
  VM_Version::initialize();

#ifndef PRODUCT
  if (PrintMiscellaneous && Verbose) {
    os::print_cpu_info(tty);
  }
#endif
}
