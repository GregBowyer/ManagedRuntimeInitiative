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

// system.c - System subsystem
//
// This file contains those functions that extend the available syscall functionality and/or
// provide user-space facilities.

#ifdef AZ_PROXIED
/*
 * os/posix_redefines.h renames posix calls with the prefix __. This is to ensure
 * that the syscalls in this file do not invoke the proxied versions. 
 */
#include <os/posix_redefines.h>
#endif // AZ_PROXIED

#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>

#include <os/config.h>
#include <os/time.h>
#include <os/utilities.h>
#include <syslog.h>

static ssize_t
fullRead (int fd, char *buf, size_t count)
{
    size_t bytesread = 0;
    while (bytesread < count) {
        ssize_t rv = read(fd, buf+bytesread, count-bytesread);
        if (rv < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue;
            }

            return rv;
        }
        else if (rv == 0) {
            return bytesread;
        }

        bytesread += rv;
    }

    return bytesread;
}

static sys_return_t 
system_clock_frequency(float* mhz)
{
    size_t filesize = 0;
    const char* filename = "/proc/cpuinfo";

    // FIXME - figure out a way to get the size of a file under /proc
    // without using calls to any of the file system routines that will
    // result in calls to the proxy front end in the AZ_PROXIED environment.
    // Files in /proc have a file size of zero.
    filesize = 1000;

    int fd = open(filename, O_RDONLY);

    /* Read the file into a buffer */
    char* filebuf = (char*) malloc(sizeof(char) * filesize);
    size_t bytes  = fullRead(fd, filebuf, filesize);
    if (bytes != filesize) {
      // FIXME - figure out a way to get the size of a file under /proc
      //os_assert (false, "system.c: read did not complete correctly");
      //return SYSERR_INVALID_STATE;
    }

    char* delim = " \t\n";
    char* token = strtok(filebuf, delim);

    while (token != NULL && strcmp(token, "MHz") != 0) {
      token = strtok(NULL, delim);
    }

    token = strtok(NULL, delim);
    if (strcmp(token, ":") != 0) {
      os_assert (false, "system_clock_frequency error: expected <string> MHz : <mhz value>\n");
      return SYSERR_INVALID_STATE;
    }

    token = strtok(NULL, delim);
    *mhz = atof(token);
    if (*mhz <= 0) {
      os_assert (false, "system_clock_frequency error: expected MHz value > 0\n");
      return SYSERR_INVALID_STATE;
    }

    close(fd);
    free(filebuf);

    return SYSERR_NONE;
}

