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


#include "allocation.hpp"
#include "array.hpp"
#include "atomic.hpp"
#include "codeCache.hpp"
#include "disassembler_pd.hpp"
#include "globals_extension.hpp"
#include "growableArray.hpp"
#include "hashtable.hpp"
#include "interpreter_pd.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "stubCodeGenerator.hpp"
#include "tickProfiler.hpp"
#include "vmTags.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#ifdef AZ_PROFILER
#include <azprof/azprof_demangle.hpp>
#include <azprof/azprof_debug.hpp>
#endif // AZ_PROFILER

#include <errno.h>
#include <os/profile.h>
#include <os/thread.h>
#include <stdio.h>

// Declarations for externally invisible functions
namespace {
void init_tick_timer(Thread*thread);
void stop_tick_timer(Thread*thread);
    void copy_thread_local_buffer(ProfileEntry* tick_tlb, int32_t next_tlb_index);
    void register_handler_callbacks();
void init_thread_local_buffer(Thread*thread);
    void signal_handler_perfcnt0(int sig, siginfo_t *info, void *context);
    void signal_handler_ttsp(int sig, siginfo_t *info, void *context);
    ProfileEntry* get_profile_entry();
    void flush_all_tick_profile_buffers();
    void flush_thread_local_buffer();
    void thread_death_callback(intptr_t);

    typedef struct itimerspec itimerspec_t;
    const int signal_number = SIGRTMAX - 2;
    const int32_t THREAD_DYING = -1;
    const int32_t NUM_PROFILE_ENTRIES = 128; // TODO: calculate this properly

    // Count of how many ticks we received while we aren't on the
    // alternate stack (i.e. while we're on the pthreads stack)
int32_t non_alt_stack_ticks=0;
    // Count of signals incorrectly received by our signal handler.
int32_t misdelivered_tick_profiling_signals=0;
    // Count of successfully handled ticks
    uint64_t successfully_received_ticks = 0ull;
    // Tick scale value of ProfileTimeToSafepointMicros
    jlong ProfileTimeToSafepointTicks = 0;
}

namespace tickprofiling 
{
    /**
     * This is the top level initialization function for tick
     * profiling. It creates the tick profiling singleton and
     * registers callbacks for profiling and thread creation/deletion.
     */
    void init() {
	// initialize TickProfiler singleton
TickProfiler::init();

	// register tick profiling signal handler 
	register_handler_callbacks();

	// register thread creation/deletion callbacks
	// TODO: turn on tick profiling for existing threads
	thread_death_callback_register(thread_death_callback);
	thread_start_callback_register(tickprofiling::thread_init_callback);
    }

    // libos calls us back here when a new thread gets kicked off
    void thread_init_callback(intptr_t rsp) {
	assert0(rsp == (intptr_t) (rsp & ~(thread_stack_size-1)));
	Thread* thread = reinterpret_cast<Thread*>(rsp); 
init_tick_timer(thread);
    }
}

namespace {
    /**
     * Libos calls us back here when a thread dies. From here, we just
     * jump to a routine that deletes the timer and flushes the thread
     * local buffer to the global buffer.
     */
    void thread_death_callback(intptr_t rsp) {
	Thread* thread = reinterpret_cast<Thread*> (rsp);
	if (UseTickProfiler)
stop_tick_timer(thread);
    }

    /**
     * Create an individual thread's CPU time timer.
     */
void init_tick_timer(Thread*thread){
	// TODO: this is Linux specific code that shouldn't live here.
	int err = 0;
	
	// Set up pointers to thread local 8k buffer
init_thread_local_buffer(thread);

	// Set up data structures for timer
itimerspec_t it;
	::memset(&it, 0, sizeof(itimerspec_t));
	it.it_interval.tv_nsec = it.it_value.tv_nsec = TickProfilerFrequency * 1000;
sigevent_t event;
	::memset(&event, 0, sizeof(event));
	event.sigev_notify = SIGEV_THREAD_ID;
	// is the following line necessary? i'm guessing no.
        event._sigev_un._tid = thread_gettid();
	// add user data to event struct
	event.sigev_signo = signal_number;
	// send the timer id to the signal handler
	event.sigev_value.sival_ptr = &(thread->_profiling_timer_id);

	err = ::timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &(thread->_profiling_timer_id));
	if (err) {
tty->print_cr("timer_create failed");
	    return;
	}
    
	err = ::timer_settime(thread->_profiling_timer_id, 0, &it, NULL);
	if (err) {
tty->print_cr("timer_settime failed");
	    return;
	}
    }

