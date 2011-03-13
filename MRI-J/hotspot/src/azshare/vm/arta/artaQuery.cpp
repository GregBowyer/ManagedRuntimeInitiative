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


#ifdef AZ_PROFILER

#include "arguments.hpp"
#include "artaThreadState.hpp"
#include "codeCache.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "compileBroker.hpp"
#include "deoptimization.hpp"
#include "disassembler_pd.hpp"
#include "exceptions.hpp"
#include "interfaceSupport.hpp"
#include "interpreter.hpp"
#include "gpgc_stats.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "preserveException.hpp"
#include "artaQuery.hpp"
#include "artaObjects.hpp"
#include "safepoint.hpp"
#include "sharedRuntime.hpp"
#include "statistics.hpp"
#include "stubCodeGenerator.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"
#include "vm_version_pd.hpp"
#include "wlmuxer.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
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
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include <time.h>
#include <azprof/azprof_demangle.hpp>

#include <azprof/azprof_web.hpp>
#include <azprof/azprof_servlets.hpp>
#include <azprof/azprof_debug.hpp>


#ifdef AZ_PROXIED
# include <proxy/proxy_be.h>
# include <proxy/proxy_java.h>
# include <transport/transport.h>
# include <azpr/azpr_psinfo.h>
#else // !AZ_PROXIED:
# include <sys/time.h>
# include <sys/resource.h>
#endif // !AZ_PROXIED


extern struct JavaVM_ main_vm;
using azprof::FileServlet;

////////////////////////////////////////////////////////////////////////////////
// Error handlers
////////////////////////////////////////////////////////////////////////////////

static void handle_error(xmlBuffer *xb, const char *fmt, ...) {
  xmlElement e(xb, "error");
va_list ap;
va_start(ap,fmt);
  xb->vprint(fmt, ap);
  va_end(ap);
}

static void handle_missing_param(xmlBuffer *xb, const char *param_name) {
  handle_error(xb, "No '%s' parameter specified in HTTP query.", param_name);
}

static void handle_invalid_param(xmlBuffer *xb, const char *param_name) {
  handle_error(xb, "Invalid value specified for the '%s' HTTP query parameter.", param_name);
}

static void handle_invalid_params(xmlBuffer *xb) {
  handle_error(xb, "Invalid HTTP query parameters.");
}

static void handle_jvm_busy(
  azprof::Request *req, azprof::Response *res, const char *msg
) {
  const char *host = req->header_by_name("Host");
  if (!host) {
res->bad_request();
      return;
  }
  char url[256];
  ssize_t n = snprintf(
      url, sizeof(url), "%s://%s%s",
      req->protocol_name(), host, req->raw_uri()
  );
  if (!((n > 0) && (n < (ssize_t) sizeof(url)))) {
res->bad_request();
      return;
  }
  res->ok("text/xml", -1);
  res->header("Cache-Control", "no-cache");
res->end_header();
  azprof::WebServer::begin_xml_response(req, res, "hotspot");
  { azprof::Xml tag(res, "jvm-busy");
    azprof::WebServer::self()->to_xml(req, res);
    req->session()->to_xml(res);
    azprof::Xml::leaf(res, "request-url", url);
    azprof::Xml::leaf(res, "message", msg);
  }
  azprof::WebServer::end_xml_response(req, res);
}

////////////////////////////////////////////////////////////////////////////////
// Java properties
////////////////////////////////////////////////////////////////////////////////

static const char* get_property(const char* name, TRAPS) {
  // setup arguments
  Handle key_str = java_lang_String::create_from_str(name, false/*No SBA*/, CHECK_0);

  JavaValue result(T_OBJECT);
  // public static String getProperty(String key, String def);
  JavaCalls::call_static(&result,
SystemDictionary::system_klass(),
                         vmSymbolHandles::getProperty_name(),
                         vmSymbolHandles::string_string_signature(),
key_str,CHECK_0);

  oop value_oop = ((objectRef*)result.get_value_addr())->as_oop();
  if (value_oop == NULL) {
    return NULL;
  }

  // convert Java String to utf8 string
  char* value = java_lang_String::as_utf8_string(value_oop);
  return value;
}

static const char* get_property(const char* name) {
const char*result=NULL;
  {
    PRESERVE_EXCEPTION_MARK;
    result = get_property(name, THREAD);
    // Ignore any pending exceptions
    CLEAR_PENDING_EXCEPTION;
  }
  return (result != NULL) ? result : "";
}

////////////////////////////////////////////////////////////////////////////////
// Customer visable content query handlers
////////////////////////////////////////////////////////////////////////////////


