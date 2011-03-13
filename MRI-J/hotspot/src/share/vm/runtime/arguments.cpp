/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "c1_globals.hpp"
#include "c2_globals.hpp"
#include "defaultStream.hpp"
#include "globals_extension.hpp"
#include "heapIterator.hpp"
#include "java.hpp"
#include "javaAssertions.hpp"
#include "jvm_os.h"
#include "vm_version_pd.hpp"
#include "os_os.hpp"
#include "os_os.inline.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

#define DEFAULT_VENDOR_URL_BUG "http://www.azulsystems.com/support/"
#define DEFAULT_JAVA_LAUNCHER  "generic"

char**  Arguments::_jvm_flags_array             = NULL;
int     Arguments::_num_jvm_flags               = 0;
char**  Arguments::_jvm_args_array              = NULL;
int     Arguments::_num_jvm_args                = 0;
char*  Arguments::_java_command                 = NULL;
SystemProperty* Arguments::_system_properties   = NULL;
const char*  Arguments::_gc_log_filename        = NULL;
bool   Arguments::_has_profile                  = false;
bool   Arguments::_has_alloc_profile            = false;
uintx  Arguments::_initial_heap_size            = 0;
uintx  Arguments::_min_heap_size                = 0;
Arguments::Mode Arguments::_mode                = _mixed;
bool   Arguments::_java_compiler                = false;
bool   Arguments::_xdebug_mode                  = false;
const char*  Arguments::_java_vendor_url_bug    = DEFAULT_VENDOR_URL_BUG;
const char*  Arguments::_sun_java_launcher      = DEFAULT_JAVA_LAUNCHER;
int    Arguments::_sun_java_launcher_pid        = -1;

// These parameters are reset in method parse_vm_init_args(JavaVMInitArgs*)
bool   Arguments::_UseOnStackReplacement        = UseOnStackReplacement;
bool   Arguments::_C1BackgroundCompilation      = C1BackgroundCompilation;
bool   Arguments::_C2BackgroundCompilation      = C2BackgroundCompilation;
bool   Arguments::_ClipInlining                 = ClipInlining;

short  Arguments::ITRTraceOnlyThreadsNum        = 0;
short  Arguments::ITRTraceOnlyThreadsMax        = 0;
char**Arguments::ITRTraceOnlyThreads=NULL;

char*  Arguments::SharedArchivePath             = NULL;

AgentLibraryList Arguments::_libraryList;
AgentLibraryList Arguments::_agentList;

abort_hook_t     Arguments::_abort_hook         = NULL;
exit_hook_t      Arguments::_exit_hook          = NULL;
vfprintf_hook_t  Arguments::_vfprintf_hook      = NULL;


SystemProperty *Arguments::_java_ext_dirs = NULL;
SystemProperty *Arguments::_java_endorsed_dirs = NULL;
SystemProperty *Arguments::_sun_boot_library_path = NULL;
SystemProperty *Arguments::_java_library_path = NULL;
SystemProperty *Arguments::_java_home = NULL;
SystemProperty *Arguments::_java_class_path = NULL;
SystemProperty *Arguments::_sun_boot_class_path = NULL;

char* Arguments::_meta_index_path = NULL;
char* Arguments::_meta_index_dir = NULL;

int32_t   Arguments::_ignore_flags  = 0;

static bool force_client_mode = false;

// Check if head of 'option' matches 'name', and sets 'tail' remaining part of option string

static bool match_option(const JavaVMOption *option, const char* name,
                         const char** tail) {  
  int len = (int)strlen(name);
  if (strncmp(option->optionString, name, len) == 0) {
    *tail = option->optionString + len;
    return true;
  } else {
    return false;
  }
}

static void logOption(const char* opt) {
  if (PrintVMOptions) {
    jio_fprintf(defaultStream::output_stream(), "VM option '%s'\n", opt);
  }
}

// Process java launcher properties.
void Arguments::process_sun_java_launcher_properties(JavaVMInitArgs* args) {
  // See if sun.java.launcher or sun.java.launcher.pid is defined.
  // Must do this before setting up other system properties,
  // as some of them may depend on launcher type.
  bool compiler_override = false;
  for (int index = 0; index < args->nOptions; index++) {
    const JavaVMOption* option = args->options + index;    
    const char* tail;
    
    if (match_option(option, "-Dsun.java.launcher=", &tail)) {
      process_java_launcher_argument(tail, option->extraInfo);
}else if(match_option(option,"-XX:+UseC1",&tail)){
FLAG_SET_CMDLINE(bool,UseC1,true);
}else if(match_option(option,"-XX:+UseC2",&tail)){
FLAG_SET_CMDLINE(bool,UseC2,true);
}else if(match_option(option,"-XX:-UseC1",&tail)){
FLAG_SET_CMDLINE(bool,UseC1,false);
}else if(match_option(option,"-XX:-UseC2",&tail)){
FLAG_SET_CMDLINE(bool,UseC2,false);
}else if(match_option(option,"-client",&tail)){
FLAG_SET_CMDLINE(bool,UseC1,true);
FLAG_SET_CMDLINE(bool,UseC2,false);
}else if(match_option(option,"-server",&tail)){
FLAG_SET_CMDLINE(bool,UseC1,false);
FLAG_SET_CMDLINE(bool,UseC2,true);
}else if(match_option(option,"-tiered",&tail)){
FLAG_SET_CMDLINE(bool,UseC1,true);
FLAG_SET_CMDLINE(bool,UseC2,true);
}else if(match_option(option,"-XX:+OverrideVMProperties",&tail)){
FLAG_SET_CMDLINE(bool,OverrideVMProperties,true);
    }

  }
}

// Initialize system properties key and value.
void Arguments::init_system_properties() {
  // -server and -tiered need different defaults.  Keep the old sane defaults
  // for -server and while allowing the existing flags to be tuned for tiered.
  if( UseC2 && !UseC1 ) {
    C2FreqInlineSize = 100;
    InlineFrequencyCount = 50;
  }

  // Azul - If OverrideVMProperties is enabled, make the properties writeable.
  // This may be needed to appease some tools.
  const bool writable = OverrideVMProperties;

  PropertyList_add(&_system_properties, new SystemProperty("java.vm.specification.version", "1.0", writable));
  PropertyList_add(&_system_properties, new SystemProperty("java.vm.specification.name",
"Java Virtual Machine Specification",writable));
  PropertyList_add(&_system_properties, new SystemProperty("java.vm.specification.vendor",
"Sun Microsystems Inc.",writable));
PropertyList_add(&_system_properties,new SystemProperty("java.vm.version",VM_Version::vm_release(),writable));
PropertyList_add(&_system_properties,new SystemProperty("java.vm.name",VM_Version::vm_name(),writable));
PropertyList_add(&_system_properties,new SystemProperty("java.vm.vendor",VM_Version::vm_vendor(),writable));
PropertyList_add(&_system_properties,new SystemProperty("java.vm.info",VM_Version::vm_info_string(),writable));

  // following are JVMTI agent writeable properties.
  // Properties values are set to NULL and they are
  // os specific they are initialized in os::init_system_properties_values().
  _java_ext_dirs = new SystemProperty("java.ext.dirs", NULL,  true);
  _java_endorsed_dirs = new SystemProperty("java.endorsed.dirs", NULL,  true);
  _sun_boot_library_path = new SystemProperty("sun.boot.library.path", NULL,  true);
  _java_library_path = new SystemProperty("java.library.path", NULL,  true);
  _java_home =  new SystemProperty("java.home", NULL,  true);
  _sun_boot_class_path = new SystemProperty("sun.boot.class.path", NULL,  true);

  _java_class_path = new SystemProperty("java.class.path", "",  true);

  // Add to System Property list.
  PropertyList_add(&_system_properties, _java_ext_dirs);
  PropertyList_add(&_system_properties, _java_endorsed_dirs);
  PropertyList_add(&_system_properties, _sun_boot_library_path);
  PropertyList_add(&_system_properties, _java_library_path);
  PropertyList_add(&_system_properties, _java_home);
  PropertyList_add(&_system_properties, _java_class_path);
  PropertyList_add(&_system_properties, _sun_boot_class_path);

  // Set OS specific system properties values
  os::init_system_properties_values();
}

// String containing commands that will be ignored and cause a
// warning to be issued.  These commands should be accepted
// for 1.6 but not 1.7.  The string should be cleared at the
// beginning of 1.7.
static const char*  obsolete_jvm_flags_1_5_0[] = {
					   "UseTrainGC", 
					   "UseSpecialLargeObjectHandling",
					   "UseOversizedCarHandling",
					   "TraceCarAllocation",
					   "PrintTrainGCProcessingStats",
					   "LogOfCarSpaceSize",
					   "OversizedCarThreshold",
					   "MinTickInterval",
					   "DefaultTickInterval",
					   "MaxTickInterval",
					   "DelayTickAdjustment",
					   "ProcessingToTenuringRatio",
					   "MinTrainLength",
					   0};

bool Arguments::made_obsolete_in_1_5_0(const char *s) {
  int i = 0;
  while (obsolete_jvm_flags_1_5_0[i] != NULL) {
    // <flag>=xxx form
    // [-|+]<flag> form
    if ((strncmp(obsolete_jvm_flags_1_5_0[i], s, 
	       strlen(obsolete_jvm_flags_1_5_0[i])) == 0) ||
	((s[0] == '+' || s[0] == '-') &&
	(strncmp(obsolete_jvm_flags_1_5_0[i], &s[1],
	       strlen(obsolete_jvm_flags_1_5_0[i])) == 0))) {
      return true;
    }
    i++;
  }
  return false;
}

// Constructs the system class path (aka boot class path) from the following
// components, in order:
// 
//     prefix		// from -Xbootclasspath/p:...
//     endorsed		// the expansion of -Djava.endorsed.dirs=...
//     base		// from os::get_system_properties() or -Xbootclasspath=
//     suffix		// from -Xbootclasspath/a:...
// 
// java.endorsed.dirs is a list of directories; any jar or zip files in the
// directories are added to the sysclasspath just before the base.
//
// This could be AllStatic, but it isn't needed after argument processing is
// complete.
class SysClassPath: public StackObj {
public:
  SysClassPath(const char* base);
  ~SysClassPath();

  inline void set_base(const char* base);
  inline void add_prefix(const char* prefix);
  inline void add_suffix(const char* suffix);
  inline void reset_path(const char* base);

  // Expand the jar/zip files in each directory listed by the java.endorsed.dirs
  // property.  Must be called after all command-line arguments have been
  // processed (in particular, -Djava.endorsed.dirs=...) and before calling
  // combined_path().
  void expand_endorsed();

  inline const char* get_base()     const { return _items[_scp_base]; }
  inline const char* get_prefix()   const { return _items[_scp_prefix]; }
  inline const char* get_suffix()   const { return _items[_scp_suffix]; }
  inline const char* get_endorsed() const { return _items[_scp_endorsed]; }

  // Combine all the components into a single c-heap-allocated string; caller
  // must free the string if/when no longer needed.
  char* combined_path();

private:
  // Utility routines.
  static char* add_to_path(const char* path, const char* str, bool prepend);
  static char* add_jars_to_path(char* path, const char* directory);

  inline void reset_item_at(int index);

  // Array indices for the items that make up the sysclasspath.  All except the
  // base are allocated in the C heap and freed by this class.
  enum {
    _scp_prefix,	// from -Xbootclasspath/p:...
    _scp_endorsed,	// the expansion of -Djava.endorsed.dirs=...
    _scp_base,		// the default sysclasspath
    _scp_suffix,	// from -Xbootclasspath/a:...
    _scp_nitems		// the number of items, must be last.
  };

  const char* _items[_scp_nitems];
  DEBUG_ONLY(bool _expansion_done;)
};

SysClassPath::SysClassPath(const char* base) {
  memset(_items, 0, sizeof(_items));
  _items[_scp_base] = base;
  DEBUG_ONLY(_expansion_done = false;)
}

SysClassPath::~SysClassPath() {
  // Free everything except the base.
  for (int i = 0; i < _scp_nitems; ++i) {
    if (i != _scp_base) reset_item_at(i);
  }
  DEBUG_ONLY(_expansion_done = false;)
}

inline void SysClassPath::set_base(const char* base) {
  _items[_scp_base] = base;
}

inline void SysClassPath::add_prefix(const char* prefix) {
  _items[_scp_prefix] = add_to_path(_items[_scp_prefix], prefix, true);
}

inline void SysClassPath::add_suffix(const char* suffix) {
  _items[_scp_suffix] = add_to_path(_items[_scp_suffix], suffix, false);
}

inline void SysClassPath::reset_item_at(int index) {
  assert(index < _scp_nitems && index != _scp_base, "just checking");
  if (_items[index] != NULL) {
    FREE_C_HEAP_ARRAY(char, _items[index]);
    _items[index] = NULL;
  }
}

inline void SysClassPath::reset_path(const char* base) {
  // Clear the prefix and suffix.
  reset_item_at(_scp_prefix);
  reset_item_at(_scp_suffix);
  set_base(base);
}

//------------------------------------------------------------------------------

void SysClassPath::expand_endorsed() {
  assert(_items[_scp_endorsed] == NULL, "can only be called once.");

  const char* path = Arguments::get_property("java.endorsed.dirs");
  if (path == NULL) {
    path = Arguments::get_endorsed_dir();
    assert(path != NULL, "no default for java.endorsed.dirs");
  }

  char* expanded_path = NULL;
  const char separator = *os::path_separator();
  const char* const end = path + strlen(path);
  while (path < end) {
    const char* tmp_end = strchr(path, separator);
    if (tmp_end == NULL) {
      expanded_path = add_jars_to_path(expanded_path, path);
      path = end;
    } else {
      char* dirpath = NEW_C_HEAP_ARRAY(char, tmp_end - path + 1);
      memcpy(dirpath, path, tmp_end - path);
      dirpath[tmp_end - path] = '\0';
      expanded_path = add_jars_to_path(expanded_path, dirpath);
      FREE_C_HEAP_ARRAY(char, dirpath);
      path = tmp_end + 1;
    }
  }
  _items[_scp_endorsed] = expanded_path;
  DEBUG_ONLY(_expansion_done = true;)
}

// Combine the bootclasspath elements, some of which may be null, into a single
// c-heap-allocated string.
char* SysClassPath::combined_path() {
  assert(_items[_scp_base] != NULL, "empty default sysclasspath");
  assert(_expansion_done, "must call expand_endorsed() first.");

  size_t lengths[_scp_nitems];
  size_t total_len = 0;

  const char separator = *os::path_separator();

  // Get the lengths.
  int i;
  for (i = 0; i < _scp_nitems; ++i) {
    if (_items[i] != NULL) {
      lengths[i] = strlen(_items[i]);
      // Include space for the separator char (or a NULL for the last item).
      total_len += lengths[i] + 1;
    }
  }
  assert(total_len > 0, "empty sysclasspath not allowed");

  // Copy the _items to a single string.
  char* cp = NEW_C_HEAP_ARRAY(char, total_len);
  char* cp_tmp = cp;
  for (i = 0; i < _scp_nitems; ++i) {
    if (_items[i] != NULL) {
      memcpy(cp_tmp, _items[i], lengths[i]);
      cp_tmp += lengths[i];
      *cp_tmp++ = separator;
    }
  }
  *--cp_tmp = '\0';	// Replace the extra separator.
  return cp;
}