void stop_tick_timer(Thread*thread){
	// TODO: this is Linux specific code that shouldn't live here.

	// Turn off the timer, and clean up the thread local buffer.
	if (thread->_profiling_timer_id) {
	    ::timer_delete(thread->_profiling_timer_id);
	    // Save off the current index so we can flush it
	    intptr_t index = thread->_next_profiling_entry_index;
	    // Store the magic value into the Thread's index atomically in
	    // case any new ticks come in.
	    Atomic::store(THREAD_DYING, &thread->_next_profiling_entry_index);
	    copy_thread_local_buffer(thread->_tick_profiling_buffer, index);
	}
    }

    /**
     * Retrieves and zeros a new profile entry. We normally return one
     * from the thread local buffer unless the thread is dying. This
     * is also where we flush the thread local buffer to the global
     * buffer when it's full.
     */
    ProfileEntry* get_profile_entry() {
ProfileEntry*entry=NULL;
	Thread* thread = Thread::current();

	if (thread->_next_profiling_entry_index != THREAD_DYING) {
	    if (thread->_next_profiling_entry_index >= NUM_PROFILE_ENTRIES) {
		// Local buffer is now full. Flush it.
		flush_thread_local_buffer();
	    }
	    int32_t index = thread->_next_profiling_entry_index;
	    entry = &(thread->_tick_profiling_buffer[index]);
thread->_next_profiling_entry_index++;
	}
	else {
	    // Stick this one in the global since we're dying.
	    entry = TickProfiler::global_entry();
	}
return entry->clz();
    }

    /**
     * Flush entries from the thread local buffer into the global tick
     * buffer and reset the thread local buffer index.
     */
    void flush_thread_local_buffer() {
	Thread* thread = Thread::current();
	ProfileEntry* tick_tlb = thread->_tick_profiling_buffer;
	int32_t next_tlb_index = thread->_next_profiling_entry_index;
	copy_thread_local_buffer(tick_tlb, next_tlb_index);
	thread->_next_profiling_entry_index = 0;
    }

    /**
     * This function does the actual copying from the thread local
     * buffer into the global buffer.
     */
    void copy_thread_local_buffer(ProfileEntry* tick_tlb, int32_t next_tlb_index) {
	if (next_tlb_index > 0) {
	    const int32_t buflen = next_tlb_index;
	    // Increment the global buffer end index so we have space
	    // to copy the ticks into the buffer, then mask off the
	    // index with the size for the ring buffer.
	    intptr_t global_buffer_end_index = TickProfiler::increment_index(buflen);
	    if (buflen <= global_buffer_end_index) {
		intptr_t global_buffer_start_index = global_buffer_end_index - buflen;
		ProfileEntry* global_buffer = &(TickProfiler::instance()->entries()[global_buffer_start_index]);
		::memcpy(global_buffer, tick_tlb, sizeof(ProfileEntry) * buflen);
	    } else {
		// The global tick ring buffer wraps back to the
		// beginning. First, copy however many ticks that will
		// fit at the end of the global buffer from the
		// beginning of the thread local buffer.
		int num_entries = buflen - global_buffer_end_index;
		intptr_t global_buffer_start_index = TickProfilerEntryCount - num_entries;
		ProfileEntry* global_buffer = &(TickProfiler::instance()->entries()[global_buffer_start_index]);
		::memcpy(global_buffer, tick_tlb, sizeof(ProfileEntry) * num_entries);
		// Next, copy the remaining ticks from the thread
		// local buffer into the beginning of the global
		// buffer. 
		int local_buffer_idx = num_entries;
		num_entries = buflen - num_entries;
		global_buffer = TickProfiler::instance()->entries();
		::memcpy(global_buffer, &tick_tlb[local_buffer_idx], 
			 sizeof(ProfileEntry) * num_entries);
	    }
	}
    }

    void register_handler_callbacks() {
      // HACK - TODO: make this live happily with chained exception handling.
      //        should call exception_register_direct_handler() in exception.c

sigset_t sigset;
sigfillset(&sigset);
      sigdelset(&sigset,SIGSEGV); // don't block SEGV's when in tickhandler
      struct sigaction sa;
      ::memset(&sa, 0, sizeof(sa));
      // Depending on the type of tick profiling we're doing, we enable a different
      // signal handler.  This is done so that we don't need a lot of option testing
      // in the signal handlers, and we're interested in having tick profile signals
      // be handled as quickly as possible.
      if ( TickProfileTimeToSafepoint ) {
        sa.sa_sigaction = signal_handler_ttsp;
        // Convert ProfileTimeToSafepointMicros into ticks for fast comparisons later
        ProfileTimeToSafepointTicks = ProfileTimeToSafepointMicros * os::elapsed_frequency() / 1000000;
      } else { 
        sa.sa_sigaction = signal_handler_perfcnt0;
      }
sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_SIGINFO | SA_RESTART;
      int ret = ::sigaction(signal_number, &sa, NULL);
      debug_only(int err = errno);
      assert0(ret == 0);
    }

void init_thread_local_buffer(Thread*thread){
	ProfileEntry* pe_buf = reinterpret_cast<ProfileEntry*> (thread);
	pe_buf += thread_map_arta_buf / sizeof (ProfileEntry);
	thread->_tick_profiling_buffer = pe_buf;
	thread->_next_profiling_entry_index = 0;
    }

    /**
     * Our thread CPU time timer lets us know when a thread has run
     * for a certain amount of time by delivering a signal to this
     * handler. 
     */

    // This is the signal handler installed when we're using conventional
    // percnt0 timer ticks to profile code execution time.
    void signal_handler_perfcnt0(int signum, siginfo_t* si, void* ucx) {
	// TODO: this is Linux specific code that shouldn't live here.
ucontext_t*uc=(ucontext_t*)ucx;
	uint64_t epc = uc->uc_mcontext.gregs[REG_RIP];
	uint64_t esp = uc->uc_mcontext.gregs[REG_RSP];
	uint64_t efp = uc->uc_mcontext.gregs[REG_RBP];
	uint64_t eva = (intptr_t)si->si_addr;

	// Bug 25156: Fix for the case where the tick profiler is
	// trying to profile a thread that's exiting. We check to be
	// certain that the thread is in the alternate stack (and
	// thus, the thread local profile buffer is still intact) by
	// checking to see whether the stack pointer is in the range
	// of the AVM stack area. If not then the thread is a pthread
	// that's exiting.

	if (esp <  __THREAD_STACK_REGION_START_ADDR__ || 
	    esp >= __THREAD_STACK_REGION_END_ADDR__ ) {
	    ++non_alt_stack_ticks;
	    return;
	}

	// Ensure that we are profiling the correct thread by comparing the timer id
	// we stored when we set the timer up.
	if (si->si_value.sival_ptr != &(Thread::current()->_profiling_timer_id)) {
	    ++misdelivered_tick_profiling_signals;
	    return;
	}

	TickProfiler::perfcnt0_tick((intptr_t)uc, epc, esp, efp, eva);
	++successfully_received_ticks;
    }

    // This is the signal handler installed when we're using timer ticks to
    // profile time-to-safepoint delays.
    void signal_handler_ttsp(int signum, siginfo_t* si, void* ucx) {
	// TODO: this is Linux specific code that shouldn't live here.
ucontext_t*uc=(ucontext_t*)ucx;
	uint64_t epc = uc->uc_mcontext.gregs[REG_RIP];
	uint64_t esp = uc->uc_mcontext.gregs[REG_RSP];
	uint64_t efp = uc->uc_mcontext.gregs[REG_RBP];
	uint64_t eva = (intptr_t)si->si_addr;

	// Bug 25156: Fix for the case where the tick profiler is
	// trying to profile a thread that's exiting. We check to be
	// certain that the thread is in the alternate stack (and
	// thus, the thread local profile buffer is still intact) by
	// checking to see whether the stack pointer is in the range
	// of the AVM stack area. If not then the thread is a pthread
	// that's exiting.

	if (esp <  __THREAD_STACK_REGION_START_ADDR__ || 
	    esp >= __THREAD_STACK_REGION_END_ADDR__ ) {
	    ++non_alt_stack_ticks;
	    return;
	}

	// Ensure that we are profiling the correct thread by comparing the timer id
	// we stored when we set the timer up.
	if (si->si_value.sival_ptr != &(Thread::current()->_profiling_timer_id)) {
	    ++misdelivered_tick_profiling_signals;
	    return;
	}

	TickProfiler::ttsp_tick((intptr_t)uc, epc, esp, efp, eva);
	++successfully_received_ticks;
    }

    /**
     * Flush all thread local buffers to the global buffer.
     */
    void flush_all_tick_profile_buffers() {
	// HACK TODO FAIL: this only works for Java Threads,
	// obviously. It probably needs to work for non-Java threads
	// as well.
	// NOTE: this looks barely used. is it necessary?
	MutexLockerAllowGC mu(&Threads_lock, 1);
	for (JavaThread* thread = Threads::first(); thread; thread = thread->next()) {
	    copy_thread_local_buffer(thread->_tick_profiling_buffer, 
				     thread->_next_profiling_entry_index);
	    thread->_next_profiling_entry_index = 0;
	}
    }

}