static void handle_jvm_config(azprof::Request *req, azprof::Response *res) {
  char buf[1024];
  { azprof::Xml avm_config_tag(res, "jvm-config");
    azprof::Xml::leaf(res, "vm-name", VM_Version::vm_name() );
    azprof::Xml::leaf(res, "vm-vendor", VM_Version::vm_vendor());
    azprof::Xml::leaf(res, "vm-info", VM_Version::vm_info_string());
    azprof::Xml::leaf(res, "vm-release", VM_Version::vm_release());
    azprof::Xml::leaf(res, "vm-internal-info", VM_Version::internal_vm_info_string());
    azprof::Xml::leaf(res, "vm-flags", Arguments::jvm_flags() ? Arguments::jvm_flags() : "");
    azprof::Xml::leaf(res, "vm-args", Arguments::jvm_args () ? Arguments::jvm_args () : "");
    azprof::Xml::leaf(res, "java-vm-specification-version", get_property("java.vm.specification.version"));
    azprof::Xml::leaf(res, "java-vm-specification-name", get_property("java.vm.specification.name"));
    azprof::Xml::leaf(res, "java-vm-specification-vendor", get_property("java.vm.specification.vendor"));
    azprof::Xml::leaf(res, "java-ext-dirs", get_property("java.ext.dirs"));
    azprof::Xml::leaf(res, "java-endorsed-dirs", get_property("java.endorsed.dirs"));
    azprof::Xml::leaf(res, "java-library-path", get_property("java.library.path"));
    azprof::Xml::leaf(res, "java-home", get_property("java.home"));
    azprof::Xml::leaf(res, "java-class-path", get_property("java.class.path"));
    azprof::Xml::leaf(res, "sun-boot-library-path", get_property("sun.boot.library.path"));
    azprof::Xml::leaf(res, "sun-boot-class-path", Arguments::get_sysclasspath() ? Arguments::get_sysclasspath() : "");
    azprof::Xml::leaf(res, "sun-java-command", Arguments::java_command() ? Arguments::java_command() : "");

#ifdef AZ_PROXIED
    const proxy_command_line_t* _proxy_command = proxy_get_proxy_command_line();
    azprof::Xml::leaf(res, "azul-proxy-executable",     _proxy_command->pcl_executable ? _proxy_command->pcl_executable : "");
    azprof::Xml::leaf(res, "azul-proxy-pre-flags",      _proxy_command->pcl_pre_flags ? _proxy_command->pcl_pre_flags : "");
    azprof::Xml::leaf(res, "azul-proxy-pre-file",       _proxy_command->pcl_pre_file ? _proxy_command->pcl_pre_file : "");
    azprof::Xml::leaf(res, "azul-proxy-cline-flags",    _proxy_command->pcl_args ? _proxy_command->pcl_args : "");
    azprof::Xml::leaf(res, "azul-proxy-post-flags",     _proxy_command->pcl_post_flags ? _proxy_command->pcl_post_flags : "");
    azprof::Xml::leaf(res, "azul-proxy-post-file",      _proxy_command->pcl_post_file ? _proxy_command->pcl_post_file : "");
    azprof::Xml::leaf(res, "azul-proxy-cpm-flags",      _proxy_command->pcl_cpm_flags ? _proxy_command->pcl_cpm_flags : "");
    azprof::Xml::leaf(res, "azul-proxy-jar-or-class",   _proxy_command->pcl_command ? _proxy_command->pcl_command : "");
    azprof::Xml::leaf(res, "azul-proxy-java-args",      _proxy_command->pcl_cmd_args ? _proxy_command->pcl_cmd_args : "");
#endif // AZ_PROXIED

    azprof::Xml::leaf(res, "current-working-directory", getcwd(buf, sizeof(buf)) ? buf : "");

uint32_t nopenfds=0;
    if (os::getnopenfds(&nopenfds) == 0) {
        azprof::Xml::leaf(res, "open-file-descriptor-count", (uint64_t) nopenfds);
    }
#ifdef AZ_PROXIED
    proxy_rlimit rlim;
    proxy_getrlimit(PROXY_RLIMIT_NOFILE, &rlim);
    if (rlim.rlim_cur == PROXY_RLIM_INFINITY) {
        azprof::Xml::leaf(res, "open-file-descriptor-count-limit", "unlimited");
    } else {
        azprof::Xml::leaf(res, "open-file-descriptor-count-limit", rlim.rlim_cur);
    }
    proxy_getrlimit(PROXY_RLIMIT_CORE, &rlim);
    if (rlim.rlim_cur == PROXY_RLIM_INFINITY) {
        azprof::Xml::leaf(res, "core-file-size-limit", "unlimited");
    } else {
        azprof::Xml::leaf(res, "core-file-size-limit", rlim.rlim_cur);
    }
    azprof::Xml::leaf(res, "direct-path-state", (int64_t) atcpn_get_source_switching_state());
    azprof::Xml::leaf(res, "direct-path-initiate", (int64_t) proxy_get_use_direct_connect());
    azprof::Xml::leaf(res, "io-stack", proxy_get_io_stack());
#else // !AZ_PROXIED:
    {
        struct rlimit rlim;
getrlimit(RLIMIT_NOFILE,&rlim);
        if (rlim.rlim_cur == RLIM_INFINITY) {
            azprof::Xml::leaf(res, "open-file-descriptor-count-limit", "unlimited");
        } else {
            azprof::Xml::leaf(res, "open-file-descriptor-count-limit", rlim.rlim_cur);
        }
        getrlimit(RLIMIT_CORE, &rlim);
        if (rlim.rlim_cur == RLIM_INFINITY) {
            azprof::Xml::leaf(res, "core-file-size-limit", "unlimited");
        } else {
            azprof::Xml::leaf(res, "core-file-size-limit", rlim.rlim_cur);
        }
    }
#endif // !AZ_PROXIED


   // generate warning info
   const char * const single_thread_mode = ((CompileBroker::_c1.get_single_thread_compiler_mode()) ||
					     (CompileBroker::_c2.get_single_thread_compiler_mode()) ?
					     "true" : "false");
   int total_thread_count = CompileBroker::_c1.num_cx_threads() + 
	                     CompileBroker::_c2.num_cx_threads();
   azprof::Xml warnings_xml_tag(res, "warnings");
   {
     azprof::Xml compilers_xml_tag(res, "compilers");
     azprof::Xml::leaf(res, "single-thread-mode", single_thread_mode);
     azprof::Xml::leaf(res, "total-thread-count", total_thread_count);
   }
  }
}

static void handle_heap(azprof::Request *req, xmlBuffer *xb) {
  Universe::heap()->print_xml_on(xb, false);
  os::print_memory_statistics_xml_on(xb);
}

static void handle_gc_summary_xml(azprof::Request *req, azprof::Response *res) {
  if (UseGenPauselessGC) {
    GPGC_HistoricalCycleStats::print_summary_xml(req, res);
  } else {
    azprof::Xml::leaf(res, "error", "No GC summary is available because PGC or GPGC are not being used.");
  }
}

static void handle_gc_history_xml(azprof::Request *req, azprof::Response *res) {
  if (UseGenPauselessGC) {
    GPGC_HistoricalCycleStats::print_history_xml(req, res);
  } else {
    azprof::Xml::leaf(res, "error", "No GC history is available because PGC or GPGC are not being used.");
  }
}

static void handle_gc_history_txt(azprof::Request *req, azprof::Response *res) {
  if (UseGenPauselessGC) {
    GPGC_HistoricalCycleStats::print_history_txt(req, res);
  } else {
res->not_found();
  }
}

static void handle_thread_list(azprof::Request *req, azprof::Response *res) {
  ArtaThreadState::all_to_xml(req, res);
}

static int thread_detail(azprof::Request *req) {
  // "detail" level is
  // 0 for reference/link only,
  // 1 for 1 line per method
  // 2 for 1 line per Java local/expression-stack
  // 3 for 1 line per word of physical frame
  const char *s = req->parameter_by_name("detail");
  if (!(req->is_azul_engineer() && s)) {
    return 1;
}else if(strcmp(s,"low")==0){
    return 1;
}else if(strcmp(s,"med")==0){
    return 2;
}else if(strcmp(s,"high")==0){
//TODO: This is a temporary hack until we get the "high" detail working.
// In the meantime, everything "high" is treated as "medium" (i.e. 3's become 2's).
    return 2; // was 3.
  } else {
    return -1;
  }
}

static void handle_stack_trace(azprof::Request *req, xmlBuffer *xb) {
  int detail = thread_detail(req);
  if (detail < 0 || detail > 3) {
    handle_invalid_param(xb, "detail");
    return;
  }

  const char *id_s = req->parameter_by_name("id");
  if (id_s) {
    intptr_t id;
    if ((id = strtoul(id_s, NULL, 0))) {
      Threads::thread_print_xml_on(id, xb, detail);
    } else {
      handle_invalid_param(xb, "id");
    }
  } else {
    int32_t start = req->int32_parameter_by_name("start");
    int32_t stride = req->int32_parameter_by_name("stride");
    int group = req->int32_parameter_by_name("bygroup");
    const char *name = req->parameter_by_name("byname");
    const char *status = req->parameter_by_name("bystatus");
    ThreadFilter filt(group, name, status);
    Threads::all_threads_print_xml_on(xb, start, stride, detail, &filt);
  }
}

static void handle_hot_locks(azprof::Request *req, xmlBuffer *xb) {
  ResourceMark rm;
  char* kid_str = req->parameter_by_name("kid");
if(kid_str!=NULL){
    unsigned int kid = (unsigned int) strtoul(kid_str, NULL, 10);
    // FIXME: check errno here
    if (!KlassTable::is_valid_klassId(kid)) {
      handle_error(xb, "Stale klass id");
      return;
    }
    Klass* k = Klass::cast(KlassTable::getKlassByKlassId(kid).as_klassOop());
    k->print_lock_xml_on(xb);
  } else {
    xmlElement s(xb, "hot_locks");
    xb->name_value_item("sma", UseSMA ? "true" : "false");
    MutexLocker::print_to_xml_lock_contention(xb);
  }
}

