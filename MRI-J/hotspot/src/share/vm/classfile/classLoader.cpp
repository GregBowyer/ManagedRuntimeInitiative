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


#include "allocation.hpp"
#include "arguments.hpp"
#include "classFileParser.hpp"
#include "classFileStream.hpp"
#include "classLoader.hpp"
#include "collectedHeap.hpp"
#include "compilationPolicy.hpp"
#include "compileBroker.hpp"
#include "constantPoolKlass.hpp"
#include "hashtable.hpp"
#include "hpi.hpp"
#include "hpi_os.hpp"
#include "interfaceSupport.hpp"
#include "javaClasses.hpp"
#include "jvm_os.h"
#include "log.hpp"
#include "management.hpp"
#include "mutexLocker.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "vmSymbols.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "hashtable.inline.hpp"
#include "mutex.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

// Entry points in zip.dll for loading zip/jar file entries
#if defined(AZ_PROXIED)
extern "C" {
  jzfile * JNICALL ZIP_Open(const char *name, char **pmsg);
  jzentry * JNICALL ZIP_FindEntry(jzfile *zip, const char *name, jint *sizeP, jint *nameLen);
  jboolean JNICALL ZIP_ReadEntry(jzfile *zip, jzentry *entry, unsigned char *buf, char *namebuf);
NEEDS_CLEANUP;//Turn on once we use mmap.
  //jboolean JNICALL ZIP_ReadMappedEntry(jzfile *zip, jzentry *entry, unsigned char **buf, char *namebuf);
  jzentry * JNICALL ZIP_GetNextEntry(jzfile *zip, jint n);
  int JNICALL Canonicalize(JNIEnv *env, char *orig, char *out, int len);
};

#else // !AZ_PROXIED
typedef void * * (JNICALL *ZipOpen_t)(const char *name, char **pmsg);
typedef void (JNICALL *ZipClose_t)(jzfile *zip);
typedef jzentry* (JNICALL *FindEntry_t)(jzfile *zip, const char *name, jint *sizeP, jint *nameLen);
typedef jboolean (JNICALL *ReadEntry_t)(jzfile *zip, jzentry *entry, unsigned char *buf, char *namebuf);
typedef jboolean (JNICALL *ReadMappedEntry_t)(jzfile *zip, jzentry *entry, unsigned char **buf, char *namebuf);
typedef jzentry* (JNICALL *GetNextEntry_t)(jzfile *zip, jint n);

static ZipOpen_t         ZipOpen            = NULL;
static ZipClose_t        ZipClose           = NULL;
static FindEntry_t       FindEntry          = NULL;
static ReadEntry_t       ReadEntry          = NULL;
static ReadMappedEntry_t ReadMappedEntry    = NULL;
static GetNextEntry_t    GetNextEntry       = NULL;
static canonicalize_fn_t CanonicalizeEntry  = NULL;
#endif // !AZ_PROXIED

// Globals
PerfCounter*    ClassLoader::_perf_accumulated_time = NULL;
PerfCounter*    ClassLoader::_perf_classes_inited = NULL;
PerfCounter*    ClassLoader::_perf_class_init_time = NULL;
PerfCounter*    ClassLoader::_perf_class_verify_time = NULL;
PerfCounter*    ClassLoader::_perf_classes_linked = NULL;
PerfCounter*    ClassLoader::_perf_class_link_time = NULL;
PerfCounter*    ClassLoader::_sync_systemLoaderLockContentionRate = NULL;
PerfCounter*    ClassLoader::_sync_nonSystemLoaderLockContentionRate = NULL;
PerfCounter*    ClassLoader::_sync_JVMFindLoadedClassLockFreeCounter = NULL;
PerfCounter*    ClassLoader::_sync_JVMDefineClassLockFreeCounter = NULL;
PerfCounter*    ClassLoader::_sync_JNIDefineClassLockFreeCounter = NULL;
PerfCounter*    ClassLoader::_unsafe_defineClassCallCounter = NULL;
PerfCounter*    ClassLoader::_isUnsyncloadClass = NULL;
PerfCounter*    ClassLoader::_load_instance_class_failCounter = NULL;

ClassPathEntry* ClassLoader::_first_entry         = NULL;
ClassPathEntry* ClassLoader::_last_entry          = NULL;
PackageHashtable* ClassLoader::_package_hash_table = NULL;

// helper routines
bool string_starts_with(const char* str, const char* str_to_find) {
  size_t str_len = strlen(str);
  size_t str_to_find_len = strlen(str_to_find);
  if (str_to_find_len > str_len) {
    return false;
  }
  return (strncmp(str, str_to_find, str_to_find_len) == 0);
}

bool string_ends_with(const char* str, const char* str_to_find) {
  size_t str_len = strlen(str);
  size_t str_to_find_len = strlen(str_to_find);
  if (str_to_find_len > str_len) {
    return false;
  }
  return (strncmp(str + (str_len - str_to_find_len), str_to_find, str_to_find_len) == 0);
}


MetaIndex::MetaIndex(char** meta_package_names, int num_meta_package_names) {
  if (num_meta_package_names == 0) {
    _meta_package_names = NULL;
    _num_meta_package_names = 0;
  } else {
    _meta_package_names = NEW_C_HEAP_ARRAY(char*, num_meta_package_names);
    _num_meta_package_names = num_meta_package_names;
    memcpy(_meta_package_names, meta_package_names, num_meta_package_names * sizeof(char*));
  }
}


MetaIndex::~MetaIndex() {
  FREE_C_HEAP_ARRAY(char*, _meta_package_names);
}


bool MetaIndex::may_contain(const char* class_name) {
  if ( _num_meta_package_names == 0) {
    return false;
  }
  size_t class_name_len = strlen(class_name);
  for (int i = 0; i < _num_meta_package_names; i++) {
    char* pkg = _meta_package_names[i];
    size_t pkg_len = strlen(pkg);
    size_t min_len = MIN2(class_name_len, pkg_len);
    if (!strncmp(class_name, pkg, min_len)) {
      return true;
    }
  }
  return false;
}


ClassPathEntry::ClassPathEntry() {
  set_next(NULL);
}


bool ClassPathEntry::is_lazy() {
  return false;
}

ClassPathDirEntry::ClassPathDirEntry(char* dir) : ClassPathEntry() {
  _dir = NEW_C_HEAP_ARRAY(char, strlen(dir)+1);
  strcpy(_dir, dir);
}


ClassFileStream* ClassPathDirEntry::open_stream(const char* name) {
  // note: This class loading should be handled entirely by the proxy
  // since we need the whole .class file it can be read in one go.
  // construct full path name
  char path[JVM_MAXPATHLEN];
  if (jio_snprintf(path, sizeof(path), "%s%s%s", _dir, os::file_separator(), name) == -1) {
    return NULL;
  }
  // check if file exists
  struct stat st;
  if (os::stat(path, &st) == 0) {
    // found file, open it
    int file_handle = hpi::open(path, 0, 0);
if(file_handle>=0){
      // read contents into resource array
      u1* buffer = NEW_RESOURCE_ARRAY(u1, st.st_size);
JavaThread*jt=JavaThread::current();
      jt->jvm_unlock_self();    // Unlock around blocking call
      size_t num_read = os::read(file_handle, (char*) buffer, st.st_size);
jt->jvm_lock_self();
      // close file
      hpi::close(file_handle);
      // construct ClassFileStream
      if (num_read == (size_t)st.st_size) {
        return new ClassFileStream(buffer, st.st_size, _dir);    // Resource allocated
      }
    }
  }
  return NULL;
}


