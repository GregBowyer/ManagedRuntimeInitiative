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
#ifndef GPGC_SLT_HPP
#define GPGC_SLT_HPP


// This class coordinates the GPGC collector interactions with the SurrogateLockerThread.
class GPGC_SLT:AllStatic{
  private:
    enum States {
      Unlocked   = 0,
      Locking    = 1,
      Unlocking  = 2,
      Locked     = 3,
      TwoLocked  = 4
    };

    static volatile intptr_t _state;
    static volatile bool     _notify;

    static volatile bool     _new_locked;
    static volatile bool     _old_locked;

  public:
    static bool is_locked    ();
    static void acquire_lock ();
    static void release_lock ();
};

#endif // GPGC_SLT_HPP