static void handle_java_lang_Object(azprof::Request *req, xmlBuffer *xb) {
  SystemDictionary::object_klass()->print_xml_on(xb,false);
}

static void handle_class_list(azprof::Request *req, xmlBuffer *xb) {
  SystemDictionary::print_xml_on(xb);
}

static void handle_browse_object(azprof::Request *req, xmlBuffer *xb) {
  oop obj = NULL;
  char *idStr = req->parameter_by_name("id");
  char *kidStr = req->parameter_by_name("kid");
  char *midStr = req->parameter_by_name("mid");
if(idStr!=NULL){
    unsigned int id = (unsigned int) strtoul(idStr, NULL, 10);
    // FIXME: check errno here
    
    if (!xb->object_pool()->is_id_live(id)) {
      handle_error(xb, "Stale object handle");
      return;
    }
    obj = xb->object_pool()->get_oop(id);
}else if(kidStr!=NULL){
    unsigned int kid = (unsigned int) strtoul(kidStr, NULL, 10);
    // FIXME: check errno here
    if (!KlassTable::is_valid_klassId(kid)) {
      handle_error(xb, "Stale klass id");
      return;
    }
    obj = KlassTable::getKlassByKlassId(kid).as_oop();
}else if(midStr!=NULL){
    int mid = (int)strtol(midStr, NULL, 10);
    
    obj = methodOopDesc::from_method_id(mid);
  } else {
    handle_missing_param(xb, "id");
    return;
  }

if(obj==NULL){
    handle_error(xb, "Object has been garbage collected");
    return;
  }

  {
    obj->print_xml_on(xb, false);
  }
}

static void handle_profile(azprof::Request *req, xmlBuffer *xb) {
  ProfileIterator it(req);

  double cutoff = 0.1;
  char *cutoffstr = req->parameter_by_name("cutoff");
if(cutoffstr!=NULL){
    cutoff = strtod(cutoffstr, NULL);
    if ((cutoff < 0) || (cutoff > 90)) cutoff = 0.1;
  }

  TickProfiler::print_xml_on(xb, it, cutoff);
}

static void handle_hotspot_flags(azprof::Request *req, xmlBuffer *xb) {
  CommandLineFlags::print_xml_on(xb);
}

static void handle_gc_log_settings(azprof::Request *req, azprof::Response *res) {
  // Grab the tty_lock to be able to safely touch the GC log globals.
  ttyLocker ttyl;

  // Process any form input.
  char *new_mode_str = req->parameter_by_name("new_mode");
if(new_mode_str!=NULL){
    // Reset the GC log settings.
    PrintGC = false;
    PrintGCDetails = false;
    gclog->close();
gclog_or_tty=tty;

    // Set the flags based on the specified mode.
    int new_mode = strtod(new_mode_str, NULL);
    if (new_mode == 1) {
      PrintGC = true;
    } else if (new_mode == 2) {
      PrintGC = true;
      PrintGCDetails = true;
    }

    // Set the log file based on what was specified.
    char *file_name = req->parameter_by_name("file_name");
    bool  append    = req->bool_parameter_by_name("append");
    if ((file_name != NULL) && (file_name[0] != '\0') && PrintGC) {
      gclog->open(file_name, append);
      if (gclog->is_open()) {
        // Update the time stamp of the GC log to be synced up with the tty.
        gclog->time_stamp().update_to(tty->time_stamp().ticks());
        gclog_or_tty = gclog;
      }
    }
  }

  // Spit out the current settings as XML.
  azprof::Xml tag(res, "gc-log-settings");
  int mode;
  if (PrintGCDetails) {
    mode = 2;
  } else if (PrintGC) {
    mode = 1;
  } else {
    mode = 0;
  }
  azprof::Xml::leaf(res, "mode", mode);
  if (gclog->is_open()) {
    azprof::Xml::leaf(res, "file-name", gclog->file_name());
    azprof::Xml::leaf(res, "append", gclog->append() ? "true" : "false");
  }
}

static void handle_allocated_objects_xml(azprof::Request *req, azprof::Response *res) {
  AllocatedObjects::to_xml(req, res);
}

static void handle_allocated_objects_csv(azprof::Request *req, azprof::Response *res) {
  AllocatedObjects::to_csv(req, res);
}

static void handle_live_objects_xml(azprof::Request *req, azprof::Response *res) {
  LiveObjects::to_xml(req, res);
}

static void handle_live_objects_csv(azprof::Request *req, azprof::Response *res) {
  LiveObjects::to_csv(req, res);
}

static void handle_safepoints(azprof::Request *req, xmlBuffer* xb) {
  Threads::thread_print_safepoint_xml_on(xb);
}

static void handle_reset_safepoints(azprof::Request *req, azprof::Response *res) {
Threads::threads_reset_safepoints();
res->redirect_to_referer();
}

static void handle_compile_brokers(azprof::Request *req, azprof::Response *res) {
  CompileBroker::all_to_xml(req, res);
}

////////////////////////////////////////////////////////////////////////////////
// Operations query handlers
////////////////////////////////////////////////////////////////////////////////

static void handle_force_gc(azprof::Request *req, azprof::Response *res) {
  Universe::heap()->collect(GCCause::_java_lang_system_gc);

  // fix for bug 24920 
  // res->redirect_to_referer();
  const char *host = req->header_by_name("Host");
  if (host) {
      res->see_other("%s://%s/config/process", req->protocol_name(), host);
  } else {
res->bad_request();
  }
}

static void handle_reset_tick_profile(azprof::Request *req, azprof::Response *res) {
TickProfiler::reset();
res->redirect_to_referer();
}

////////////////////////////////////////////////////////////////////////////////
// Azul engineering/support content query handlers
////////////////////////////////////////////////////////////////////////////////

#ifdef AZ_PROXIED
static void handle_old_proxy(azprof::Request *req, xmlBuffer *xb) {
  char *buffer;
  int buffer_len;
  proxy_error_t rc = proxy_monitor_hotspot_callback("call_profile", &buffer, &buffer_len);
  if (rc != PROXY_ERROR_NONE) {
    handle_error(xb, "Request failed (%d)", rc);
  } else {
    xb->print_raw(buffer);
free(buffer);
  }
}
#endif // AZ_PROXIED

#ifdef AZ_PROXIED
static void handle_old_proxy_clear(azprof::Request *req, azprof::Response *res) {
  char *buffer;
  int buffer_len;
  proxy_error_t rc = proxy_monitor_hotspot_callback("call_profile_clear", &buffer, &buffer_len);
  if (rc == PROXY_ERROR_NONE) {
res->redirect_to_referer();
  } else {
res->internal_server_error();
  }
}
#endif // AZ_PROXIED

#ifdef AZ_PROXIED
static void handle_old_proxy_toggle(azprof::Request *req, azprof::Response *res) {
  char *buffer;
  int buffer_len;
  proxy_error_t rc = proxy_monitor_hotspot_callback("call_profile_toggle", &buffer, &buffer_len);
  if (rc == PROXY_ERROR_NONE) {
res->redirect_to_referer();
  } else {
res->internal_server_error();
  }
}
#endif // AZ_PROXIED