ClassPathZipEntry::ClassPathZipEntry(jzfile* zip, const char* zip_name) : ClassPathEntry() {
  _zip = zip;
  _zip_name = NEW_C_HEAP_ARRAY(char, strlen(zip_name)+1);
  strcpy(_zip_name, zip_name);
  _negative_cache = (char **)NEW_C_HEAP_ARRAY(char *, (NegativeJARCacheSize+1));
for(uint i=0;i<=NegativeJARCacheSize;i++){
    _negative_cache[i] = (char *)NEW_C_HEAP_ARRAY(char *, _cache_entry_length);
_negative_cache[i][0]='\0';
  }
  _last_cache_entry_written = NegativeJARCacheSize - 1;
  _negative_cache_lock = new AzLock(BeforeExit_lock._rank, "_negative_cache_mutex", false);
}

ClassPathZipEntry::~ClassPathZipEntry() {
#ifndef AZ_PROXIED
  if (ZipClose != NULL) {
    (*ZipClose)(_zip);
  }
#endif // !AZ_PROXIED
  FREE_C_HEAP_ARRAY(char, _zip_name);
}

ClassFileStream* ClassPathZipEntry::open_stream(const char* name) {
  char name0 = name[0]; // cache name[0]
  // the _negative_cache is a little trick to keep from going into a 
  // jar file when we already know that we won't find this class in
  // the .jar . It keeps a list of the last <NegativeJARCacheSize>
  // unique class names that failed to match in this jar.
  // Since jar's are not modified once they're loaded in, this trick
  // will work nicely.
  // 
  // We lock the update of this table but not the lookup because we
  // update the table in one instruction.
  for (int i = (NegativeJARCacheSize - 1); i >= 0; i--) {
    if (_negative_cache[i][0] == name0) {
      if (strcmp(_negative_cache[i], name) == 0) {
return NULL;//hit in negative cache
      }
    }
  }
  // note: This class loading should be handled entirely by the proxy
  // since we need the whole .class file it can be read in one go.
  // enable call to C land
  JavaThread* thread = JavaThread::current();
  ThreadToNativeFromVM ttn(thread);
  // check whether zip archive contains name
  jint filesize, name_len;
#if defined(AZ_PROXIED)
  jzentry* entry = ZIP_FindEntry(_zip, name, &filesize, &name_len);
#else // !AZ_PROXIED
  jzentry* entry = (*FindEntry)(_zip, name, &filesize, &name_len);
#endif //!AZ_PROXIED
if(entry==NULL){
    if (strlen(name) < (size_t)_cache_entry_length) {
      MutexLocker mn(*_negative_cache_lock);
      char *tmp;

      // we keep an extra entry in _negative_cache that we use to allow us perform the
      // final update in one instruction. This allows the lookups to be done without 
      // acquiring the _negative_cache_lock.
      _last_cache_entry_written = (_last_cache_entry_written + 1) % NegativeJARCacheSize;
      strcpy(_negative_cache[NegativeJARCacheSize], name);
      tmp = _negative_cache[_last_cache_entry_written];
      _negative_cache[_last_cache_entry_written] = _negative_cache[NegativeJARCacheSize];
      _negative_cache[NegativeJARCacheSize] = tmp;
    }
    return NULL;
  }
  u1* buffer;
  char name_buf[128];
  char* filename;
  if (name_len < 128) {
    filename = name_buf;
  } else {
    filename = NEW_RESOURCE_ARRAY(char, name_len + 1);
  }

#if defined(AZ_PROXIED)
NEEDS_CLEANUP;//Support mmapp'ed zip files.
  // mmaped access not available, perhaps due to compression,
  // read contents into resource array
  buffer     = NEW_RESOURCE_ARRAY(u1, filesize);
  if (!ZIP_ReadEntry(_zip, entry, buffer, filename)) return NULL;
#else // !AZ_PROXIED
  // file found, get pointer to class in mmaped jar file.
  if (ReadMappedEntry == NULL ||
      !(*ReadMappedEntry)(_zip, entry, &buffer, filename)) {
    // mmaped access not available, perhaps due to compression,
    // read contents into resource array
    buffer     = NEW_RESOURCE_ARRAY(u1, filesize);
    if (!(*ReadEntry)(_zip, entry, buffer, filename)) return NULL;
  }
#endif // !AZ_PROXIED
  // return result
  return new ClassFileStream(buffer, filesize, _zip_name);    // Resource allocated
}

// invoke function for each entry in the zip file
void ClassPathZipEntry::contents_do(void f(const char* name, void* context), void* context) {
  JavaThread* thread = JavaThread::current();
  HandleMark  handle_mark(thread);
  ThreadToNativeFromVM ttn(thread);  
  for (int n = 0; ; n++) {
#if defined(AZ_PROXIED)
    jzentry * ze = (ZIP_GetNextEntry(_zip, n));
#else // !AZ_PROXIED
    jzentry * ze = ((*GetNextEntry)(_zip, n));
#endif // !AZ_PROXIED
    if (ze == NULL) break;
    (*f)(ze->name, context);
  }
}

LazyClassPathEntry::LazyClassPathEntry(char* path, struct stat st) : ClassPathEntry() {
_path=strdup(path);//LEAK!  never free'd
  _st = st;
  _meta_index = NULL;
  _resolved_entry = NULL;
}

bool LazyClassPathEntry::is_jar_file() {
  return ((_st.st_mode & S_IFREG) == S_IFREG);
}

ClassPathEntry* LazyClassPathEntry::resolve_entry() {
  if (_resolved_entry != NULL) {
    return (ClassPathEntry*) _resolved_entry;
  }
  ClassPathEntry* new_entry = NULL;
  ClassLoader::create_class_path_entry(_path, _st, &new_entry, false);
  assert(new_entry != NULL, "earlier code should have caught this");
  {
MutexLocker ml(ThreadCritical_lock);
    if (_resolved_entry == NULL) {
      _resolved_entry = new_entry;
      return new_entry;
    }
  }
  assert(_resolved_entry != NULL, "bug in MT-safe resolution logic");
  delete new_entry;
  return (ClassPathEntry*) _resolved_entry;
}

ClassFileStream* LazyClassPathEntry::open_stream(const char* name) {
  if (_meta_index != NULL &&
      !_meta_index->may_contain(name)) {
    return NULL;
  }
  return resolve_entry()->open_stream(name);
}

bool LazyClassPathEntry::is_lazy() {
  return true;
}

static void print_meta_index(LazyClassPathEntry* entry, 
                             GrowableArray<char*>& meta_packages) {
  tty->print("[Meta index for %s=", entry->name());
  for (int i = 0; i < meta_packages.length(); i++) {
    if (i > 0) tty->print(" ");
    tty->print(meta_packages.at(i));
  }
  tty->print_cr("]");
}