// Note:  path must be c-heap-allocated (or NULL); it is freed if non-null.
char*
SysClassPath::add_to_path(const char* path, const char* str, bool prepend) {
  char *cp;

  assert(str != NULL, "just checking");
  if (path == NULL) {
    size_t len = strlen(str) + 1;
    cp = NEW_C_HEAP_ARRAY(char, len);
    memcpy(cp, str, len);			// copy the trailing null
  } else {
    const char separator = *os::path_separator();
    size_t old_len = strlen(path);
    size_t str_len = strlen(str);
    size_t len = old_len + str_len + 2;

    if (prepend) {
      cp = NEW_C_HEAP_ARRAY(char, len);
      char* cp_tmp = cp;
      memcpy(cp_tmp, str, str_len);
      cp_tmp += str_len;
      *cp_tmp = separator;
      memcpy(++cp_tmp, path, old_len + 1);	// copy the trailing null
      FREE_C_HEAP_ARRAY(char, path);
    } else {
      cp = REALLOC_C_HEAP_ARRAY(char, path, len);
      char* cp_tmp = cp + old_len;
      *cp_tmp = separator;
      memcpy(++cp_tmp, str, str_len + 1);	// copy the trailing null
    }
  }
  return cp;
}

// Scan the directory and append any jar or zip files found to path.
// Note:  path must be c-heap-allocated (or NULL); it is freed if non-null.
char* SysClassPath::add_jars_to_path(char* path, const char* directory) {
  DIR* dir = os::opendir(directory);
  if (dir == NULL) return path;
  
  char dir_sep[2] = { '\0', '\0' };
  size_t directory_len = strlen(directory);
  const char fileSep = *os::file_separator();
  if (directory[directory_len - 1] != fileSep) dir_sep[0] = fileSep;
    
  /* Scan the directory for jars/zips, appending them to path. */ 
  struct dirent *entry;
  char *dbuf = NEW_C_HEAP_ARRAY(char, os::readdir_buf_size(directory));
  while ((entry = os::readdir(dir, (dirent *) dbuf)) != NULL) {
    const char* name = entry->d_name;
    const char* ext = name + strlen(name) - 4;
    bool isJarOrZip = ext > name &&
      (os::file_name_strcmp(ext, ".jar") == 0 ||
       os::file_name_strcmp(ext, ".zip") == 0);
    if (isJarOrZip) {
      char* jarpath = NEW_C_HEAP_ARRAY(char, directory_len + 2 + strlen(name));
      sprintf(jarpath, "%s%s%s", directory, dir_sep, name);
      path = add_to_path(path, jarpath, false);
      FREE_C_HEAP_ARRAY(char, jarpath);
    }
  }
  FREE_C_HEAP_ARRAY(char, dbuf);
  os::closedir(dir);
  return path;
}

// Parses a memory size specification string.
static bool atomll(const char *s, jlong* result) {
  jlong n = 0;
  int args_read = sscanf(s, os::jlong_format_specifier(), &n);
  if (args_read != 1) {
    return false;
  }
  if (*s == '-') {
    s++; // skip the sign
  }
  while (*s != '\0' && isdigit(*s)) {
    s++;
  }
  // 4705540: illegal if more characters are found after the first non-digit
  if (strlen(s) > 1) {
    return false;
  }
  switch (*s) {
    case 'T': case 't':
      *result = n * G * K;
      return true;
    case 'G': case 'g':
      *result = n * G;
      return true;
    case 'M': case 'm':
      *result = n * M;
      return true;
    case 'K': case 'k':
      *result = n * K;
      return true;
    case '\0':
      *result = n;
      return true;
    default:
      return false;
  }
}

Arguments::ArgsRange Arguments::check_memory_size(jlong size, jlong min_size) {
  if (size < min_size) return arg_too_small;
  // Check that size will fit in a size_t (only relevant on 32-bit)
  if ((julong) size > max_uintx) return arg_too_big;
  return arg_in_range;
}

// Describe an argument out of range error 
void Arguments::describe_range_error(ArgsRange errcode) {
  switch(errcode) {
  case arg_too_big:
    jio_fprintf(defaultStream::error_stream(),
                "The specified size exceeds the maximum "
		"representable size.\n");
    break;
  case arg_too_small:
  case arg_unreadable:
  case arg_in_range:
    // do nothing for now
    break;
  default:
    ShouldNotReachHere();
  }
}

static bool set_bool_flag(char* name, bool value, FlagValueOrigin origin) {
  return CommandLineFlags::boolAtPut(name, &value, origin);
}


static bool set_fp_numeric_flag(char* name, char* value, FlagValueOrigin origin) {
  double v;
  if (sscanf(value, "%lf", &v) != 1) {
    return false;
  }

  if (CommandLineFlags::doubleAtPut(name, &v, origin)) {
    return true;
  }
  return false;
}


static bool set_numeric_flag(char* name, char* value, FlagValueOrigin origin) {
  jlong v;
  intx intx_v;
  bool is_neg = false;
  // Check the sign first since atomll() parses only unsigned values.
  if (*value == '-') {
    if (!CommandLineFlags::intxAt(name, &intx_v)) {
      return false;
    }
    value++;
    is_neg = true;
  }
  if (!atomll(value, &v)) {
    return false;
  }
  intx_v = (intx) v;
  if (is_neg) {
    intx_v = -intx_v;
  }
  if (CommandLineFlags::intxAtPut(name, &intx_v, origin)) {
    return true;
  }
  uintx uintx_v = (uintx) v;
  if (!is_neg && CommandLineFlags::uintxAtPut(name, &uintx_v, origin)) {
    return true;
  }
  return false;
}


static bool set_string_flag(char* name, const char* value, FlagValueOrigin origin) {
  if (!CommandLineFlags::ccstrAtPut(name, &value, origin))  return false;
  // Contract:  CommandLineFlags always returns a pointer that needs freeing.
  FREE_C_HEAP_ARRAY(char, value);
  return true;
}

static bool append_to_string_flag(char* name, const char* new_value, FlagValueOrigin origin) {
  const char* old_value = "";
  if (!CommandLineFlags::ccstrAt(name, &old_value))  return false;
  size_t old_len = old_value != NULL ? strlen(old_value) : 0;
  size_t new_len = strlen(new_value);
  const char* value;
  char* free_this_too = NULL;
  if (old_len == 0) {
    value = new_value;
  } else if (new_len == 0) {
    value = old_value;
  } else {
    char* buf = NEW_C_HEAP_ARRAY(char, old_len + 1 + new_len + 1);
    // each new setting adds another LINE to the switch:
    sprintf(buf, "%s\n%s", old_value, new_value);
    value = buf;
    free_this_too = buf;
  }
  (void) CommandLineFlags::ccstrAtPut(name, &value, origin);
  // CommandLineFlags always returns a pointer that needs freeing.
  FREE_C_HEAP_ARRAY(char, value);
  if (free_this_too != NULL) {
    // CommandLineFlags made its own copy, so I must delete my own temp. buffer.
    FREE_C_HEAP_ARRAY(char, free_this_too);
  }
  return true;
}


bool Arguments::parse_argument(const char* arg, FlagValueOrigin origin) {

  // range of acceptable characters spelled out for portability reasons
#define NAME_RANGE  "[abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_]"
#define BUFLEN 255
  char name[BUFLEN+1];
  char dummy;

  if (sscanf(arg, "-%" XSTR(BUFLEN) NAME_RANGE "%c", name, &dummy) == 1) {
    return set_bool_flag(name, false, origin);
  }
  if (sscanf(arg, "+%" XSTR(BUFLEN) NAME_RANGE "%c", name, &dummy) == 1) {
    return set_bool_flag(name, true, origin);
  }

  char punct;
  if (sscanf(arg, "%" XSTR(BUFLEN) NAME_RANGE "%c", name, &punct) == 2 && punct == '=') {
    const char* value = strchr(arg, '=') + 1;
    // Note that normal -XX:Foo=WWW accumulates
    Flag* flag = Flag::find_flag(name,strlen(name));
    if (flag != NULL && flag->is_ccstr()) {
      if (flag->ccstr_accumulates()) {
        return append_to_string_flag(name, value, origin);
      } else {
        if (value[0] == '\0') {
          value = NULL;
        }
        return set_string_flag(name, value, origin);
      }
    }
  }

  if (sscanf(arg, "%" XSTR(BUFLEN) NAME_RANGE ":%c", name, &punct) == 2 && punct == '=') {
    const char* value = strchr(arg, '=') + 1;
    // -XX:Foo:=xxx will reset the string flag to the given value.
    if (value[0] == '\0') {
      value = NULL;
    }
    return set_string_flag(name, value, origin);
  }

#define SIGNED_FP_NUMBER_RANGE "[-0123456789.]"
#define SIGNED_NUMBER_RANGE    "[-0123456789]"
#define        NUMBER_RANGE    "[0123456789]"
  char value[BUFLEN + 1];
  char value2[BUFLEN + 1];
  if (sscanf(arg, "%" XSTR(BUFLEN) NAME_RANGE "=" "%" XSTR(BUFLEN) SIGNED_NUMBER_RANGE "." "%" XSTR(BUFLEN) NUMBER_RANGE "%c", name, value, value2, &dummy) == 3) {
    // Looks like a floating-point number -- try again with more lenient format string
    if (sscanf(arg, "%" XSTR(BUFLEN) NAME_RANGE "=" "%" XSTR(BUFLEN) SIGNED_FP_NUMBER_RANGE "%c", name, value, &dummy) == 2) {
      return set_fp_numeric_flag(name, value, origin);
    }
  }

#define VALUE_RANGE "[-kmgtKMGT0123456789]"
  if (sscanf(arg, "%" XSTR(BUFLEN) NAME_RANGE "=" "%" XSTR(BUFLEN) VALUE_RANGE "%c", name, value, &dummy) == 2) {
    return set_numeric_flag(name, value, origin);
  }

  return false;
}


void Arguments::add_string(char*** bldarray, int* count, const char* arg) {
  assert(bldarray != NULL, "illegal argument");

  if (arg == NULL) {
    return;
  }

  int index = *count;

  // expand the array and add arg to the last element 
  (*count)++; 
  if (*bldarray == NULL) {
    *bldarray = NEW_C_HEAP_ARRAY(char*, *count);
  } else {
    *bldarray = REALLOC_C_HEAP_ARRAY(char*, *bldarray, *count);
  }
  (*bldarray)[index] = strdup(arg);
}

void Arguments::build_jvm_args(const char* arg) {
  add_string(&_jvm_args_array, &_num_jvm_args, arg);
}

void Arguments::build_jvm_flags(const char* arg) {
  add_string(&_jvm_flags_array, &_num_jvm_flags, arg);
}

// utility function to return a string that concatenates all
// strings in a given char** array
const char* Arguments::build_resource_string(char** args, int count) {
  if (args == NULL || count == 0) {
    return NULL;
  }
  size_t length = strlen(args[0]) + 1; // add 1 for the null terminator
  for (int i = 1; i < count; i++) {
    length += strlen(args[i]) + 1; // add 1 for a space
  }
char*s=NEW_C_HEAP_ARRAY(char,length);
  strcpy(s, args[0]);
  for (int j = 1; j < count; j++) {
    strcat(s, " ");
    strcat(s, args[j]);
  }
  return (const char*) s;
}

static bool match_string(const char* option, const char* name) {  
  int len = (int) strlen(name);
  if (strncmp(option, name, len) == 0) {
    return true;
  } else {
    return false;
  }
}

// List of options present in Javasoft's VM but not in Azul VM.
// Any option that is deleted should be added to this list.
// Currently we just include the product options in this list. The debug options
// are not included here.  The intention is to not fail for an option that would
// have been recognized by the HotSpot VM on the native platform. We do not
// include diagnostic options either. 

typedef struct optionAttr_s {
  const char* option;
  void (*call)(const char*, int32_t*); // function to call for the option
  int32_t overrideAction; // To override the action for unsupported native vm flags 
                          // as UNSUPPORTED_NATIVE_VM_FLAGS_WARN, ERROR or IGNORE
} optionAttr_t;

static bool someNativeVMOptionOverriddenAsError = false;