#ifdef AZ_PROXIED
static void handle_proxy_usage(azprof::Request *req, xmlBuffer *xb) {
  xmlElement lst(xb, "proxy-usage-data");
  if (proxy_get_stats_collection_enabled()) {
      sys_return_t rc;
azpr_system_process_stats_t*_data;
      proxy_system_process_stats_t* _pstats;
int32_t _start,_end;

      _pstats = proxy_get_proxy_usage_stats();

      rc = mutex_lock(&_pstats->ps_lock);
      os_syscallok(rc, "mutex_lock");

      _data = _pstats->ps_stats + _pstats->ps_current;
      if (_data->relative_time_nanos > 0) {
          _start = _pstats->ps_current;
          _end = (_start) ? (_start - 1) : (_pstats->ps_size - 1);
      } else {
          _start = 0;
          _end = _pstats->ps_current;
      }

      while (_start != _end) {
          _data = _pstats->ps_stats + _start;
          xmlElement entry(xb, "proxy-usage-entry");

          // break down the date into something readable
          int64_t _secs = _data->relative_time_nanos / 1000000000L;
struct tm _time;

          if (!localtime_r((const time_t*)&_secs, &_time)) {
              // hmmm...
              xb->response()->internal_server_error();
              return;
          }

          xb->name_value_item("timestamp",  "%d-%d-%d %02d:%02d:%02d GMT",
_time.tm_year+1900,_time.tm_mon+1,_time.tm_mday,
_time.tm_hour,_time.tm_min,_time.tm_sec);

          // azpr_system_stats_t
          {
              xmlElement sys(xb, "system-stats");
              azpr_system_stats_t* _sys = &_data->system;
              xb->name_value_item("num-cpus",               (intptr_t) _sys->num_cpus);
              xb->name_value_item("ticks-per-second",       (intptr_t) _sys->ticks_per_second);
              xb->name_value_item("user-ticks",             (intptr_t) _sys->user_ticks);
              xb->name_value_item("other-ticks",            (intptr_t) _sys->other_ticks);
              xb->name_value_item("system-ticks",           (intptr_t) _sys->system_ticks);
              xb->name_value_item("idle-ticks",             (intptr_t) _sys->idle_ticks);
              xb->name_value_item("load-average-1-minute",  "%g", _sys->load_average_1_minute);
              xb->name_value_item("load-average-5-minute",  "%g", _sys->load_average_5_minute);
              xb->name_value_item("load-average-15-minute", "%g", _sys->load_average_15_minute);
              xb->name_value_item("total-memory-bytes",     (intptr_t) _sys->total_memory_bytes);
              xb->name_value_item("used-memory-bytes",      (intptr_t) _sys->used_memory_bytes);

              // each network interface
              for (size_t i = 0; i < _sys->num_interfaces; i++) {
                  azpr_interface_stats_t* _iface = &_sys->interfaces[i];
                  xmlElement iface(xb, "proxy-network-interface");
                  xb->name_value_item("iface-name", "%s", _iface->name);
                  xb->name_value_item("rx-bytes",           (intptr_t) _iface->rx_bytes);
                  xb->name_value_item("rx-packets",         (intptr_t) _iface->rx_packets);
                  xb->name_value_item("tx-bytes",           (intptr_t) _iface->tx_bytes);
                  xb->name_value_item("tx-packets",         (intptr_t) _iface->tx_packets);
              }
          }

          // azpr_process_stats_t
          {
              xmlElement proc(xb, "process-stats");
              azpr_process_stats_t* _proc = &_data->process;
              xb->name_value_item("user-ticks",             (intptr_t) _proc->user_ticks);
              xb->name_value_item("system-ticks",           (intptr_t) _proc->system_ticks);
              xb->name_value_item("virtual-memory-bytes",   (intptr_t) _proc->virtual_memory_bytes);
              xb->name_value_item("resident-memory-bytes",  (intptr_t) _proc->resident_memory_bytes);
              xb->name_value_item("open-file-descriptors",  (intptr_t) _proc->open_file_descriptors);
          }

          _start = (_start + 1) % _pstats->ps_size;
      }

      rc = mutex_unlock(&_pstats->ps_lock);
      os_syscallok(rc, "mutex_unlock");
  }
}
#endif // AZ_PROXIED

static void handle_deadlocks(azprof::Request *req, xmlBuffer *xb) {
  xmlElement s(xb, "deadlocks");
VM_FindDeadlocks op1(xb);
  VMThread::execute(&op1);
}

static void print_perfdata(xmlBuffer *xb, PerfData *pd) {
  if (pd == NULL || !pd->is_valid()) return;

  char value[256];
  if (pd->units() == PerfData::U_Ticks) {
    snprintf(value, sizeof(value), "%ld", os::ticks_to_millis(((PerfLong*) pd)->get_value()));
  } else {
    pd->format(value, sizeof(value));
    // Note: This is needed because value may contain non-friendly
    // xml characters.
    for (int index = 0; value[index] != (char)0; index++) {
char c=value[index];
        if ((c < 32) || (c == 34) || (c == 38) || (c == 39) ||
            (c == 60) || (c == 62) || (c >= 127)) {
value[index]='?';
        }
    }
  }

  const char *units="";
  switch (pd->units()) {
  case PerfData::U_None:   units = "none"; break;
  case PerfData::U_Bytes:  units = "bytes"; break;
  case PerfData::U_Ticks:  units = "ticks"; break;
  case PerfData::U_Events: units = "events"; break;
  case PerfData::U_String: units = "string"; break;
  case PerfData::U_Hertz:  units = "hertz"; break;
  default: ShouldNotReachHere();
  }

  const char *variability="";
  switch (pd->variability()) {
  case PerfData::V_Constant:  variability = "constant"; break;
  case PerfData::V_Monotonic: variability = "monotonic"; break;
  case PerfData::V_Variable:  variability = "variable"; break;
  default: ShouldNotReachHere();
  }

  xmlElement p(xb, "perf-data");
  xb->name_value_item("name", pd->name());
  xb->name_value_item("value", value);
  xb->name_value_item("units", units);
  xb->name_value_item("variability", variability);
}

static void handle_perfdata(azprof::Request *req, xmlBuffer *xb) {
  // PerfData were designed to be read by an external
  // process by shared memory; Sun code advises to treat
  // PerfData as read-only since using values read from
  // shared mem is a security risk.  Azul is not using
  // shared mem, so reading from the PerfData is ok (though
  // cumbersome).

  if (!UsePerfData) {
    handle_error(xb, "-XX:+UsePerfData was not specified");
    return;
  }

#ifdef PURE6_FIXME // FIXME - need to use the new API
  const char *query = req->parameter_by_name("query");
  if (query == NULL) query = "*";

  PerfDataList *found = PerfDataManager::all()->find_by_regex(query);
  found->sort_by_name(PerfDataList::ASCENDING);

  xmlElement ps(xb, "perf-data-query-results");
for(int i=0;i<found->length();i++){
    print_perfdata(xb, found->at(i));
  }
#else // !PURE6_FIXME
  return;
#endif // !PURE6_FIXME
}

