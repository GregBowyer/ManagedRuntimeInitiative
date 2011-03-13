/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
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
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View,
// CA 94043 USA, or visit www.azulsystems.com if you need additional information
// or have any questions.

package java.lang.ref;

import sun.misc.Cleaner;

/* for stats */
import java.util.Properties;

/**
 * Abstract base class for reference objects.  This class defines the
 * operations common to all reference objects.	Because reference objects are
 * implemented in close cooperation with the garbage collector, this class may
 * not be subclassed directly.
 *
 * @author   Mark Reinhold
 * @since    1.2
 */

public abstract class Reference<T> {

  /* 
   * To ensure that concurrent collector can discover active Reference 
   * objects without interfering with application threads that may apply 
   * the enqueue() method to those objects, collectors should link 
   * discovered objects through the discovered field.
   */

  /* Make sure native method setup is done first. */
  private static native void registerNatives();
  static {
      registerNatives();
  }

  private T referent;		/* Treated specially by GC */
  private Reference pending;

  ReferenceQueue<? super T> queue;

  Reference next;
  transient private Reference<T> discovered; 	/* used by VM */


  /* Object used to synchronize with the garbage collector.  The collector
   * must acquire this lock at the beginning of each collection cycle.  It is
   * therefore critical that any code holding this lock complete as quickly
   * as possible, allocate no new objects, and avoid calling user code.
   */
  static private class Lock { };
  private static Lock lock = new Lock();


  /* List of References waiting to be enqueued.  The collector adds
   * References to this list, while the Reference-handler thread removes
   * them.  This list is protected by the above lock object.
   */
  private static Reference pending_list = null;

  /**
   * Determine if the garbage collector is linking pending references 
   * through the pending field instead of the next field.
   */
  static native boolean pendingInNext();

  /**
   * Retrieve the referent in a manner that is safe in conjunction 
   * with a garbage collector implementing concurrent 
   * soft/weak/final/phantom/JNI reference processing.
   * @return referent
   */
  native T concurrentGetReferent();

  /* for finalizer stats */
  void updateStatistics() { /* nothing */ };

  /* High-priority thread to enqueue pending References
   */
  private static class ReferenceHandler extends Thread {
      int   myNumber = 0;
      static boolean booting = true;

      ReferenceHandler(ThreadGroup g, String name) {
          super(g, name);
      }

    // This constructor for naming the extra threads
      ReferenceHandler(ThreadGroup g, String name, int number) {
          super(g, name);
          myNumber = number;
      }

      void startupTasks() {
          try {
              Properties props  = System.getProperties();
              /* NOTE: If you change this default thread count, change it in
               Finalizer.java as well so there are an equal number of threads for each */
              String threadCmd  = props.getProperty("azul.reference.threads", "4");
              int extraThreads  = Integer.parseInt(threadCmd);
              if (extraThreads > 1) {
                  ThreadGroup tg = Thread.currentThread().getThreadGroup();
                  for (int i = 1; i < extraThreads; i++ ) {
                      Thread handler = new ReferenceHandler(tg, new String("Reference Handler-" + i), i );
                      handler.setPriority(Thread.MAX_PRIORITY - 2);
                      handler.setDaemon(true);
                      handler.start();
                  }
              }
          } catch (java.lang.SecurityException se) { 
        // Security is too restrictive to permit reading the properties, so this feature
        // is disabled. We will continue normally with 1 ref handler thread.
          } catch (Exception e) { 
                System.out.println( "[F: Exception = " + e );
          }
          booting = false;
      }

      public void run() {
          // We don't call startupTasks() until the VM is more initialized then
          // when the run method is first called.  We delay this call until
          // first time someone notifies the lock.  There's gotta be a cleaner
          // way to wait until System.getProperties() is a valid call.
          if (booting && (myNumber == 0)) {
              synchronized (lock) {
                  try {
                      lock.wait();
                  } catch (InterruptedException x) { }
              }
              startupTasks();
          }

          if (pendingInNext()) {
              processThroughNext();
          } else {
              processThroughPending();
          }
      }