static optionAttr_t UnsupportedJavasoftOptions[] = {
  {"-Xconcgc", NULL, 0},
  {"-Xincgc", NULL, 0},
  {"-Xnoconcgc", NULL, 0},
  {"-Xnoincgc", NULL, 0},
  {"-Xoss", NULL, 0},
  {"-Xss", NULL, 0},
  {"AggressiveHeap", NULL, 0},
  {"AllocatePrefetchDistance", NULL, 0},
  {"AllocatePrefetchInstr", NULL, 0},
  {"AllocatePrefetchLines", NULL, 0},
  {"AllocatePrefetchStepSize", NULL, 0},
  {"AllocatePrefetchStyle", NULL, 0},
  {"AllowJNIEnvProxy", NULL, 0},
  {"AlwaysActAsServerClassMachine", NULL, 0},
  {"AlwaysCompileLoopMethods", NULL, 0},
  {"BackgroundCompilation", NULL, 0},
  {"BiasedLockingBulkRebiasThreshold", NULL, 0},
  {"BiasedLockingBulkRevokeThreshold", NULL, 0},
  {"BiasedLockingDecayTime", NULL, 0},
  {"BiasedLockingStartupDelay", NULL, 0},
  {"BindGCTaskThreadsToCPUs", NULL, 0},
  {"BranchOnRegister", NULL, 0},
  {"CICompilerCount", NULL, 0},
  {"CICompilerCountPerCPU", NULL, 0},
  {"CMSAbortablePrecleanMinWorkPerIteration", NULL, 0},
  {"CMSAbortablePrecleanWaitMillis", NULL, 0},
  {"CMSAbortSemantics", NULL, 0},
  {"CMSBitMapYieldQuantum", NULL, 0},
  {"CMSBootstrapOccupancy", NULL, 0},
  {"CMSClassUnloadingEnabled", NULL, 0},
  {"CMSCleanOnEnter", NULL, 0},
  {"CMSCompactWhenClearAllSoftRefs", NULL, 0},
  {"CMSConcMarkMultiple", NULL, 0},
  {"CMSConcurrentMTEnabled", NULL, 0},
  {"CMSCoordinatorYieldSleepCount", NULL, 0},
  {"CMSExpAvgFactor", NULL, 0},
  {"CMS_FLSPadding", NULL, 0},
  {"CMS_FLSWeight", NULL, 0},
  {"CMSFullGCsBeforeCompaction", NULL, 0},
  {"CMSIncrementalDutyCycle", NULL, 0},
  {"CMSIncrementalDutyCycleMin", NULL, 0},
  {"CMSIncrementalMode", NULL, 0},
  {"CMSIncrementalOffset", NULL, 0},
  {"CMSIncrementalPacing", NULL, 0},
  {"CMSIncrementalSafetyFactor", NULL, 0},
  {"CMSIndexedFreeListReplenish", NULL, 0},
  {"CMSInitiatingOccupancyFraction", NULL, 0},
  {"CMSLoopWarn", NULL, 0},
  {"CMSMarkStackSize", NULL, 0},
  {"CMSMarkStackSizeMax", NULL, 0},
  {"CMSMaxAbortablePrecleanLoops", NULL, 0},
  {"CMSMaxAbortablePrecleanTime", NULL, 0},
  {"CMSParallelRemarkEnabled", NULL, 0},
  {"CMSParallelSurvivorRemarkEnabled", NULL, 0},
  {"CMSParPromoteBlocksToClaim", NULL, 0},
  {"CMSPermGenPrecleaningEnabled", NULL, 0},
  {"CMSPermGenSweepingEnabled", NULL, 0},
  {"CMSPLABRecordAlways", NULL, 0},
  {"CMSPrecleanDenominator", NULL, 0},
  {"CMSPrecleaningEnabled", NULL, 0},
  {"CMSPrecleanIter", NULL, 0},
  {"CMSPrecleanNumerator", NULL, 0},
  {"CMSPrecleanRefLists1", NULL, 0},
  {"CMSPrecleanRefLists2", NULL, 0},
  {"CMSPrecleanSurvivors1", NULL, 0},
  {"CMSPrecleanSurvivors2", NULL, 0},
  {"CMSPrecleanThreshold", NULL, 0},
  {"CMSRemarkVerifyVariant", NULL, 0},
  {"CMSRescanMultiple", NULL, 0},
  {"CMSRevisitStackSize", NULL, 0},
  {"CMSSamplingGrain", NULL, 0},
  {"CMSScavengeBeforeRemark", NULL, 0},
  {"CMSScheduleRemarkEdenPenetration", NULL, 0},
  {"CMSScheduleRemarkEdenSizeThreshold", NULL, 0},
  {"CMSScheduleRemarkSamplingRatio", NULL, 0},
  {"CMS_SweepPadding", NULL, 0},
  {"CMS_SweepTimerThresholdMillis", NULL, 0},
  {"CMS_SweepWeight", NULL, 0},
  {"CMSTriggerRatio", NULL, 0},
  {"CMSUseOldDefaults", NULL, 0},
  {"CMSWaitDuration", NULL, 0},
  {"CMSWorkQueueDrainThreshold", NULL, 0},
  {"CMSYield", NULL, 0},
  {"CMSYieldSleepCount", NULL, 0},
  {"CMSYoungGenPerWorker", NULL, 0},
  {"CompilationPolicyChoice", NULL, 0},
  {"CompilationRepeat", NULL, 0},
  {"CompilerThreadStackSize", NULL, 0},
  {"CompileThreshold", NULL, 0},
  {"ConvertSleepToYield", NULL, 0},
  {"ConvertYieldToSleep", NULL, 0},
  {"DeferPollingPageLoopCount", NULL, 0},
  {"DeferThrSuspendLoopCount", NULL, 0},
  {"DTraceAllocProbes", NULL, 0},
  {"DTraceMethodProbes", NULL, 0},
  {"DTraceMonitorProbes", NULL, 0},
  {"DumpSharedSpaces", NULL, 0},
  {"EliminateLocks", NULL, 0},
  {"EventLogLength", NULL, 0},
  {"ExplicitGCInvokesConcurrent", NULL, 0},
  {"ExtendedDTraceProbes", NULL, 0},
  {"FLSCoalescePolicy", NULL, 0},
  {"ForceSharedSpaces", NULL, 0},
  {"FreqInlineSize", NULL, 0},
  {"FullGCALot", NULL, 0},
  {"HPILibPath", NULL, 0},
  {"InterpreterProfilePercentage", NULL, 0},
  {"MaxInlineSize", NULL, 0},
  {"MaxTLERatio", NULL, 0},
  {"NeverActAsServerClassMachine", NULL, 0},
  {"OnStackReplacePercentage", NULL, 0},
  {"OptoScheduling", NULL, 0},
  {"ParallelCMSThreads", NULL, 0},
  {"ParallelGCOldGenAllocBufferSize", NULL, 0},
  {"ParallelGCToSpaceAllocBufferSize", NULL, 0},
  {"ParCMSPromoteBlocksToClaim", NULL, 0},
  {"ParGCArrayScanChunk", NULL, 0},
  {"ParGCDesiredObjsFromOverflowList", NULL, 0},
  {"PerBytecodeRecompilationCutoff", NULL, 0},
  {"PerBytecodeTrapLimit", NULL, 0},
  {"PerMethodRecompilationCutoff", NULL, 0},
  {"PerMethodTrapLimit", NULL, 0},
  {"PreferInterpreterNativeStubs", NULL, 0},
  {"PrintCMSInitiationStatistics", NULL, 0},
  {"PrintCMSStatistics", NULL, 0},
  {"PrintFLSCensus", NULL, 0},
  {"PrintFLSStatistics", NULL, 0},
  {"PrintSharedSpaces", NULL, 0},
  {"ProfileMaturityPercentage", NULL, 0},
  {"ReadPrefetchInstr", NULL, 0},
  {"RequireSharedSpaces", NULL, 0},
  {"SafepointSpinBeforeYield", NULL, 0},
  {"SharedDummyBlockSize", NULL, 0},
  {"SharedMiscCodeSize", NULL, 0},
  {"SharedMiscDataSize", NULL, 0},
  {"SharedReadOnlySize", NULL, 0},
  {"SharedReadWriteSize", NULL, 0},
  {"StackRedPages", NULL, 0},
  {"StackReguardSlack", NULL, 0},
  {"StackShadowPages", NULL, 0},
  {"StarvationMonitorInterval", NULL, 0},
  {"StressTieredRuntime", NULL, 0},
  {"TaggedStackInterpreter", NULL, 0},
  {"Tier1BytecodeLimit", NULL, 0},
  {"Tier1FreqInlineSize", NULL, 0},
  {"Tier1Inline", NULL, 0},
  {"Tier1LoopOptsCount", NULL, 0},
  {"Tier1MaxInlineSize", NULL, 0},
  {"Tier1UpdateMethodData", NULL, 0},
  {"Tier2BackEdgeThreshold", NULL, 0},
  {"Tier2CompileThreshold", NULL, 0},
  {"TieredCompilation", NULL, 0},
  {"TimeLinearScan", NULL, 0},
  {"ThreadSafetyMargin", NULL, 0},
  {"ThreadStackSize", NULL, 0},
  {"TLEFragmentationRatio", NULL, 0},
  {"TLEThreadRatio", NULL, 0},
  {"TraceBiasedLocking", NULL, 0},
  {"Use486InstrsOnly", NULL, 0},
  {"UseBiasedLocking", NULL, 0},
  {"UseCMSBestFit", NULL, 0},
  {"UseCMSCollectionPassing", NULL, 0},
  {"UseCMSCompactAtFullCollection", NULL, 0},
  {"UseCMSInitiatingOccupancyOnly", NULL, 0},
  {"UseCompiler", NULL, 0},
  {"UseCompilerSafepoints", NULL, 0},
  {"UseConcMarkSweepGC", NULL, 0},
  {"UseCounterDecay", NULL, 0},
  {"UseDefaultStackSize", NULL, 0},
  {"UseExtendedFileIO", NULL, 0},
  {"UseGCTimeLimit", NULL, 0},
  {"UseISM", NULL, 0},
  {"UseLargePages", NULL, 0},
  {"UseLinuxPosixThreadCPUClocks", NULL, 0},
  {"UseLoopCounter", NULL, 0},
  {"UseMembar", NULL, 0},
  {"UseMPSS", NULL, 0},
  {"UseNiagaraInstrs", NULL, 0},
  {"UseOnDemandStackAllocation", NULL, 0},
  {"UseOprofile", NULL, 0},
  {"UseOSErrorReporting", NULL, 0},
  {"UseParNewGC", NULL, 0},
  {"UsePermISM", NULL, 0},
  {"UseSharedSpaces", NULL, 0},
  {"UseSSE", NULL, 0},
  {"UseStoreImmI16", NULL, 0},
  {"UseTLAB", NULL, 0},
  {"UseTLE", NULL, 0},
  {"UseVectoredExceptions", NULL, 0},
  {"UseVMInterruptibleIO", NULL, 0},
  {"ValueMapInitialSize", NULL, 0},
  {"ValueMapMaxLoopSize", NULL, 0},
  {"VMThreadStackSize", NULL, 0},
  {NULL, NULL, 0}
};


static bool is_unsupported_javasoft_option(const char *arg, bool is_XX_option,
                                           int32_t* ignore_unrecognized_p) {
  // range of acceptable characters spelled out for portability reasons
  char name[256];
  char punct;
  int32_t ignore_unrecognized_tmp = *ignore_unrecognized_p;
  int count;

  #define NAME_RANGE  "[abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_]"

  // We try to match just the name part of the -XX option without the +,-,=,etc.
  // -X options are passed as is
  if (is_XX_option) {
    if (sscanf(arg, "-%255" NAME_RANGE, name) != 1 &&
        sscanf(arg, "+%255" NAME_RANGE, name) != 1 &&
        !(sscanf(arg, "%255" NAME_RANGE "%c", name, &punct) == 2 && punct == '=') &&
        !(sscanf(arg, "%255" NAME_RANGE ":%c", name, &punct) == 2 && punct == '=')) {
      return false;
    }
  } else {
    // Make sure we truncate it if it's not going to fit. Bug 9284
    strncpy(name, arg, 255);
    name[255] = '\0';
  }

  count = 0;
  while (UnsupportedJavasoftOptions[count].option != NULL) {
    if (match_string(name, UnsupportedJavasoftOptions[count].option)) {
      if (UnsupportedJavasoftOptions[count].overrideAction != 0) {
        // zero out the 3 bits that contain the UNSUPPORTED_NATIVE_VM_FLAGS bits
        // and then set the override action
        *ignore_unrecognized_p = (ignore_unrecognized_tmp & 
                                  ~UNSUPPORTED_NATIVE_VM_FLAGS_ERROR &
                                  ~UNSUPPORTED_NATIVE_VM_FLAGS_WARN &
                                  ~UNSUPPORTED_NATIVE_VM_FLAGS_IGNORE) |
                                  UnsupportedJavasoftOptions[count].overrideAction;
        // If the override action is ERROR even for just one flag, we need to exit at
        // the end of argument parsing
        if (UnsupportedJavasoftOptions[count].overrideAction == 
                                   UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) {
          someNativeVMOptionOverriddenAsError = true;
        } 
      }
      // If the option has a function to call, call it with the arg coming in
      if (UnsupportedJavasoftOptions[count].call != NULL) {
        (UnsupportedJavasoftOptions[count].call)(arg, ignore_unrecognized_p);
      }
      return true;
    }
    count++;
  }

  return false;
}


static bool is_unsupported_nativevm_option(const char *arg, bool is_XX_option,
                                           int32_t* ignore_unrecognized_p) {
  bool result = false;
  result = is_unsupported_javasoft_option(arg, is_XX_option, ignore_unrecognized_p);
  return result;
}



// These are options that we still use in the code, but for one reason
// or other we can't allow to be changed by customers.
const char* unchangeable_options[] = { "-Xshare:dump",
"-Xshare:on",
"-Xshare:auto",
"-Xshare:off",
"CacheTimeMillis",
"CITime",
"OnError",
"PerfAllowAtExitRegistration",
"PerfBypassFileSystemCheck",
"PerfDataMemorySize",
"PerfDataSamplingInterval",
"PerfDataSaveToFile",
"PerfDisableSharedMem",
"PerfMaxStringConstLength",
"PerfTraceDataCreation",
"PerfTraceMemOps",
"PrintHeapUsageOverTime",
"PrintConcurrentLocks",
"ReduceSignalUsage",
                                       NULL                           };
  

static bool is_unchangeable_option(const JavaVMOption* option, const char** tail) {
  // range of acceptable characters spelled out for portability reasons
  char name[256];
  char punct;
  int count;
  bool is_XX_option = false;
const char*arg=NULL;

  #define NAME_RANGE  "[abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_]"

if(match_option(option,"-XX:",&arg)){//-XX:xxxx
    is_XX_option = true;
  } else {
    arg = option->optionString;
  }

  *tail = arg;

  // We try to match just the name part of the -XX option without the +,-,=,etc.
  // -X options are passed as is
  if (is_XX_option) {
    if (sscanf(arg, "-%255" NAME_RANGE, name) != 1 &&
        sscanf(arg, "+%255" NAME_RANGE, name) != 1 &&
        !(sscanf(arg, "%255" NAME_RANGE "%c", name, &punct) == 2 && punct == '=') &&
        !(sscanf(arg, "%255" NAME_RANGE ":%c", name, &punct) == 2 && punct == '=')) {
      return false;
    }
  } else {
    // Make sure we truncate it if it's not going to fit. Bug 9284
    strncpy(name, arg, 255);
    name[255] = '\0';
  }

  count = 0;
  while (unchangeable_options[count] != NULL) {
    if (match_string(name, unchangeable_options[count])) {      
      return true;
    }
    count++;
  }

  return false;
}


void Arguments::print_on(outputStream* st) {
  st->print_cr("VM Arguments:");
  if (num_jvm_flags() > 0) {
    st->print("jvm_flags: "); print_jvm_flags_on(st);
  }
  if (num_jvm_args() > 0) {
    st->print("jvm_args: "); print_jvm_args_on(st);
  }
  st->print_cr("java_command: %s", java_command() ? java_command() : "<unknown>");
  st->print_cr("Launcher Type: %s", _sun_java_launcher);
}

void Arguments::print_jvm_flags_on(outputStream* st) {
  if (_num_jvm_flags > 0) {
    for (int i=0; i < _num_jvm_flags; i++) {
      st->print("%s ", _jvm_flags_array[i]);
    }
    st->cr();
  }
}

void Arguments::print_jvm_args_on(outputStream* st) {
  if (_num_jvm_args > 0) {
    for (int i=0; i < _num_jvm_args; i++) {
      st->print("%s ", _jvm_args_array[i]);
    }
    st->cr();
  }
}