// system_timestamp_counter_frequency_estimate
// Determining the TSC frequency should only be done for the VMware
// environment where the TSC that we are reading is a virtual TSC.
//
static sys_return_t 
system_timestamp_counter_frequency_estimate(float* mhz) 
{
    // Use the time and ticks from 2 gettimeofday calls (calls 2 and 4) to 
    // calculate the time stamp counter register's update frequency. Use two 
    // calls (calls 1 and 3) to ensure good cache locality for the calls
    // that we use for the measurement.
    uint64_t gtod1_tick_before, gtod3_tick_before = 0; // Used to "ensure" locality
    uint64_t gtod2_tick_before, gtod2_tick_after = 0;
    uint64_t gtod4_tick_before, gtod4_tick_after = 0;
    int gtod1_return, gtod3_return = 0; // Used to "ensure" locality
    int gtod2_return, gtod4_return = 0;
    struct timeval tv1, tv2, tv3, tv4 = {0,0};

    // We nanosleep for a time long enough to ensure some accuracy in the
    // calculation (10 ms is about 20,000,000 cycles for a TSC with an update
    // frequency of 2000 MHz)
    struct timespec ts; 
    ts.tv_sec = 0;
    ts.tv_nsec = 10000000; // 10 milli seconds = 10 000 000 nanoseconds

    // Start collecting timing info
    // BEGIN: DO NOT ADD ANY CODE TO THIS SECTION
     
    gtod1_tick_before = system_tick_count();
    gtod1_return = gettimeofday(&tv1, NULL); // 1 - Used to get code into cache

    gtod2_tick_before = system_tick_count();
    gtod2_return = gettimeofday(&tv2, NULL); // 2
    gtod2_tick_after = system_tick_count();
   
    // sleep - but we'll use the gettimeofday values to do the actual calculation
    nanosleep(&ts, NULL);     
   
    gtod3_tick_before = system_tick_count();
    gtod3_return = gettimeofday(&tv3, NULL); // 3 - Used to get code into cache

    gtod4_tick_before = system_tick_count();
    gtod4_return = gettimeofday(&tv4, NULL); // 4
    gtod4_tick_after = system_tick_count();

    // END: DO NOT ADD ANY CODE TO THIS SECTION
    // End collecting timing info

    // Check to be certain that everything worked correctly (and prevent
    // the optimizer from removing any of the timing code)
    if ((gtod1_return != 0) || (gtod2_return != 0) || 
        (gtod3_return != 0) || (gtod4_return != 0) ||
        (gtod1_tick_before >= gtod2_tick_before) || 
        (gtod2_tick_before == 0) || (gtod2_tick_after == 0) ||
        (gtod2_tick_before >= gtod2_tick_after) || 
        (gtod2_tick_after >= gtod3_tick_before) || 
        (gtod3_tick_before >= gtod4_tick_before) || 
        (gtod4_tick_before == 0) || (gtod4_tick_after == 0) ||
        (gtod4_tick_before >= gtod4_tick_after)) {
        os_guarantee(false, "TSC timing collection data is incorrect");
        // We could also go back to the calculation again at this point and 
        // try again, but for now we'll just exit
        return SYSERR_INVALID_STATE;
    }

    // Calculate the approximate time of the acquisition of the two
    // gettimeofday timestamps that we will use (we estimate)
    uint64_t gettime2_duration_ticks = gtod2_tick_after - gtod2_tick_before;
    uint64_t gettime4_duration_ticks = gtod4_tick_after - gtod4_tick_before;

    // Check for wild fluctuations in tick counts for the gettimeofday call
    if ((gettime4_duration_ticks > (2.0 * gettime2_duration_ticks)) ||
        (gettime2_duration_ticks > (2.0 * gettime4_duration_ticks))) {
        syslog (LOG_ERR, "ERROR: Azul JVM measured high variability (>2x) in Time-Stamp Counter intervals that should be equal length: %llu, %llu)", 
            (unsigned long long)gettime2_duration_ticks, 
            (unsigned long long)gettime4_duration_ticks); 
        // TO DO: what to do here? We can still do the calculation, but we may
        // have been swapped out at a bad time - for now, we will just continue
        // after logging the message
    }

    // Estimate the tick count when the gettimeofday value is generated
    uint64_t gettime2_tick = (uint64_t)(gtod2_tick_before + ((gettime2_duration_ticks)/2));
    uint64_t gettime4_tick = (uint64_t)(gtod4_tick_before + ((gettime4_duration_ticks)/2));

    // Get the gettimeofday times
    uint64_t gettime2 = 1000000llu*tv1.tv_sec + tv1.tv_usec;
    uint64_t gettime4 = 1000000llu*tv4.tv_sec + tv4.tv_usec;

    // We are going to use the difference between the gettimeofday values as a
    // divisor, so let's check for sanity
    if (gettime2 >= gettime4) {
        syslog (LOG_ERR, "ERROR: Azul JVM queried gettimeofday and found values that are not sequential: first value: %llu; second value: %llu", 
            (unsigned long long)gettime2, (unsigned long long)gettime4);
        return SYSERR_INVALID_STATE;
    }

    // Calculate the frequency in ticks/microsecond == (millions of ticks)/second
    *mhz = ((double)(gettime4_tick - gettime2_tick))/(gettime4 - gettime2);  

    return SYSERR_NONE;
}