void ClassLoader::setup_meta_index() {
  // Set up meta index which allows us to open boot jars lazily if
  // class data sharing is enabled
  const char* known_version = "% VERSION 2";
  char* meta_index_path = Arguments::get_meta_index_path();
  char* meta_index_dir  = Arguments::get_meta_index_dir();
  FILE* file = fopen(meta_index_path, "r");
  int line_no = 0;
  if (file != NULL) {
    ResourceMark rm;
    LazyClassPathEntry* cur_entry = NULL;
    GrowableArray<char*> boot_class_path_packages(10);
    char package_name[256];
    bool skipCurrentJar = false;
    while (fgets(package_name, sizeof(package_name), file) != NULL) {
      ++line_no;
      // Remove trailing newline
      package_name[strlen(package_name) - 1] = '\0';
      switch(package_name[0]) {
        case '%':
        {
          if ((line_no == 1) && (strcmp(package_name, known_version) != 0)) {
            if (TraceClassLoading && Verbose) {  
              tty->print("[Unsupported meta index version]");
            }
            fclose(file);
            return;
          }
        }

        // These directives indicate jar files which contain only
        // classes, only non-classfile resources, or a combination of
        // the two. See src/share/classes/sun/misc/MetaIndex.java and
        // make/tools/MetaIndex/BuildMetaIndex.java in the J2SE
        // workspace.
        case '#':
        case '!':
        case '@':
        {
          // Hand off current packages to current lazy entry (if any)
          if ((cur_entry != NULL) &&
              (boot_class_path_packages.length() > 0)) {
            if (TraceClassLoading && Verbose) {  
              print_meta_index(cur_entry, boot_class_path_packages);
            }
            MetaIndex* index = new MetaIndex(boot_class_path_packages.adr_at(0),
                                             boot_class_path_packages.length());
            cur_entry->set_meta_index(index);
          }         
          cur_entry = NULL;
          boot_class_path_packages.clear();

          // Find lazy entry corresponding to this jar file
          for (ClassPathEntry* entry = _first_entry; entry != NULL; entry = entry->next()) {
            if (entry->is_lazy() &&
                string_starts_with(entry->name(), meta_index_dir) &&
                string_ends_with(entry->name(), &package_name[2])) {
              cur_entry = (LazyClassPathEntry*) entry;
              break;
            }
          }
   
          // If the first character is '@', it indicates the following jar
          // file is a resource only jar file in which case, we should skip
          // reading the subsequent entries since the resource loading is
          // totally handled by J2SE side.
          if (package_name[0] == '@') {
            if (cur_entry != NULL) {
              cur_entry->set_meta_index(new MetaIndex(NULL, 0));
            }
            cur_entry = NULL;
            skipCurrentJar = true;
          } else {
            skipCurrentJar = false;
          }
  
          break;
        }

        default:
        {
          if (!skipCurrentJar && cur_entry != NULL) {
            char* new_name = strdup(package_name);
            boot_class_path_packages.append(new_name);
          }
        }
      }
    }
    // Hand off current packages to current lazy entry (if any)
    if ((cur_entry != NULL) &&
        (boot_class_path_packages.length() > 0)) {
      if (TraceClassLoading && Verbose) {  
        print_meta_index(cur_entry, boot_class_path_packages);
      }
      MetaIndex* index = new MetaIndex(boot_class_path_packages.adr_at(0),
                                       boot_class_path_packages.length());
      cur_entry->set_meta_index(index);
    }          
    fclose(file);
  }
}

void ClassLoader::setup_bootstrap_search_path() {
  // note: This bootstrap search path setup should be handled entirely by the proxy.
  assert(_first_entry == NULL, "should not setup bootstrap class search path twice");
  char* sys_class_path = os::strdup(Arguments::get_sysclasspath());
  if (TraceClassLoading && Verbose) {  
    tty->print_cr("[Bootstrap loader class path=%s]", sys_class_path);
  }

  int len = (int)strlen(sys_class_path);
  int end = 0;

  // Iterate over class path entries
  for (int start = 0; start < len; start = end) {
    while (sys_class_path[end] && sys_class_path[end] != os::path_separator()[0]) {
      end++;
    }
    char* path = NEW_C_HEAP_ARRAY(char, end-start+1);
    strncpy(path, &sys_class_path[start], end-start);
    path[end-start] = '\0';
    update_class_path_entry_list(path, false);
    FREE_C_HEAP_ARRAY(char, path);
    while (sys_class_path[end] == os::path_separator()[0]) {
      end++;
    }
  }
}

void ClassLoader::create_class_path_entry(char *path, struct stat st, ClassPathEntry **new_entry, bool lazy) {
  JavaThread* thread = JavaThread::current();
  if (lazy) {
    *new_entry = new LazyClassPathEntry(path, st);
    return;
  }
if(S_ISREG(st.st_mode)){

    // Regular file, should be a zip file
    // Canonicalized filename
    char canonical_path[JVM_MAXPATHLEN];
    if (!get_canonical_path(path, canonical_path, JVM_MAXPATHLEN)) {
      // This matches the classic VM
      EXCEPTION_MARK;
      THROW_MSG(vmSymbols::java_io_IOException(), "Bad pathname");          
    }
    char* error_msg = NULL;
    jzfile* zip;
    {
      // enable call to C land
      ThreadToNativeFromVM ttn(thread);
      HandleMark hm(thread);
#if defined(AZ_PROXIED)
      zip = ZIP_Open(canonical_path, &error_msg);
#else // !AZ_PROXIED
      zip = (*ZipOpen)(canonical_path, &error_msg);
#endif // !AZ_PROXIED
    }
    if (zip != NULL && error_msg == NULL) {
      *new_entry = new ClassPathZipEntry(zip, path);
      if (TraceClassLoading) {
        tty->print_cr("[Opened %s]", path);
      }
    } else { 
      ResourceMark rm(thread);
      char *msg;
      if (error_msg == NULL) {
        msg = NEW_RESOURCE_ARRAY(char, strlen(path) + 128); ;
        jio_snprintf(msg, strlen(path) + 127, "error in opening JAR file %s", path);
      } else {
        int len = (int)(strlen(path) + strlen(error_msg) + 128);
        msg = NEW_RESOURCE_ARRAY(char, len); ;
        jio_snprintf(msg, len - 1, "error in opening JAR file <%s> %s", error_msg, path);
      }
      EXCEPTION_MARK;
      THROW_MSG(vmSymbols::java_lang_ClassNotFoundException(), msg);          
    } 
  } else {
    // Directory
    *new_entry = new ClassPathDirEntry(path);
    if (TraceClassLoading) {
      tty->print_cr("[Path %s]", path);
    }
  }      
}