bool Arguments::process_argument(const char*arg,int32_t ignore_unrecognized,FlagValueOrigin origin){

  if (parse_argument(arg, origin)) {
    // do nothing
  } else if (made_obsolete_in_1_5_0(arg)) {
    jio_fprintf(defaultStream::error_stream(),
      "Warning: The flag %s has been EOL'd as of 1.5.0 and will"
      " be ignored\n", arg); 
  // ignore_unrecognized will be changed if there is an override action for the option
  } else if (is_unsupported_nativevm_option(arg, true, &ignore_unrecognized)) {
      if ((ignore_unrecognized & UNSUPPORTED_NATIVE_VM_FLAGS_WARN) != 0) {
jio_fprintf(defaultStream::error_stream(),"%s warning: Ignoring unsupported native VM option '%s'\n",
                    VM_Version::vm_name(), arg);
      } else if ((ignore_unrecognized & UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) != 0) {
jio_fprintf(defaultStream::error_stream(),"%s error: Unsupported native VM option '%s'\n",
                    VM_Version::vm_name(), arg);
        // allow for commandline "commenting out" options like -XX:#+Verbose
        if (strlen(arg) == 0 || arg[0] != '#') {
          return false;
        }
      }
  } else {
    if ((ignore_unrecognized & UNRECOGNIZED_FLAGS_WARN) != 0) {
      jio_fprintf(defaultStream::error_stream(),
"%s warning: Ignoring unrecognized VM option '%s'\n",
                  VM_Version::vm_name(), arg);
    } else if ((ignore_unrecognized & UNRECOGNIZED_FLAGS_ERROR) != 0) {
      jio_fprintf(defaultStream::error_stream(),
"%s error: Unrecognized VM option '%s'\n",
                  VM_Version::vm_name(), arg);
      // allow for commandline "commenting out" options like -XX:#+Verbose
      if (strlen(arg) == 0 || arg[0] != '#') {
        return false;
      }
    }
  }
  return true;
}


bool Arguments::process_settings_file(const char*file_name,bool should_exist,int32_t ignore_unrecognized){
  FILE* stream = fopen(file_name, "rb");
  if (stream == NULL) {
    if (should_exist) {
      jio_fprintf(defaultStream::error_stream(),
		  "Could not open settings file %s\n", file_name);
      return false;
    } else {
      return true;
    }
  }

  char token[1024];
  int  pos = 0;

  bool in_white_space = true;
  bool in_comment     = false;
  bool in_quote       = false;
  char quote_c        = 0;
  bool result         = true;

  int c = getc(stream);
  while(c != EOF) {
    if (in_white_space) {
      if (in_comment) {
	if (c == '\n') in_comment = false;
      } else {
        if (c == '#') in_comment = true;
        else if (!isspace(c)) {
          in_white_space = false;
	  token[pos++] = c;
        }
      }
    } else {
      if (c == '\n' || (!in_quote && isspace(c))) {
	// token ends at newline, or at unquoted whitespace
	// this allows a way to include spaces in string-valued options
        token[pos] = '\0';
	logOption(token);
        result &= process_argument(token, ignore_unrecognized, CONFIG_FILE);
        build_jvm_flags(token);
	pos = 0;
	in_white_space = true;
	in_quote = false;
      } else if (!in_quote && (c == '\'' || c == '"')) {
	in_quote = true;
	quote_c = c;
      } else if (in_quote && (c == quote_c)) {
	in_quote = false;
      } else {
        token[pos++] = c;
      }
    }
    c = getc(stream);
  }
  if (pos > 0) {
    token[pos] = '\0';
    result &= process_argument(token, ignore_unrecognized, CONFIG_FILE);
    build_jvm_flags(token);
  }
  fclose(stream);
  return result;
}

//=============================================================================================================
// Parsing of properties (-D) 

const char* Arguments::get_property(const char* key) {
  return PropertyList_get_value(system_properties(), key);
}

bool Arguments::add_property(const char* prop) {
  const char* eq = strchr(prop, '=');
  char* key;
  // ns must be static--its address may be stored in a SystemProperty object.
  const static char ns[1] = {0};
  char* value = (char *)ns;

  size_t key_len = (eq == NULL) ? strlen(prop) : (eq - prop);
  key = AllocateHeap(key_len + 1, "add_property");
  strncpy(key, prop, key_len);
  key[key_len] = '\0';

  if (eq != NULL) {
    size_t value_len = strlen(prop) - key_len - 1;
    value = AllocateHeap(value_len + 1, "add_property");
    strncpy(value, &prop[key_len + 1], value_len + 1);    
  }

  if (strcmp(key, "java.compiler") == 0) {
    process_java_compiler_argument(value);
    FreeHeap(key);
    if (eq != NULL) {
      FreeHeap(value);
    }
    return true;
  }
  else if (strcmp(key, "sun.java.command") == 0) {

    _java_command = value;

    // don't add this property to the properties exposed to the java application
    FreeHeap(key);
    return true;
  }
  else if (strcmp(key, "java.vendor.url.bug") == 0) {
    // save it in _java_vendor_url_bug, so JVM fatal error handler can access
    // its value without going through the property list or making a Java call.
    _java_vendor_url_bug = value;
  }

  // Create new property and add at the end of the list
  PropertyList_unique_add(&_system_properties, key, value);
  return true;
}

//===========================================================================================================
// Setting int/mixed/comp mode flags 

void Arguments::set_mode_flags(Mode mode) {
  // Set up default values for all flags.
  // If you add a flag to any of the branches below,
  // add a default value for it here.
  set_java_compiler(false);
  _mode                      = mode;

  // Ensure Agent_OnLoad has the correct initial values.
  // This may not be the final mode; mode may change later in onload phase.
  PropertyList_unique_add(&_system_properties, "java.vm.info",
     (char*)Abstract_VM_Version::vm_info_string());
  
  UseInterpreter             = true;

  // Default values may be platform/compiler dependent -
  // use the saved values
  ClipInlining               = Arguments::_ClipInlining;
  UseOnStackReplacement      = Arguments::_UseOnStackReplacement;
  C1BackgroundCompilation    = Arguments::_C1BackgroundCompilation;
  C2BackgroundCompilation    = Arguments::_C2BackgroundCompilation;

  // Change from defaults based on mode
  switch (mode) {
  default:
    ShouldNotReachHere();
    break;
  case _int:
    UseC1 = UseC2            = false;
    UseOnStackReplacement    = false;
    break;
  case _mixed:
    // same as default
    break;
  case _comp:
    UseInterpreter           = false;
    C1BackgroundCompilation  = false;
    C2BackgroundCompilation  = false;
    ClipInlining             = false;
    C1PromotionThreshold     = 75;
    break;
  }
}

void Arguments::set_ergonomics_flags() {
if(os::is_server_class_machine()){
    // If no other collector is requested explicitly, 
    // let the VM select the collector based on
    // machine class and automatic selection policy.
    if (!UseSerialGC &&
        !UseGenPauselessGC &&
        FLAG_IS_DEFAULT(UseParallelGC)) {
      if (!init_libraries_at_startup()) {
        FLAG_SET_ERGO(bool, UseParallelGC, true);
      }
    }
  }
}

void Arguments::set_gc_flags(){
  // If parallel old was requested, automatically enable parallel scavenge.
  if (UseParallelOldGC && !UseParallelGC && FLAG_IS_DEFAULT(UseParallelGC)) {
    FLAG_SET_DEFAULT(UseParallelGC, true);
  }

  // AZUL - The default MaxHeapSize is set in
  // calcAvmMinMemory inside java-proxy and the MemCommit size is calculated
  // based on that value. If Xmx is not specified and the initial_heap_size has
  // not been set with -Xms, set the initial_heap_size to 1/4th the MaxHeapSize.

  if (FLAG_IS_DEFAULT(MaxHeapSize)) {
#ifdef AZ_PROXIED
    FLAG_SET_CMDLINE(uintx, MaxHeapSize, (unsigned) proxy_default_xmx_mb() * M);
#else // !AZ_PROXIED:
    FLAG_SET_CMDLINE(uintx, MaxHeapSize, (unsigned) 600 * M);
#endif // !AZ_PROXIED

    if (initial_heap_size() == 0) {
      set_initial_heap_size(MaxHeapSize / 4);
      set_min_heap_size(initial_heap_size());
    }

    // If InitialSurvivorRatio or MinSurvivorRatio were not specified, but the
    // SurvivorRatio has been set, reset their default values to SurvivorRatio +
    // 2.  By doing this we make SurvivorRatio also work for Parallel Scavenger.
    // See CR 6362902 for details.
    if (!FLAG_IS_DEFAULT(SurvivorRatio)) {
      if (FLAG_IS_DEFAULT(InitialSurvivorRatio)) {
         FLAG_SET_DEFAULT(InitialSurvivorRatio, SurvivorRatio + 2); 
      }
      if (FLAG_IS_DEFAULT(MinSurvivorRatio)) {
        FLAG_SET_DEFAULT(MinSurvivorRatio, SurvivorRatio + 2);
      }
    }
    if (PrintGCDetails && Verbose) {
      // Cannot use gclog_or_tty yet.
tty->print_cr("  Maximum heap size for server class platform "
SIZE_FORMAT,MaxHeapSize);
      tty->print_cr("  Initial heap size for server class platform "
                    SIZE_FORMAT, initial_heap_size());	
    }
    
    if (UseParallelOldGC) {
      // Par compact uses lower default values since they are treated as
      // minimums.
      if (FLAG_IS_DEFAULT(MarkSweepDeadRatio)) {
	MarkSweepDeadRatio = 1;
      }
      if (FLAG_IS_DEFAULT(PermMarkSweepDeadRatio)) {
	PermMarkSweepDeadRatio = 5;
      }
    }
  }

os::check_heap_size();
}

// This must be called after ergonomics because we want bytecode rewriting
// if the server compiler is used.
void Arguments::set_bytecode_flags() {
  if (!RewriteBytecodes) {
    FLAG_SET_DEFAULT(RewriteFrequentPairs, false);
  }
}

// Aggressive optimization flags  -XX:+AggressiveOpts
void Arguments::set_aggressive_opts_flags() {
#ifdef COMPILER2
  if (AggressiveOpts || !FLAG_IS_DEFAULT(AutoBoxCacheMax)) {
    if (FLAG_IS_DEFAULT(EliminateAutoBox)) {
      FLAG_SET_DEFAULT(EliminateAutoBox, true);
    }
    if (FLAG_IS_DEFAULT(AutoBoxCacheMax)) {
      FLAG_SET_DEFAULT(AutoBoxCacheMax, 20000);
    } 

    // Feed the cache size setting into the JDK
    char buffer[1024];
    sprintf(buffer, "java.lang.Integer.IntegerCache.high=%d", AutoBoxCacheMax);
    add_property(buffer);
  }
#endif

  if (AggressiveOpts) {
if(FLAG_IS_DEFAULT(ProcessQuantumMS)){
    // Thread Quantum
    // azsched/linux do not support setting a thread's quantum which is used
    // for scheduling.  Because we might want to implement this in the future,
    // I'll leave the code but it will follow these rules.
    // (1) Any attempt to change/set thread quanta will succeed but will have
    //     no effect and be ignored.  This applies to both commandline options
    //     and other programmatic interfaces.
    // (2) Any attempt to read/get thread quanta will return a -1.

    //  FLAG_SET_DEFAULT(ProcessQuantumMS, 160);
    }
  }
}

//===========================================================================================================
// Parsing of java.compiler property

void Arguments::process_java_compiler_argument(char* arg) {
  // For backwards compatibility, Djava.compiler=NONE or "" 
  // causes us to switch to -Xint mode UNLESS -Xdebug
  // is also specified.
  if (strlen(arg) == 0 || strcasecmp(arg, "NONE") == 0) {
    set_java_compiler(true);    // "-Djava.compiler[=...]" most recently seen.
  }
}

void Arguments::process_java_launcher_argument(const char* launcher, void* extra_info) {
  _sun_java_launcher = strdup(launcher);
}

bool Arguments::created_by_java_launcher() {
  assert(_sun_java_launcher != NULL, "property must have value");  
  return strcmp(DEFAULT_JAVA_LAUNCHER, _sun_java_launcher) != 0;
}

//===========================================================================================================
// Parsing of main arguments

bool Arguments::verify_percentage(uintx value, const char* name) {
  if (value <= 100) {
    return true;
  }
  jio_fprintf(defaultStream::error_stream(),
	      "%s of " UINTX_FORMAT " is invalid; must be between 0 and 100\n",
	      name, value);
  return false;
}

