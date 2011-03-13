/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef TIMER_HPP
#define TIMER_HPP

#include "allocation.hpp"

// Timers for simple measurement.

class elapsedTimer VALUE_OBJ_CLASS_SPEC {
 private:
  jlong _counter;
  jlong _start_counter;
  bool  _active;
 public:
  elapsedTimer() : _counter(0), _start_counter(0), _active(false) {}
  void add(elapsedTimer t);
void add(jlong ticks);
  void start();
void start(jlong ticks);
  void stop();
  void reset()               { _counter = 0; }
  double seconds() const;
  jlong milliseconds() const;
  jlong microseconds() const;
  jlong ticks() const        { return _counter; }
  jlong start_ticks() const  { return _start_counter; }
  jlong active_ticks() const;
  bool  is_active() const { return _active; }
};

// TimeStamp is used for recording when an event took place.
class TimeStamp VALUE_OBJ_CLASS_SPEC {
 private:
  jlong _counter;
 public:
TimeStamp():_counter(0){}
  void clear() { _counter = 0; }
  // has the timestamp been updated since being created or cleared?
  bool is_updated() const { return _counter != 0; }
  // update to current elapsed time
  void update();
  // update to given elapsed time
  void update_to(jlong ticks);
  // returns seconds since updated
  // (must not be in a cleared state:  must have been previously updated)
  double seconds() const;
  jlong milliseconds() const;
  // ticks elapsed between VM start and last update
  jlong ticks() const { return _counter; }
  // ticks elapsed since last update
  jlong ticks_since_update() const;
};


class CommonTracer:public StackObj{
 protected:
  bool          _active;    // do timing
  bool          _verbose;   // report every timing
  bool          _print_cr;  // add a CR to the end of the timer report
  bool          _output_at_end; // only log at destruction of tracer
  elapsedTimer  _t;         // timer
  elapsedTimer* _accum;     // accumulator
  outputStream* _logfile;   // output log file

  virtual void  emit_trace() = 0;

  // Constuctors
CommonTracer(const char*title,
               bool doit = true,
               bool print_cr = true,
outputStream*logfile=NULL,
bool output_at_end=false);
CommonTracer(const char*title,
               elapsedTimer* accumulator,
               bool doit = true,
               bool verbose = false,
               outputStream *logfile = NULL );

  // Destructor
  void CommonDestructor();

 public:
  // Accessors
  void set_verbose(bool verbose)  { _verbose = verbose; }
  bool verbose() const            { return _verbose;    }

  // Activation
  void suspend()  { if (_active) _t.stop();  }
  void resume()   { if (_active) _t.start(); }
};

class TraceCPUTime: public StackObj {
 private:
  bool _active;			// true if times will be measured and printed
  bool _print_cr;		// if true print carriage return at end
  double _starting_user_time;	// user time at start of measurement
  double _starting_system_time; // system time at start of measurement
  double _starting_real_time;	// real time at start of measurement
  outputStream* _logfile;	// output is printed to this stream
  bool _error;			// true if an error occurred, turns off output

 public:
  TraceCPUTime(bool doit = true, 
	       bool print_cr = true, 
	       outputStream *logfile = NULL);
  ~TraceCPUTime();
};

// TraceTime is used for tracing the execution time of a block
// Usage:
//  { TraceTime t("block time")
//    some_code();
//  }
//

class TraceTime:public CommonTracer{
 protected:
  void emit_trace();

 public:
  // Constuctors
  TraceTime(const char* title,
	    bool doit = true,
	    bool print_cr = true,
	    outputStream *logfile = NULL,
            bool output_at_end = false) : CommonTracer(title, doit, print_cr, logfile, output_at_end) {}
  TraceTime(const char* title,
	    elapsedTimer* accumulator,
	    bool doit = true,
            bool verbose = false,
	    outputStream *logfile = NULL ) : CommonTracer(title, accumulator, doit, verbose, logfile) {}
  ~TraceTime() { CommonDestructor(); }
};


class GCLogMessage:public AllStatic{
  public:
    static void initialize();
    static void print_warning_summary_xml(azprof::Request*, azprof::Response*);
    static void print_warning_history_xml(azprof::Request*, azprof::Response*);

    static void log_a(bool active, outputStream* logfile, bool indent, const char* format, ...);
    static void log_b(bool active, outputStream* logfile, const char* format, ...);
    static void warn(bool active, outputStream* logfile, const char* format, ...);

  private:
    // Ring buffer which stores a history of GC warnings for display through
    // ARTA.
    static char **_warning_history;
    static size_t _warning_count;
};


class TimeDetailTracer:public StackObj{
  protected:
bool _active;//Log time messages
bool _verbose;//Log time details
    bool          _details; // There will be DetailTracers (when verbose)
outputStream*_logfile;//Destination for log messages

elapsedTimer _t;
elapsedTimer _pause;
elapsedTimer _safepoint;
    bool          _ended;

  public:
    void initial_message(bool active, bool verbose, bool details, bool no_flush,
                         outputStream *logfile, const char* format, va_list ap);

    TimeDetailTracer(bool active, bool verbose, bool details, outputStream *logfile, const char* format, ...);
    TimeDetailTracer(bool active, bool verbose, bool details, bool no_flush, outputStream *logfile, const char* format, ...);
    ~TimeDetailTracer();

    outputStream* stream()  { return _logfile;  }
    bool          active()  { return _active;  }
    bool          verbose() { return _verbose; }
    bool          details() { return _verbose && _details; }

    void start_pause();
    void start_pause(long ticks);
    void add_safepoint_ticks(long ticks);
    void start_safepointing();
    void end_safepointing();
    void end_pause();

    jlong pause_start_ticks() { return _pause.start_ticks(); }
    jlong pause_ticks()   { return _pause.ticks(); }

    // Logs message only if active:
void print(const char*format,...);

    // Log a detail on it's own line (when details active):
void detail(const char*format,...);

    // Used to finish the timing log prior to the destructor being called:
void end_log(const char*format,...);
};


class DetailTracer:public StackObj{
  protected:
bool _active;//Log time messages
    bool          _details; // There will be details within the tracer (when active)
    long          _indent;
outputStream*_logfile;
    elapsedTimer  _t;

    bool          _print_idle;
    double        _longest_idle;
    double        _average_idle;

void start_log(const char*format,va_list ap);

  public:
    DetailTracer(TimeDetailTracer* tdt, bool details, const char* format, ...);
    DetailTracer(DetailTracer*      dt, const char* format, ...);
    ~DetailTracer();

    void          set_idle(double longest_idle, double average_idle) {
                                                                       _print_idle   = true;
                                                                       _longest_idle = longest_idle;
                                                                       _average_idle = average_idle;
                                                                     }

    bool          active() { return _active;  }
    long          indent() { return _indent;  }
    outputStream* stream() { return _logfile; }

    // Logs message only if active:
void print(const char*format,...);

    // Log a detail on it's own line (when active):
void detail(const char*format,...);
};

#endif // TIMER_HPP
