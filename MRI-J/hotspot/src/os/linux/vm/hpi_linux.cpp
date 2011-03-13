/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "hpi.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"

#include "allocation.inline.hpp"

#include "os_os.inline.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#else // !AZ_PROXIED:
#include <dlfcn.h>
#endif // !AZ_PROXIED


// Needed until new <sys/ioctl.h> appears:
extern "C" {
# include <sys/ioctl.h>
}


extern "C" {
  char *unproxied_nativepath(char *path) {
    return path;
  }

  int unproxied_lasterror(char *buf, int len) {
    // TODO: strerror() isn't thread safe (according to the man page)!
const char*msg=strerror(errno);
if(msg==NULL){
msg="Unknown error code";
    }
strncpy(buf,msg,len);
return strlen(buf);
  }

};


size_t hpi::read(int fd,void*buf,unsigned int nBytes){
    size_t result = ::read(fd, buf, nBytes);
    if (result < 0) {
if(errno==EINTR){
        result = (size_t)JVM_IO_INTR;
      } else {
result=JVM_IO_ERR;
      }
    }
    if (TraceHPI) {
MutexLocker ml(ThreadCritical_lock);
tty->print("hpi::read(");
      tty->print("fd = %d, buf = %p, nBytes = %d", fd, buf, nBytes) ;
tty->print(") = ");
tty->print_cr("%zd",result);
    }
    return result;
}

size_t hpi::write(int fd, const void *buf, unsigned int nBytes) {
    size_t result = ::write(fd, buf, nBytes);
    if (result < 0) {
if(errno==EINTR){
        result = (size_t)JVM_IO_INTR;
      } else {
result=JVM_IO_ERR;
      }
    }
    if (TraceHPI) {
MutexLocker ml(ThreadCritical_lock);
tty->print("hpi::write(");
      tty->print("fd = %d, buf = %p, nBytes = %d", fd, buf, nBytes) ;
tty->print(") = ");
tty->print_cr("%zd",result);
    }
    return result;
}

void   hpi::dll_build_name(char *buf, int buf_len, char* dir, const char *name, bool use_debug_library_suffix) {
  if (TraceHPI) {
    tty->print("hpi::dll_build_name(buf = %p, buflen = %d, path = %s, name = %s, use_debug_library_suffix = %d)", buf, buf_len, dir, name, use_debug_library_suffix);
  }
#ifdef AZ_PROXIED
  proxy_library_path_for_name(buf, buf_len, dir, name, (use_debug_library_suffix? 1 : 0));
#else // !AZ_PROXIED:
if(dir==NULL||strlen(dir)==0){
    sprintf(buf,    "lib%s%s.so",      name, use_debug_library_suffix?"_g":"");
  } else {
    sprintf(buf, "%s/lib%s%s.so", dir, name, use_debug_library_suffix?"_g":"");
  }
#endif // !AZ_PROXIED
  if (TraceHPI) {
tty->print_cr(" = %s",buf);
  }
}

void  *hpi::dll_load(const char *name, char *ebuf, int ebuflen) {
  if (TraceHPI) {
    tty->print("hpi::dll_load(name = %s, ebuf = %p, ebuflen = %d) = ", name, ebuf, ebuflen);
  }
  void *lib_handle;
#ifdef AZ_PROXIED
const char*lib_name=NULL;
  proxy_java_library_islocal(name, &lib_name);
if(lib_name!=NULL){
    lib_handle = (void *)((uintptr_t)lib_name      | (uintptr_t)PI_IMAGE_THIS);
  } else {
    lib_handle = proxy_library_load(name, ebuf, ebuflen);
if(lib_handle!=NULL){
      lib_handle = os::mark_remote_handle((address)lib_handle);
    }
  }
#else // !AZ_PROXIED:
  // Extract the core library name that might have been passed to hpi::dll_build_name.
  // We expect name to be either "PATH/libX.so", "PATH/libX_g.so", "libX.so", or "libX_g.so".
  // We hope to set lib_name to the "X" part of the name.
  //
  // TODO: This code all needs to be reviewed before we ship an unproxied AVM.
char*last_slash=strrchr(name,'/');
  const char* token;
if(last_slash==NULL){
    token = name;           // No slashes, point to the start of the library name
  } else {
    token = &last_slash[1]; // Found a slash, point to the start of the filename at the end of the path.
  }
if(strncmp(token,"lib",3)==0){
    token = &token[3];
  }
  char*       lib_name = strdup(token);     // free'd at end of this method
  // Trim off trailing "_g.so" or ".so"
  int         token_len = strlen(lib_name);
  const char* suffix1   = "_g.so";
  const char* suffix2   = ".so";
  if ( strcmp(&lib_name[token_len-strlen(suffix1)], suffix1) == 0 ) {
    lib_name[token_len - strlen(suffix1)] = '\0';
  }
  else if ( strcmp(&lib_name[token_len-strlen(suffix2)], suffix2) == 0 ) {
    lib_name[token_len - strlen(suffix2)] = '\0';
  }
  // magic handle for libjava
if(strncmp(token,"java",4)==0){
    lib_handle = (void *) -1;
  } else {
    // Open the library globally.  Future lookups will find symbols in the library.
    // This might not be correct if one library shadows another.  The library
    // handle *is* passed around, so we can do per-library lookups if required.
    dlerror();
    lib_handle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
    const char *err = dlerror();
    if( err ) strncpy(ebuf,err,ebuflen);
  }
  
#endif // !AZ_PROXIED
  if (TraceHPI) {
tty->print_cr("(void *)%p",lib_handle);
if(lib_handle==NULL){
tty->print_cr("HPI returned error: %s",ebuf);
    }
  }
  if (PrintJNILoading) {
if(lib_handle!=NULL){
      tty->print_cr("[Loaded %slibrary: %s]", (lib_name == NULL) ? "remote " : "", name);
    }
  }
#ifdef AZ_PROXIED
#else
if(lib_name!=NULL){
free(lib_name);
  }
#endif
  return lib_handle;
}