// Check the consistency of vm_init_args
bool Arguments::check_vm_args_consistency() {
  // Method for adding checks for flag consistency.
  // The intent is to warn the user of all possible conflicts,
  // before returning an error.
  // Note: Needs platform-dependent factoring.
  bool status = true;

  if( _mode == _int && (UseC1 || UseC2) ) {
    if (ShowMessageBoxOnError) {
      // Prevent noisy output from polluting test harness.
      jio_fprintf(stderr, "Both -Xint and compilers specified. Disabling compilers.\n");
    }
    UseC1 = UseC2 = false;
  }

  if ( ( !UseC1 || !UseC2 ) && FLAG_IS_DEFAULT(ReservedCodeCacheSize) ) {
    FLAG_SET_DEFAULT(ReservedCodeCacheSize, (uintx) (128*M));
  }

  if (!(UseInterpreter || UseC1 || UseC2)) {
    jio_fprintf(stderr, "Interpreter and compilers disabled. Can't run Java code.\n");
    status = false;
  }

  // Azul - UseParallelOldGC cannot be used without UseParallelGC. If UseParallelGC
  // was turned off in the commnad line, turn off UseParallelOldGC.
  if (UseParallelOldGC && !UseParallelGC && !FLAG_IS_DEFAULT(UseParallelGC)) {
    jio_fprintf(defaultStream::error_stream(),
"+UseParallelOldGC cannot be used with -UseParallelGC.\n");
    FLAG_SET_DEFAULT(UseParallelOldGC, false);
  }

  // Ensure that the user has not selected conflicting sets
  // of collectors. [Note: this check is merely a user convenience;
  // collectors over-ride each other so that only a non-conflicting
  // set is selected; however what the user gets is not what they
  // may have expected from the combination they asked for. It's
  // better to reduce user confusion by not allowing them to
  // select conflicting combinations.
  uint i = 0;
  if (UseSerialGC)                       i++;
  if (UseParallelGC || UseParallelOldGC) i++;
if(UseGenPauselessGC)i++;
  if (i > 1) {
    jio_fprintf(defaultStream::error_stream(),
                "Conflicting collector combinations in option list; "
"please refer to the release notes for the combinations allowed\n");
    status = false;
  }

  // if no other collector has been selected at this point, choose UseGenPauselessGC
  // when azmem is available. UseParallelGC if not using azmem.
  if (i == 0) {
if(os::use_azmem()){
FLAG_SET_DEFAULT(UseGenPauselessGC,true);
    } else {
      FLAG_SET_DEFAULT(UseParallelGC, true);
    }
  }

  if ((!((TaskQueueSize != 0) && (TaskQueueSize != 1) && (((TaskQueueSize-1) & TaskQueueSize ) == 0 ))) || (TaskQueueSize > 65536)) {
    jio_fprintf(stderr, "Illegal TaskQueueSize, setting it to 8K\n");
    TaskQueueSize = 8192;
  }

  if (UseParallelGC && ParkTLAB) {
    // Since ParallelGC already has tlab resizing we won't bother to 
    // support parking with parallel. Pauseless does not have resizing - 
    // it always uses 1mb tlabs - so we will use parking. 
if(!FLAG_IS_DEFAULT(ParkTLAB)){
      jio_fprintf(stderr, "+ParkTLAB cannot be used with +UseParallelGC, reset to false\n");
    }
    ParkTLAB = false;
  }

  if (UseSerialGC && ParkTLAB) {
    // Similarly, UseSerialGC already has tlab resizing we won't bother to 
    // support parking with it. 
if(!FLAG_IS_DEFAULT(ParkTLAB)){
      jio_fprintf(stderr, "+ParkTLAB cannot be used with +UseSerialGC, reset to false\n");
    }
    ParkTLAB = false;
  }

  if (UseGenPauselessGC) {
    // If UseGenPauselessGC is set, then LVBs must be set as well.
    UseLVBs = true;    
    #ifdef ASSERT 
    // In DEBUG mode, force the heap verification options to true:
      GPGCVerifyHeap = true;
      GPGCAuditTrail = true;
    #endif // DEBUG

    // The allocation profiler is not supported with pauseless GC.
    if (has_alloc_profile()) {
      jio_fprintf(stderr, "-Xaprof cannot be used with generational pauseless GC\n");
      status = false;
    }
  }

  if (UseSerialGC && ProfileAllocatedObjects) {
if(!FLAG_IS_DEFAULT(ProfileAllocatedObjects)){
      jio_fprintf(stderr, "+ProfileAllocatedObjects cannot be used with +UseSerialGC, reset to false\n");
    }
FLAG_SET_DEFAULT(ProfileAllocatedObjects,false);
  }

  if (UseNUMA) {
    jio_fprintf(defaultStream::error_stream(),
                "error:  NUMA allocator (-XX:+UseNUMA) requires thread-local "
                "allocation\nbuffers (-XX:+UseTLAB).\n");
    status = false;
  }

  if (UseSerialGC && ProfileLiveObjects) {
if(!FLAG_IS_DEFAULT(ProfileLiveObjects)){
      jio_fprintf(stderr, "+ProfileLiveObjects cannot be used with +UseSerialGC, reset to false\n");
    }
FLAG_SET_DEFAULT(ProfileLiveObjects,false);
  }
					      
  if (UseParallelGC && ProfileLiveObjects) {
if(!FLAG_IS_DEFAULT(ProfileLiveObjects)){
      jio_fprintf(stderr, "+ProfileLiveObjects cannot be used with +UseParallelGC, reset to false\n");
    }
FLAG_SET_DEFAULT(ProfileLiveObjects,false);
  }

  if( MultiMapMetaData && 
      (UseAdaptiveGCBoundary ||
       KIDInRef) ) {
if(!FLAG_IS_DEFAULT(MultiMapMetaData)){
      jio_fprintf(stderr, "+MultiMapMetaData does not work with GPGC, UseAdaptiveGCBoundary or kids-in-refs, resetting to false\n");
    }
FLAG_SET_DEFAULT(MultiMapMetaData,false);
  }


#if defined(ASSERT)
#else  // ! ASSERT
#ifndef PRODUCT
     // RefPoisoning isn't supported for non ASSERT builds.
     if (RefPoisoning) {
       if (ShowMessageBoxOnError) {
         // Prevent noisy output from polluting test harness.
         jio_fprintf(stderr, "RefPoisoning only supported for debug and fastdebug builds.\n");
         jio_fprintf(stderr, "Switching off RefPoisoning.\n");
       }
       RefPoisoning = false;
     }
#endif // PRODUCT
#endif // ASSERT

  if (ProfilerLogGC && (Arguments::gc_log_filename() != NULL)) {
    jio_fprintf(stderr, "-XX:+ProfilerLogGC and -Xloggc cannot both be used; using just ProfilerLogGC\n");
_gc_log_filename=NULL;
  }

  if (!is_power_of_2(TickProfilerEntryCount)) {
jio_fprintf(stderr,
"%s warning: TickProfilerEntryCount isn't a power of 2. Rounding up.\n",
VM_Version::vm_name());
    intptr_t n = 1;
    while (n < TickProfilerEntryCount) n <<= 1;
TickProfilerEntryCount=n;
  }

#ifdef AZ_PROXIED
if(FLAG_IS_DEFAULT(WeblogicNativeIO)){
FLAG_SET_DEFAULT(WeblogicNativeIO,true);
  }
#else  // !AZ_PROXIED       
  // default true for non-proxied build...
if(FLAG_IS_DEFAULT(CanGenerateNativeMethodBindEvents)){
FLAG_SET_DEFAULT(CanGenerateNativeMethodBindEvents,true);
  }
#endif // !AZ_PROXIED


  return status;
}

bool Arguments::is_bad_option(const JavaVMOption*option,int32_t ignore_unrecognized,
  const char* option_type) {

  const char* spacer = " ";
  if (option_type == NULL) {
    option_type = ++spacer; // Set both to the empty string.
  }

  if (os::obsolete_option(option)) {
    jio_fprintf(defaultStream::error_stream(),
		"Obsolete %s%soption: %s\n", option_type, spacer,
      option->optionString);
    return false;
  // ignore_unrecognized will be changed if there is an override action for the option
  } else if (is_unsupported_nativevm_option(option->optionString, 
                                            false, &ignore_unrecognized)) {
    if ((ignore_unrecognized & UNSUPPORTED_NATIVE_VM_FLAGS_WARN) != 0) {
      jio_fprintf(defaultStream::error_stream(), 
"%s warning: Ignoring unsupported %s%snative VM option '%s'\n",
                  VM_Version::vm_name(), option_type, spacer, option->optionString);
      return false;
    } else if ((ignore_unrecognized & UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) != 0) {
      jio_fprintf(defaultStream::error_stream(),
"%s error: Unsupported %s%snative VM option '%s'\n",
                  VM_Version::vm_name(), option_type, spacer, option->optionString);
      return true;
    }
  } else {
    if ((ignore_unrecognized & UNRECOGNIZED_FLAGS_WARN) != 0) {
jio_fprintf(defaultStream::error_stream(),"%s warning: Ignoring Unrecognized %s%soption '%s'\n",
                  VM_Version::vm_name(), option_type, spacer, option->optionString);
      return false;
    } else if ((ignore_unrecognized & UNRECOGNIZED_FLAGS_ERROR) != 0) {
      jio_fprintf(stderr, "%s error: Unrecognized %s%soption '%s'\n",
                  VM_Version::vm_name(), option_type, spacer, option->optionString);
      return true;
    }
  }
  return false;
}

static const char* user_assertion_options[] = {
  "-da", "-ea", "-disableassertions", "-enableassertions", 0
};

static const char* system_assertion_options[] = {
  "-dsa", "-esa", "-disablesystemassertions", "-enablesystemassertions", 0
};

// Return true if any of the strings in null-terminated array 'names' matches.
// If tail_allowed is true, then the tail must begin with a colon; otherwise,
// the option must match exactly.
static bool match_option(const JavaVMOption* option, const char** names, const char** tail,
  bool tail_allowed) {
  for (/* empty */; *names != NULL; ++names) {
    if (match_option(option, *names, tail)) {
if(**tail=='\0'||(tail_allowed&&**tail==':')){
	return true;
      }
    }
  }
  return false;
}

Arguments::ArgsRange Arguments::parse_memory_size(const char* s,
						  jlong* long_arg,
						  jlong min_size) {
  if (!atomll(s, long_arg)) return arg_unreadable;
  return check_memory_size(*long_arg, min_size);
}

// Parse JavaVMInitArgs structure

jint Arguments::parse_vm_init_args(const JavaVMInitArgs*args,
                                   bool java_options_env_specified,
                                   const JavaVMInitArgs* java_options_args_ptr,
                                   bool java_tool_options_env_specified,
                                   const JavaVMInitArgs* java_tool_options_args_ptr) {
jint retval=JNI_OK;
jint result=JNI_OK;
char*buf=NULL;
  
  // For components of the system classpath.
  SysClassPath scp(Arguments::get_sysclasspath());
  bool scp_assembly_required = false;

  // Save default settings for some mode flags
  Arguments::_UseOnStackReplacement    = UseOnStackReplacement;
  Arguments::_ClipInlining             = ClipInlining;
  Arguments::_C1BackgroundCompilation  = C1BackgroundCompilation;
  Arguments::_C2BackgroundCompilation  = C2BackgroundCompilation;

  // Parse JAVA_TOOL_OPTIONS environment variable (if present) 
  if (java_tool_options_env_specified) {
    if (Verbose) {
      buf = NEW_C_HEAP_ARRAY(char, java_tool_options_args_ptr->nOptions * 255);
      for (int count = 0; count < java_tool_options_args_ptr->nOptions; count++) {
        sprintf(buf, "%s ", java_tool_options_args_ptr->options[count].optionString);
      }
      jio_fprintf(defaultStream::error_stream(),
                  "Picked up JAVA_TOOL_OPTIONS: %s\n", buf);
FREE_C_HEAP_ARRAY(char,buf);
    }
    result = parse_each_vm_init_arg(java_tool_options_args_ptr,
                                    &scp, &scp_assembly_required, COMMAND_LINE);
    if (result != JNI_OK) {
retval=result;
    }
  }

  // Parse JavaVMInitArgs structure passed in
  result = parse_each_vm_init_arg(args, &scp, &scp_assembly_required, COMMAND_LINE);
  if (result != JNI_OK) {
if(retval==JNI_OK){
retval=result;
      }
  }

  // Parse _JAVA_OPTIONS environment variable (if present)
  if (java_options_env_specified) {
    if (Verbose) {
      buf = NEW_C_HEAP_ARRAY(char, java_options_args_ptr->nOptions * 255);
      for (int count = 0; count < java_options_args_ptr->nOptions; count++) {
        sprintf(buf, "%s ", java_options_args_ptr->options[count].optionString);
      }
      jio_fprintf(defaultStream::error_stream(),
                  "Picked up _JAVA_OPTIONS: %s\n", buf);
FREE_C_HEAP_ARRAY(char,buf);
    }
    result = parse_each_vm_init_arg(java_options_args_ptr,
                                    &scp, &scp_assembly_required, COMMAND_LINE);
    if (result != JNI_OK) {
if(retval==JNI_OK){
retval=result;
      }
    }
  }

  // All options have been parsed.  If we encountered an error, return here.
if(retval!=JNI_OK){
    return retval;
  }

  // Do final processing now that all arguments have been parsed
  result = finalize_vm_init_args(&scp, scp_assembly_required);
  if (result != JNI_OK) {
    return result;
  }

  return JNI_OK;
}


