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


#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

void elapsedTimer::add(elapsedTimer t) {
  _counter += t._counter;
}

void elapsedTimer::add(jlong ticks) {
_counter+=ticks;
}

void elapsedTimer::start() {
  if (!_active) {
    _active = true;
    _start_counter = os::elapsed_counter();
  }
}

void elapsedTimer::start(jlong ticks) {
  if (!_active) {
    _active = true;
_start_counter=ticks;
  }
}

void elapsedTimer::stop() {
  if (_active) {
    _counter += os::elapsed_counter() - _start_counter;
    _active = false;
  }
}

double elapsedTimer::seconds() const {
  double count = (double) _counter;
  double freq  = (double) os::elapsed_frequency();
  return count/freq;
}

jlong elapsedTimer::milliseconds() const {
  jlong ticks_per_ms = os::elapsed_frequency() / 1000;
  return _counter / ticks_per_ms;
}

jlong elapsedTimer::microseconds()const{
  return _counter*1000000 / os::elapsed_frequency();
}

jlong elapsedTimer::active_ticks() const {
  if (!_active) {
    return ticks();
  }
  jlong counter = _counter + os::elapsed_counter() - _start_counter; 
  return counter;
}

void TimeStamp::update_to(jlong ticks) {
  _counter = ticks;
  if (_counter == 0)  _counter = 1;
  assert(is_updated(), "must not look clear");
}

void TimeStamp::update() { 
  update_to(os::elapsed_counter());
}

double TimeStamp::seconds() const {
  assert(is_updated(), "must not be clear");
  jlong new_count = os::elapsed_counter();
  double count = (double) new_count - _counter;
  double freq  = (double) os::elapsed_frequency();
  return count/freq;
}

jlong TimeStamp::milliseconds() const {
  assert(is_updated(), "must not be clear");

  jlong new_count = os::elapsed_counter();
  jlong count = new_count - _counter;
  jlong ticks_per_ms = os::elapsed_frequency() / 1000;
  return count / ticks_per_ms;
}

jlong TimeStamp::ticks_since_update() const {
  assert(is_updated(), "must not be clear");
  return os::elapsed_counter() - _counter;
}

CommonTracer::CommonTracer(const char* title,
		           bool doit,
		           bool print_cr,
outputStream*logfile,
bool output_at_end){
  _active   = doit;
  _verbose  = true;
  _print_cr = print_cr;
  _logfile = (logfile != NULL) ? logfile : tty;

  if (_active) {
    _accum = NULL;
    if (PrintGCTimeStamps) {
      _logfile->stamp();
      _logfile->print(": ");
    }
    _logfile->print("[%s", title);
    if ( ! output_at_end ) {
      _logfile->flush();
    }
    _t.start();
  }
}

CommonTracer::CommonTracer(const char* title,
		           elapsedTimer* accumulator,
		           bool doit,
		           bool verbose,
		           outputStream* logfile) {
  _active = doit;
  _verbose = verbose;
  _print_cr = true;
  _logfile = (logfile != NULL) ? logfile : tty;
  if (_active) {
    if (_verbose) {
      if (PrintGCTimeStamps) {
	_logfile->stamp();
	_logfile->print(": ");
      }
      _logfile->print("[%s", title);
      _logfile->flush();
    }
    _accum = accumulator;
    _t.start();
  }
}

void CommonTracer::CommonDestructor() {
  if (_active) {
    _t.stop();
    if (_accum!=NULL) _accum->add(_t);
    if (_verbose) {
      emit_trace();
      _logfile->flush();
    }
  }
}

void TraceTime::emit_trace() {
  if (_print_cr) {
    _logfile->print_cr(", %3.7f secs]", _t.seconds());
  } else {
    _logfile->print(", %3.7f secs]", _t.seconds());
  }
}

char **GCLogMessage::_warning_history = NULL;
size_t GCLogMessage::_warning_count = 0;

void GCLogMessage::initialize(){
MutexLocker ml(GcHistory_lock);
  if (GCWarningHistory > 0) {
    _warning_history = NEW_C_HEAP_ARRAY(char*, GCWarningHistory);
    memset(_warning_history, 0, sizeof(char*) * GCWarningHistory);
  }
}

void GCLogMessage::print_warning_summary_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
assert_lock_strong(GcHistory_lock);
  azprof::Xml::leaf(res, "gc-warning-count", (int64_t) _warning_count);
#endif // AZ_PROFILER
}

void GCLogMessage::print_warning_history_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
assert_lock_strong(GcHistory_lock);
  if (_warning_history) {
    size_t position, length;
    if (_warning_count < GCWarningHistory) {
      position = 0;
length=_warning_count;
    } else {
      position = _warning_count % GCWarningHistory;
length=GCWarningHistory;
    }
    azprof::Xml tag(res, "gc-warning-history");
    azprof::Xml::leaf(res, "count", (int64_t) _warning_count);
    azprof::Xml::leaf(res, "length", (int64_t) length);
res->indent();
res->print("<content>");
    if (_warning_count > 0) {
res->print("<![CDATA[");
      for (size_t k = 0; k < length; k++) {
        char *msg = _warning_history[(position + k) % GCWarningHistory];
size_t len=strlen(msg);
        res->write(msg, len);
      }
res->print("]]>");
    }
res->print("</content>\n");
  }