// Create a class path zip entry for a given path (return NULL if not found
// or zip/JAR file cannot be opened)
ClassPathZipEntry* ClassLoader::create_class_path_zip_entry(const char *path) {
  // check for a regular file
  struct stat st;
  if (os::stat(path, &st) == 0) {
    if ((st.st_mode & S_IFREG) == S_IFREG) {	        
      char orig_path[JVM_MAXPATHLEN];
      char canonical_path[JVM_MAXPATHLEN];
      
      strcpy(orig_path, path);
      if (get_canonical_path(orig_path, canonical_path, JVM_MAXPATHLEN)) {
        char* error_msg = NULL;
	jzfile* zip;
	{
	  // enable call to C land
	  JavaThread* thread = JavaThread::current();
	  ThreadToNativeFromVM ttn(thread);
	  HandleMark hm(thread);
#if defined(AZ_PROXIED)
          zip = ZIP_Open(canonical_path, &error_msg);
#else // !AZ_PROXIED
          zip = (*ZipOpen)(canonical_path, &error_msg);
#endif // !AZ_PROXIED
	}
	if (zip != NULL && error_msg == NULL) {
	  // create using canonical path
          return new ClassPathZipEntry(zip, canonical_path);
	}
      }
    }
  }
  return NULL;
}

// returns true if entry already on class path
bool ClassLoader::contains_entry(ClassPathEntry *entry) {
  ClassPathEntry* e = _first_entry;
  while (e != NULL) {
    // assume zip entries have been canonicalized
    if (strcmp(entry->name(), e->name()) == 0) {   
      return true;
    }
    e = e->next();
  }
  return false;
}

void ClassLoader::add_to_list(ClassPathEntry *new_entry) {
  if (new_entry != NULL) {
    if (_last_entry == NULL) {
      _first_entry = _last_entry = new_entry;
    } else {
      _last_entry->set_next(new_entry);
      _last_entry = new_entry;
    }
  }
}

void ClassLoader::update_class_path_entry_list(const char *path, 
                                               bool check_for_duplicates) {
  struct stat st;
  if (os::stat((char *)path, &st) == 0) {
    // File or directory found
    ClassPathEntry* new_entry = NULL;
    create_class_path_entry((char *)path, st, &new_entry, LazyBootClassLoader);
    // The kernel VM adds dynamically to the end of the classloader path and
    // doesn't reorder the bootclasspath which would break java.lang.Package
    // (see PackageInfo).
    // Add new entry to linked list 
    if (!check_for_duplicates || !contains_entry(new_entry)) {
      add_to_list(new_entry);
    }
  }
}

void ClassLoader::load_zip_library() {
  // First make sure native library is loaded
  os::native_java_library();
#if defined(AZ_PROXIED)
  // Azul note: Should do something more specific here: load local library and trigger proxy load thereof,
  // though the proxy might already have done that itself.
  // Assign zip entry points
  // Otherwise, nothing to do ...
#else // !AZ_PROXIED
  assert(ZipOpen == NULL, "should not load zip library twice");
  // Load zip library
  char path[JVM_MAXPATHLEN];
  char ebuf[1024];
hpi::dll_build_name(path,sizeof(path),Arguments::get_dll_dir(),"zip",0);
  void* handle = hpi::dll_load(path, ebuf, sizeof ebuf);
  if (handle == NULL) {
    vm_exit_during_initialization("Unable to load ZIP library", path);
  }
  // Lookup zip entry points
  ZipOpen      = CAST_TO_FN_PTR(ZipOpen_t, hpi::dll_lookup(handle, "ZIP_Open"));
  ZipClose     = CAST_TO_FN_PTR(ZipClose_t, hpi::dll_lookup(handle, "ZIP_Close"));
  FindEntry    = CAST_TO_FN_PTR(FindEntry_t, hpi::dll_lookup(handle, "ZIP_FindEntry"));
  ReadEntry    = CAST_TO_FN_PTR(ReadEntry_t, hpi::dll_lookup(handle, "ZIP_ReadEntry"));
  ReadMappedEntry = CAST_TO_FN_PTR(ReadMappedEntry_t, hpi::dll_lookup(handle, "ZIP_ReadMappedEntry"));
  GetNextEntry = CAST_TO_FN_PTR(GetNextEntry_t, hpi::dll_lookup(handle, "ZIP_GetNextEntry"));

  // ZIP_Close is not exported on Windows in JDK5.0 so don't abort if ZIP_Close is NULL
  if (ZipOpen == NULL || FindEntry == NULL || ReadEntry == NULL || GetNextEntry == NULL) {
    vm_exit_during_initialization("Corrupted ZIP library", path);
  }

  // Lookup canonicalize entry in libjava.dll  
  void *javalib_handle = os::native_java_library();
  CanonicalizeEntry = CAST_TO_FN_PTR(canonicalize_fn_t, hpi::dll_lookup(javalib_handle, "Canonicalize"));
  // This lookup only works on 1.3. Do not check for non-null here
#endif // !AZ_PROXIED
}

// PackageInfo data exists in order to support the java.lang.Package
// class.  A Package object provides information about a java package
// (version, vendor, etc.) which originates in the manifest of the jar
// file supplying the package.  For application classes, the ClassLoader
// object takes care of this.

// For system (boot) classes, the Java code in the Package class needs
// to be able to identify which source jar file contained the boot
// class, so that it can extract the manifest from it.  This table
// identifies java packages with jar files in the boot classpath.

// Because the boot classpath cannot change, the classpath index is
// sufficient to identify the source jar file or directory.  (Since
// directories have no manifests, the directory name is not required,
// but is available.)

// When using sharing -- the pathnames of entries in the boot classpath
// may not be the same at runtime as they were when the archive was
// created (NFS, Samba, etc.).  The actual files and directories named
// in the classpath must be the same files, in the same order, even
// though the exact name is not the same.

class PackageInfo: public BasicHashtableEntry {
public:
  const char* _pkgname;       // Package name
  int _classpath_index;	      // Index of directory or JAR file loaded from

  PackageInfo* next() {
    return (PackageInfo*)BasicHashtableEntry::next();
  }

  const char* pkgname()           { return _pkgname; }
  void set_pkgname(char* pkgname) { _pkgname = pkgname; }

  const char* filename() {
    return ClassLoader::classpath_entry(_classpath_index)->name();
  }

  void set_index(int index) {
    _classpath_index = index;
  }
};


class PackageHashtable : public BasicHashtable {
private:
  inline unsigned int compute_hash(const char *s, int n) {
    unsigned int val = 0;
    while (--n >= 0) {
      val = *s++ + 31 * val;
    }
    return val;
  }

  PackageInfo* bucket(int index) {
    return (PackageInfo*)BasicHashtable::bucket(index);
  }

  PackageInfo* get_entry(int index, unsigned int hash,
                         const char* pkgname, size_t n) {
    for (PackageInfo* pp = bucket(index); pp != NULL; pp = pp->next()) {
      if (pp->hash() == hash &&
          strncmp(pkgname, pp->pkgname(), n) == 0 &&
          pp->pkgname()[n] == '\0') {
        return pp;
      }
    }
    return NULL;
  }

public:
  PackageHashtable(int table_size)
    : BasicHashtable(table_size, sizeof(PackageInfo)) {}

  PackageHashtable(int table_size, HashtableBucket* t, int number_of_entries)
    : BasicHashtable(table_size, sizeof(PackageInfo), t, number_of_entries) {}

  PackageInfo* get_entry(const char* pkgname, int n) {
    unsigned int hash = compute_hash(pkgname, n);
    return get_entry(hash_to_index(hash), hash, pkgname, n);
  }

