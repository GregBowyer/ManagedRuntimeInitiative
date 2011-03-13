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
#ifndef THREADCOUNTERS_HPP
#define THREADCOUNTERS_HPP

#include "allocation.hpp"

class ThreadCounters:public CHeapObj{
 private:

  int64_t _start_time; // in ticks
  int64_t _running_ticks;
  int64_t _object_blocked_ticks;
  int64_t _object_wait_ticks;
  int64_t _rpc_wait_ticks;
  int64_t _cpu_wait_ticks;
  int64_t _temp_tick;

 public:
    ThreadCounters() : _start_time(0ull), _running_ticks(0ull), _object_blocked_ticks(0ull),
		       _object_wait_ticks(0ull), _rpc_wait_ticks(0ull), _cpu_wait_ticks(0ull)
    {}
  
  int64_t start_time() const           { return _start_time; }

  int64_t object_blocked_ticks() const { return _object_blocked_ticks; }
  int64_t object_wait_ticks() const    { return _object_wait_ticks; }
  int64_t rpc_wait_ticks() const       { return _rpc_wait_ticks; }
  int64_t cpu_wait_ticks() const       { return _cpu_wait_ticks; }
  int64_t wall_ticks() const           { return os::elapsed_counter() - _start_time; }

  void add_object_blocked_ticks(int64_t val) { _object_blocked_ticks += val; }
  void add_object_wait_ticks   (int64_t val) { _object_wait_ticks    += val; }
  void add_rpc_wait_ticks      (int64_t val) { _rpc_wait_ticks       += val; }
  void add_cpu_wait_ticks      (int64_t val) { _cpu_wait_ticks       += val; }

  int64_t temp_tick() const        { return _temp_tick; }
  void set_temp_tick(int64_t val)  { _temp_tick = val; }

  void set_start_time(int64_t val) { _start_time = val; }
};

#endif // THREADCOUNTERS_HPP
