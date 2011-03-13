/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OSTREAM_HPP
#define OSTREAM_HPP


#include "resourceArea.hpp"

#ifdef __GNUC__
#define _PRINTF_(x,y) __attribute__ ((format (printf, x, y)))
#else
#define _PRINTF_(x,y)
#endif

// Output streams for printing
// 
// Printing guidelines:
// Where possible, please use tty->print() and tty->print_cr().
// For product mode VM warnings use warning() which internally uses tty.
// In places where tty is not initialized yet or too much overhead,
// we may use jio_printf:
//     jio_fprintf(defaultStream::output_stream(), "Message");
// This allows for redirection via -XX:+DisplayVMOutputToStdout and
// -XX:+DisplayVMOutputToStderr
class outputStream : public ResourceObj {
   friend class Log;

 protected:
   int _indentation; // current indentation
   int _width;       // width of the page
   int _position;    // position on the current line
   int _newlines;    // number of '\n' output so far
   julong _precount; // number of chars output, less _position
   TimeStamp _stamp; // for time stamps

   void update_position(const char* s, size_t len);
   static const char* do_vsnprintf(char* buffer, size_t buflen,
                                   const char* format, va_list ap,
                                   bool add_cr,
                                   size_t& result_len);

 public:
   // creation
   outputStream(int width = 80);
   outputStream(int width, bool has_time_stamps);

   // indentation
   void indent();
   void inc() { _indentation++; };
   void dec() { _indentation--; };
   int  indentation() const    { return _indentation; }
   void set_indentation(int i) { _indentation = i;    }
   void fill_to(int col);

   // sizing
   int width()    const { return _width;    }
   int position() const { return _position; }
   int newlines() const { return _newlines; }
   julong count() const { return _precount + _position; }
   void set_count(julong count) { _precount = count - _position; }
   void set_position(int pos)   { _position = pos; }

   // printing
void print(const char*format,...)_PRINTF_(2,3);
void print_cr(const char*format,...)_PRINTF_(2,3);
void vprint(const char*format,va_list argptr)_PRINTF_(2,0);
void vprint_cr(const char*format,va_list argptr)_PRINTF_(2,0);
   void print_raw(const char* str)            { write(str, strlen(str)); }
   void print_raw(const char* str, int len)   { write(str,         len); }
   void print_raw_cr(const char* str)         { write(str, strlen(str)); cr(); }
   void print_raw_cr(const char* str, int len){ write(str,         len); cr(); }
   void put(char ch);
   void sp();
   void cr();
   void bol() { if (_position > 0)  cr(); }

   virtual size_t size_to_fit()  { ShouldNotReachHere(); return 0; }

   // Time stamp
   TimeStamp& time_stamp() { return _stamp; }
   TimeStamp* get_time_stamp() { return &_stamp; }
   virtual void stamp();
   // Date stamp
   void date_stamp(bool guard, const char* prefix, const char* suffix);
   // A simplified call that includes a suffix of ": "
   void date_stamp(bool guard) {
     date_stamp(guard, "", ": ");
   }

   // portable printing of 64 bit integers
   void print_jlong(jlong value);
   void print_julong(julong value);

   // flushing
   virtual void flush() {}
   virtual void write(const char* str, size_t len) = 0;
   virtual ~outputStream() {}  // close properly on deletion

   void dec_cr() { dec(); cr(); }
   void inc_cr() { inc(); cr(); }
};

// For writing to the GC log file.
class gclogStream:public outputStream{
 private:
  int   _file;
  char *_file_name;
  bool  _append;

 public:
  gclogStream();
  ~gclogStream();

  bool is_open() { return _file >= 0; }
  const char* file_name() const { return _file_name; }
  bool append() const { return _append; }

  void open(const char* file_name, bool append = false);
  virtual void flush();
virtual void write(const char*buf,size_t len);
  void close();
};

// standard output
				// ANSI C++ name collision
extern outputStream* tty;	          // tty output
extern gclogStream*  gclog;         // stream for gc log if -Xloggc:<f>
extern outputStream* gclog_or_tty;  // stream for gc log if -Xloggc:<f>, or tty
extern outputStream* cpilog_or_tty; // stream for cpi log if -XX:TraceCPIFileName, or tty