jint Arguments::parse_each_vm_init_arg(const JavaVMInitArgs* args, 
                                       SysClassPath* scp_p,
                                       bool* scp_assembly_required_p,
                                       FlagValueOrigin origin) {
jint retval=JNI_OK;
  
  // Remaining part of option string
  const char* tail;

  // iterate over arguments  
  for (int index = 0; index < args->nOptions; index++) {
    bool is_absolute_path = false;  // for -agentpath vs -agentlib

    const JavaVMOption* option = args->options + index;    

    if (!match_option(option, "-Djava.class.path", &tail) &&
        !match_option(option, "-Dsun.java.command", &tail) &&
        !match_option(option, "-Dsun.java.launcher", &tail)) { 

        // add all jvm options to the jvm_args string. This string
        // is used later to set the java.vm.args PerfData string constant.
        // the -Djava.class.path and the -Dsun.java.command options are
        // omitted from jvm_args string as each have their own PerfData
        // string constant object.
	build_jvm_args(option->optionString);
    }

    // we don't want to parse arguments that are in our unchangeable
    // options, so we need to check this first
    if (is_unchangeable_option(option, &tail)) {
      if ((_ignore_flags & UNSUPPORTED_NATIVE_VM_FLAGS_WARN) != 0) {
        jio_fprintf(stderr, "%s warning: Ignoring unsupported native VM option '%s'\n",
                    VM_Version::vm_name(), tail);
      } else if ((_ignore_flags & UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) != 0) {
        jio_fprintf(stderr, "%s error: Unsupported native VM option '%s'\n",
                    VM_Version::vm_name(), tail);
        // allow for commandline "commenting out" options like -XX:#+Verbose
        if (strlen(tail) == 0 || tail[0] != '#') {
if(retval==JNI_OK){
            retval = JNI_EINVAL;
          }
        }
      }
      continue;
    }
      
    // backwards compatibility: accept -verbosegc  
if(match_option(option,"-verbosegc",&tail)){
FLAG_SET_CMDLINE(bool,PrintGC,true);
FLAG_SET_CMDLINE(bool,PrintGCTimeStamps,true);
}else if(match_option(option,"-verbose",&tail)){
    // -verbose:[class/gc/jni/pause]
      if (!strcmp(tail, ":class") || !strcmp(tail, "")) {
        FLAG_SET_CMDLINE(bool, TraceClassLoading, true);
        FLAG_SET_CMDLINE(bool, TraceClassUnloading, true);
      } else if (!strcmp(tail, ":gc")) {
        FLAG_SET_CMDLINE(bool, PrintGC, true);
        FLAG_SET_CMDLINE(bool, PrintGCTimeStamps, true);
      } else if (!strcmp(tail, ":jni")) {
        FLAG_SET_CMDLINE(bool, PrintJNIResolving, true);
FLAG_SET_CMDLINE(bool,PrintJNILoading,true);
}else if(!strcmp(tail,":pause")){
FLAG_SET_CMDLINE(bool,PrintPauses,true);
      }
    // -da / -ea / -disableassertions / -enableassertions
    // These accept an optional class/package name separated by a colon, e.g.,
    // -da:java.lang.Thread.
    } else if (match_option(option, user_assertion_options, &tail, true)) {
      bool enable = option->optionString[1] == 'e';	// char after '-' is 'e'
      if (*tail == '\0') {
	JavaAssertions::setUserClassDefault(enable);
      } else {
	assert(*tail == ':', "bogus match by match_option()");
	JavaAssertions::addOption(tail + 1, enable);
      }
    // -dsa / -esa / -disablesystemassertions / -enablesystemassertions
    } else if (match_option(option, system_assertion_options, &tail, false)) {
      bool enable = option->optionString[1] == 'e';	// char after '-' is 'e'
      JavaAssertions::setSystemClassDefault(enable);
    // -bootclasspath:
    } else if (match_option(option, "-Xbootclasspath:", &tail)) {
      scp_p->reset_path(tail);
      *scp_assembly_required_p = true;
    // -bootclasspath/a:
    } else if (match_option(option, "-Xbootclasspath/a:", &tail)) {
      scp_p->add_suffix(tail);
      *scp_assembly_required_p = true;
    // -bootclasspath/p:
    } else if (match_option(option, "-Xbootclasspath/p:", &tail)) {
      scp_p->add_prefix(tail);
      *scp_assembly_required_p = true;
    // -Xrun
    } else if (match_option(option, "-Xrun", &tail)) {
      if(tail != NULL) {
        const char* pos = strchr(tail, ':');
        size_t len = (pos == NULL) ? strlen(tail) : pos - tail;
        char* name = (char*)memcpy(NEW_C_HEAP_ARRAY(char, len + 1), tail, len);
        name[len] = '\0';

        char *options = NULL;
        if(pos != NULL) {
          size_t len2 = strlen(pos+1) + 1; // options start after ':'.  Final zero must be copied.
          options = (char*)memcpy(NEW_C_HEAP_ARRAY(char, len2), pos+1, len2);
        }
#ifdef JVMTI_KERNEL
        if ((strcmp(name, "hprof") == 0) || (strcmp(name, "jdwp") == 0)) {
          warning("profiling and debugging agents are not supported with Kernel VM");
        } else
#endif // JVMTI_KERNEL
        add_init_library(name, options);
      }
    // -agentlib and -agentpath
    } else if (match_option(option, "-agentlib:", &tail) ||
          (is_absolute_path = match_option(option, "-agentpath:", &tail))) {
      if(tail != NULL) {
        const char* pos = strchr(tail, '=');
        size_t len = (pos == NULL) ? strlen(tail) : pos - tail;
        char* name = strncpy(NEW_C_HEAP_ARRAY(char, len + 1), tail, len);
        name[len] = '\0';

        char *options = NULL;
        if(pos != NULL) {
          options = strcpy(NEW_C_HEAP_ARRAY(char, strlen(pos + 1) + 1), pos + 1);
        }
        add_init_agent(name, options, is_absolute_path);
      }
    // -javaagent
    } else if (match_option(option, "-javaagent:", &tail)) {
      if(tail != NULL) {
        char *options = strcpy(NEW_C_HEAP_ARRAY(char, strlen(tail) + 1), tail);
        add_init_agent("instrument", options, false);
      }
    // -Xnoclassgc
    } else if (match_option(option, "-Xnoclassgc", &tail)) {
      FLAG_SET_CMDLINE(bool, ClassUnloading, false);
    // -Xbatch
    } else if (match_option(option, "-Xbatch", &tail)) {
      // Silently ignore, as background compilation is way cheap for Azul
      //FLAG_SET_CMDLINE(bool, C1BackgroundCompilation, false);
      //FLAG_SET_CMDLINE(bool, C2BackgroundCompilation, false);
    // -Xmn for compatibility with other JVM vendors
    } else if (match_option(option, "-Xmn", &tail)) {
      jlong long_initial_eden_size = 0;
      ArgsRange errcode = parse_memory_size(tail, &long_initial_eden_size, 1);
      if (errcode != arg_in_range) {
        jio_fprintf(defaultStream::error_stream(),
		    "Invalid initial eden size: %s\n", option->optionString);
        describe_range_error(errcode);
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      } else {
        FLAG_SET_CMDLINE(uintx, MaxNewSize, (size_t) long_initial_eden_size);
        FLAG_SET_CMDLINE(uintx, NewSize, (size_t) long_initial_eden_size);
      }
    // -Xms
    } else if (match_option(option, "-Xms", &tail)) {
      jlong long_initial_heap_size = 0;
      ArgsRange errcode = parse_memory_size(tail, &long_initial_heap_size, 1);
      if (errcode != arg_in_range) {
        jio_fprintf(defaultStream::error_stream(),
		    "Invalid initial heap size: %s\n", option->optionString);
        describe_range_error(errcode);
if(retval==JNI_OK){
          retval = JNI_EINVAL;        
        }
      } else {
        set_initial_heap_size((size_t) long_initial_heap_size);
        // Currently the minimum size and the initial heap sizes are the same.
        set_min_heap_size(initial_heap_size());      
      }
    // -Xmx
    } else if (match_option(option, "-Xmx", &tail)) {
      jlong long_max_heap_size = 0;
      ArgsRange errcode = parse_memory_size(tail, &long_max_heap_size, 1);
      if (errcode != arg_in_range) {
        jio_fprintf(defaultStream::error_stream(),
		    "Invalid maximum heap size: %s\n", option->optionString);
        describe_range_error(errcode);
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      } else {        
        FLAG_SET_CMDLINE(uintx, MaxHeapSize, (size_t) long_max_heap_size);
      }
    // Xmaxf
    } else if (match_option(option, "-Xmaxf", &tail)) {
      int maxf = (int)(atof(tail) * 100);
      if (maxf < 0 || maxf > 100) {
        jio_fprintf(defaultStream::error_stream(),
		    "Bad max heap free percentage size: %s\n",
		    option->optionString);
if(retval==JNI_OK){
          retval = JNI_EINVAL;		    
        }
      } else {
        FLAG_SET_CMDLINE(uintx, MaxHeapFreeRatio, maxf);
      }
    // Xminf
    } else if (match_option(option, "-Xminf", &tail)) {
      int minf = (int)(atof(tail) * 100);
      if (minf < 0 || minf > 100) {
        jio_fprintf(defaultStream::error_stream(),
		    "Bad min heap free percentage size: %s\n",
		    option->optionString);
if(retval==JNI_OK){
          retval = JNI_EINVAL;		    
        }
      } else {
        FLAG_SET_CMDLINE(uintx, MinHeapFreeRatio, minf);
      }
    // -Xmaxjitcodesize
    } else if (match_option(option, "-Xmaxjitcodesize", &tail)) {
      jlong long_ReservedCodeCacheSize = 0;
      ArgsRange errcode = parse_memory_size(tail, &long_ReservedCodeCacheSize,
					    CodeCacheMinimumFreeSpace);
      if (errcode != arg_in_range) {
        jio_fprintf(defaultStream::error_stream(),
		    "Invalid maximum code cache size: %s\n",
		    option->optionString);
        describe_range_error(errcode);
if(retval==JNI_OK){
          retval = JNI_EINVAL;        
        }
      } else {
        FLAG_SET_CMDLINE(uintx, ReservedCodeCacheSize, (uintx) long_ReservedCodeCacheSize);      
      }
    // -green
    } else if (match_option(option, "-green", &tail)) {
      jio_fprintf(defaultStream::error_stream(),
		  "Green threads support not available\n");
if(retval==JNI_OK){
        retval = JNI_EINVAL;
      }
    // -native
    } else if (match_option(option, "-native", &tail)) {
	  // HotSpot always uses native threads, ignore silently for compatibility
    // -Xsqnopause
    } else if (match_option(option, "-Xsqnopause", &tail)) {
	  // EVM option, ignore silently for compatibility
    // -Xrs
    } else if (match_option(option, "-Xrs", &tail)) {
	  // Classic/EVM option, new functionality
      FLAG_SET_CMDLINE(bool, ReduceSignalUsage, true);
    } else if (match_option(option, "-Xusealtsigs", &tail)) {
          // change default internal VM signals used - lower case for back compat
      FLAG_SET_CMDLINE(bool, UseAltSigs, true);
    // -Xoptimize
    } else if (match_option(option, "-Xoptimize", &tail)) {
	  // EVM option, ignore silently for compatibility
    // -Xprof
    } else if (match_option(option, "-Xprof", &tail)) {
#ifndef FPROF_KERNEL
      _has_profile = true;
#else // FPROF_KERNEL
      // do we have to exit?
      warning("Kernel VM does not support flat profiling.");
#endif // FPROF_KERNEL
    // -Xaprof
    } else if (match_option(option, "-Xaprof", &tail)) {
      _has_alloc_profile = true;
    // -Xconcurrentio
    } else if (match_option(option, "-Xconcurrentio", &tail)) {
      // Silently ignore, as background compilation is way cheap for Azul
      //FLAG_SET_CMDLINE(bool, UseLWPSynchronization, true);
      //FLAG_SET_CMDLINE(bool, C1BackgroundCompilation, false);
      //FLAG_SET_CMDLINE(bool, C2BackgroundCompilation, false);
      //FLAG_SET_CMDLINE(uintx, NewSizeThreadIncrease, 16 * K);  // 20Kb per thread added to new generation

      // -Xinternalversion
    } else if (match_option(option, "-Xinternalversion", &tail)) {
      jio_fprintf(defaultStream::output_stream(), "%s\n",
		  VM_Version::internal_vm_info_string());
      vm_exit(0);
#ifndef PRODUCT
    // -Xprintflags
    } else if (match_option(option, "-Xprintflags", &tail)) {
      CommandLineFlags::printFlags();
      vm_exit(0);
#endif
    // -D
    } else if (match_option(option, "-D", &tail)) {      
      if (!add_property(tail)) {
if(retval==JNI_OK){
          retval = JNI_ENOMEM;
        }
      }    
      // Out of the box management support
      if (match_option(option, "-Dcom.sun.management", &tail)) {
        FLAG_SET_CMDLINE(bool, ManagementServer, true);
      }
    // -Xint
    } else if (match_option(option, "-Xint", &tail)) {
	  set_mode_flags(_int);
    // -Xmixed
    } else if (match_option(option, "-Xmixed", &tail)) {
	  set_mode_flags(_mixed);
    // -Xcomp
    } else if (match_option(option, "-Xcomp", &tail)) {
      // for testing the compiler; turn off all flags that inhibit compilation
	  set_mode_flags(_comp);

    // -Xverify
    } else if (match_option(option, "-Xverify", &tail)) {      
      if (strcmp(tail, ":all") == 0 || strcmp(tail, "") == 0) {
        FLAG_SET_CMDLINE(bool, BytecodeVerificationLocal, true);
        FLAG_SET_CMDLINE(bool, BytecodeVerificationRemote, true);
      } else if (strcmp(tail, ":remote") == 0) {
        FLAG_SET_CMDLINE(bool, BytecodeVerificationLocal, false);
        FLAG_SET_CMDLINE(bool, BytecodeVerificationRemote, true);
      } else if (strcmp(tail, ":none") == 0) {
        FLAG_SET_CMDLINE(bool, BytecodeVerificationLocal, false);
        FLAG_SET_CMDLINE(bool, BytecodeVerificationRemote, false);
}else if(is_bad_option(option,_ignore_flags,"verification")){
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      }
    // -Xdebug
    } else if (match_option(option, "-Xdebug", &tail)) {
      // note this flag has been used, then ignore
      set_xdebug_mode(true);
    // -Xnoagent 
    } else if (match_option(option, "-Xnoagent", &tail)) {    
      // For compatibility with classic. HotSpot refuses to load the old style agent.dll.
    } else if (match_option(option, "-Xboundthreads", &tail)) {    
      // Bind user level threads to kernel threads (Solaris only)
      FLAG_SET_CMDLINE(bool, UseBoundThreads, true);
    } else if (match_option(option, "-Xloggc:", &tail)) {
      // Redirect GC output to the file. -Xloggc:<filename>
      // ostream_init_log(), when called will use this filename
      // to initialize a fileStream.
      _gc_log_filename = strdup(tail);
      FLAG_SET_CMDLINE(bool, PrintGC, true);
      FLAG_SET_CMDLINE(bool, PrintGCTimeStamps, true);
}else if(match_option(option,"-Xflags",&tail)){
      // Already parsed in scan_for_Xflags_and_XXFlags
      continue;
}else if(match_option(option,"-Xnativevmflags",&tail)){
      // Already parsed in scan_for_Xflags_and_XXFlags
      continue;

    // JNI hooks
    } else if (match_option(option, "-Xcheck", &tail)) {    
      if (!strcmp(tail, ":jni")) {
        CheckJNICalls = true;
}else if(is_bad_option(option,_ignore_flags,"check")){
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      }
    } else if (match_option(option, "vfprintf", &tail)) {    
      _vfprintf_hook = CAST_TO_FN_PTR(vfprintf_hook_t, option->extraInfo);
    } else if (match_option(option, "exit", &tail)) {    
      _exit_hook = CAST_TO_FN_PTR(exit_hook_t, option->extraInfo);
    } else if (match_option(option, "abort", &tail)) {    
      _abort_hook = CAST_TO_FN_PTR(abort_hook_t, option->extraInfo);
    } else if (match_option(option, "-XX:+NeverTenure", &tail)) {    
      // The last option must always win.
      FLAG_SET_CMDLINE(bool, AlwaysTenure, false);
      FLAG_SET_CMDLINE(bool, NeverTenure, true);
    } else if (match_option(option, "-XX:+AlwaysTenure", &tail)) {    
      // The last option must always win.
      FLAG_SET_CMDLINE(bool, NeverTenure, false);
      FLAG_SET_CMDLINE(bool, AlwaysTenure, true);
}else if(match_option(option,"-XX:-UseParallelGC",&tail)){
FLAG_SET_CMDLINE(bool,UseParallelGC,false);
FLAG_SET_CMDLINE(uintx,ParallelGCThreads,0);
    // The TLE options are for compatibility with 1.3 and will be
    // removed without notice in a future release.  These options
    // are not to be documented.
      // No longer used.
    } else if (match_option(option, "-XX:+ResizeTLE", &tail)) {
      FLAG_SET_CMDLINE(bool, ResizeTLAB, true);
    } else if (match_option(option, "-XX:-ResizeTLE", &tail)) {
      FLAG_SET_CMDLINE(bool, ResizeTLAB, false);
    } else if (match_option(option, "-XX:+PrintTLE", &tail)) {
      FLAG_SET_CMDLINE(bool, PrintTLAB, true);
    } else if (match_option(option, "-XX:-PrintTLE", &tail)) {
      FLAG_SET_CMDLINE(bool, PrintTLAB, false);
      // No longer used.
    } else if (match_option(option, "-XX:TLESize=", &tail)) {
      jlong long_tlab_size = 0;
      ArgsRange errcode = parse_memory_size(tail, &long_tlab_size, 1);
      if (errcode != arg_in_range) {
        jio_fprintf(defaultStream::error_stream(),
		    "Invalid TLAB size: %s\n", option->optionString);
        describe_range_error(errcode);
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      } else {
        FLAG_SET_CMDLINE(uintx, TLABSize, long_tlab_size);      
      }
}else if(match_option(option,"-XX:+UseOptimisticThreadConcurrency",&tail)){
      FLAG_SET_CMDLINE(bool, UseSMA, true); // OptimisticThreadConcurrency is the marketing name for Speculative Multiaddress Atomicity
}else if(match_option(option,"-XX:-UseOptimisticThreadConcurrency",&tail)){
      FLAG_SET_CMDLINE(bool, UseSMA, false); // OptimisticThreadConcurrency is the marketing name for Speculative Multiaddress Atomicity
}else if(match_option(option,"-XX:+OrigJavaUtilZip",&tail)){
      OrigJavaUtilZip = true;
      os::init_system_properties_values();
}else if(match_option(option,"-XX:-OrigJavaUtilZip",&tail)){
      OrigJavaUtilZip = false;
      os::init_system_properties_values();
    } else if (match_option(option, "-XX:+UseHighScaleLib", &tail) && tail[0]==0 ) {
      UseHighScaleLib = true;
      os::init_system_properties_values();
    } else if (match_option(option, "-XX:-UseHighScaleLib", &tail) && tail[0]==0 ) {
      UseHighScaleLib = false;
      os::init_system_properties_values();
}else if(match_option(option,"-XX:+UseHighScaleLibHashtable",&tail)){
      UseHighScaleLibHashtable = true;
      os::init_system_properties_values();
}else if(match_option(option,"-XX:-UseHighScaleLibHashtable",&tail)){
      UseHighScaleLibHashtable = false;
      os::init_system_properties_values();
    } else if (match_option(option, "-XX:+DisplayVMOutputToStderr", &tail)) {      
      FLAG_SET_CMDLINE(bool, DisplayVMOutputToStdout, false);
      FLAG_SET_CMDLINE(bool, DisplayVMOutputToStderr, true);
    } else if (match_option(option, "-XX:+DisplayVMOutputToStdout", &tail)) {
      FLAG_SET_CMDLINE(bool, DisplayVMOutputToStderr, false);
      FLAG_SET_CMDLINE(bool, DisplayVMOutputToStdout, true);
}else if(match_option(option,"-XX:+ProfilerLogGC",&tail)){
      ProfilerLogGC = true;
      PrintGC = true;
      PrintGCTimeStamps = true;
}else if(match_option(option,"-XX:+HeapDumpOnCtrlBreak",&tail)){
      HeapDumpOnCtrlBreak = true;
      HeapIterator::set_should_initialize();
}else if(match_option(option,"-XX:+HeapDumpOnOutOfMemoryError",&tail)){
      HeapDumpOnOutOfMemoryError = true;
      HeapIterator::set_should_initialize();
}else if(match_option(option,"-XX:+UseC1",&tail)||
               match_option(option, "-XX:-UseC1", &tail) ||
               match_option(option, "-XX:+UseC2", &tail) ||
               match_option(option, "-XX:-UseC2", &tail) ||
               match_option(option, "-client", &tail) ||
               match_option(option, "-server", &tail) ||
match_option(option,"-tiered",&tail)){
      // Already parsed in init_system_properties
      continue;
    } else 
    if (match_option(option, "-XX:", &tail)) { // -XX:xxxx
      // Skip -XX:Flags= since that case has already been handled
      if (strncmp(tail, "Flags=", strlen("Flags=")) != 0) {
if(!process_argument(tail,_ignore_flags,origin)){
if(retval==JNI_OK){
            retval = JNI_EINVAL;
          }
        }
      }
#ifndef AZ_PROXIED
    } else if (is_valid_px_option(option)) {
      // Nothing to do - PX options were parsed in CreateJavaVM
#endif // !AZ_PROXIED
    } else if (is_bad_option(option, _ignore_flags)) {
      // Unknown option
if(retval==JNI_OK){
        retval = JNI_ERR;
      }
    }
  }
  if (retval != JNI_OK && (((_ignore_flags & UNRECOGNIZED_FLAGS_ERROR) != 0) ||
                           ((_ignore_flags & UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) != 0) ||
                           someNativeVMOptionOverriddenAsError)) {
      return retval;  // Bad arguments seen and do not ignore
  }

  if (UseITR && UseSMA) {
    jio_fprintf(stderr, "[ITR] SMA not supported with tracing --> turning off SMA...\n");
    UseSMA = false;
  }

  if (CompileTheWorld) {
    //tty->print_cr("[CTW] Forcing mixed mode");
set_mode_flags(_mixed);
  }
  // Peg compile thresholds
  C1CompileThreshold = MAX(C1CompileThreshold, 1);
  C1CompileThreshold = MIN(C1CompileThreshold, 0x000000007fffffffL);

  C2CompileThreshold = MAX(C2CompileThreshold, 1);
  C2CompileThreshold = MIN(C2CompileThreshold, 0x000000007fffffffL);

  return JNI_OK;
}