void   hpi::dll_unload(void *lib) {
  if (TraceHPI) {
tty->print("hpi::dll_unload(lib = %p)",lib);
  }
#ifdef AZ_PROXIED
  if ((uintptr_t)lib & (uintptr_t)PI_IMAGE_THIS) {
    // Nothing to do
    const char *lib_name = (const char *)((uintptr_t)lib & ~(uintptr_t)PI_IMAGE_THIS);
    if (TraceHPI) {
tty->print("[this image] name = %s",lib_name);
    }
  } else if (os::is_remote_handle((address)lib)) {
    void *lib_handle = (void *)os::get_real_remote_handle((address)lib);
    proxy_library_unload(lib_handle);
    if (TraceHPI) {
tty->print("[remote]");
    }
  } else {
    ShouldNotReachHere();
  }
#else // !AZ_PROXIED:
  // Unproxied JVMs don't currently support actually unloading a library.
  const char *lib_name = (const char *)lib;
  if (TraceHPI) {
tty->print("[this image] name = %s",lib_name);
  }
#endif // !AZ_PROXIED
  if (TraceHPI) {
    tty->cr();
  }
}

void*  hpi::dll_lookup(void *lib, const char *name) {
  if (TraceHPI) {
tty->print("hpi::dll_lookup(lib = %p, name = %s) = ",lib,name);
  }
void*sym_handle=NULL;

#ifdef AZ_PROXIED
  proxy_error_t retval;
  if ((uintptr_t)lib & (uintptr_t)PI_IMAGE_THIS) {
    // Function should be built-in.
    // Note: May need to mangle the name if it's not found on the first try.
    // sym_handle = pi_symbol_to_address_by_name(PI_IMAGE_THIS, name);
    retval = proxy_java_locallibrary_lookup(PI_IMAGE_THIS, name, (uintptr_t *)&sym_handle);
    if (retval == PROXY_ERROR_NONE) {
      if (!sym_handle) {
        char mangled_name[256];
        const char *lib_name = (const char *)((uintptr_t)lib & ~(uintptr_t)PI_IMAGE_THIS);
        snprintf(mangled_name, sizeof(mangled_name), "%s_%s", name, lib_name);
        if (TraceHPI) {
tty->print_cr("(trying with mangled name) '%s' ",mangled_name);
        }
        // sym_handle = pi_symbol_to_address_by_name(PI_IMAGE_THIS, mangled_name);
        retval = proxy_java_locallibrary_lookup(PI_IMAGE_THIS, mangled_name, (uintptr_t *)&sym_handle);
        if (retval != PROXY_ERROR_NONE) {
sym_handle=NULL;
        }
      }
    } else {
sym_handle=NULL;
    }
  } else if (os::is_remote_handle((address)lib)) {
    void *lib_handle = (void *)os::get_real_remote_handle((address)lib);
    retval = proxy_java_remotelibrary_lookup(lib_handle, name, (uintptr_t *)&sym_handle);
    if (retval != PROXY_ERROR_NONE) {
sym_handle=NULL;
    }
  } else {
    ShouldNotReachHere();
  }
#else // !AZ_PROXIED:
  if ( lib == (void *)-1 ) lib = 0; // catch our magic symbol and change to default dlsym
  dlerror(); // clear out old error code
  sym_handle = dlsym(lib, name);
  const char * error = dlerror(); // any errors
  if (TraceHPI && error) tty->print_cr("dlerror '%s' ", error);
  if ( !sym_handle ) {
    char mangled_name[256];
    const char *lib_name = (const char *)lib;
    snprintf(mangled_name, sizeof(mangled_name), "%s_%s", name, lib_name);
    if (TraceHPI) {
tty->print_cr("(trying with mangled name) '%s' ",mangled_name);
    }
    sym_handle = dlsym(lib, mangled_name);
    const char *error2 = dlerror(); // any errors
    if ( TraceHPI && error2) tty->print_cr("dlerror '%s' ", error2);
  }
#endif // !AZ_PROXIED
  if (TraceHPI) {
tty->print_cr("(void *)%p",sym_handle);
  }
  return sym_handle;
}


int hpi::available(int fd, jlong *bytes) {
  if (TraceHPI) {
tty->print("hpi::available(fd = %d, bytes = %p) = ",fd,bytes);
  }
  int bytes_int = 0;
  int rc = ::ioctl(fd, FIONREAD, &bytes_int);
  if (rc == 0) {
    rc = 1;  // In this case, 1 indicates success
    *bytes = (jlong)bytes_int;
  } else {
    rc = 0;  // Failure
  }
  if (TraceHPI) {
tty->print_cr("%d",rc);
  }
  return rc;
}