#endif // AZ_PROFILER
}

void GCLogMessage::log_a(bool active, outputStream *logfile, bool indent, const char* format, ...)
{
  if (active) {
    if (PrintGCTimeStamps) {
      logfile->stamp();
      logfile->print(": ");
    }

    if (indent) {
logfile->print("    ");
    }

logfile->print("[");

    va_list ap;
    va_start(ap, format);
logfile->vprint(format,ap);
    va_end(ap);

logfile->print_cr("]");
    logfile->flush();
  }
}

void GCLogMessage::log_b(bool active, outputStream *logfile, const char* format, ...)
{
  if (active) {
    if (PrintGCTimeStamps) {
      logfile->stamp();
      logfile->print(": ");
    }

logfile->print("[");

    va_list ap;
    va_start(ap, format);
logfile->vprint(format,ap);
    va_end(ap);

logfile->print_cr("]");
    logfile->flush();
  }
}

void GCLogMessage::warn(bool active, outputStream* logfile, const char* format, ...) {
  if (active || _warning_history) {
    char buf[1024];
char*msg=buf;
char*ptr=buf;
    char *end = buf + sizeof(buf);

    // Don't get the 'TimeStamp' from 'logfile' since it may be a 'LogBufferStream'.
    TimeStamp& ts = gclog_or_tty->time_stamp();
    if (!ts.is_updated()) ts.update();
    ptr += snprintf(ptr, end - ptr, "%.3f: ", ts.seconds());

    // Always timestamp the message in the history but only timestamp the log
    // message if 'PrintGCTimeStamps' is used.
    char *log = PrintGCTimeStamps ? msg : ptr;

if(ptr<end){
      ptr += snprintf(ptr, end - ptr, "[");
if(ptr<end){
        va_list ap;
        va_start(ap, format);
        ptr += vsnprintf(ptr, end - ptr, format, ap);
        va_end(ap);
if(ptr<end){
          ptr += snprintf(ptr, end - ptr, "]\n");
        }
      }
    }

    if (active) logfile->write(log, strlen(log));

    if (_warning_history) {
      size_t msglen = strlen(msg);
      char *msgcpy = NEW_C_HEAP_ARRAY(char, msglen+1);
      memcpy(msgcpy, msg, msglen);
      msgcpy[msglen] = '\0';

MutexLocker ml(GcHistory_lock);
      size_t i = (_warning_count++) % GCWarningHistory;
      char *oldmsg = _warning_history[i];
      _warning_history[i] = msgcpy;
FREE_C_HEAP_ARRAY(char,oldmsg);
    }
  }
}

void TimeDetailTracer::initial_message(bool active, bool verbose, bool details, bool no_flush,
                                       outputStream *logfile, const char* format, va_list ap)
{
_active=active;
  _verbose = verbose;
  _details = details;
  _logfile = logfile;

  _ended = false;

  if (_active) {
_t.start();

    if (PrintGCTimeStamps) {
      _logfile->stamp();
      _logfile->print(": ");
    }

_logfile->print("[");

    if (this->details()) {
_logfile->vprint_cr(format,ap);
    } else {
_logfile->vprint(format,ap);
    }

    if ( ! no_flush ) {
      _logfile->flush();
    }
  }
}


TimeDetailTracer::TimeDetailTracer(bool active, bool verbose, bool details, outputStream *logfile, const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  initial_message(active, verbose, details, false, logfile, format, ap);
  va_end(ap);
}

TimeDetailTracer::TimeDetailTracer(bool active, bool verbose, bool details, bool no_flush,
                                   outputStream *logfile, const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  initial_message(active, verbose, details, no_flush, logfile, format, ap);
  va_end(ap);
}


void TimeDetailTracer::start_pause()
{
_pause.start();
}


void TimeDetailTracer::start_pause(long ticks)
{
  _pause.start(ticks);
}


void TimeDetailTracer::add_safepoint_ticks(long ticks)
{
  _safepoint.add(ticks);
}


void TimeDetailTracer::start_safepointing()
{
_safepoint.start();
}


void TimeDetailTracer::end_safepointing()
{
_safepoint.stop();
}


void TimeDetailTracer::end_pause()
{
_pause.stop();
}


void TimeDetailTracer::print(const char* format, ...)
{
  if ( _active ) {
    va_list ap;
    va_start(ap, format);
_logfile->vprint(format,ap);
    va_end(ap);
  }
}