  PackageInfo* new_entry(char* pkgname, int n) {
    unsigned int hash = compute_hash(pkgname, n);
    PackageInfo* pp;
    pp = (PackageInfo*)BasicHashtable::new_entry(hash);
    pp->set_pkgname(pkgname);
    return pp;
  }

  void add_entry(PackageInfo* pp) {
    int index = hash_to_index(pp->hash());
    BasicHashtable::add_entry(index, pp);
  }

  void copy_pkgnames(const char** packages) {
    int n = 0;
    for (int i = 0; i < table_size(); ++i) {
      for (PackageInfo* pp = bucket(i); pp != NULL; pp = pp->next()) {
        packages[n++] = pp->pkgname();
      }
    }
    assert(n == number_of_entries(), "just checking");
  }

  void copy_table(char** top, char* end, PackageHashtable* table);
};


void PackageHashtable::copy_table(char** top, char* end,
                                  PackageHashtable* table) {
  // Copy (relocate) the table to the shared space.
  BasicHashtable::copy_table(top, end);

  // Calculate the space needed for the package name strings.
  int i;
  int n = 0;
  for (i = 0; i < table_size(); ++i) {
    for (PackageInfo* pp = table->bucket(i);
                      pp != NULL;
                      pp = pp->next()) {
      n += (int)(strlen(pp->pkgname()) + 1);
    }
  }
  if (*top + n + sizeof(intptr_t) >= end) {
    warning("\nThe shared miscellaneous data space is not large "
            "enough to \npreload requested classes.  Use "
            "-XX:SharedMiscDataSize= to increase \nthe initial "
            "size of the miscellaneous data space.\n");
    exit(2);
  }

  // Copy the table data (the strings) to the shared space.
  n = align_size_up(n, sizeof(HeapWord));
  *(intptr_t*)(*top) = n;
  *top += sizeof(intptr_t);

  for (i = 0; i < table_size(); ++i) {
    for (PackageInfo* pp = table->bucket(i);
                      pp != NULL;
                      pp = pp->next()) {
      int n1 = (int)(strlen(pp->pkgname()) + 1);
      pp->set_pkgname((char*)memcpy(*top, pp->pkgname(), n1));
      *top += n1;
    }
  }
  *top = (char*)align_size_up((intptr_t)*top, sizeof(HeapWord));
}


void ClassLoader::copy_package_info_buckets(char** top, char* end) {
  _package_hash_table->copy_buckets(top, end);
}

void ClassLoader::copy_package_info_table(char** top, char* end) {
  _package_hash_table->copy_table(top, end, _package_hash_table);
}


PackageInfo* ClassLoader::lookup_package(const char *pkgname) {
  const char *cp = strrchr(pkgname, '/');
  if (cp != NULL) {
    // Package prefix found
    int n = cp - pkgname + 1;
    return _package_hash_table->get_entry(pkgname, n);
  }
  return NULL;
}


bool ClassLoader::add_package(const char *pkgname, int classpath_index, TRAPS) {
  assert(pkgname != NULL, "just checking");
  // Bootstrap loader no longer holds system loader lock obj serializing
  // load_instance_class and thereby add_package
  {
MutexLocker ml(PackageTable_lock);
    // First check for previously loaded entry
    PackageInfo* pp = lookup_package(pkgname);
    if (pp != NULL) {
      // Existing entry found, check source of package
      pp->set_index(classpath_index);
      return true;
    }

    const char *cp = strrchr(pkgname, '/');
    if (cp != NULL) {
      // Package prefix found
      int n = cp - pkgname + 1;

      char* new_pkgname = NEW_C_HEAP_ARRAY(char, n + 1);
      if (new_pkgname == NULL) {
        return false;
      }
  
      memcpy(new_pkgname, pkgname, n);
      new_pkgname[n] = '\0';
      pp = _package_hash_table->new_entry(new_pkgname, n);
      pp->set_index(classpath_index);
      
      // Insert into hash table
      _package_hash_table->add_entry(pp);
    }
    return true;
  }
}


oop ClassLoader::get_system_package(const char* name, TRAPS) {
  PackageInfo* pp;
  {
    MutexLocker ml(PackageTable_lock, THREAD);
    pp = lookup_package(name);
  }
  if (pp == NULL) {
    return NULL;
  } else {
Handle p=java_lang_String::create_from_str(pp->filename(),false/*No SBA*/,THREAD);
    return p();
  }
}


objArrayOop ClassLoader::get_system_packages(TRAPS) {
  ResourceMark rm(THREAD);
  int nof_entries;
  const char** packages;
  {
    MutexLocker ml(PackageTable_lock, THREAD);
    // Allocate resource char* array containing package names
    nof_entries = _package_hash_table->number_of_entries();
    if ((packages = NEW_RESOURCE_ARRAY(const char*, nof_entries)) == NULL) {
      return NULL;
    }
    _package_hash_table->copy_pkgnames(packages);
  }
  // Allocate objArray and fill with java.lang.String
  objArrayOop r = oopFactory::new_objArray(SystemDictionary::string_klass(),
nof_entries,false/*No SBA*/,CHECK_0);
  objArrayHandle result(THREAD, r);
  for (int i = 0; i < nof_entries; i++) {
Handle str=java_lang_String::create_from_str(packages[i],false/*No SBA*/,CHECK_0);
    result->obj_at_put(i, str());
  }

  return result();
}


instanceKlassHandle ClassLoader::load_classfile(symbolHandle h_name, TRAPS) {
  ResourceMark rm(THREAD);
  LoggerMark m(NOTAG, Log::M_CLASSLOADER | Log::L_LO, "loading class INTPTR_FORMAT", h_name());

  stringStream st;
  // st.print() uses too much stack space while handling a StackOverflowError
  // st.print("%s.class", h_name->as_utf8());
  st.print_raw(h_name->as_utf8());
  st.print_raw(".class");
  char* name = st.as_string();

  // Lookup stream for parsing .class file
  ClassFileStream* stream = NULL;
  int classpath_index = 0;
  {
    PerfTraceTime vmtimer(perf_accumulated_time());
    ClassPathEntry* e = _first_entry;
    while (e != NULL) {
      stream = e->open_stream(name);
      if (stream != NULL) {
        break;
      }
      e = e->next();
      ++classpath_index;
    }
  }

  instanceKlassHandle h(THREAD, klassOop(NULL));
  if (stream != NULL) {

    // class file found, parse it
    ClassFileParser parser(stream);
    Handle class_loader;
    Handle protection_domain;
    symbolHandle parsed_name;
    instanceKlassHandle result = parser.parseClassFile(h_name, 
                                                       class_loader, 
                                                       protection_domain, 
                                                       parsed_name,
                                                       CHECK_(h));

    // add to package table
    if (add_package(name, classpath_index, THREAD)) {
      h = result;
    }
  }

  return h;
}


void ClassLoader::create_package_info_table(HashtableBucket *t, int length,
                                            int number_of_entries) {
  assert(_package_hash_table == NULL, "One package info table allowed.");
  assert(length == package_hash_table_size * sizeof(HashtableBucket),
         "bad shared package info size.");
  _package_hash_table = new PackageHashtable(package_hash_table_size, t,
                                             number_of_entries);
}