void ProfileEntry::print_xml_on(xmlBuffer *xb) {
    xmlElement xe(xb, "profile-entry-types");
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::perfcnt0_tick);
	xb->name_value_item("type", "HW");
	xb->name_value_item("number", "0");
	xb->name_value_item("count", TickProfilerFrequency);
	xb->name_value_item("control", TickProfilerControlWord);
    }
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::perfcnt1_tick);
	xb->name_value_item("type", "HW");
	xb->name_value_item("number", "1");
	xb->name_value_item("count", TickProfilerCount1);
	xb->name_value_item("control", TickProfilerControlWord1);
    }
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::perfcnt4_tick);
	xb->name_value_item("type", "HW");
	xb->name_value_item("number", "4");
	xb->name_value_item("count", TickProfilerCount4);
	xb->name_value_item("control", TickProfilerControlWord4);
    }
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::perfcnt5_tick);
	xb->name_value_item("type", "HW");
	xb->name_value_item("number", "5");
	xb->name_value_item("count", TickProfilerCount5);
	xb->name_value_item("control", TickProfilerControlWord5);
    }
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::tlb0_tick);
	xb->name_value_item("type", "SW TLB");
	xb->name_value_item("number", "0");
	xb->name_value_item("count", TickProfilerCountTLB);
    }
    { xmlElement xe(xb, "profile-entry-type");
	xb->name_value_item("id", ProfileEntry::tlb1_tick);
	xb->name_value_item("type", "SW TLB");
	xb->name_value_item("number", "1");
	xb->name_value_item("count", TickProfilerCountTLB1);
    }
}

// The default profile recording the performance counter events.
TickProfiler* TickProfiler::_profile = NULL;
bool          TickProfiler::_can_get_thread_cpu_time = false;

ProfileEntry* ProfileEntry::allocate()
{
  size_t n = sizeof(ProfileEntry);
  void* ptr = NEW_C_HEAP_ARRAY(char, n + (BytesPerCacheLine-1));
  ProfileEntry* entry = (ProfileEntry*)((((intptr_t) ptr) + (BytesPerCacheLine-1)) & ~(BytesPerCacheLine-1));
  entry->clz();
  return entry;
}

// Allocate the buffer so that it's cache aligned.
static ProfileEntry *aligned_entries() {
    size_t n = sizeof(ProfileEntry) * TickProfilerEntryCount;
    void *ptr = NEW_C_HEAP_ARRAY(char, n + (BytesPerCacheLine-1));
    ProfileEntry *entries = (ProfileEntry*)((((intptr_t) ptr) + (BytesPerCacheLine-1)) & ~(BytesPerCacheLine-1));
    memset(entries, 0, n);
    return entries;
}

TickProfiler::TickProfiler() : _next_idx(0), _entries(aligned_entries()) {
    guarantee(is_power_of_2(TickProfilerEntryCount), "TickProfilerEntryCount must be a power of 2");

    assert0((((intptr_t) _entries) & (BytesPerCacheLine-1)) == 0);
}

void TickProfiler::slave_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva) {
    Thread* thread = Thread::current();

    bool safep_suspend = thread->has_safep_suspend();
    if (!safep_suspend && OnlySafepointRequestTicks) return;

    Unimplemented();
}

void TickProfiler::perfcnt0_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva) {
    Thread* thread = Thread::current();

    bool safep_suspend = thread->has_safep_suspend();
    if (!safep_suspend && OnlySafepointRequestTicks) return;

jlong tstamp=os::elapsed_counter();

    UserProfileEntry* upe = (UserProfileEntry*) get_profile_entry();
    if (upe) {
	const pid_t thread_id = thread_gettid();
	upe->set_values(
			ProfileEntry::perfcnt0_tick,
			(os::elapsed_counter() & ProfileEntry::event_mask),
			thread_id,
			safep_suspend ? 1 : 0,
			(intptr_t) epc,
			esp, efp,
			thread->is_Complete_Thread() ? thread->vm_tag() : unknownThread_tag,
			tstamp
			);
    } else {
	// no place to store this tick yet, so drop it on the floor
	return;
    }
}

// When Time-To-Safepoint profiling, this method saves a pending UserProfileEntry into the tick buffer
// after it has been established that the PrfileTimeToSafepointMicros threshold has been exceeded.
void TickProfiler::record_ttsp_tick(JavaThread*jt){
  UserProfileEntry* upe = (UserProfileEntry*) get_profile_entry();

  if ( ! upe ) {
    // No place to store the profile entry, so drop it on the floor.
    return;
  }

  memcpy(upe, jt->ttsp_profile_entry(), sizeof(UserProfileEntry));
}