static void handle_codecache(azprof::Request *req, xmlBuffer *xb) {
  CodeCache::print_xml_on(xb);
}

static void handle_codeblob(azprof::Request *req, xmlBuffer *xb) {
  char* id_str = req->parameter_by_name("id");
if(id_str==NULL){
    handle_missing_param(xb, "id");
    return;
  }
  intptr_t id = (intptr_t)strtoul(id_str, NULL, 0);

  CodeBlob *cb = CodeCache::find_blob((void*)id);
  if( !cb ) {
    handle_invalid_param(xb, "id");
    return;
  }
  cb->print_xml_on(xb, false);
}

static void handle_pc(azprof::Request *req, xmlBuffer *xb) {
  char *addr_str = req->parameter_by_name("id");
  if (!addr_str) {
    handle_missing_param(xb, "id");
    return;
  }
  address addr = (address) strtoull(addr_str, NULL, 0);

  char mangled[256];
  char demangled[1024];
  int offset;
  size_t size;
  if (os::dll_address_to_function_name(addr, mangled, sizeof(mangled), &offset, &size)) {
    xmlElement xe(xb, "pc");
    // Right now, the demangle function calls into a machine-gen'd fcn with a
    // >32K stack frame
    //if (azprof::Demangler::demangle(name(), demangled, sizeof(demangled)) == 0) {
    //  xb->name_value_item("pretty_name", demangled);
    //}
    xb->name_value_item("name", azprof::Demangler::demangle(mangled, demangled, sizeof(demangled)) ? mangled : demangled);
    ProfileEntry::print_xml_on(xb);
    address begin = addr - offset;
    address end = begin + size;
    const char *view = xb->request()->parameter_by_name("view");
    if ((view == NULL) || (strcasecmp(view, "asm") == 0)) {
      Disassembler::decode_xml(xb, begin, end);
    } else if (strcasecmp(view, "callee") == 0) {
      RpcTreeNode::print_xml(xb, begin, end - 1, false);
    } else if (strcasecmp(view, "caller") == 0) {
      RpcTreeNode::print_xml(xb, begin, end - 1, true);
    } else {
      Disassembler::decode_xml(xb, begin, end);
    }
  } else {
    xb->name_value_item("error", "invalid address (%p)", addr);
  }

}

static void handle_codeprofile(azprof::Request *req, xmlBuffer* xb) {
  intptr_t moop_id = -1;
  char* moop_str = req->parameter_by_name("moop");
if(moop_str==NULL){
    handle_missing_param(xb, "moop");
    return;
  }
  moop_id = (intptr_t)strtoul(moop_str, NULL, 0);


  const char* compiler_str = req->parameter_by_name("compiler");
if(compiler_str==NULL){
    compiler_str = "1";
  }
  intptr_t compiler = (intptr_t)strtoul(compiler_str, NULL, 0);

  methodOop moop = (methodOop)xb->object_pool()->get_oop(moop_id);
  methodCodeOop mco = compiler == 1 ? moop->lookup_c1() : moop->lookup_c2();
  xb->print_raw(compiler == 1 ? "(C1)" : "(C2)"); xb->cr();

if(mco!=NULL){
    mco->get_codeprofile()->print_xml_on(moop, xb, false, compiler==2?0:-1);
  } else {
    handle_error(xb, "methodcodeoop not found");
  }
}

static void handle_stubcode(azprof::Request *req, xmlBuffer *xb) {
  char* id_str = req->parameter_by_name("id");
if(id_str==NULL){
    handle_missing_param(xb, "id");
    return;
  }

  unsigned long int pc = strtoul( id_str, NULL, 0 );
  if (pc == 0) {
    handle_error(xb, "Could not covert stub code address.");
    return;
  }

  if (pc == ULONG_MAX) {
    handle_error(xb, "Stub code address is out of range.");
    return;
  }

  StubCodeDesc* d = (StubCodeDesc*)pc; // maybe a valid StubCode
  StubCodeDesc* d2 = StubCodeDesc::desc_for_index(d->index());

  if (d2 == NULL || d != d2) {
    handle_error(xb, "There is no stubcode at that address.");
    return;
  }

  {
    d->print_xml_on(xb, false);
  }
}

static void handle_interpreter(azprof::Request *req, xmlBuffer *xb) {
  InterpreterCodelet *codelet = NULL;

  const char *id_str = req->parameter_by_name("id");
  const char *name = req->parameter_by_name("name");

  if (id_str && name) {
    handle_error(xb, "specifying an 'id' and 'name' parameter is ambiguous");
    return;
  }

if(id_str!=NULL){
    intptr_t id = (intptr_t) strtoul(id_str, NULL, 0);
    codelet = InterpreterCodelet::codelet_for_index(id);
if(codelet==NULL){
      handle_invalid_param(xb, "id");
      return;
    }
  }

  if (name != NULL) {
    codelet = InterpreterCodelet::codelet_for_name(name);
if(codelet==NULL){
      handle_invalid_param(xb, "name");
      return;
    }
  }

if(codelet==NULL){
    InterpreterCodelet::print_xml_on(xb);
  } else {
    codelet->print_xml_on(xb, false);
  }
}

static void handle_statistics(azprof::Request *req, xmlBuffer *xb) {
  Statistics::stats_print_xml_on(xb, false);
}

static void handle_old_stats(azprof::Request *req, xmlBuffer *xb) {
  xmlElement xe(xb,"old_stats");
  Deoptimization::print_statistics(xb);
  SharedRuntime::print_statistics(xb);
}

static void handle_sba_stats(azprof::Request *req, xmlBuffer *xb) {
  xmlElement xe(xb,"sba_stats");
  StackBasedAllocation::print_statistics(xb);
}

static void handle_flush_memory(azprof::Request *req, xmlBuffer *xb) {
if(os::use_azmem()){
    // We may not have to call flush memory with azmem
    // Also, we should only iterate through existing accounts.
    // Unimplemented();

    xmlElement xe(xb, "flush_memory");

    // TODO: Disabled. Renable later, when this is supported.
    /*****
    // by default flush all accounts
    int accounts = (1 << os::nof_MEMORY_ACCOUNTS) - 1;

    {
      const char *arg = req->parameter_by_name("acct");
      if (arg != NULL) {
        unsigned long tmp = strtoul(arg, NULL, 0);
        accounts = (int)(tmp & (unsigned long)accounts);
      }
    }

    int cur_account = 0;
    while (accounts != 0) {
      if ((accounts & 1) != 0) {
        xmlElement xe(xb, "memory_account_flush");
        size_t flushed;
        size_t allocated;
        os::MemoryAccount mem_account = (os::MemoryAccount)cur_account;
        os::flush_memory(mem_account, &flushed, &allocated);
        xb->name_value_item("id", (int)mem_account);
        xb->name_value_item("name", os::memory_account_name(mem_account));
        xb->name_value_item("allocated", allocated);
        xb->name_value_item("flushed", flushed);
      }
      accounts = accounts >> 1;
      cur_account++;
    }
    *****/
  }
}

