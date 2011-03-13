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
#ifndef LOG_HPP
#define LOG_HPP



#include "ostream.hpp"

// ============================================================================
// This is the centralized event logging facility.
//
// ============================================================================

// ============================================================================
// Static Log class.
//
// Defines all logging flags and masks. Provides all static logging functions.
//
class Log:AllStatic{

 private:
#ifndef PRODUCT
  static int _opt_mask;
#endif

 public:
  // log event printing flags
  static const int PRINT                = 1;
  static const int FLUSH                = 1 << 1;

  // log event types
  static const int ENTER                = 1 << 8;
  static const int LEAVE                = 1 << 9;

  // log event importance levels
  static const int L_LO                 = 1 << 7;
  static const int L_HI                 = 1 << 6;

  // log event associated modules
  static const int M_GC                 = 1 << 31;
  static const int M_CLASSLOADER        = 1 << 30;
  static const int M_NMETHOD            = 1 << 29;
  static const int M_SAFEPOINT          = 1 << 28;
  static const int M_THREAD             = 1 << 27;
  static const int M_EXCEPTION          = 1 << 26;
  static const int M_COMPILE            = 1 << 25;
  static const int M_DEOPT              = 1 << 24;
  static const int M_PATCH              = 1 << 23;
  static const int M_VM                 = 1 << 22;

  static const int PRINT_MASK           = FLUSH | PRINT;
  static const int TYPES_MASK           = ENTER | LEAVE;
  static const int LEVEL_MASK           = L_LO | L_HI;

  static int get_opt_mask() PRODUCT_RETURN0;
  static void set_opt_mask(int opt_mask) PRODUCT_RETURN;

  // should event be output to stream?
  static inline bool stream_enabled(bool stp) {
    return stp;
  }

  // should event be output at all?
  static inline bool enabled(int opt, bool stp) {
    return stream_enabled(stp);
  }

  // no event comes to the Log but through Me
  static void logs(const char* tag, int opt, bool stp, outputStream* st, const char* message);

  static void vlog(const char* tag, int opt, bool stp, bool crp, outputStream* st, const char* format, va_list ap);

  //
  // Various inline logging functions. In general, "log" efficiently checks if 
  // event logging is enabled and "vlog" checks in a rather more expensive way 
  // inside the non-inline function above.
  //

  static inline void vlog(const char* tag, int opt, bool stp, bool crp, const char* format, va_list ap) {
    vlog(tag, opt, stp, crp, tty, format, ap);
  }

  static inline void vlog(const char* tag, int opt, bool stp, const char* format, va_list ap) {
    vlog(tag, opt, stp, true, tty, format, ap);
  }

  static inline void vlog(const char* tag, int opt, const char* format, va_list ap) {
    vlog(tag, opt, false, true, tty, format, ap);
  }

  static inline void log6(const char* tag, int opt, bool stp, bool crp, outputStream* st, const char* format, ...) {
    if (enabled(opt, stp)) {
      va_list ap;
      va_start(ap, format);
      vlog(tag, opt, stp, crp, st, format, ap);
      va_end(ap);
    }
  }

  static inline void log5(const char* tag, int opt, bool stp, bool crp, const char* format, ...) {
    if (enabled(opt, stp)) {
      va_list ap;
      va_start(ap, format);
      vlog(tag, opt, stp, crp, format, ap);
      va_end(ap);
    }
  }

  static inline void log3(const char* tag, int opt, bool stp, const char* format, ...) {
    if (enabled(opt, stp)) {
      va_list ap;
      va_start(ap, format);
      vlog(tag, opt, stp, format, ap);
      va_end(ap);
    }
  }

  static inline void log4(const char* tag, int opt, const char* format, ...) {
    // no event logging any more
  }
};

// ============================================================================
// Base class for all stack-dwelling Logger objects. Such loggers usually emit 
// an event during or shortly after construction (since the tag may default to 
// the line number of stack object instantiation).
//
// This class of loggers will also frequently emit an event during destruction 
// (since this occurs at block exit) to indicate that the logged operation has 
// completed.
//
class LoggerStackObj:public StackObj{

 private:
  const char* _tag;             // unique event tag, usually file and line
  int _opt;                     // event logging options
  bool _stp;                    // print to log stream?
  bool _crp;                    // print CR line terminator to stream?
  outputStream* _st;            // log output stream, usually tty

 public:
  LoggerStackObj(const char* tag, int opt, bool stp = false, bool crp = true, outputStream* st = tty) : _tag(tag), _opt(opt), _stp(stp), _crp(crp), _st(st) {}
  bool enabled() { return Log::enabled(_opt, _stp); }

 protected:
  bool crp() { return _crp; }
  void vlog(int opt, const char* format, va_list ap);
  void log(int opt, const char* format, ...);
  void logs(int opt, const char* message);
};

// ============================================================================
// Marks an operation by emitting log events at construct and destruct time.
//
// An ENTER event is emitted by the constructor, a LEAVE event by the 
// destructor. Basically works like the old EventMark interface.
//
class LoggerMark : public LoggerStackObj {

 public:
  //LoggerMark(const char* tag, int opt, bool stp, bool crp, outputStream* st, const char* format, ...);
  //LoggerMark(const char* tag, int opt, bool stp, bool crp, const char* format, ...);
  //LoggerMark(const char* tag, int opt, bool stp, const char *format, ...);
  LoggerMark(const char* tag, int opt, const char *format, ...);
  ~LoggerMark();
};

// ============================================================================
// Stores the log event message for BufferedLoggerMark.
class LogBufferStream : public stringStream {

 protected:
outputStream*_wst;

 public:
  LogBufferStream(outputStream* wst, size_t len);
  virtual void stamp();
};

// ============================================================================
// Allows gradual composition of a log event message, emitting the event at 
// destruct time. Every BufferedLoggerMark allocates a string buffer 
// (stringStream) on the stack. This makes it a somewhat heavier-weight 
// logging construct than Log or LoggerMark.
//
// Use of enter(), leave(), and flush() may be non-obvious:
// enter: Flushes current output buffer as an ENTER event. This causes events 
//        following to be indented.
// leave: Flushes current output buffer as a LEAVE event. The leave event will 
//        be back at the same indent level as the last enter.
// flush: Flushes current output buffer without changing indent level.
//
// Although it's theoretically possible to output any number of events from a 
// single BufferedLoggerMark, the usual maximum should be two: one shortly 
// after construct, and one at or shortly before destruct.
//
class BufferedLoggerMark : public LoggerStackObj {

 public:
  static const size_t MAX_MESSAGE_LEN = 1*K;

 private:
  LogBufferStream _mst;
  int _depth;

 public:
  BufferedLoggerMark(const char* tag, int opt, bool stp = false, bool crp = true, outputStream* st = tty,  long bufferSize = MAX_MESSAGE_LEN );
  ~BufferedLoggerMark();
  LogBufferStream* stream() { return &_mst; }
void vout(const char*format,va_list ap);
void out(const char*format,...);
void outs(const char*message);
  bool enter();
  bool leave();
  bool flush();
};


// ============================================================================
// Allows gradual composition of a log message, emitting the message when flush
// is called.
class AtomicBufferStream : public stringStream {

 public:
  static const size_t MAX_MESSAGE_LEN = 1*K;

 protected:
outputStream*_fst;

 public:
  AtomicBufferStream(outputStream *st, size_t bufferSize = MAX_MESSAGE_LEN);
  virtual void flush();
};

#endif // LOG_HPP
