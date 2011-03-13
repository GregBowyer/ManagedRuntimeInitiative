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


#include "auditTrail.hpp"
#include "codeCache.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_verifyClosure.hpp"
#include "gpgc_verify_tasks.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "management.hpp"
#include "oopTable.hpp"
#include "artaObjects.hpp"
#include "symbolTable.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "universe.hpp"
#include "vmSymbols.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "os_os.inline.hpp"


void GPGC_Verify_ThreadTask::do_it(GCTaskManager*manager,uint which){
  GPGC_VerifyClosure verify_closure((long)which);

  AuditTrail::log_time(_thread, AuditTrail::GPGC_START_VERIFY_THREAD);

  _thread->oops_do( &verify_closure );

  AuditTrail::log_time(_thread, AuditTrail::GPGC_END_VERIFY_THREAD);
}


void GPGC_Verify_VMThreadTask::do_it(GCTaskManager*manager,uint which){
  GPGC_VerifyClosure verify_closure((long)which);
  VMThread::vm_thread()->oops_do( &verify_closure );
}


void GPGC_Verify_RootsTask::do_it(GCTaskManager*manager,uint which){
  GPGC_VerifyClosure verify_closure((long)which);

  switch (_type) {
    case symbol_table_strong_refs            : SymbolTable::GPGC_verify_strong_refs();  break;
    case string_table_strong_refs            : StringTable::GPGC_verify_strong_refs();  break;

    case universe                            : Universe::oops_do                           (&verify_closure);  break;
    case jni_handles                         : JNIHandles::oops_do                         (&verify_closure);  break;
    case object_synchronizer                 : ObjectSynchronizer::oops_do                 (&verify_closure);  break;
    case management                          : Management::oops_do                         (&verify_closure);  break;
    case jvmti_export                        : JvmtiExport::oops_do                        (&verify_closure);  break;
    case system_dictionary                   : SystemDictionary::oops_do                   (&verify_closure);  break;
    case vm_symbols                          : vmSymbols::oops_do                          (&verify_closure);  break;
    case code_cache                          : CodeCache::oops_do                          (&verify_closure);  break;
    case symbol_table                        : SymbolTable::GC_verify_marks                (&verify_closure);  break;
    case string_table                        : StringTable::GC_verify_marks                (&verify_closure);  break;
    case newgc_ref_lists                     : GPGC_GCManagerNewStrong::oops_do_ref_lists  (&verify_closure);  break;
    case oldgc_ref_lists                     : GPGC_GCManagerOldStrong::oops_do_ref_lists  (&verify_closure);  break;
    case arta_objects                        : ArtaObjects::oops_do                        (&verify_closure);  break;
    case klass_table                         : KlassTable::oops_do                         (&verify_closure);  break;
    case code_cache_oop_table                : CodeCacheOopTable::oops_do                  (&verify_closure);  break;
    case weak_jni_handles                    : JNIHandles::weak_oops_do (verify_closure.is_alive(), &verify_closure);  break;

default:fatal("Unknown root type");
  }
}


const char*GPGC_Verify_RootsTask::name(){
  switch (_type) {
    case symbol_table_strong_refs            : return (char*)"gpgc-verify-symbol-table-strong-refs";
    case string_table_strong_refs            : return (char*)"gpgc-verify-string-table-strong-refs";
    case universe                            : return (char*)"gpgc-verify-universe";
    case jni_handles                         : return (char*)"gpgc-verify-jni-handles";
    case object_synchronizer                 : return (char*)"gpgc-verify-object-synchronizer";
    case management                          : return (char*)"gpgc-verify-management";
    case jvmti_export                        : return (char*)"gpgc-verify-jvmti-export";
    case system_dictionary                   : return (char*)"gpgc-verify-system-dictionary";
    case vm_symbols                          : return (char*)"gpgc-verify-vm-symbols";
    case code_cache                          : return (char*)"gpgc-verify-code-cache";
    case symbol_table                        : return (char*)"gpgc-verify-symbol-table";
    case string_table                        : return (char*)"gpgc-verify-string-table";
    case newgc_ref_lists                     : return (char*)"gpgc-verify-newgc-ref-lists";
    case oldgc_ref_lists                     : return (char*)"gpgc-verify-oldgc-ref-lists";
    case arta_objects                        : return (char*)"gpgc-verify-arta-objects";
    case klass_table                         : return (char*)"gpgc-verify-klass-table";
    case code_cache_oop_table                : return (char*)"gpgc-verify-code-cache-oop-table";
    case weak_jni_handles                    : return (char*)"gpgc-verify-weak-jni-handles";

    default: { fatal("Unknown root type"); return (char*)"error: Unknown root type"; }
  }
}