static void handle_find(azprof::Request *req, xmlBuffer *xb) {
  char* id_str = req->parameter_by_name("addr");
if(id_str==NULL){
    handle_missing_param(xb, "addr");
    return;
  }

  void* p = (void*)strtoul(id_str, NULL, 0);
  { xmlElement xe(xb, "raw");
#ifndef PRODUCT
    Debug::find((intptr_t)p, false, xb);
#endif
  }
}

static void handle_dumpstack(azprof::Request *req, xmlBuffer *xb) {
  char* id_str = req->parameter_by_name("id");
if(id_str==NULL){
    handle_missing_param(xb, "id");
    return;
  }

  void* p = (void*)strtoul(id_str, NULL, 0);
  { xmlElement xe(xb, "raw");
    Debug::dump_stack((Thread*)p, xb);
  }
}

static void handle_gdb(azprof::Request *req, xmlBuffer *xb) {
  char* command = req->parameter_by_name("cmd");
if(command==NULL){
    handle_missing_param(xb, "cmd");
    return;
  }

  char* option = req->parameter_by_name("opt");
if(option==NULL){
    handle_missing_param(xb, "opt");
    return;
  }

  { xmlElement xe(xb, "raw");
#if 0
    char command[32];
char option[64];
    if (sscanf(queryArgs, "%s %s", command, option) != 2) {
      xb->print_cr("Illegal examine command: %s", queryArgs);
      return;
    };
#endif // 0

    address addr = (address)strtoul(option, NULL, 0);
    Debug::examine(command, addr, xb);
  }
}

static void handle_monitors(azprof::Request *req, xmlBuffer *xb) {
  ResourceMark rm;
  char* name_str = req->parameter_by_name("name");

if(name_str!=NULL){
    // Handle hot-lock hold time request
    xmlElement list(xb, "vm_lock_hold_list");
    xb->name_value_item("name", name_str); 
    AzLock::print_to_xml_lock_hold_times(name_str,xb);
    return;
  }
  
  {
    xmlElement list(xb, "monitor_list");
    ObjectSynchronizer::summarize_sma_status_xml(xb);
  }
  
}

////////////////////////////////////////////////////////////////////////////////
// Monitoring query handlers used by CPM
////////////////////////////////////////////////////////////////////////////////

#if 0 // (Should be AZ_PROXIED)
// TODO: Do we need a different #define for CPM being enabled?
static void handle_cpm_avm_config(azprof::Request *req, azprof::Response *res) {
  sud_jvm_conf_rev1_t sud;
  if (shared_user_data_get_jvm_conf_rev1(process_self_cached(), &sud)) {
res->internal_server_error();
    return;
  }

  // The CPM libraries consuming this XML don't distinguish between an integer
  // element which is not present and one with a value of zero. So, we apply a
  // "abs(k) + 1" function to the return value. This way the interface to CPM is
  // the following.
  // 0 = unsupported (no "direct_path_state" element)
  // 1 = enabled
  // 2+ = disabled
  int direct_path_state = abs(atcpn_get_source_switching_state()) + 1;
  // 0 = unsupported (no "direct_path_initiate" element)
  // 1 = disabled
  // 2 = enabled
  int direct_path_initiate = abs(proxy_get_use_direct_connect()) + 1;

  res->ok("text/xml", -1);
res->end_header();
  { azprof::Xml response_tag(res, "response");
    { azprof::Xml avm_config_tag(res, "avm_config");
      azprof::Xml::leaf(res, "java_vm_name", sud.name);
      azprof::Xml::leaf(res, "java_vm_vendor", sud.vendor);
      azprof::Xml::leaf(res, "java_vm_info", sud.info);
      azprof::Xml::leaf(res, "java_vm_release", sud.release);
      azprof::Xml::leaf(res, "java_vm_internal_info", sud.internal_info);
      azprof::Xml::leaf(res, "java_vm_flags", sud.flags);
      azprof::Xml::leaf(res, "java_vm_args", sud.args);
      azprof::Xml::leaf(res, "java_vm_specification_version", sud.specification_version);
      azprof::Xml::leaf(res, "java_vm_specification_name", sud.specification_name);
      azprof::Xml::leaf(res, "java_vm_specification_vendor", sud.specification_vendor);
      azprof::Xml::leaf(res, "java_ext_dirs", sud.ext_dirs);
      azprof::Xml::leaf(res, "java_endorsed_dirs", sud.endorsed_dirs);
      azprof::Xml::leaf(res, "java_library_path", sud.library_path);
      azprof::Xml::leaf(res, "java_home", sud.java_home);
      azprof::Xml::leaf(res, "java_class_path", sud.classpath);
      azprof::Xml::leaf(res, "sun_boot_library_path", sud.boot_library_path);
      azprof::Xml::leaf(res, "sun_boot_class_path", sud.boot_classpath);
      azprof::Xml::leaf(res, "sun_java_command", sud.command);
      azprof::Xml::leaf(res, "direct_path_state", (int64_t) direct_path_state);
      azprof::Xml::leaf(res, "direct_path_initiate", (int64_t) direct_path_initiate);

#ifdef AZ_PROXIED
      const proxy_command_line_t* cmd = proxy_get_proxy_command_line();
      azprof::Xml::leaf(res, "azul_proxy_executable",   cmd->pcl_executable ? cmd->pcl_executable : "");
      azprof::Xml::leaf(res, "azul_proxy_pre_flags",    cmd->pcl_pre_flags  ? cmd->pcl_pre_flags  : "");
      azprof::Xml::leaf(res, "azul_proxy_pre_file",     cmd->pcl_pre_file   ? cmd->pcl_pre_file   : "");
      azprof::Xml::leaf(res, "azul_proxy_cline_flags",  cmd->pcl_args       ? cmd->pcl_args       : "");
      azprof::Xml::leaf(res, "azul_proxy_post_flags",   cmd->pcl_post_flags ? cmd->pcl_post_flags : "");
      azprof::Xml::leaf(res, "azul_proxy_post_file",    cmd->pcl_post_file  ? cmd->pcl_post_file  : "");
      azprof::Xml::leaf(res, "azul_proxy_cpm_flags",    cmd->pcl_cpm_flags  ? cmd->pcl_cpm_flags  : "");
      azprof::Xml::leaf(res, "azul_proxy_jar_or_class", cmd->pcl_command    ? cmd->pcl_command    : "");
      azprof::Xml::leaf(res, "azul_proxy_java_args",    cmd->pcl_cmd_args   ? cmd->pcl_cmd_args   : "");
#endif // AZ_PROXIED
    }
  }
}
#endif // #if 0 (Should be AZ_PROXIED)

#if 0 // #if 0 (Should be AZ_PROXIED)
// TODO: Do we need a different #define for CPM being enabled?
static void handle_cpm_heap(azprof::Request *req, xmlBuffer *xb) {
  sud_jvm_heap_rev2_t sud;
  if (shared_user_data_get_jvm_heap_rev2(process_self_cached(), &sud)) {
    xb->response()->internal_server_error();
    return;
  }
  xb->response()->ok("text/xml", -1);
  xb->response()->end_header();
  { xmlElement xe(xb, "response");
    { xmlElement xe(xb, "heap");
      xb->name_value_item("name", sud.name);
      xb->name_ptr_item  ("id", (void*) 0);
      xb->name_value_item("used", sud.used_bytes);
      xb->name_value_item("capacity", sud.capacity_bytes);
      xb->name_value_item("max_capacity", sud.max_capacity_bytes);
      xb->name_value_item("max_capacity_specified", sud.max_capacity_bytes);
      xb->name_value_item("total_collections", sud.total_collections);
      xb->name_value_item("supports_tlab_allocation",
        ((sud.flags & SUD_JVM_HEAP_FLAG_TLAB_ALLOCATION) != 0)  ? "yes" : "no");
    }
    os::print_memory_statistics_xml_on(xb);
  }
}
#endif // #if 0 (Should be AZ_PROXIED)