jint Arguments::finalize_vm_init_args(SysClassPath* scp_p, bool scp_assembly_required) {
  // This must be done after all -D arguments have been processed.
  scp_p->expand_endorsed();

  if (scp_assembly_required || scp_p->get_endorsed() != NULL) {
    // Assemble the bootclasspath elements into the final path.
    Arguments::set_sysclasspath(scp_p->combined_path());
  }

  // This must be done after all arguments have been processed.
  // java_compiler() true means set to "NONE" or empty.
  if (java_compiler() && !xdebug_mode()) {
    // For backwards compatibility, we switch to interpreted mode if
    // -Djava.compiler="NONE" or "" is specified AND "-Xdebug" was
    // not specified.
    set_mode_flags(_int);
  }
  if ((C1CompileThreshold == 0) && (C2CompileThreshold == 0)) {
    set_mode_flags(_int);
  }

  if (!check_vm_args_consistency()) {
    return JNI_ERR;
  }

  return JNI_OK;
}

jint Arguments::scan_for_Xflags_and_XXFlags(const JavaVMInitArgs* args, const char** tail_Flags,
                                            bool& settings_file_specified) {
jint retval=JNI_OK;
  const char* tail;

  // If flag "-Xflags:[ignore|error|warn] is specified it is processed and then
  // if flag "-Xnativevmflags:[ignore|error|warn] is specified it is processed and then
  // "-XX:Flags=<flags-file>"
  for (int index = 0; index < args->nOptions; index++) {
    const JavaVMOption *option = args->options + index;
if(match_option(option,"-Xflags",&tail)){
      if (!strcmp(tail, ":warn")) {  // Print warning on Unrecognized -XX and -X flags
        _ignore_flags = (_ignore_flags & ~UNRECOGNIZED_FLAGS_ERROR & 
                            ~UNRECOGNIZED_FLAGS_IGNORE) | UNRECOGNIZED_FLAGS_WARN;
      } else if (!strcmp(tail, ":error")) {  // Print error and terminate
        _ignore_flags = (_ignore_flags & ~UNRECOGNIZED_FLAGS_WARN & 
                            ~UNRECOGNIZED_FLAGS_IGNORE) | UNRECOGNIZED_FLAGS_ERROR;
      } else if (!strcmp(tail, ":ignore")) {  // ignore quietly
        _ignore_flags = (_ignore_flags & ~UNRECOGNIZED_FLAGS_WARN &
                            ~UNRECOGNIZED_FLAGS_ERROR) | UNRECOGNIZED_FLAGS_IGNORE;
      } else if (is_bad_option(option, _ignore_flags, "check")) {
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      }
}else if(match_option(option,"-Xnativevmflags",&tail)){
      if (!strcmp(tail, ":warn")) {  // Print warning on unsupported -XX and -X flags
                                     // (these are options supported on the native VM)
        _ignore_flags = (_ignore_flags & ~UNSUPPORTED_NATIVE_VM_FLAGS_ERROR &
                         ~UNSUPPORTED_NATIVE_VM_FLAGS_IGNORE) |
                         UNSUPPORTED_NATIVE_VM_FLAGS_WARN;
      } else if (!strcmp(tail, ":error")) {  // Print error and terminate
        _ignore_flags = (_ignore_flags & ~UNSUPPORTED_NATIVE_VM_FLAGS_WARN &
                         ~UNSUPPORTED_NATIVE_VM_FLAGS_IGNORE) |
                         UNSUPPORTED_NATIVE_VM_FLAGS_ERROR;
      } else if (!strcmp(tail, ":ignore")) {  // ignore quietly
        _ignore_flags = (_ignore_flags & ~UNSUPPORTED_NATIVE_VM_FLAGS_WARN &
                         ~UNSUPPORTED_NATIVE_VM_FLAGS_ERROR) |
                         UNSUPPORTED_NATIVE_VM_FLAGS_IGNORE;
      } else if (is_bad_option(option, _ignore_flags, "check")) {
if(retval==JNI_OK){
          retval = JNI_EINVAL;
        }
      }
    } else if (match_option(option, "-XX:Flags=", tail_Flags)) {
      settings_file_specified = true;
    }
  }
  
  return retval;
}

jint Arguments::parse_java_options_environment_variable(JavaVMInitArgs** vm_args,
                           char* buffer, size_t bufsize, const int N_MAX_OPTIONS) {
  return parse_options_environment_variable("_JAVA_OPTIONS", vm_args, buffer, bufsize, N_MAX_OPTIONS);
}

jint Arguments::parse_java_tool_options_environment_variable(JavaVMInitArgs** vm_args,
                           char* buffer, size_t bufsize, const int N_MAX_TOOL_OPTIONS) {
  return parse_options_environment_variable("JAVA_TOOL_OPTIONS", vm_args, buffer, bufsize, N_MAX_TOOL_OPTIONS);
}

jint Arguments::parse_options_environment_variable(const char* name, JavaVMInitArgs** vm_args,
                           char* buffer, size_t bufsize, const int N_MAX_OPTIONS) {
  // The variable will be ignored if it exceeds the length of the buffer.
  // Don't check this variable if user has special privileges
  // (e.g. unix su command).

  if (os::getenv(name, buffer, bufsize) &&
      !os::have_special_privileges()) {
    char* rd = buffer;                        // pointer to the input string (rd)
    int i;
    for (i = 0; i < N_MAX_OPTIONS;) {    // repeat for all options in the input string
      while (isspace(*rd)) rd++;              // skip whitespace
      if (*rd == 0) break;                    // we re done when the input string is read completely

      // The output, option string, overwrites the input string.
      // Because of quoting, the pointer to the option string (wrt) may lag the pointer to 
      // input string (rd).
      char* wrt = rd;

      (*vm_args)->options[i++].optionString = wrt;   // Fill in option
      while (*rd != 0 && !isspace(*rd)) {     // unquoted strings terminate with a space or NULL
        if (*rd == '\'' || *rd == '"') {      // handle a quoted string
          int quote = *rd;                    // matching quote to look for
          rd++;                               // don't copy open quote
          while (*rd != quote) {              // include everything (even spaces) up until quote
            if (*rd == 0) {                   // string termination means unmatched string
              jio_fprintf(defaultStream::error_stream(),
			  "Unmatched quote in %s\n", name);
              return JNI_ERR;
            }
            *wrt++ = *rd++;                   // copy to option string
          }
          rd++;                               // don't copy close quote
        } else {
          *wrt++ = *rd++;                     // copy to option string
        }
      }
      // Need to check if we're done before writing a NULL,
      // because the write could be to the byte that rd is pointing to.
      if (*rd++ == 0) {
        *wrt = 0;
        break;
      }
      *wrt = 0;                               // Zero terminate option
    }
    // Construct JavaVMInitArgs structure and parse as if it was part of the command line
(*vm_args)->version=JNI_VERSION_1_2;
(*vm_args)->nOptions=i;
//Though ignoreUnrecognized is initialized here, it is not actually used
(*vm_args)->ignoreUnrecognized=false;

    if (PrintVMOptions) {
      const char* tail;
for(int i=0;i<(*vm_args)->nOptions;i++){
const JavaVMOption*option=(*vm_args)->options+i;
	if (match_option(option, "-XX:", &tail)) {
	  logOption(tail);
	}
      }
    }

    SysClassPath scp(Arguments::get_sysclasspath());
    bool scp_assembly_required = false;
    return(parse_each_vm_init_arg(*vm_args, &scp, &scp_assembly_required, ENVIRON_VAR));
  }
  return JNI_OK;
}

// Parse entry point called from JNI_CreateJavaVM