// advisory locking for the shared tty stream:
class ttyLocker: StackObj {
 private:
  intx _holder;

 public:
  static intx  hold_tty();                // returns a "holder" token
  static void  release_tty(intx holder);  // must witness same token
  static void  break_tty_lock_for_safepoint(intx holder);

  ttyLocker()  { _holder = hold_tty(); }
  ~ttyLocker() { release_tty(_holder); }
};

// for writing to strings; buffer will expand automatically
class stringStream : public outputStream {
 protected:
  char*  buffer;
  size_t buffer_pos;
  size_t buffer_length;
  bool   buffer_fixed;
  bool   _resource;
  ReallocMark _nesting;
 public:
  // If resource is true, the underlying buffer will be allocated as a resource array
  stringStream(bool resource=true, size_t initial_bufsize = 256);
  stringStream(char* fixed_buffer, size_t fixed_buffer_size);
  stringStream(const stringStream* ss);
  void init(bool resource, size_t initialize_size);
  ~stringStream();
  size_t size_to_fit();
  virtual void write(const char* c, size_t len);
  size_t      size() { return buffer_pos; }
  const char* base() { return buffer; }
  void  reset() { buffer_pos = 0; _precount = 0; _position = 0; }
  void set_buf_pos(int bufpos) { buffer_pos = _position = bufpos; _precount = 0; }
  char* as_string();
  char unget() { return buffer_pos ? buffer[buffer_pos--] : 0; }
};

class fileStream : public outputStream {
 protected:
int _file;
  bool  _need_close;
 public:
fileStream(const char*file_name,bool append=false);
fileStream(int file){_file=file;_need_close=false;}
  ~fileStream();
bool is_open()const{return _file>=0;}
  virtual void write(const char* c, size_t len);
  void flush();
};

// unlike fileStream, fdStream does unbuffered I/O by calling
// open() and write() directly. It is async-safe, but output
// from multiple thread may be mixed together. Used by fatal
// error handler.
class fdStream : public outputStream {
 protected:
  int  _fd;
  bool _need_close;
 public:
  fdStream(const char* file_name);
  fdStream(int fd = -1) { _fd = fd; _need_close = false; }
  ~fdStream();
  bool is_open() const { return _fd != -1; }
  void set_fd(int fd) { _fd = fd; _need_close = false; }
  int fd() const { return _fd; }
  virtual void write(const char* c, size_t len);
  void flush() {};
};

void ostream_init();
void ostream_init_log();
void ostream_exit();
void ostream_abort();

// staticBufferStream uses a user-supplied buffer for all formatting.
// Used for safe formatting during fatal error handling.  Not MT safe.
// Do not share the stream between multiple threads.
class staticBufferStream : public outputStream {
 private:
  char* _buffer;
  size_t _buflen;
  outputStream* _outer_stream;
 public:
  staticBufferStream(char* buffer, size_t buflen,
		     outputStream *outer_stream);
  ~staticBufferStream() {};
  virtual void write(const char* c, size_t len);
  void flush();
void print(const char*format,...)_PRINTF_(2,3);
void print_cr(const char*format,...)_PRINTF_(2,3);
void vprint(const char*format,va_list argptr)_PRINTF_(2,0);
void vprint_cr(const char*format,va_list argptr)_PRINTF_(2,0);
};

// In the non-fixed buffer case an underlying buffer will be created and
// managed in C heap. Not MT-safe.
class bufferedStream : public outputStream {
 protected:
  char*  buffer;
  size_t buffer_pos;
  size_t buffer_length;
  bool   buffer_fixed;
 public:
  bufferedStream(size_t initial_bufsize = 256);
  bufferedStream(char* fixed_buffer, size_t fixed_buffer_size);
  ~bufferedStream();
  virtual void write(const char* c, size_t len);
  size_t      size() { return buffer_pos; }
  const char* base() { return buffer; }
  void  reset() { buffer_pos = 0; _precount = 0; _position = 0; }
  char* as_string();
};

#define O_BUFLEN 2000   // max size of output of individual print() methods

#endif // OSTREAM_HPP