// Time-to-safepoint tick handling.
// 
// When the profiling timer triggers, we create a UserProfileEntry, capture the local time, and then set
// a please_self_suspend flag.  When the JavaThread hits a safepoint, it calculates the time delta, and
// if it exceeds ProfileTimeToSafepointMicros microseconds, the UserProfile entry created here is pushed
// into the profile buffers so that it will be displayed.
void TickProfiler::ttsp_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva) {
    Thread* thread = Thread::current();

if(!thread->is_Complete_Java_thread())return;

    JavaThread* jt = (JavaThread*) thread;

    if ( ! jt->jvm_locked_by_self() ) {
      // Threads that are already at a safepoint can't be slow getting to one.  Ignore the tick.
      return;
    }

    int self_suspend_flags = jt->please_self_suspend();
    bool ttsp_suspend = (self_suspend_flags & JavaThread::ttsp_suspend) != 0;

    if ( ttsp_suspend ) {
      // Apparently this thread is taking so long to reach a safepoint that it already has a pending
      // time-to-safepoint suspend request on it.  If it's been pending longer than the profile threshold,
      // log the prior tick before we start a new one.  Otherwise ignore the new tick just received.
jlong tstamp=os::elapsed_counter();
      jlong tick_delta = tstamp - jt->ttsp_tick_time();
      if ( tick_delta < ProfileTimeToSafepointTicks ) return;

      record_ttsp_tick(jt);
    }

    UserProfileEntry* upe = (UserProfileEntry*) jt->ttsp_profile_entry();
    upe->clz();

    const pid_t thread_id = thread_gettid();
    bool safep_suspend = (self_suspend_flags & JavaThread::safep_suspend) != 0;
jlong tstamp=os::elapsed_counter();

    upe->set_values(
		    ProfileEntry::perfcnt0_tick,
		    (tstamp & ProfileEntry::event_mask),
		    thread_id,
		    safep_suspend ? 1 : 0,
		    (intptr_t) epc,
		    esp, efp,
thread->vm_tag(),
		    tstamp
		    );

    jt->set_suspend_request( JavaThread::ttsp_suspend );
    jt->set_ttsp_tick_time( os::elapsed_counter() );
}

// When we reach a safepoint, see if we exceeded the ProfileTimeToSafepointMicros threshold, and should
// record a UserProfileEntry.
void TickProfiler::ttsp_evaluate(JavaThread*jt){
jlong tstamp=os::elapsed_counter();
  jlong tick_delta = tstamp - jt->ttsp_tick_time();

  if ( tick_delta < ProfileTimeToSafepointTicks ) {
    // The thread reached a safepoint under our profiling threshold, so we don't record a UserProfileEntry.
    return;
  }

  record_ttsp_tick(jt);
}

void TickProfiler::tlb0_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva) {
    Thread* thread = Thread::current();

    bool safep_suspend = thread->has_safep_suspend();
    if (!safep_suspend && OnlySafepointRequestTicks) return;

    TlbProfileEntry* tpe = (TlbProfileEntry*) get_profile_entry();
    tpe->set_values(
		    ProfileEntry::tlb0_tick,
thread->reversible_tid(),
		    safep_suspend ? 1 : 0,
		    (intptr_t) epc,
		    esp, efp,
		    (intptr_t) eva,
		    os::elapsed_counter()
		    );
}

void TickProfiler::tlb1_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva) {
    Thread* thread = Thread::current();

    bool safep_suspend = thread->has_safep_suspend();
    if (!safep_suspend && OnlySafepointRequestTicks) return;

    TlbProfileEntry* tpe = (TlbProfileEntry*) get_profile_entry();
    tpe->set_values(
		    ProfileEntry::tlb1_tick,
thread->reversible_tid(),
		    safep_suspend ? 1 : 0,
		    (intptr_t) epc,
		    esp, efp,
		    (intptr_t) eva,
		    os::elapsed_counter()
		    );
}


void TickProfiler::meta_tick(int meta, intptr_t info, intptr_t tag) {
    if (UseMetaTicks && (_profile != NULL)) {
	if (OnlySafepointRequestTicks && !Thread::current()->has_safep_suspend()) return;
	GET_RSP;
	assert0((meta & MetaProfileEntry::meta_tick_mask) == meta);
	assert0((info & MetaProfileEntry::meta_info_mask) == info);
    
	MetaProfileEntry* mpe = (MetaProfileEntry*) get_profile_entry();
	mpe->set_values(
Thread::current()->reversible_tid(),
			meta,
			info,
			(intptr_t)__builtin_return_address(0), RSP, (intptr_t)__builtin_frame_address(0),
			tag,
			os::elapsed_counter()
			);
    }
}

void TickProfiler::init(){
assert(_profile==NULL,"should not call init multiple times");
    _profile = new TickProfiler();
}

void TickProfiler::reset(){
    if (_profile != NULL) memset(_profile->_entries, 0, sizeof(ProfileEntry) * TickProfilerEntryCount);
}

class TickEntry : public HashEntry {
private:
    int _ticks;

public:
    TickEntry() : HashEntry() {
	_ticks = 0;
    }

virtual TickEntry*clone()=0;

    int ticks() const { return _ticks; }
    void increment()  { _ticks++; }


    virtual void print_on(outputStream *st) = 0;
    virtual void print_xml_on(xmlBuffer *xb) = 0;

void print_on(outputStream*st,int total_count){
	if (total_count != 0) {
	    st->print("%2.1f%% ", ((float)ticks()/(float)total_count)*100.0);
	}
st->print("%6d ",ticks());
print_on(st);
    }

    void print_xml_on(xmlBuffer* xb, int total_count) {
	if (total_count != 0) {
	    xmlElement xe(xb, "percent");
	    xb->print("%.1f%% ", ((float)ticks()/(float)total_count)*100.0);
	}
	{ xmlElement xe(xb, "ticks");
	    xb->print("%d ", ticks());
	}
	{ xmlElement xe(xb, "value");
	    print_xml_on(xb);
	}
    }
};


class CodeBlobEntry : public TickEntry {
private:
CodeBlob*_blob;
    intptr_t _tag;

public:
    CodeBlobEntry(CodeBlob* blob, intptr_t tag) : TickEntry() {
	_blob = blob;
	_tag = _tag;
    }

CodeBlobEntry*clone(){
	CodeBlobEntry* entry = new CodeBlobEntry(NULL, 0);
	memcpy(entry, this, sizeof(CodeBlobEntry));
	return entry;
    }

CodeBlob*blob()const{return _blob;}
    void set_blob(CodeBlob* blob) { _blob = blob; }

    intptr_t tag()        const { return _tag; }
    void set_tag(intptr_t tag)  { _tag = tag; }

    uint hash_code() { return ((uintptr_t)_blob) >> exact_log2(CodeEntryAlignment); }

