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


#include "log.hpp"

#include "allocation.inline.hpp"

// ============================================================================
// Log implementation
// ============================================================================

#ifndef PRODUCT
int Log::_opt_mask = ~Log::L_LO;

int Log::get_opt_mask() {
  return _opt_mask;
}

void Log::set_opt_mask(int opt_mask) {
  _opt_mask = opt_mask;
}
#endif

void Log::logs(const char* tag, int opt, bool stp, outputStream* st, const char* message) {

  // strip any directories from tag
  const char* bare_tag = strrchr(tag, '/');
if(bare_tag==NULL){
bare_tag=tag;
  } else {
    bare_tag++;
  }

  // potentially write to output stream
  if (stream_enabled(stp)) {
    // lock tty for duration of print
    ttyLocker ttyl;
st->print_raw(message);
  }
}

void Log::vlog(const char* tag, int opt, bool stp, bool crp, outputStream* st, const char* format, va_list ap) {
  // TODO: don't need to allocate buffer in some scenarios; see do_vsnprintf 
  // in ostream.cpp; also, try to ensure buffer size is identical to size in 
  // ostream.cpp
char buf[2000];
  size_t len;
  // optimized variable-argument buffer printing
  const char* message = outputStream::do_vsnprintf(buf, sizeof (buf), format, ap, crp, len);
  // log the message
  logs(tag, opt, stp, st, message);
}

// ============================================================================
// LoggerStackObj implementation
// ============================================================================

void LoggerStackObj::vlog(int opt, const char* format, va_list ap) {
  Log::vlog(_tag, _opt | opt, _stp, _crp, _st, format, ap);
}

void LoggerStackObj::log(int opt, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  vlog(opt, format, ap);
  va_end(ap);
}

void LoggerStackObj::logs(int opt, const char* message) {
  Log::logs(_tag, _opt | opt, _stp, _st, message);
}

// ============================================================================
// LoggerMark implementation
// ============================================================================

//LoggerMark::LoggerMark(const char* tag, int opt, bool stp, bool crp, outputStream* st, const char* format, ...) : LoggerStackObj(tag, opt, stp, crp, st) {
//  if (enabled()) {
//    va_list ap;
//    va_start(ap, format);
//    vlog(Log::ENTER, format, ap);
//    va_end(ap);
//  }
//}
//
//LoggerMark::LoggerMark(const char* tag, int opt, bool stp, bool crp, const char* format, ...) : LoggerStackObj(tag, opt, stp, crp) {
//  if (enabled()) {
//    va_list ap;
//    va_start(ap, format);
//    vlog(Log::ENTER, format, ap);
//    va_end(ap);
//  }
//}
//
//LoggerMark::LoggerMark(const char* tag, int opt, bool stp, const char* format, ...) : LoggerStackObj(tag, opt, stp) {
//  if (enabled()) {
//    va_list ap;
//    va_start(ap, format);
//    vlog(Log::ENTER, format, ap);
//    va_end(ap);
//  }
//}
//
LoggerMark::LoggerMark(const char* tag, int opt, const char* format, ...) : LoggerStackObj(tag, opt) {
  if (enabled()) {
    va_list ap;
    va_start(ap, format);
    vlog(Log::ENTER, format, ap);
    va_end(ap);
  }
}

LoggerMark::~LoggerMark() {
  logs(Log::LEAVE, "done\n");
}

// ============================================================================
// LogBufferStream implementation
// ============================================================================

LogBufferStream::LogBufferStream(outputStream* wst, size_t len) : stringStream(NEW_RESOURCE_ARRAY(char, len), len), _wst(wst) {
}

void LogBufferStream::stamp() {
if(_wst!=NULL){
    TimeStamp* stamp = _wst->get_time_stamp();
    if (!stamp->is_updated()) {
      stamp->update();
    }
    print("%.3f", stamp->seconds());
  } else {
    stringStream::stamp();
  }
}

// ============================================================================
// BufferedLoggerMark implementation
// ============================================================================


BufferedLoggerMark::BufferedLoggerMark(const char* tag, int opt, bool stp, bool crp, outputStream* st, long bufferSize) : LoggerStackObj(tag, opt, stp, crp, st), _mst(st, bufferSize), _depth(0) {
}

void BufferedLoggerMark::vout(const char*format,va_list ap){
  _mst.vprint(format, ap);
}

void BufferedLoggerMark::out(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
vout(format,ap);
  va_end(ap);
}

void BufferedLoggerMark::outs(const char* message) {
  _mst.print_raw(message);
}

bool BufferedLoggerMark::enter() {
if(_mst.size()>0){
    if (crp()) _mst.cr();
    logs(Log::ENTER, _mst.as_string());
_mst.reset();
  } else {
    logs(Log::ENTER, "begin\n");
  }
  _depth++;
  return true;
}

bool BufferedLoggerMark::leave() {
  if (_depth > 0) {
    _depth--;
if(_mst.size()>0){
      if (crp()) _mst.cr();
      logs(Log::LEAVE, _mst.as_string());
_mst.reset();
    } else {
      logs(Log::LEAVE, "done\n");
    }
    return true;
  }
  return false;
}

bool BufferedLoggerMark::flush() {
if(_mst.size()>0){
    if (crp()) _mst.cr();
    logs(0, _mst.as_string());
_mst.reset();
    return true;
  }
  return false;
}

BufferedLoggerMark::~BufferedLoggerMark() {
  if (_depth == 0) {
    flush();
  } else {
    while (leave());
  }
}


// ============================================================================
// AtomicBufferStream implementation
// ============================================================================

AtomicBufferStream::AtomicBufferStream(outputStream* st, size_t bufferSize)
  : stringStream(NEW_RESOURCE_ARRAY(char, bufferSize), bufferSize), _fst(st)
{}

void AtomicBufferStream::flush()
{
  if (size() > 0) {
    {
      ttyLocker ttyl;
      _fst->print_raw(as_string());
    }
    reset();
  }
}