sys_return_t 
system_configuration( uint64_t _what, address_t _buffer, size_t _buffer_size )
{
    switch (_what) {
    case SYSTEM_CONFIGURATION_CPU_FREQUENCY: {
        os_assert(_buffer_size == sizeof(struct sysconf_frequency), "Wrong buffer size");
        float mhz=0.0;
        sys_return_t rc = system_clock_frequency(&mhz);
        if (rc != SYSERR_NONE) {
          return rc;
        }
        struct sysconf_frequency *sf = (struct sysconf_frequency *)_buffer;
        sf->sysc_denominator = 1;
        sf->sysc_numerator = (int64_t)(mhz * 1.e6);
        return SYSERR_NONE;
    }
    case SYSTEM_CONFIGURATION_TSC_FREQUENCY: {
        os_assert(_buffer_size == sizeof(struct sysconf_frequency), "Wrong buffer size");
        float mhz=0.0;
        sys_return_t rc = system_timestamp_counter_frequency_estimate(&mhz);
        if (rc != SYSERR_NONE) {
          return rc;
        }
        struct sysconf_frequency *sf = (struct sysconf_frequency *)_buffer;
        sf->sysc_denominator = 1;
        sf->sysc_numerator = (int64_t)(mhz * 1.e6);
        return SYSERR_NONE;
    }
    default:
        assert(0); // "Unsupported system_configuration what"
        break;
    }
}

/*
 * Return -1 on error
 */
long slow_thread_cpu_time(pid_t tid,
                          int clock_tics_per_sec,
                          int user_sys_cpu_time) {
  static bool proc_task_unchecked = true;
  static const char *proc_stat_path = NULL;
  char stat[2048];
  char proc_name[64];
  long sys_time, user_time;
  int idummy;
  long ldummy;
  int fd = -1;

  // The /proc/<pid>/stat aggregates per-process usage on
  // new Linux kernels 2.6+ where NPTL is supported.
  // The /proc/self/task/<tid>/stat still has the per-thread usage.
  if (proc_task_unchecked) {
    // This is executed only once
    proc_task_unchecked = false;

    fd = open("/proc/self/task", O_RDONLY);
    if ( fd != -1 ) {
      proc_stat_path = "/proc/self/task/%d/stat";
      close(fd);
    }
  }

  // If proc_stat_path did not get initialized, early exit.
  if (proc_stat_path == NULL) return -1;

  sprintf(proc_name, proc_stat_path, tid);
  fd = open(proc_name, O_RDONLY);
  if (fd == -1) return -1;

  // FIXME - figure out a way to get the size of a file under /proc
  // without using calls to any of the file system routines that will
  // result in calls to the proxy front end in the AZ_PROXIED environment.
  // Files in /proc have a file size of zero.
  const size_t filesize = 2047;

  size_t statlen  = fullRead(fd, stat, filesize);

  if (statlen != filesize) {
    // FIXME - figure out a way to get the size of a file under /proc
    //os_assert (false, "system.c: read did not complete correctly");
    //return SYSERR_INVALID_STATE;
  }

  stat[statlen] = '\0';
  close(fd);

  // Skip pid and the command string. Note that we could be dealing with
  // weird command names, e.g. user could decide to rename java launcher
  // to "java 1.4.2 :)", then the stat file would look like
  //                1234 (java 1.4.2 :)) R ... ...
  // We don't really need to know the command string, just find the last
  // occurrence of ")" and then start parsing from there. See bug 4726580.
  char *s = strrchr(stat, ')');
  if (s == NULL) return -1;

  // Skip blank chars
  do {
    s++;
  } while (isspace(*s));

  int count = sscanf(s,"%*c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu",
                 &idummy, &idummy, &idummy, &idummy, &idummy,
                 &ldummy, &ldummy, &ldummy, &ldummy, &ldummy,
                 &user_time, &sys_time);
  if (count != 12) return -1;
  if (user_sys_cpu_time) {
    return (long)(((long)sys_time + (long)user_time)
                  * (1000000000 / clock_tics_per_sec));
  } else {
    return ((long)user_time * (1000000000 / clock_tics_per_sec));
  }
}