    bool equals(HashEntry* entry) {
	if (HashEntry::equal_type(this, entry)) {
	    if (blob() == ((CodeBlobEntry*)entry)->blob()) {
		return true;
	    }
	}
	return false;
    }

    void print_on(outputStream* st) {
	if (!blob()->is_methodCode()) {
st->print("CodeBlob used for %s",blob()->name());
	}
	st->cr();
    }

    void print_xml_on(xmlBuffer* xb) {
	blob()->print_xml_on(xb, true, tag());
    }
};


class StubCodeEntry : public TickEntry {
private:
StubCodeDesc*_stub;

public:
    StubCodeEntry(StubCodeDesc* stub) : TickEntry() {
	_stub = stub;
    }

StubCodeEntry*clone(){
	StubCodeEntry* entry = new StubCodeEntry(NULL);
	memcpy(entry, this, sizeof(StubCodeEntry));
	return entry;
    }

StubCodeDesc*stub()const{return _stub;}
    void set_stub(StubCodeDesc* stub) { _stub = stub; }

    uint hash_code() { return ((uintptr_t)_stub) >> exact_log2(CodeEntryAlignment); }

    bool equals(HashEntry* entry) {
	if (HashEntry::equal_type(this, entry)) {
	    if (stub() == ((StubCodeEntry*)entry)->stub()) {
		return true;
	    }
	}
	return false;
    }

    void print_on(outputStream* st) {
stub()->print_on(st);
	st->cr();
    }

    void print_xml_on(xmlBuffer* xb) {
	stub()->print_xml_on(xb, true);
    }
};


class CodeletEntry : public TickEntry {
private:
InterpreterCodelet*_codelet;

public:
    CodeletEntry(InterpreterCodelet *codelet) : TickEntry() {
	_codelet = codelet;
    }

CodeletEntry*clone(){
	CodeletEntry* entry = new CodeletEntry(NULL);
	memcpy(entry, this, sizeof(CodeletEntry));
	return entry;
    }

    InterpreterCodelet* codelet()           const { return _codelet; }
    void set_codelet(InterpreterCodelet* codelet) { _codelet = codelet; }

    uint hash_code() { return ((uintptr_t)_codelet) >> exact_log2(CodeEntryAlignment); }

    bool equals(HashEntry* entry) {
	if (HashEntry::equal_type(this, entry)) {
	    if (codelet() == ((CodeletEntry*)entry)->codelet()) {
		return true;
	    }
	}
	return false;
    }

    void print_on(outputStream* st) {
codelet()->print_on(st);
	st->cr();
    }

    void print_xml_on(xmlBuffer* xb) {
	codelet()->print_xml_on(xb, true);
    }
};


class TagEntry : public TickEntry {
private:
    intptr_t _tag;
public:
    TagEntry(intptr_t tag) : TickEntry() {
	_tag = tag;
    }

TagEntry*clone(){
	TagEntry* entry = new TagEntry(tag());
	memcpy(entry, this, sizeof(TagEntry));
	return entry;
    }

    intptr_t tag()       const { return _tag; }
    void set_tag(intptr_t tag) { _tag = tag; }

    uint hash_code()      { return (uint)_tag; }

    bool equals(HashEntry *entry) {
	if (HashEntry::equal_type(this, entry)) {
	    if (tag() == ((TagEntry*)entry)->tag()) {
		return true;
	    }
	}
	return false;
    }

    void print_on(outputStream* st) {
	st->print(vmTags::name_for(tag()));
    }

    void print_xml_on(xmlBuffer* xb) {
	xmlElement xe(xb, "tag");
	xb->name_value_item("name", vmTags::name_for(tag()));
	xb->name_value_item("id", tag());
    }
};


class PcEntry : public TickEntry {
private:
    address _pc;
    const char *_name;
public:
    PcEntry(address pc, const char *name) : _pc(pc), _name(name) {}

PcEntry*clone(){
	PcEntry* entry = new PcEntry(pc(), name());
	memcpy(entry, this, sizeof(PcEntry));
	// Now we need to copy the string to make a surviving copy of the buffer.
char*name_copy=NEW_RESOURCE_ARRAY(char,strlen(_name)+1);
	entry->set_name(strcpy(name_copy, _name));
	return entry;
    }

    address pc()              const { return _pc; }
void set_pc(address pc){_pc=pc;}
    const char* name()        const { return _name; }
    void set_name(const char* name) { _name = name; }

uint hash_code(){
uint h=0;
	for (const char *p = _name; *p; ++p) h = 31*h + *p;
	return h;
    }

    bool equals(HashEntry* entry) {
	if (HashEntry::equal_type(this, entry)) {
	    if (strcmp(name(), ((PcEntry*)entry)->name()) == 0) {
		return true;
	    }
	}
	return false;
    }

    void print_on(outputStream* st) {
st->print(name());
    }

    void print_xml_on(xmlBuffer* xb) {
	char demangled[1024];
	xmlElement xe(xb, "pc_ref");
	xb->name_ptr_item("address", (void*) pc());
	xb->name_value_item("name", name());
#ifdef AZ_PROFILER
	// Right now, the demangle function calls into a machine-gen'd fcn with a
	// >32K stack frame.
	if (azprof::Demangler::demangle(name(), demangled, sizeof(demangled)) == 0) {
	  xb->name_value_item("pretty_name", demangled);
	}
#endif // AZ_PROFILER
    }
};

class UnknownEntry : public TickEntry {
public:
    UnknownEntry() : TickEntry() { }
UnknownEntry*clone(){
	UnknownEntry* entry = new UnknownEntry();
	memcpy(entry, this, sizeof(UnknownEntry));
	return entry;
    }
    uint hash_code() { return 5; }
    bool equals(HashEntry* entry) { return HashEntry::equal_type(this, entry); }
    void print_on(outputStream* st) { st->print_cr("[zombie code]"); }
    void print_xml_on(xmlBuffer* xb) {
	xmlElement name(xb,"name");
xb->print("[zombie code]");
    }
};

define_array(TickEntryArray, TickEntry*);

int compare_TickEntry(TickEntry** left, TickEntry** right) {
    return (*right)->ticks() - (*left)->ticks();
}


