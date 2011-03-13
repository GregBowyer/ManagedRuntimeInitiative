/*
 * Copyright 1999-2004 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OSTHREAD_OS_HPP
#define OSTHREAD_OS_HPP


// This is embedded via include into the class OSThread

 private:

  // _thread_id is kernel thread id (similar to LWP id on Solaris). Each
  // thread has a unique thread_id (LinuxThreads or NPTL). It can be used
  // to access /proc.
  pid_t     _thread_id;

  // _pthread_id is the pthread id, which is used by library calls
  // (e.g. pthread_kill).
  pthread_t _pthread_id;

  uint64_t  _thread_num;      	 // integer thread number
  bool      _vm_created_thread;  // true if the VM create this thread
                                 // false if primary thread or attached thread
 public:

pid_t thread_id()const{return _thread_id;}
void set_thread_id(pid_t id){_thread_id=id;}

  pthread_t pthread_id() const       { return _pthread_id; }
  void set_pthread_id(pthread_t id)  { _pthread_id = id;   }

  // Set and get state of _vm_created_thread flag
  void set_vm_created()           { _vm_created_thread = true; }
  bool is_vm_created()            { return _vm_created_thread; }

  // Used for debugging, return a unique integer for each thread.
  uint64_t thread_identifier() const        { return _thread_num; }
  void set_thread_identifier(uint64_t n)    { _thread_num = n; }
  
  // ***************************************************************
  // Platform dependent initialization and cleanup
  // ***************************************************************

private:

  void pd_initialize();
  void pd_destroy();

#endif // OSTHREAD_OS_HPP