void ClassLoader::create_package_info_table() {
    assert(_package_hash_table == NULL, "shouldn't have one yet");
    _package_hash_table = new PackageHashtable(package_hash_table_size);
}

// Initialize the class loader's access to methods in libzip.  Parse and
// process the boot classpath into a list ClassPathEntry objects.  Once
// this list has been created, it must not change order (see class PackageInfo)
// it can be appended to and is by jvmti and the kernel vm.

void ClassLoader::initialize() {
  assert(_package_hash_table == NULL, "should have been initialized by now.");
  EXCEPTION_MARK;

  if (UsePerfData) {
    // jvmstat performance counters
    NEWPERFTICKCOUNTER(_perf_accumulated_time, SUN_CLS, "time"); 
    NEWPERFTICKCOUNTER(_perf_class_init_time, SUN_CLS, "classInitTime");
    NEWPERFTICKCOUNTER(_perf_class_verify_time, SUN_CLS, "classVerifyTime");
    NEWPERFTICKCOUNTER(_perf_class_link_time, SUN_CLS, "classLinkedTime");

    NEWPERFEVENTCOUNTER(_perf_classes_inited, SUN_CLS, "initializedClasses");
    NEWPERFEVENTCOUNTER(_perf_classes_linked, SUN_CLS, "linkedClasses");

    // The following performance counters are added for measuring the impact
    // of the bug fix of 6365597. They are mainly focused on finding out
    // the behavior of system & user-defined classloader lock, whether 
    // ClassLoader.loadClass/findClass is being called synchronized or not.
    // Also two additional counters are created to see whether 'UnsyncloadClass'
    // flag is being set or not and how many times load_instance_class call
    // fails with linkageError etc.
    NEWPERFEVENTCOUNTER(_sync_systemLoaderLockContentionRate, SUN_CLS, 
			"systemLoaderLockContentionRate");    
    NEWPERFEVENTCOUNTER(_sync_nonSystemLoaderLockContentionRate, SUN_CLS,
			"nonSystemLoaderLockContentionRate");
    NEWPERFEVENTCOUNTER(_sync_JVMFindLoadedClassLockFreeCounter, SUN_CLS,
			"jvmFindLoadedClassNoLockCalls");
    NEWPERFEVENTCOUNTER(_sync_JVMDefineClassLockFreeCounter, SUN_CLS,
			"jvmDefineClassNoLockCalls");

    NEWPERFEVENTCOUNTER(_sync_JNIDefineClassLockFreeCounter, SUN_CLS,
			"jniDefineClassNoLockCalls");
    
    NEWPERFEVENTCOUNTER(_unsafe_defineClassCallCounter, SUN_CLS,
			"unsafeDefineClassCalls");
    
    NEWPERFEVENTCOUNTER(_isUnsyncloadClass, SUN_CLS, "isUnsyncloadClassSet");
    NEWPERFEVENTCOUNTER(_load_instance_class_failCounter, SUN_CLS,
			"loadInstanceClassFailRate");
    
    // increment the isUnsyncloadClass counter if UnsyncloadClass is set.
    if (UnsyncloadClass) {
      _isUnsyncloadClass->inc();
    }
  }

  // lookup zip library entry points
  load_zip_library();
  // initialize search path
  setup_bootstrap_search_path();
  if (LazyBootClassLoader) {
    // set up meta index which makes boot classpath initialization lazier
    setup_meta_index();
  }
}


jlong ClassLoader::classloader_time_ms() {
  return UsePerfData ?
    Management::ticks_to_ms(_perf_accumulated_time->get_value()) : -1;
}

jlong ClassLoader::class_init_count() {
  return UsePerfData ? _perf_classes_inited->get_value() : -1;
}

jlong ClassLoader::class_init_time_ms() {
  return UsePerfData ? 
    Management::ticks_to_ms(_perf_class_init_time->get_value()) : -1;
}

jlong ClassLoader::class_verify_time_ms() {
  return UsePerfData ? 
    Management::ticks_to_ms(_perf_class_verify_time->get_value()) : -1;
}

jlong ClassLoader::class_link_count() {
  return UsePerfData ? _perf_classes_linked->get_value() : -1;
}

jlong ClassLoader::class_link_time_ms() {
  return UsePerfData ? 
    Management::ticks_to_ms(_perf_class_link_time->get_value()) : -1;
}

int ClassLoader::compute_Object_vtable() {
  // hardwired for JDK1.2 -- would need to duplicate class file parsing
  // code to determine actual value from file
  // Would be value '11' if finals were in vtable
  int JDK_1_2_Object_vtable_size = 5;
  return JDK_1_2_Object_vtable_size * vtableEntry::size();
}


void classLoader_init() {
  ClassLoader::initialize();
}


bool ClassLoader::get_canonical_path(char* orig, char* out, int len) {
  assert(orig != NULL && out != NULL && len > 0, "bad arguments");
#if defined(AZ_PROXIED)
  JNIEnv* env = JavaThread::current()->jni_environment();
  if (Canonicalize(env, hpi::native_path(orig), out, len) < 0) {
    return false;
  }
#else // !AZ_PROXIED
  if (CanonicalizeEntry != NULL) {
    JNIEnv* env = JavaThread::current()->jni_environment();
    if ((CanonicalizeEntry)(env, hpi::native_path(orig), out, len) < 0) {    
      return false;  
    }    
  } else {
    // On JDK 1.2.2 the Canonicalize does not exist, so just do nothing
    strncpy(out, orig, len);
    out[len - 1] = '\0';    
  }
#endif // !AZ_PROXIED
  return true;
}

#ifndef PRODUCT

void ClassLoader::verify() {
  _package_hash_table->verify();
}


// CompileTheWorld
//
// Iterates over all class path entries and forces compilation of all methods
// in all classes found. Currently, only zip/jar archives are searched.
// 
// The classes are loaded by the Java level bootstrap class loader, and the
// initializer is called. If DelayCompilationDuringStartup is true (default),
// the interpreter will run the initialization code. Note that forcing 
// initialization in this way could potentially lead to initialization order
// problems, in which case we could just force the initialization bit to be set.


// We need to iterate over the contents of a zip/jar file, so we replicate the
// jzcell and jzfile definitions from zip_util.h but rename jzfile to real_jzfile,
// since jzfile already has a void* definition.
//
// Note that this is only used in debug mode.
//
// HotSpot integration note:
// Matches zip_util.h 1.14 99/06/01 from jdk1.3 beta H build


// JDK 1.3 version
typedef struct real_jzentry13 { 	/* Zip file entry */
    char *name;	  	  	/* entry name */
    jint time;            	/* modification time */
    jint size;	  	  	/* size of uncompressed data */
    jint csize;  	  	/* size of compressed data (zero if uncompressed) */
    jint crc;		  	/* crc of uncompressed data */
    char *comment;	  	/* optional zip file comment */
    jbyte *extra;	  	/* optional extra data */
    jint pos;	  	  	/* position of LOC header (if negative) or data */
} real_jzentry13;