TickEntryArray* TickProfiler::collect_data(ProfileIterator& it, int &tick_count) {
    HashTable*     hash_table    = new HashTable(256);
    CodeBlobEntry* blob_entry    = new CodeBlobEntry(NULL, 0);
    StubCodeEntry* stub_entry    = new StubCodeEntry(NULL);
    CodeletEntry*  codelet_entry = new CodeletEntry(NULL);
    TagEntry*      tag_entry     = new TagEntry(0);
    PcEntry*       pc_entry      = new PcEntry(0, NULL);
    UnknownEntry*  unknown_entry = new UnknownEntry();
    char function_name[256];

    ProfileEntry *pe;
    while ((pe = it.next()) != NULL) {
	if( pe->is_meta_tick() ) continue;

	assert0( pe->is_perfcnt_tick() );
	UserProfileEntry* tick = (UserProfileEntry*)pe;
address pc=tick->pc();
	TickEntry* key_entry = unknown_entry;
	if (vmTags::is_native_call(tick->tag()) && (it.filt().tag() == -1)) {
	    pc = (address) tick->tag();
	}
	if (Interpreter::contains(pc)) {
	    // Use a pseudo-tag for interpreter ticks so that we can break them down
	    // into individual interpreter codelets.
	    if (it.filt().tag() == VM_Interpreter_tag) {
InterpreterCodelet*codelet=Interpreter::codelet_containing(pc);
if(codelet!=NULL){
		    codelet_entry->set_codelet(codelet);
		    key_entry = codelet_entry;
		} else {
		    pc_entry->set_name("[Unknown interpreter code]");
		    key_entry = pc_entry;
		}
	    } else {
		tag_entry->set_tag(VM_Interpreter_tag);
		key_entry = tag_entry;
	    }
	} else {
CodeBlob*blob=CodeCache::find_blob(pc);
	    if (blob != NULL) {
StubCodeDesc*stub;
		if (!blob->is_methodCode() && ((stub = StubCodeDesc::desc_for(pc)) != NULL)) {
		    stub_entry->set_stub(stub);
		    key_entry = stub_entry;
		} else {
		    blob_entry->set_blob(blob);
		    if (vmTags::is_native_call(tick->tag())) blob_entry->set_tag(tick->tag());
		    key_entry = blob_entry;
		}
	    } else {
		if (it.filt().tag() == -1) {
		    tag_entry->set_tag(tick->tag());
		    key_entry = tag_entry;
		} else {
		    // either tag matched or we are printing a completely flat profile
		    pc_entry->set_pc(pc);
		    int offset;
		    size_t size;
		    bool found = os::dll_address_to_function_name(pc, function_name, sizeof(function_name), &offset, &size);
		    if (found) {
			pc_entry->set_name(function_name);
		    } else {
			pc_entry->set_name("[Unknown function]");
		    }
		    key_entry = pc_entry;
		}
	    }
	}

	TickEntry* entry = (TickEntry*)hash_table->find(key_entry);
	if (entry == NULL) {
	    entry = key_entry->clone();
	    hash_table->insert(entry);
	}
entry->increment();
    }

    TickEntryArray* tea = new TickEntryArray(hash_table->entry_count());
    HashTableIterator iter(hash_table);
    int idx = 0;
    tick_count = 0;
    while (iter.next()) {
	TickEntry* entry = (TickEntry*)iter.entry();
	tick_count += entry->ticks();
	tea->at_put(idx++, entry);
    }
    tea->sort(compare_TickEntry);

    return tea;
}


void TickProfiler::print(double tickcutoff) {
if(_profile!=NULL){

	flush_all_tick_profile_buffers();

	ResourceMark rm;

ProfileIterator it;
	it.filt().set_type(-1);      // Allow all type matches

	int tick_count = 0;
	TickEntryArray* tea = collect_data(it, tick_count);

tty->print_cr("Tick Profile: overall %d ticks",tick_count);
	int idx = 0;
	while (idx < tea->length()) {
	    TickEntry* entry = tea->at(idx);
	    if (tickcutoff != 0 && (entry->ticks() * (100 / tickcutoff)) < tick_count) break; // Do not print ticks less than tickcutoff
entry->print_on(tty,tick_count);
	    tty->cr();
	    idx++;
	}

	int other_ticks = 0;
	while (idx < tea->length()) {
	    other_ticks += tea->at(idx)->ticks();
	    idx++;
	}

	if (tick_count > 0) {
	    tty->print_cr("%2.1f%% %6d All other ticks.", ((float)other_ticks/(float)tick_count)*100.0, other_ticks);
	}
    }
}