      private void processPending(Reference r) {
          // Fast path for cleaners
          if (r instanceof Cleaner) {
              ((Cleaner)r).clean();
          } else {
              ReferenceQueue q = r.queue;
              if (q != ReferenceQueue.NULL) q.enqueue(r);
              if (r instanceof FinalReference) r.updateStatistics();
          }
      }

      private void processThroughNext() {
          for (;;) {
              Reference r;
              synchronized (lock) {
                  if (pending_list != null) {
                      r = pending_list;
                      Reference rn = r.next;
                      pending_list = (rn == r) ? null : rn;
                      r.next = r;  // Once enqueued by GC, r.next will never be null again.
                  } else {
                      try {
                          lock.wait();
                      } catch (InterruptedException x) { }
                      continue;
                  }
              }

              processPending(r);
          }
      }

      private void processThroughPending() {
          for (;;) {
              Reference r;
              synchronized (lock) {
                  if (pending_list != null) {
                      r = pending_list;
                      Reference rn = r.pending;
                      pending_list = (rn == r) ? null : rn;
                      r.pending = r;  // Once enqueued by GC, r.pending will never be null again.
                  } else {
                      try {
                          lock.wait();
                      } catch (InterruptedException x) { }
                      continue;
                  }
              }

              processPending(r);
          }
      }
  }

  static {
      ThreadGroup tg = Thread.currentThread().getThreadGroup();
      for (ThreadGroup tgn = tg;
              tgn != null;
              tg = tgn, tgn = tg.getParent());
      Thread handler = new ReferenceHandler(tg, "Reference Handler");
      /* If there were a special system-only priority greater than
       * MAX_PRIORITY, it would be used here
       */
      handler.setPriority(Thread.MAX_PRIORITY);
      handler.setDaemon(true);
      handler.start();
  }

    /* -- Referent accessor and setters -- */

    /**
     * Returns this reference object's referent.  If this reference object has
     * been cleared, either by the program or by the garbage collector, then
     * this method returns <code>null</code>.
     *
     * @return	 The object to which this reference refers, or
     *		 <code>null</code> if this reference object has been cleared
     */
  public T get() {
      return this.referent;
  }

  /**
   * Retrieve the referent in a manner that is safe but slow in conjunction 
   * with a garbage collector implementing concurrent 
   * soft/weak/final/phantom/JNI reference processing.
   * @return referent
   */
  T getReferentSafe() {
      synchronized (lock) {
          // Holding the lock, we know that GC isn't currently 
          // doing concurrent ref processing.
          return this.referent;
      }
  }

  /**
   * Clears this reference object.  Invoking this method will not cause this
   * object to be enqueued.
   *
   * <p> This method is invoked only by Java code; when the garbage collector
   * clears references it does so directly, without invoking this method.
   */
  public void clear() {
      this.referent = null;
  }


  /* -- Queue operations -- */

  /**
   * Tells whether or not this reference object has been enqueued, either by
   * the program or by the garbage collector.	 If this reference object was
   * not registered with a queue when it was created, then this method will
   * always return <code>false</code>.
   *
   * @return	 <code>true</code> if and only if this reference object has
   *		 been enqueued
   */
  public boolean isEnqueued() {
      synchronized (this) {
          return (this.queue != ReferenceQueue.NULL) && (this.next != null);
      }
  }

  /**
   * Adds this reference object to the queue with which it is registered,
   * if any.
   *
   * <p> This method is invoked only by Java code; when the garbage collector
   * enqueues references it does so directly, without invoking this method.
   *
   * @return	 <code>true</code> if this reference object was successfully
   *		 enqueued; <code>false</code> if it was already enqueued or if
   *		 it was not registered with a queue when it was created
   */
  public boolean enqueue() {
      synchronized (lock) {
          // Holding the lock, we know that GC isn't currently doing concurrent ref processing.
          return this.queue.enqueue(this);
      }
  }


  /* -- Constructors -- */

  Reference(T referent) {
      this(referent, null);
  }

  Reference(T referent, ReferenceQueue<? super T> queue) {
      this.referent = referent;
      this.queue = (queue == null) ? ReferenceQueue.NULL : queue;
  }

}