////////////////////////////////////////////////////////////////////////////////
// HotSpotServlet
////////////////////////////////////////////////////////////////////////////////

ArtaObjectPool* HotSpotServlet::_object_pool = NULL;

#if !defined(AZ_PROXIED)
// --- Start ARTA in the non-proxied world
void HotSpotServlet::start_arta_noproxy() {
    // Start the ARTA server if the command line argument is not the default.
    int arta_port = ARTAPort;
    if (arta_port != 0) {
	azprof::Result result;
	azprof::WebServer::Config conf;
	conf.set_addr1(azprof::WebServer::Addr("0.0.0.0", ARTAPort));
	conf.set_authorization("9@"); // Turn it all on!
	conf.set_debug_flags(ARTADebugFlags);
	azprof::WebServer *server = azprof::WebServer::init(conf, result);
    }
}
#endif // !AZ_PROXIED

// --- Connect to ARTA
void HotSpotServlet::init(){
  ARTAMaxResponseSizeMB =
      azprof::HttpStream::setARTAMaxResponseSizeMB(ARTAMaxResponseSizeMB);
  azprof::WebServer *server = azprof::WebServer::self();
  if (!server) return;

  _object_pool = new ArtaObjectPool();
  ArtaObjects::add(_object_pool);

  server->set_detach_callback(detach_callback, NULL);

  int login = 0;
  int nologin = azprof::Servlet::NO_LOGIN_REQUIRED;
  azprof::Privilege data = azprof::Privilege::app_data_access();
  azprof::Privilege azdata = azprof::Privilege::app_data_azengr_access();
  azprof::Privilege nodata = azprof::Privilege::no_app_data_access();
  azprof::Privilege aznodata = azprof::Privilege::no_app_data_azengr_access();
  azprof::Privilege max = azprof::Privilege::max_access();
  azprof::Privilege azmax = azprof::Privilege::max_azengr_access();

  // Customer visable content
  add("/",                          "Configuration",  "Process",          login,  nodata, handle_jvm_config);
  add("/config/process",            "Configuration",  "Process",          login,  nodata, handle_jvm_config);
  add("/config/hotspot_flags",      "Configuration",  "HotSpot flags",    login,  nodata, handle_hotspot_flags);
  add("/threads/list",              "Threads",        "List",             login,  nodata, handle_thread_list);
  add("/threads/stack_trace",       "Threads",        "Stack trace",      login,  nodata, handle_stack_trace);
  add("/threads/deadlocks",         "Threads",        "Deadlocks",        login,  nodata, handle_deadlocks);
  add("/ticks/profile",             "Ticks",          "Profile",          login,  nodata, handle_profile);
  add("/ticks/reset_profile",                                             login,  nodata, handle_reset_tick_profile);
  add("/monitors/contention",       "Monitors",       "Contention",       login,  nodata, handle_hot_locks);
  add("/memory/summary",            "Memory",         "Summary",          login,  nodata, handle_heap);
  add("/memory/gc_summary",         "Memory",         "GC summary",       login,  nodata, handle_gc_summary_xml);
  add("/memory/gc_history",         "Memory",         "GC history",       login,  nodata, handle_gc_history_xml);
  add("/memory/gc_history.txt",                                           login,  nodata, handle_gc_history_txt);
  add("/memory/allocated_objects",  "Memory",         "Allocated objects",login,  nodata, handle_allocated_objects_xml);
  add("/memory/allocated_objects.csv",                                    login,  nodata, handle_allocated_objects_csv);
  add("/memory/live_objects",       "Memory",         "Live objects",     login,  nodata, handle_live_objects_xml);
  add("/memory/live_objects.csv",                                         login,  nodata, handle_live_objects_csv);
  add("/compilers/tasks",           "Compilers",      "Tasks",            login,  nodata, handle_compile_brokers);
  add("/settings/gc_log",           "Settings",       "GC logging",       login,  nodata, handle_gc_log_settings);

  // Azul engineering/support content
  add0("/force_gc",                 "Azul Support", "Force GC",         login,  azdata,     handle_force_gc);
  add("/perfdata",                  "Azul Support", "Perf data",        login,  aznodata,   handle_perfdata);
  add("/codecache",                 "Azul Support", "Code cache",       login,  aznodata,   handle_codecache);
  add("/codeblob",                  "Azul Support", "Code blob",        login,  aznodata,   handle_codeblob);
  add("/interpreter",               "Azul Support", "Interpreter",      login,  aznodata,   handle_interpreter);
  add("/pc",                        "Azul Support", "PC",               login,  aznodata,   handle_pc);
  add("/codeprofile",               "Azul Support", "Code profile",     login,  aznodata,   handle_codeprofile);
#ifdef AZ_PROXIED
  add("/proxy_call_profile",        "Azul Support", "Old proxy calls",  login,  aznodata,   handle_old_proxy);
  add("/proxy_call_profile_clear",                                      login,  aznodata,   handle_old_proxy_clear);
  add("/proxy_call_profile_toggle",                                     login,  aznodata,   handle_old_proxy_toggle);
  add("/proxy_usage",               "Azul Support", "Proxy Utilization",login,  aznodata,   handle_proxy_usage);
#endif // AZ_PROXIED
  add("/stubcode",                  "Azul Support", "Stub code",        login,  aznodata,   handle_stubcode);
  add("/statistics",                "Azul Support", "Statistics",       login,  aznodata,   handle_statistics);
  add("/old_stats",                 "Azul Support", "Old stats",        login,  aznodata,   handle_old_stats);
  add("/sba_stats",                 "Azul Support", "SBA stats",        login,  aznodata,   handle_sba_stats);
  add("/flush_memory",              "Azul Support", "Flush memory",     login,  azdata,     handle_flush_memory);
  add("/find",                                                          login,  azdata,     handle_find);
  add("/dumpstack",                                                     login,  aznodata,   handle_dumpstack);
  add("/gdb",                                                           login,  azmax,      handle_gdb);
  add("/monitors",                  "Azul Support", "Monitor",          login,  aznodata,   handle_monitors);
  add("/java_lang_Object",          "Azul Support", "java.lang.Object", login,  azdata,     handle_java_lang_Object);
  add("/class_list",                "Azul Support", "Class List",       login,  azdata,     handle_class_list);
  add("/object",                    "Azul Support", "Browse object",    login,  azdata,     handle_browse_object);
  add("/wlmuxer",                   "Azul Support", "Weblogic muxer",   login,  azdata,     WLMuxer::print_xml);
  add("/reset_wlmuxer",                                                 login,  azdata,     WLMuxer::reset);
  add("/safepoints",                "Azul Support", "Safepoints",       login,  azdata,     handle_safepoints);
  add("/reset_safepoints",                                              login,  azdata,     handle_reset_safepoints);

#if 0 // Should be AZ_PROXIED
  // TODO: Do we need a different #define for CPM being enabled?
  // Monitoring queries used by CPM
  server->add_servlet("/GetAvmConfig", new azprof::StatelessServlet(nologin, nodata, handle_cpm_avm_config));
  server->add_servlet("/GetHeap", new XmlServletAdapter(nologin, nodata, new StatelessXmlServlet(handle_cpm_heap)));
#endif // #if 0 should be AZ_PROXIED
}