int compare_ProfileEntry(const void* left, const void* right) {
    jlong d = ((const ProfileEntry *)left)->timestamp() - ((const ProfileEntry *)right)->timestamp();
    return (d<0) ? -1 : 1;
}
void TickProfiler::print_timeline() {
    if( !_profile ) return;
    ResourceMark rm;

    flush_all_tick_profile_buffers();

    ProfileEntry *pes = NEW_RESOURCE_ARRAY(ProfileEntry,TickProfilerEntryCount);
    memcpy(pes,TickProfiler::_profile->entries(),TickProfilerEntryCount* sizeof(ProfileEntry));
    qsort(pes, TickProfilerEntryCount, sizeof(ProfileEntry), compare_ProfileEntry);

    int lastid = -1;
for(int i=0;i<TickProfilerEntryCount;i++){
	char buf[256];
	ProfileEntry *e = &pes[i];
	jlong timestamp= e->timestamp();
	if( !timestamp ) continue; // the non-tick
	int tid = e->thread_id();
	const char *s=0;
	if( e->is_perfcnt_tick() ) {
	    //UserProfileEntry *ep = (UserProfileEntry*)e;
s="tick";
	} else if( e->is_meta_tick() ) {
	    MetaProfileEntry *ep = (MetaProfileEntry*)e;
	    s = vmTicks::name_for(ep->meta_tick());
	    // Multiple vm lock/releases on the same thread are very common
	    // and print with a short form.
	    if( (ep->meta_tick() == vmlock_acquired_tick ||
		 ep->meta_tick() == vmlock_released_tick) && 
		strcmp(((AzLock*)ep->meta_info())->_name,"objectMonitor") && // NOT objectMonitor
		(lastid == tid) &&    // same thread in timeline
		(tid==1) &&           // and only for thread#1 (initial thread)
		(i+1<TickProfilerEntryCount) &&
		(pes[i+1].thread_id() == tid) && // and next tick also vmlock metatick
		pes[i+1].is_meta_tick() &&
		(((MetaProfileEntry*)&pes[i+1])->meta_tick() == vmlock_acquired_tick ||
		 ((MetaProfileEntry*)&pes[i+1])->meta_tick() == vmlock_released_tick )
		) {
		if( ep->meta_tick() == vmlock_acquired_tick ) tty->print("[%s ", ((AzLock*)ep->meta_info())->_name);
else tty->print("] ");
		continue;
	    }

	    switch( ep->meta_tick() ) {
		case objectmonitor_lock_acquired_tick: 
		case objectmonitor_lock_wait_tick: 
		case objectmonitor_unlock_tick: 
		case objectmonitor_wait_block_tick: 
		case objectmonitor_wait_broadcast_tick: 
		case objectmonitor_wait_notify_tick: 
		case objectmonitor_wait_wakeup_tick: 
		case objectmonitor_revoke_bias_dead_tick:
		case objectmonitor_revoke_bias_self_tick:
		case objectmonitor_revoke_bias_remote_tick:
		case vmlock_acquired_tick: 
		case vmlock_blocked_tick: 
		case vmlock_notify_all_tick: 
		case vmlock_notify_tick: 
		case vmlock_notify_nobody_home_tick: 
		case vmlock_released_tick: 
		case vmlock_wait_tick: 
		case vmlock_wakeup_tick: {
		    AzLock *m = ((AzLock*)ep->meta_info());
		    if( !m ) {
			// nothing
		    } else if(!strcmp(m->_name,"objectMonitor") ) {
			ObjectMonitor *om = (ObjectMonitor*)m;
sprintf(buf,"%s (ObjectMonitor*)%p",s,om);
		    } else {
			sprintf(buf,"%s (AzLock*)%p %s",s,m, m->_name);
		    }
s=buf;
		}
	    }
	} else {
	    Unimplemented();
	}
	tty->print_cr("%p %2d %s",(void*)timestamp,tid,s);
lastid=tid;
    }
}


void TickProfiler::print_xml_on(xmlBuffer* xb, ProfileIterator& it, double tickcutoff) {
    int type = it.filt().type();
    if (ProfileEntry::is_type_enabled(type)) {
	ResourceMark rm;

	int tick_count = 0;
	TickEntryArray* tea = collect_data(it, tick_count);

	xmlElement xe(xb, "profile");
	it.filt().print_xml_on(xb);
	ProfileEntry::print_xml_on(xb);
	xmlElement xf(xb, "entry_list");
	int idx;
for(idx=0;idx<tea->length();idx++){
	    TickEntry* entry = tea->at(idx);
	    if (tickcutoff != 0 && (entry->ticks() * (100 / tickcutoff)) < tick_count)
		break;                  // Don't show ticks less than tickcutoff
	    xmlElement xg(xb, "entry");
	    entry->print_xml_on(xb, tick_count);
	}
	int other_ticks = 0;
	for (; idx < tea->length(); idx++) {
	    other_ticks += tea->at(idx)->ticks();
	}
	{ xmlElement xg(xb, "other_entries");
	    if (other_ticks != 0) {
		xmlElement xe(xb, "percent");
		xb->print("%.1f%% ", 100.0 * ((float) other_ticks/ (float) tick_count));
	    }
	    xb->name_value_item("ticks", other_ticks);
	}
    } else {
	xb->name_value_item("error", "No tick profile is available.");
    }
}

ProfileFilter::ProfileFilter() :
    _type(ProfileEntry::perfcnt0_tick),
    _tag(-1),
    _safep_suspend(-1),
    _thr(-1),
    _low(0),
    _hi((address) (intptr_t) ~0llu),
    _matched(0),
    _unmatched(0) {}

bool ProfileFilter::from_req(azprof::Request *req) {
#ifdef AZ_PROFILER
    char *s;

    s = req->parameter_by_name("type");
    if (s != NULL) {
	int k = strtol(s, NULL, 0);
	if (ProfileEntry::is_type(k)) {
set_type(k);
	} else {
	    return true;
	}
    }

    s = req->parameter_by_name("thr");
    if (s != NULL) set_thr(strtoul(s, NULL, 0));

    s = req->parameter_by_name("tag");
    if (s != NULL) set_tag(strtoul(s, NULL, 0));

    s = req->parameter_by_name("safep_suspend");
    if (s != NULL) set_safep_suspend(strtoul(s, NULL, 0));

    return false;
#else // !AZ_PROFILER
    return false;
#endif // !AZ_PROFILER
}

bool ProfileFilter::from_xb(xmlBuffer *xb) {
    return from_req(xb->request());
}

bool ProfileFilter::matches(ProfileEntry *e) {
switch(e->type()){
	case ProfileEntry::perfcnt0_tick:
	case ProfileEntry::perfcnt1_tick:
	case ProfileEntry::perfcnt4_tick:
	case ProfileEntry::perfcnt5_tick: {
	    UserProfileEntry *upe = (UserProfileEntry*) e;
	    if (
		(upe->pc() != 0) &&
		(low() <= upe->pc()) && (upe->pc() <= hi()) &&
		((tag() < 0) || (tag() == VM_Interpreter_tag) || (tag() == upe->tag())) &&
		((tag() != VM_Interpreter_tag) || Interpreter::contains(upe->pc())) &&
		((thr() < 0) || (thr() == upe->thread_id())) &&
		((type() < 0) || (type() == upe->type())) &&
		(((safep_suspend() < 0)) || (safep_suspend() == upe->safep_suspend()))
		) {
		++_matched;
		return true;
	    } else {
		++_unmatched;
		return false;
	    }
	}
	case ProfileEntry::tlb0_tick:
	case ProfileEntry::tlb1_tick: {
	    TlbProfileEntry *tpe = (TlbProfileEntry*) e;
	    if (
		(tpe->pc() != 0) &&
		(low() <= tpe->pc()) && (tpe->pc() <= hi()) &&
		((thr() < 0) || (thr() == tpe->thread_id())) &&
		((type() < 0) || (type() == tpe->type())) &&
		(((safep_suspend() < 0)) || (safep_suspend() == tpe->safep_suspend()))
		) {
		++_matched;
		return true;
	    } else {
		++_unmatched;
		return false;
	    }
	}
	case ProfileEntry::meta_tick: {
	    MetaProfileEntry *mpe = (MetaProfileEntry*) e;
	    if (
		((type() < 0) || (type() == mpe->type())) &&
		((thr() <= 0) || (thr() == mpe->thread_id())) &&
		((tag() < 0) || (tag() == mpe->tag()))
		) {
		++_matched;
		return true;
	    } else {
		++_unmatched;
		return false;
	    }
	}
	default:
	    ShouldNotReachHere();
	    return false;
    }
}