typedef struct real_jzfile13 {  /* Zip file */
    char *name;	  	        /* zip file name */
    jint refs;		        /* number of active references */
    jint fd;		        /* open file descriptor */
    void *lock;		        /* read lock */
    char *comment; 	        /* zip file comment */
    char *msg;		        /* zip error message */
    void *entries;          	/* array of hash cells */
    jint total;	  	        /* total number of entries */
    unsigned short *table;      /* Hash chain heads: indexes into entries */
    jint tablelen;	        /* number of hash eads */
    real_jzfile13 *next;        /* next zip file in search list */
    jzentry *cache;             /* we cache the most recently freed jzentry */
    /* Information on metadata names in META-INF directory */
    char **metanames;           /* array of meta names (may have null names) */
    jint metacount;	        /* number of slots in metanames array */
    /* If there are any per-entry comments, they are in the comments array */
    char **comments;
} real_jzfile13;

// JDK 1.2 version
typedef struct real_jzentry12 {  /* Zip file entry */
    char *name;                  /* entry name */
    jint time;                   /* modification time */
    jint size;                   /* size of uncompressed data */
    jint csize;                  /* size of compressed data (zero if uncompressed) */
    jint crc;                    /* crc of uncompressed data */
    char *comment;               /* optional zip file comment */
    jbyte *extra;                /* optional extra data */
    jint pos;                    /* position of LOC header (if negative) or data */
    struct real_jzentry12 *next; /* next entry in hash table */
} real_jzentry12;

typedef struct real_jzfile12 {  /* Zip file */
    char *name;                 /* zip file name */
    jint refs;                  /* number of active references */
    jint fd;                    /* open file descriptor */
    void *lock;                 /* read lock */
    char *comment;              /* zip file comment */
    char *msg;                  /* zip error message */
    real_jzentry12 *entries;    /* array of zip entries */
    jint total;                 /* total number of entries */
    real_jzentry12 **table;     /* hash table of entries */
    jint tablelen;              /* number of buckets */
    jzfile *next;               /* next zip file in search list */
} real_jzfile12;


void ClassPathDirEntry::compile_the_world(Handle loader, TRAPS) {
  // For now we only compile all methods in all classes in zip/jar files
  tty->print_cr("CompileTheWorld : Skipped classes in %s", _dir);
  tty->cr();
}


bool ClassPathDirEntry::is_rt_jar() {
  return false;
}

void ClassPathZipEntry::compile_the_world(Handle loader, TRAPS) {
  real_jzfile13* zip = (real_jzfile13*) _zip;
  tty->print_cr("CompileTheWorld : Compiling all classes in %s", zip->name);
  tty->cr();
  // Iterate over all entries in zip file
  for (int n = 0; ; n++) {
#if defined(AZ_PROXIED)
    real_jzentry13 * ze = (real_jzentry13 *)(ZIP_GetNextEntry(_zip, n));
#else // !AZ_PROXIED
    real_jzentry13 * ze = (real_jzentry13 *)((*GetNextEntry)(_zip, n));
#endif // !AZ_PROXIED
    if (ze == NULL) break;
    ClassLoader::compile_the_world_in(ze->name, loader, CHECK);
  }
  if (HAS_PENDING_EXCEPTION) {
    if (PENDING_EXCEPTION->is_a(SystemDictionary::OutOfMemoryError_klass())) {
      CLEAR_PENDING_EXCEPTION;
      tty->print_cr("\nCompileTheWorld : Ran out of memory\n");
      size_t used = Universe::heap()->permanent_used();
      size_t capacity = Universe::heap()->permanent_capacity();
tty->print_cr("Permanent generation used %ldK of %ldK",used/K,capacity/K);
tty->print_cr("Increase size by setting e.g. -XX:MaxPermSize=%ldK\n",capacity*2/K);
    } else {
      tty->print_cr("\nCompileTheWorld : Unexpected exception occurred\n");
    }
  }
}

// JDK 1.3 and up version
bool ClassPathZipEntry::is_rt_jar() {
  real_jzfile13* zip = (real_jzfile13*) _zip;
  int len = (int)strlen(zip->name);
  // Check whether zip name ends in "rt.jar"
  // This will match other archives named rt.jar as well, but this is
  // only used for debugging.
  return (len >= 6) && (strcasecmp(zip->name + len - 6, "rt.jar") == 0);
}

jlong firstFAMer = 0;
void ClassLoader::freeze_and_melt(int system_dictionary_modification_counter) {
  Unimplemented();
//  EXCEPTION_MARK;
//  HandleMark hm(THREAD);
//  ResourceMark rm(THREAD);
//
//  if (FreezeAndMeltCompiler == 2) {
//    if (!THREAD->is_C2Compiler_thread()) {
//      ThreadInVMfromUnknown t;
//      MutexLockerAllowGC mx(FAMTrap_lock, JavaThread::current());
//      FAMTrap_lock.wait();
//    }
//  } else if (FreezeAndMeltCompiler == 1) {
//    if (!THREAD->is_C1Compiler_thread()) {
//      ThreadInVMfromUnknown t;
//      MutexLockerAllowGC mx(FAMTrap_lock, JavaThread::current());
//      FAMTrap_lock.wait();
//    }
//  } else {
//    ShouldNotReachHere(); // Invalid FreezeAndMelt value
//  }
//
//  if (Atomic::post_increment(&firstFAMer) != 0) {
//    ThreadInVMfromUnknown t;
//    MutexLocker mx(FAMTrap_lock);
//    FAMTrap_lock.wait();
//  }
//
//  elapsedTimer famtimer;
//  famtimer.start();
//
//  AbstractCompiler* ac;
//  FAMPtr old_cim;
//
//  FreezeAndMelt fam(FreezeAndMeltInFile, FreezeAndMeltOutFile);
//
//  if (FreezeAndMeltCompiler == 2) {
//    char* buf = fam.getStringFromGDBCmd("x/i *(intptr_t*)%p", fam.thread());
//    assert(strstr(buf, "C2CompilerThread") != NULL, "You have requested melting of a C2 thread, but the thread you specified is not a C2 thread");
//    old_cim = fam.getOldPtr("((struct C2CompilerThread*)%p)->_compile->_method", fam.thread());
//    ac = CompileBroker::_c2._abstract_compiler;
//  }
//  else if (FreezeAndMeltCompiler == 1) {
//    char* buf = fam.getStringFromGDBCmd("x/i *(intptr_t*)%p", fam.thread());
//    assert(strstr(buf, "C1CompilerThread") != NULL, "You have requested melting of a C1 thread, but the thread you specified is not a C1 thread");
//    old_cim = fam.getOldPtr("((struct C1CompilerThread*)%p)->_compile->_method", fam.thread());
//    ac = CompileBroker::_c1._abstract_compiler;
//  }
//
//  // Create ci environment
//  ciEnv ci_env(&fam, old_cim);
//
//  // Now that the environment is set up, we can refer to the fam structure with 'FAM->'
//  
//  int osr_bci = fam.getInt("((struct CompilerThread*)%p)->_task->_osr_bci", FAM->thread());
//  if (osr_bci == (unsigned short)InvocationEntryBci) {
//    osr_bci = InvocationEntryBci;
//  }
//
//  // Kick the compile
//  ciMethod* cim = (ciMethod*)FAM->getNewFromOldPtr(old_cim);
//
//  famtimer.stop();
//
//  tty->print_cr("[FAM] Finished reconstituting ci blob (%.2f s)", famtimer.seconds());
//
//  while(true) {
//    ac->compile_method(&ci_env, cim, osr_bci);
//
//    // Reset all global flags in case of a second run trhough
//    if (FreezeAndMeltCompiler == 2) {
//      C2CompilerThread::current()->_compile = NULL;
//    }
//
//    tty->print_cr("[FAM] Your core is now a puddle on the ground..");
//
//    BREAKPOINT;
//  }

ThreadInVMfromUnknown t;
  vm_exit(0);
}