jint Arguments::parse(const JavaVMInitArgs* args) {

  // Sharing support
  // Construct the path to the archive
  char jvm_path[JVM_MAXPATHLEN];
  os::jvm_path(jvm_path, sizeof(jvm_path));
#ifdef TIERED
  if (strstr(jvm_path, "client") != NULL) {
    force_client_mode = true;
  }
#endif // TIERED
  char *end = strrchr(jvm_path, *os::file_separator());
  if (end != NULL) *end = '\0';
  char *shared_archive_path = NEW_C_HEAP_ARRAY(char, strlen(jvm_path) +
                                        strlen(os::file_separator()) + 20);
  if (shared_archive_path == NULL) return JNI_ENOMEM;
  strcpy(shared_archive_path, jvm_path);
  strcat(shared_archive_path, os::file_separator());
  strcat(shared_archive_path, "classes");
  DEBUG_ONLY(strcat(shared_archive_path, "_g");)
  strcat(shared_archive_path, ".jsa");
  SharedArchivePath = shared_archive_path;

  const char* tail_Flags; // Remaining part of -XX:Flags=
  bool settings_file_specified = false;
  bool java_options_env_specified = false;
  bool java_tool_options_env_specified = false;
jint retval=JNI_OK;
  
  _ignore_flags = args->ignoreUnrecognized ?
                            UNRECOGNIZED_FLAGS_WARN : UNRECOGNIZED_FLAGS_ERROR;
  _ignore_flags |= UNSUPPORTED_NATIVE_VM_FLAGS_IGNORE;

  // Pre-scan incoming args for -Xflags: -Xnativevmflags: and set _ignore_flags
  // and also scan -XX:Flags
  retval = scan_for_Xflags_and_XXFlags(args, &tail_Flags, settings_file_specified);
  
  // Build java_options_args options passed through _JAVA_OPTIONS environment variable 
  // (if present) (mimics classic VM) but don't parse all the options yet
const int N_MAX_OPTIONS=32;
  char buffer[1024];
JavaVMOption javaOptionsArray[N_MAX_OPTIONS];//Construct JavaVMOption array
JavaVMInitArgs java_options_args;
  java_options_args.options = javaOptionsArray;
  JavaVMInitArgs* java_options_args_ptr = &java_options_args;
  java_options_env_specified = parse_java_options_environment_variable(
                                   &java_options_args_ptr, buffer, sizeof(buffer),
                                   N_MAX_OPTIONS);
  
  // Pre-scan _JAVA_OPTIONS for -Xflags:, -Xnativevmflags: and set _ignore_flags
  // and also scan -XX:Flags
  if (java_options_env_specified) {
    retval = scan_for_Xflags_and_XXFlags((const JavaVMInitArgs*) java_options_args_ptr,
                                         &tail_Flags,
                                         settings_file_specified);
  }

  // Build java_tool_options_args options passed through JAVA_TOOL_OPTIONS
  // environment variable 
  // (if present) (mimics classic VM) but don't parse all the options yet
  const int N_MAX_TOOL_OPTIONS = 64;
  char tool_buffer[1024];
JavaVMOption javaToolOptionsArray[N_MAX_OPTIONS];//Construct JavaVMOption array
JavaVMInitArgs java_tool_options_args;
  java_tool_options_args.options = javaToolOptionsArray;
  JavaVMInitArgs* java_tool_options_args_ptr = &java_tool_options_args;
  java_tool_options_env_specified = parse_java_tool_options_environment_variable(
                                        &java_tool_options_args_ptr,
                                        tool_buffer, sizeof(tool_buffer),
                                        N_MAX_TOOL_OPTIONS);
  // Pre-scan JAVA_TOOL_OPTIONS for -Xflags:, -Xnativevmflags: and set _ignore_flags
  // and also scan -XX:Flags
  if (java_tool_options_env_specified) {
    retval = scan_for_Xflags_and_XXFlags(
                      (const JavaVMInitArgs*) java_tool_options_args_ptr,
                      &tail_Flags,
                      settings_file_specified);
  }

  // Parse settings file if specified or default .hotspotrc settings file
  if (settings_file_specified) {
      if (!process_settings_file(tail_Flags, true, _ignore_flags)) {
        retval = JNI_EINVAL;
      }
  } else {
    if (!process_settings_file(".hotspotrc", false, _ignore_flags)) {
      retval = JNI_EINVAL;
    }
  }
  
  if (PrintVMOptions) {
    int index;
    for (index = 0; index < args->nOptions; index++) {
      const JavaVMOption *option = args->options + index;
if(match_option(option,"-XX:",&tail_Flags)){
logOption(tail_Flags);
      }
    }
  }

  // Parse JavaVMInitArgs structure passed in, as well as JAVA_TOOL_OPTIONS and _JAVA_OPTIONS
jint result=parse_vm_init_args(args,java_options_env_specified,
                                   (const JavaVMInitArgs*) java_options_args_ptr,
                                   java_tool_options_env_specified,
                                   (const JavaVMInitArgs*) java_tool_options_args_ptr);
  if (result != JNI_OK) {
if(retval==JNI_OK){
retval=result;
      }
  }

  // If we had any error processing arguments return error
if(retval!=JNI_OK){
    return retval;
  }

#ifndef PRODUCT
  if (TraceBytecodesAt != 0) {
    TraceBytecodes = true;
  }
#endif // PRODUCT

  if (AbortOnOOM) {
os::set_abort_on_out_of_memory(true);
  }

#ifdef AZ_PROXIED
  // WeblogicNativeIO is relevant only in proxied mode.
  if (WeblogicNativeIO) {
    // Add the Azul implementation of the Weblogic socket muxer to the classpath
    // so that it overrides the default Weblogic implementation used when
    // Weblogic native I/O is enabled.

    // Find the "java.class.path" system property in the list.
const char*cp=NULL;
    SystemProperty *prop = system_properties();
while(prop!=NULL){
      if (strcmp(prop->key(), "java.class.path") == 0) {
        cp = prop->value();
        break;
      }
      prop = prop->next();
    }
guarantee(cp!=NULL,"java.class.path should be set by now");

    // Build up the new classpath and check that wlmuxer.jar exists.
    const char *java_home   = get_java_home(),
               *file_sep    = os::file_separator(),
               *path_sep    = os::path_separator(),
               *wlmuxer_jar = "wlmuxer.jar";

    size_t wlmuxer_path_size = strlen(java_home) + strlen(file_sep) + strlen("lib") + strlen(file_sep) + strlen(wlmuxer_jar);
    char *wlmuxer_path = AllocateHeap(wlmuxer_path_size+1, "WeblogicNativeIO");
    snprintf(wlmuxer_path, wlmuxer_path_size+1, "%s%s%s%s%s", java_home, file_sep, "lib", file_sep, wlmuxer_jar);
    assert(wlmuxer_path_size == strlen(wlmuxer_path), "Just checking");

    struct stat st;
    if (os::stat(wlmuxer_path, &st)) {
warning("WeblogicNativeIO: Weblogic muxer jar doesn't exist: %s",wlmuxer_path);
    }

    size_t new_cp_size = wlmuxer_path_size + strlen(path_sep) + strlen(cp);
    char *new_cp = AllocateHeap(new_cp_size + 1, "WeblogicNativeIO");
    snprintf(new_cp, new_cp_size+1, "%s%s%s", wlmuxer_path, path_sep, cp);
    assert(new_cp_size == strlen(new_cp), "Just checking");

FreeHeap(wlmuxer_path);

    // Replace the old "java.class.path" system property with the new one.
prop->set_value(new_cp);
  }
#endif // AZ_PROXIED

  if (PrintGCDetails) {
    // Turn on -verbose:gc options as well
    PrintGC = true;
  }
#ifdef KERNEL
  // no_shared_spaces();
#endif // KERNEL
  // Set some flags for ParallelGC if needed.
  set_gc_flags();
  // Set flags based on ergonomics.
  set_ergonomics_flags();
  
  // Set bytecode rewriting flags
  set_bytecode_flags();

  // Set flags if Aggressive optimization flags (-XX:+AggressiveOpts) enabled.
  set_aggressive_opts_flags();

  if (Log4J12Optimized) {
    // Add the Azul implementation of log4j 1.2.12

    // Find the "java.class.path" system property in the list.
const char*cp=NULL;
    SystemProperty *prop = system_properties();
while(prop!=NULL){
      if (strcmp(prop->key(), "java.class.path") == 0) {
        cp = prop->value();
        break;
      }
      prop = prop->next();
    }
guarantee(cp!=NULL,"java.class.path should be set by now");

    // Build up the new classpath and check that log4j-1.2.12.jar exists
    const char *java_home    = _java_home->value(),
               *file_sep     = os::file_separator(),
               *path_sep     = os::path_separator(),
               *log4j_jar    = "log4j-1.2.12.jar";

    size_t log4j_path_size = strlen(java_home) + strlen(file_sep) + strlen("lib") + strlen(file_sep) + strlen(log4j_jar);
    char *log4j_path = AllocateHeap(log4j_path_size + 1, "Log4J");
    snprintf(log4j_path, log4j_path_size+1, "%s%s%s%s%s", java_home, file_sep, "lib", file_sep, log4j_jar);
    assert(log4j_path_size == strlen(log4j_path), "Just checking");

    struct stat st;
    if (os::stat(log4j_path, &st)) {
warning("Log4J12Optimized: Optimized log4j jar doesn't exist: %s",log4j_path);
    }

    size_t new_cp_size = log4j_path_size + strlen(path_sep) + strlen(cp);
    char *new_cp = AllocateHeap(new_cp_size + 1);
    snprintf(new_cp, new_cp_size + 1, "%s%s%s", log4j_path, path_sep, cp);
    assert(new_cp_size == strlen(new_cp), "Just checking");

FreeHeap(log4j_path);

    // Replace the old "java.class.path" system property with the new one.
prop->set_value(new_cp);
  }


  if (PrintCommandLineFlags) {
    CommandLineFlags::printSetFlags();
  }

  if (HTTPDaemonPort) {
warning("ignoring \"-XX:HTTPDaemonPort=%d\" since it is no longer "
"supported and \"-PX:ARTAPort=%d\" should be used instead");
  }

  return JNI_OK;
}

#ifndef AZ_PROXIED
static int pxOptionsCount = 0;
static int maxPXOptionsCount = 0;
static const JavaVMOption** pxOptions = NULL;

void Arguments::add_to_valid_px_options(const JavaVMOption* option) {
  // Expand pxOptions array if needed
  if (pxOptionsCount >= maxPXOptionsCount) {
    if (pxOptions == 0) {
      maxPXOptionsCount = 4;
      pxOptions = (const JavaVMOption**) malloc(maxPXOptionsCount * sizeof(JavaVMOption*));
    } else {
      JavaVMOption** tmp;
      maxPXOptionsCount *= 2;
      tmp = (JavaVMOption**) malloc(maxPXOptionsCount * sizeof(JavaVMOption*));
      memcpy(tmp, pxOptions, pxOptionsCount * sizeof(JavaVMOption*));
free(pxOptions);
      pxOptions = (const JavaVMOption**) tmp;
    }
  }

  pxOptions[pxOptionsCount++] = option;
}

bool Arguments::is_valid_px_option(const JavaVMOption* option) {
  int count = 0;

while(count<pxOptionsCount){
    if (strcmp(pxOptions[count]->optionString, option->optionString) == 0) {
      return true;
    }
    count++;
  }

  return false;
}

// Function to parse PX options (these are PX options to keep them consistent with the
// proxied environment.
// Caution: VM is not initialized yet at this point. Be careful about what you call here.
void Arguments::parse_px_options(const JavaVMInitArgs* args) {
  int  ret = 0;
  int64_t xmx = 0;
  int64_t maxpermsize = 0;
  int64_t memcommit = 0;
  int64_t memmax = 0;
  const char* tail;
  char failure_msg[O_BUFLEN];

  for (int index = 0; index < args->nOptions; index++) {
    const JavaVMOption *option = args->options + index;

if(match_option(option,"-PX:-UseAzMem",&tail)){
      add_to_valid_px_options(option);
      os_disable_azmem();
}else if(match_option(option,"-PX:-UseAzSched",&tail)){
      add_to_valid_px_options(option);
      os_disable_azsched();
}else if(match_option(option,"-PX:MemCommit=",&tail)){
      add_to_valid_px_options(option);
      ret = parse_memory_size(tail, (jlong*) &memcommit, 1);
      if (ret < 0) {
        snprintf(failure_msg, sizeof(failure_msg), "Invalid MemCommit specified: %s\n", option->optionString);
vm_exit_during_initialization(failure_msg);
      }
}else if(match_option(option,"-PX:MemMax=",&tail)){
      add_to_valid_px_options(option);
      ret = parse_memory_size(tail, (jlong*) &memmax, 1);
      if (ret < 0) {
        snprintf(failure_msg, sizeof(failure_msg), "Invalid MemMax specified: %s\n", option->optionString);
vm_exit_during_initialization(failure_msg);
      }
}else if(match_option(option,"-PX:+StartSuspended",&tail)){
      add_to_valid_px_options(option);
      os::set_start_suspended(1);
    } else if (match_option(option, "-Xmx", &tail)) {
      ret = parse_memory_size(tail, (jlong*) &xmx, 1);
      if (ret < 0) {
        snprintf(failure_msg, sizeof(failure_msg), "Invalid Xmx specified: %s\n", option->optionString);
vm_exit_during_initialization(failure_msg);
      }
}else if(match_option(option,"-XX:MaxPermSize=",&tail)){
      ret = parse_memory_size(tail, (jlong*) &maxpermsize, 1);
      if (ret < 0) {
        snprintf(failure_msg, sizeof(failure_msg), "Invalid MaxPermSize specified: %s\n", option->optionString);
vm_exit_during_initialization(failure_msg);
      }
    }
  }

  // Check the memory values for consistency
  if (maxpermsize == 0) {
maxpermsize=128*M;
  }

  if (xmx == 0) {
xmx=600*M;
  }

  if (memcommit == 0) {
    // Align-up to GigaB. This will enable allocating 1-Gig aligned aliasable
    // address-ranges for Java Heap as part of MultiMapMetaData support.
    memcommit = (long)((192 * M) + align_size_up(((long)(1.25 * (xmx + maxpermsize))), G));
  }

  if (memmax == 0) {
    // TODO - calculate grant memory configured and assign
    // memmax = memcommit + grant;
    memmax = memcommit;
  }

  os::set_memcommit(memcommit);
  os::set_memmax(memmax);
}
#endif // !AZ_PROXIED

bool Arguments::TraceThread(char* name) {
  for (short i=0; i<ITRTraceOnlyThreadsNum; i++) {
    if (strcmp(ITRTraceOnlyThreads[i], name) == 0) {
      return 1;
    }
  }
  return false;
}

int Arguments::PropertyList_count(SystemProperty* pl) {
  int count = 0;
  while(pl != NULL) {
    count++;
    pl = pl->next();
  }
  return count;
}

const char* Arguments::PropertyList_get_value(SystemProperty *pl, const char* key) {
  assert(key != NULL, "just checking");
  SystemProperty* prop;
  for (prop = pl; prop != NULL; prop = prop->next()) {
    if (strcmp(key, prop->key()) == 0) return prop->value();
  }
  return NULL;
}

const char* Arguments::PropertyList_get_key_at(SystemProperty *pl, int index) {
  int count = 0;
  const char* ret_val = NULL;

  while(pl != NULL) {
    if(count >= index) {
      ret_val = pl->key();
      break;
    }
    count++;
    pl = pl->next();
  }

  return ret_val;
}

char* Arguments::PropertyList_get_value_at(SystemProperty* pl, int index) {
  int count = 0;
  char* ret_val = NULL;

  while(pl != NULL) {
    if(count >= index) {
      ret_val = pl->value();
      break;
    }
    count++;
    pl = pl->next();
  }

  return ret_val;
}

void Arguments::PropertyList_add(SystemProperty** plist, SystemProperty *new_p) {
  SystemProperty* p = *plist;
  if (p == NULL) {
    *plist = new_p;
  } else {
    while (p->next() != NULL) {
      p = p->next();
    }
    p->set_next(new_p);
  }
}

void Arguments::PropertyList_add(SystemProperty** plist, const char* k, char* v) {
  if (plist == NULL)
    return;

  SystemProperty* new_p = new SystemProperty(k, v, true);
  PropertyList_add(plist, new_p);
}

// This add maintains unique property key in the list.
void Arguments::PropertyList_unique_add(SystemProperty** plist, const char* k, char* v) {
  if (plist == NULL)
    return;

  // If property key exist then update with new value.
  SystemProperty* prop;
  for (prop = *plist; prop != NULL; prop = prop->next()) {
    if (strcmp(k, prop->key()) == 0) {
      prop->set_value(v);
      return;
    }
  }
      
  PropertyList_add(plist, k, v);
}

#ifdef KERNEL
char *Arguments::get_kernel_properties() {
  // Find properties starting with kernel and append them to string
  // We need to find out how long they are first because the URL's that they
  // might point to could get long.
  int length = 0;
  SystemProperty* prop;
  for (prop = _system_properties; prop != NULL; prop = prop->next()) {
    if (strncmp(prop->key(), "kernel.", 7 ) == 0) {
      length += (strlen(prop->key()) + strlen(prop->value()) + 5);  // "-D ="
    }
  }
  // Add one for null terminator.
  char *props = AllocateHeap(length + 1, "get_kernel_properties");
  if (length != 0) {
    int pos = 0;
    for (prop = _system_properties; prop != NULL; prop = prop->next()) {
      if (strncmp(prop->key(), "kernel.", 7 ) == 0) {
        jio_snprintf(&props[pos], length-pos,
                     "-D%s=%s ", prop->key(), prop->value());
        pos = strlen(props);
      }
    }
  }
  // null terminate props in case of null
  props[length] = '\0';
  return props;
}
#endif // KERNEL

// Copies src into buf, replacing "%%" with "%" and "%p" with pid
// Returns true if all of the source pointed by src has been copied over to
// the destination buffer pointed by buf. Otherwise, returns false.
// Notes: 
// 1. If the length (buflen) of the destination buffer excluding the 
// NULL terminator character is not long enough for holding the expanded
// pid characters, it also returns false instead of returning the partially
// expanded one.
// 2. The passed in "buflen" should be large enough to hold the null terminator.
bool Arguments::copy_expand_pid(const char* src, size_t srclen, 
                                char* buf, size_t buflen) {
  const char* p = src;
  char* b = buf;
  const char* src_end = &src[srclen];
  char* buf_end = &buf[buflen - 1];
 
  while (p < src_end && b < buf_end) {
    if (*p == '%') {
      switch (*(++p)) {
      case '%':         // "%%" ==> "%"
        *b++ = *p++;
        break;
      case 'p':  {       //  "%p" ==> current process id
        // buf_end points to the character before the last character so
        // that we could write '\0' to the end of the buffer.
        size_t buf_sz = buf_end - b + 1;
        int ret = jio_snprintf(b, buf_sz, "%d", os::current_process_id());
        
        // if jio_snprintf fails or the buffer is not long enough to hold
        // the expanded pid, returns false.
        if (ret < 0 || ret >= (int)buf_sz) {
          return false;
        } else {
          b += ret;
          assert(*b == '\0', "fail in copy_expand_pid");
          if (p == src_end && b == buf_end + 1) {
            // reach the end of the buffer.
            return true;
          }
        }
        p++;
        break;
      }
      default : 
        *b++ = '%';
      }
    } else {
      *b++ = *p++;
    }
  }
  *b = '\0';
  return (p == src_end); // return false if not all of the source was copied
}