// may trigger a GC when the threads-lock is grabbed to get the thread name
void ProfileFilter::print_xml_on(xmlBuffer *xb) {
    xmlElement xe(xb, "profile-filter");
    if (type() >= 0) {
	xb->name_value_item("type-id", type());
    }
    xb->name_value_item("tag-id", tag());
    if (tag() >= 0) {
	const char *name;
	if (vmTags::is_native_call(tag())) {
	    // Looking up a native call unsafely - can it be removed during printout?
	    CodeBlob* blob = CodeCache::find_blob((address) tag());
	    if (blob) {
name=blob->name();
	    } else {
name="Unknown";
	    }
	} else {
	    name = vmTags::name_for(tag());
	}
	xb->name_value_item("tag-name", name);
    }
    if (thr() >= 0) {
	const char *name = Threads::thread_name_may_gc(thr());
	xb->name_value_item("thread-id", thr());
	if (name) {
	    xb->name_value_item("thread-name", name);
	} else {
	    xb->name_value_item("thread-name", "Thread %d", thr());
	}
    }
    if (safep_suspend() >= 0) {
	xb->name_value_item("safepoint-suspend-request", safep_suspend() != 0);
    }
    xb->name_value_item("matched", matched());
    xb->name_value_item("unmatched", unmatched());
}

ProfileIterator::ProfileIterator() : _idx(0) {}

ProfileIterator::ProfileIterator(azprof::Request *req) : _idx(0) {
#ifdef AZ_PROFILER
    _filt.from_req(req);
#endif // AZ_PROFILER
}

ProfileIterator::ProfileIterator(xmlBuffer *xb) : _idx(0) {
    _filt.from_xb(xb);
}

ProfileEntry* ProfileIterator::next() {
    while (_idx < TickProfilerEntryCount) {
	ProfileEntry *e = TickProfiler::_profile->entry_at(_idx++);
	Prefetch::read(e, 4*sizeof(ProfileEntry));
	if (_filt.matches(e)) return e;
    }
    return NULL;
}

bool RpcTreeNode::pc_to_range(uint32_t pc, uint32_t& low, uint32_t& hi) {
    address low0, hi0;
    const char *name = Disassembler::static_pc_to_name((address) pc, low0, hi0, false /* demangle */);
    low = (uint32_t) (uint64_t)low0;
    hi  = (uint32_t) (uint64_t)hi0;
    return name ? false : true;
}

RpcTreeNode* RpcTreeNode::new_sibling(uint32_t pc) {
    uint32_t low, hi;
    pc_to_range(pc, low, hi);
    return (_next = new RpcTreeNode(low, hi));
}

RpcTreeNode* RpcTreeNode::new_child(uint32_t pc) {
    uint32_t low, hi;
    pc_to_range(pc, low, hi);
    return (_child = new RpcTreeNode(low, hi));
}

void RpcTreeNode::print_xml(xmlBuffer *xb, address low, address hi, bool invert) {
    ResourceMark rm;
    ProfileIterator it(xb);

    RpcTreeNode tree(0, 0);
    ProfileEntry *e;
    if (invert) {
	while ((e = it.next())) {
	    for (int i = e->_rpc_count - 1; i >= 0; i--) {
		const uint32_t *rpcs = e->rpcs() + i;
		address rpc = (address) rpcs[0];
		if ((low <= rpc) && (rpc <= hi)) {
		    RpcTreeNode *node = tree.tick(rpcs-1, i, true);
uint32_t pc;
		    if (e->is_perfcnt_tick()) {
			pc = (uint32_t) (uint64_t) ((UserProfileEntry*) e)->pc();
		    } else if (e->is_tlb_tick()) {
			pc = (uint32_t) (uint64_t) ((UserProfileEntry*) e)->pc();
		    } else {
			break;
		    }
		    if (node->_child) {
			node = node->_child;
			while (true) {
			    if (node->contains(pc)) {
				node->tick(NULL, 0, true);
				break;
			    } else if (node->_next == NULL) {
				node->new_sibling(pc)->tick(NULL, 0, true);
				break;
			    } else {
				node = node->_next;
			    }
			}
		    } else {
			node->new_child(pc)->tick(NULL, 0, true);
		    }
		    break;
		}
	    }
	}
    } else {
	it.filt().set_range(low, hi);
	while ((e = it.next())) {
	    tree.tick(e->rpcs(), e->_rpc_count, false);
	}
    }

    xmlElement xe(xb, "rpc-tree");
    tree.print_xml(xb);
}

RpcTreeNode* RpcTreeNode::tick(const uint32_t *rpcs0, int len0, bool invert) {
    ++_ticks;
    if (len0 > 0) {
	uint64_t rpc = rpcs0[0];
	if (rpc != 0) {
	    const uint32_t *rpcs = invert ? (rpcs0 - 1) : (rpcs0 + 1);
	    int len = len0 - 1;
	    if (_child) {
		RpcTreeNode *node = _child;
		while (true) {
		    if (node->contains(rpc)) {
			return node->tick(rpcs, len, invert);
		    } else if (node->_next == NULL) {
			return node->new_sibling(rpc)->tick(rpcs, len, invert);
		    } else {
			node = node->_next;
		    }
		}
	    } else {
		return new_child(rpc)->tick(rpcs, len, invert);
	    }
	}
	// broken stack crawl, so we get a busted tick event
	return NULL;
    } else {
	return this;
    }
}

void RpcTreeNode::print_xml(xmlBuffer *xb) {
    { xmlElement xe(xb, "rpc-tree-node");
	xb->name_value_item("ticks", (intptr_t) _ticks);
	if (_low != 0) Disassembler::print_static_pc_xml(xb, (address) _low);
if(_child)_child->print_xml(xb);
    }
if(_next)_next->print_xml(xb);
}