bool HotSpotServlet::add(const char *path, azprof::Servlet *servlet) {
  azprof::WebServer *server = azprof::WebServer::self();
  if (server) {
    return server->add_servlet(path, new HotSpotServlet(servlet));
  } else {
    return true;
  }
}

bool HotSpotServlet::add(
  const char *path, const char *category, const char *subcategory, azprof::Servlet *servlet
) {
  azprof::WebServer *server = azprof::WebServer::self();
  if (server) {
    return server->add_servlet(path, category, subcategory, new HotSpotServlet(servlet));
  } else {
    return true;
  }
}

bool HotSpotServlet::add(
  const char *path, int flags, azprof::Privilege privilege, azprof::StatelessServlet::Function function
) {
  return add(path, new azprof::StatelessServlet(flags, privilege, function));
}

bool HotSpotServlet::add0(
  const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege privilege,
  azprof::StatelessServlet::Function function
) {
  return add(path, category, subcategory, new azprof::StatelessServlet(flags, privilege, function));
}

bool HotSpotServlet::add(
  const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege privilege,
  azprof::StatelessServlet::Function function
) {
  return add(
    path, category, subcategory,
    new azprof::WebServlet(
      azprof::WebServer::self(), category, subcategory, "hotspot",
      new azprof::StatelessServlet(flags, privilege, function))
  );
}

bool HotSpotServlet::add(const char *path, int flags, azprof::Privilege privilege, XmlServletFunction function) {
  return add(path, new XmlServletAdapter(flags, privilege, new StatelessXmlServlet(function)));
}

bool HotSpotServlet::add(
  const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege privilege,
  XmlServletFunction function
) {
  return add(
    path, category, subcategory,
    new azprof::WebServlet(
      azprof::WebServer::self(), category, subcategory, "hotspot",
      new XmlServletAdapter(flags, privilege, new StatelessXmlServlet(function))
    )
  );
}

HotSpotServlet::HotSpotServlet(azprof::Servlet *__servlet) :
  Servlet(__servlet->flags(), __servlet->required_privilege()),
  _servlet(__servlet) {}

void HotSpotServlet::pre_service(azprof::Request *req, azprof::Response *res) {
  res->save_parameter("byname");
  res->save_parameter("bystatus");
  res->save_parameter("bygroup");
   _servlet->pre_service(req, res);
}

void HotSpotServlet::service(azprof::Request *req, azprof::Response *res) {
  assert0(res->code() == azprof::Response::UNKNOWN);

  // Don't allow monitoring requests to HotSpot since attaching to the current
  // thread could block waiting for completion of a GC cycle (see bug 13074).
  if (req->is_monitoring()) {
res->forbidden();
    return;
  }

  VMTagMark vmt(Thread::current(), VM_REALTime_Performance_Monitor_tag);

  // We're about to try to attach the current thread if it isn't a JavaThread
  // yet then grab our JVM lock. Either of these operations will block while
  // we're at a safepoint. So, to give a response while this is happening we
  // spin checking for a safepoint. If we timeout then a page is returned saying
  // that the JVM is busy. We then reattempt the request showing this busy page
  // until it succeeds. There's a window between checking for a safepoint and
  // attempting to attach and grab our JVM lock but this should be rare. Mucking
  // with safepointing to avoid this race is not worth it.
  if (!req->bool_parameter_by_name("reattempt")) {
    jlong tf = os::elapsed_counter() + (ARTAJVMLockTimeout * os::elapsed_frequency()) / 1000;
    while (true) {
jlong t=os::elapsed_counter();
      if (t < tf) {
        if (!(SafepointSynchronize::is_synchronizing() || SafepointSynchronize::is_at_safepoint())) {
          break;
        }
      } else {
        handle_jvm_busy(req, res, "waiting for the garbage collector");
        return;
      }
      os::yield_all();
    }
  }

  // Attach the current thread if it isn't a JavaThread yet.
  if (!Thread::current()->is_Complete_Java_thread()) {
    JavaVM *vm = (JavaVM*) &main_vm;
    JNIEnv *env;
    JavaVMAttachArgs args = {JNI_VERSION_1_4, "ARTA Thread", Universe::system_thread_group_handle()};
    if (vm->AttachCurrentThreadAsDaemon((void**) &env, (void*) &args)) {
res->internal_server_error();
      return;
    }
  }

  // Grab our JVM lock so we can touch naked oops.
  JavaThread *thread = JavaThread::current();
thread->jvm_lock_self();
  JavaThread::current()->hint_unblocked();

  // Don't do I/O while holding VM locks.
  res->set_non_blocking(true);

  // Process the request using the underlying Servlet.
  { ResourceMark rm;
    HandleMark hm;
   _servlet->service(req, res);
  }

  // Release our JVM lock until we receive another request.
thread->jvm_unlock_self();
  thread->hint_blocked("I/O wait");
}

void HotSpotServlet::post_service(azprof::Request *req, azprof::Response *res) {
   _servlet->post_service(req, res);
}

void HotSpotServlet::detach_callback(void *arg) {
  // Detach from an HTTP or monitoring thread which we may have previously
  // attached to to process a request.
  JavaVM *vm = (JavaVM*) &main_vm;
  vm->DetachCurrentThread();
}

////////////////////////////////////////////////////////////////////////////////
// StatelessXmlServlet
////////////////////////////////////////////////////////////////////////////////

StatelessXmlServlet::StatelessXmlServlet(XmlServletFunction __function) :
  _function(__function) {}

void StatelessXmlServlet::service(azprof::Request *req, xmlBuffer *xmlbuf) {
  _function(req, xmlbuf);
}

////////////////////////////////////////////////////////////////////////////////
// XmlServletAdapter
////////////////////////////////////////////////////////////////////////////////

XmlServletAdapter::XmlServletAdapter(
  int __flags, azprof::Privilege __required_privilege, XmlServlet *__servlet
) : azprof::Servlet(__flags, __required_privilege),
  _servlet(__servlet) {}

void XmlServletAdapter::service(azprof::Request *req, azprof::Response *res) {
  const azprof::Session *session = req->session();
  int flags = 0;
  azprof::Privilege privilege = session->privilege();
  if (privilege.has_app_data_access()) {
    flags |= xmlBuffer::DATA_ACCESS;
  }
  if (privilege.has_max_access() && privilege.has_azul_engineer_access()) {
    flags |= xmlBuffer::ADDR_ACCESS;
  }
  if (privilege.has_azul_engineer_access()) {
    flags |= xmlBuffer::SYM_ACCESS;
  }
  xmlBuffer xb(res, HotSpotServlet::object_pool(), flags);
  //xb.set_non_blocking(false);
  servlet()->service(req, &xb);
}

#endif // AZ_PROFILER