void TimeDetailTracer::detail(const char* format, ...)
{
  if ( details() ) {
    if (PrintGCTimeStamps) {
      _logfile->stamp();
      _logfile->print(": ");
    }
_logfile->print("    ");

    va_list ap;
    va_start(ap, format);
_logfile->vprint_cr(format,ap);
    va_end(ap);

    _logfile->flush();
  }
}


void TimeDetailTracer::end_log(const char* format, ...)
{
  if (_active && !_ended) {
    if (details()) {
      if (PrintGCTimeStamps) {
        _logfile->stamp();
        _logfile->print(": ");
      }
_logfile->print("  ");
    }

    va_list ap;
    va_start(ap, format);
_logfile->vprint(format,ap);
    va_end(ap);

    _t.stop();

    if ( _verbose && _safepoint.ticks()>0 ) {
_logfile->print_cr(", %3.7f secs (pause %3.7f with %3.7f safepoint overhead)]",
                         _t.seconds(), _pause.seconds(), _safepoint.seconds());
    } else {
_logfile->print_cr(", %3.7f secs]",_t.seconds());
    }
    _logfile->flush();

    _ended = true;
  }
}


TimeDetailTracer::~TimeDetailTracer()
{
  end_log("");
}


DetailTracer::DetailTracer(TimeDetailTracer* tdt, bool details, const char* format, ...)
{
  _active     = tdt->verbose();
  _details    = details;
  _indent     = 1;
  _logfile    = tdt->stream();
  _print_idle = false;

  va_list ap;
  va_start(ap, format);
start_log(format,ap);
  va_end(ap);
}


DetailTracer::DetailTracer(DetailTracer* dt, const char* format, ...)
{
  _active     = dt->active();
  _indent     = dt->indent() + 1;
  _logfile    = dt->stream();
  _print_idle = false;

  va_list ap;
  va_start(ap, format);
start_log(format,ap);
  va_end(ap);
}


void DetailTracer::start_log(const char* format, va_list ap)
{
  if ( _active ) {
_t.start();

    if (PrintGCTimeStamps) {
      _logfile->stamp();
      _logfile->print(": ");
    }

    for ( long i=0; i<_indent; i++ ) {
_logfile->print("    ");
    }

_logfile->print("[");

    if (_details) {
_logfile->vprint_cr(format,ap);
    } else {
_logfile->vprint(format,ap);
    }
  }
}


void DetailTracer::print(const char* format, ...)
{
  if ( _active ) {
    va_list ap;
    va_start(ap, format);
_logfile->vprint(format,ap);
    va_end(ap);
  }
}


void DetailTracer::detail(const char* format, ...)
{
  if ( _active ) {
    if (PrintGCTimeStamps) {
      _logfile->stamp();
      _logfile->print(": ");
    }
    for ( long i=0; i<=_indent; i++ ) {
_logfile->print("    ");
    }

    va_list ap;
    va_start(ap, format);
_logfile->vprint_cr(format,ap);
    va_end(ap);
  }
}


DetailTracer::~DetailTracer()
{
  if (_active) {
    if (_details) {
      if (PrintGCTimeStamps) {
        _logfile->stamp();
        _logfile->print(": ");
      }
      for ( long i=0; i<_indent; i++ ) {
_logfile->print("    ");
      }
_logfile->print("  ");
    }

    _t.stop();

    if (_print_idle) {
      _logfile->print_cr(", (idle %.3f %.3f) %3.7f secs]", _longest_idle, _average_idle, _t.seconds());
    } else {
_logfile->print_cr(", %3.7f secs]",_t.seconds());
    }
    _logfile->flush();
  }
}

TraceCPUTime::TraceCPUTime(bool doit, 
	       bool print_cr, 
	       outputStream *logfile) :
  _active(doit),
  _print_cr(print_cr),
  _starting_user_time(0.0), 
  _starting_system_time(0.0), 
  _starting_real_time(0.0), 
  _logfile(logfile),
  _error(false) {
  if (_active) {
    if (logfile != NULL) {
      _logfile = logfile;
    } else {
      _logfile = tty;
    }

    _error = !os::getTimesSecs(&_starting_real_time, 
			       &_starting_user_time, 
			       &_starting_system_time);    
  }
}

TraceCPUTime::~TraceCPUTime() {
  if (_active) {
    bool valid = false;
    if (!_error) {
      double real_secs;  		// walk clock time
      double system_secs;		// system time
      double user_secs;			// user time for all threads

      double real_time, user_time, system_time;
      valid = os::getTimesSecs(&real_time, &user_time, &system_time);
      if (valid) {

	user_secs = user_time - _starting_user_time;
	system_secs = system_time - _starting_system_time;
	real_secs = real_time - _starting_real_time;

	_logfile->print(" [Times: user=%3.2f sys=%3.2f, real=%3.2f secs] ",
	  user_secs, system_secs, real_secs);
	  
      } else {
        _logfile->print("[Invalid result in TraceCPUTime]");
      }
    } else {
      _logfile->print("[Error in TraceCPUTime]");
    }
     if (_print_cr) {
_logfile->cr();
    }
  }
}