void LazyClassPathEntry::compile_the_world(Handle loader, TRAPS) {
  resolve_entry()->compile_the_world(loader, CHECK);
}

bool LazyClassPathEntry::is_rt_jar() {
  return resolve_entry()->is_rt_jar();
}

void ClassLoader::compile_the_world() {
  _ctw_buffer = NEW_C_HEAP_ARRAY(instanceKlassHandle, _ctw_buffer_size);
  _compile_the_world_counter2 = CompileTheWorldStartAt;
  EXCEPTION_MARK;
  HandleMark hm(THREAD);
  ResourceMark rm(THREAD);
  // Find bootstrap loader
  Handle system_class_loader (THREAD, SystemDictionary::java_system_loader());
  // Iterate over all bootstrap class path entries
  ClassPathEntry* e = _first_entry;
  while (e != NULL) {
    // We stop at rt.jar, unless it is the first bootstrap path entry
    if (e->is_rt_jar() && e != _first_entry) break;
    e->compile_the_world(system_class_loader, CATCH);
    e = e->next();
  }
  // Drain remaining klasses to compile
  while (_ctw_buffer_emptypos != _ctw_buffer_filledpos) {
    compile_the_world_helper(CATCH);
  }
  // Drain the remaining compiles
  { 
     Unimplemented();
//    MutexLockerAllowGC mx(&CompileTask_lock, 1);
//    while( CompileBroker::_c1._queue != NULL ) 
//      CompileTask_lock.wait_micros(5000000L);
//    while( CompileBroker::_c2._queue != NULL ) 
//      CompileTask_lock.wait_micros(5000000L);
  }

  tty->print_cr("CompileTheWorld : Done");
  {
    // Print statistics as if before normal exit:
    extern void print_statistics();
    print_statistics();
  }
  vm_exit(0);
}

int ClassLoader::_compile_the_world_counter = 0;
int ClassLoader::_compile_the_world_counter2=0;
instanceKlassHandle*ClassLoader::_ctw_buffer=NULL;
uint ClassLoader::_ctw_buffer_emptypos = 0;
uint ClassLoader::_ctw_buffer_filledpos = 0;
uint ClassLoader::_ctw_buffer_mask = 0x3F; // Must be power of 2
uint ClassLoader::_ctw_buffer_size = _ctw_buffer_mask+1; // Must be power of 2

void ClassLoader::compile_the_world_in(char* name, Handle loader, TRAPS) {
  if (_ctw_buffer_filledpos == ((_ctw_buffer_emptypos+1)&_ctw_buffer_mask)) {
    compile_the_world_helper(CATCH);
  }

  int len = (int)strlen(name);
  if (len > 6 && strcmp(".class", name + len - 6) == 0) {
    // We have a .class file
    char buffer[2048];
    strncpy(buffer, name, len - 6);
    buffer[len-6] = 0;
    // If the file has a period after removing .class, it's not really a
    // valid class file.  The class loader will check everything else.
    if (strchr(buffer, '.') == NULL) {
      _compile_the_world_counter++;
      if (_compile_the_world_counter >= CompileTheWorldStartAt && _compile_the_world_counter <= CompileTheWorldStopAt) {
        // Construct name without extension
        symbolHandle sym = oopFactory::new_symbol_handle(buffer, CHECK);
        // Use loader to load and initialize class
        klassOop ik = SystemDictionary::resolve_or_null(sym, loader, Handle(), THREAD);
        instanceKlassHandle k (THREAD, ik);
        if (k.not_null() && !HAS_PENDING_EXCEPTION) {
          k->initialize(THREAD);
        }
        bool exception_occurred = HAS_PENDING_EXCEPTION;
        if (k.is_null() || (exception_occurred && !CompileTheWorldIgnoreInitErrors)) {
          // If something went wrong (e.g. ExceptionInInitializerError) we skip this class
tty->print("CompileTheWorld : Skipping %s",buffer);
          if( exception_occurred ) {
tty->print(", exception: ");
java_lang_Throwable::print(PENDING_EXCEPTION_REF,tty);
          }
          tty->cr();
        } else {
          // Buffer needs filling
          _ctw_buffer[_ctw_buffer_emptypos] = k;
          _ctw_buffer_emptypos = (_ctw_buffer_emptypos+1)&_ctw_buffer_mask;
          // tty->print_cr("[ctw] Buffering (e=%d,f=%d) %s", _ctw_buffer_emptypos, _ctw_buffer_filledpos, buffer);
        }
        CLEAR_PENDING_EXCEPTION;
      }
    }
  }
}

bool ClassLoader::compile_the_world_helper(TRAPS) {
  instanceKlassHandle ktc = _ctw_buffer[_ctw_buffer_filledpos];
  _ctw_buffer_filledpos = (_ctw_buffer_filledpos+1)&_ctw_buffer_mask;
  // tty->print_cr("[ctw] Playing (e=%d,f=%d) %s", _ctw_buffer_emptypos, _ctw_buffer_filledpos, ktc->external_name());

  tty->print_cr("CompileTheWorld (%d) : %s", _compile_the_world_counter2, ktc->external_name());
  // Preload all classes to get around uncommon traps
  if (CompileTheWorldPreloadClasses) {
constantPoolKlass::preload_and_initialize_all_classes(ktc->constants(),THREAD);
    if (HAS_PENDING_EXCEPTION) {
      // If something went wrong in preloading we just ignore it
      CLEAR_PENDING_EXCEPTION;
      tty->print_cr("Preloading failed for (%d) %s", _compile_the_world_counter2, ktc->external_name());
    }
  }
  // Iterate over all methods in class
for(int n=0;n<ktc->methods()->length();n++){
methodHandle m(THREAD,methodOop(ktc->methods()->obj_at(n)));
    // Force compilation           
    if( UseC1 && CompilationPolicy::canBeCompiled(m,1))
      CompileBroker::_c1.producer_add_task(m, methodHandle(), InvocationEntryBci);
    if( UseC2 && CompilationPolicy::canBeCompiled(m,2))
      CompileBroker::_c2.producer_add_task(m, methodHandle(), InvocationEntryBci);
  }
  _compile_the_world_counter2++;
  
  { // Pause spamming compile jobs until the compiler threads (mostly) catch up
    MutexLockerAllowGC mx(&CompileTask_lock, 1);
    while( CompileBroker::_c1.waiting_tasks() > CIMaxCompilerThreads )
      CompileTask_lock.wait_micros(5000000L, false);
    while( CompileBroker::_c2.waiting_tasks() > CIMaxCompilerThreads )
      CompileTask_lock.wait_micros(5000000L, false);
  }

  return true;
}

#endif //PRODUCT
