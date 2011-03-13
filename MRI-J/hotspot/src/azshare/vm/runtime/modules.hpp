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

#ifndef _MODULES_HPP_
#define _MODULES_HPP_


#include "allocation.hpp"


class Modules:AllStatic{

  public:
    enum {
      Unknown                  =  0,
      CodeCache                =  1,
      IVSpace                  =  2,
      SBA                      =  3,
      CardTableModRefBS        =  4,
      JNI                      =  5,
      ITR                      =  6,
      VirtualSpace             =  7,
      GPGC_CardTable           =  8,
      GPGC_PageAudit           =  9,
      GPGC_PageInfo            = 10,
      GPGC_ReadTrapArray       = 11,
      GPGC_ObjectForwarding    = 12,
      GPGC_StrongMarks         = 13,
      GPGC_FinalMarks          = 14,
      GPGC_VerifyMarks         = 15,
      GPGC_MarkThroughs        = 16,
      GPGC_MarkIDs             = 17,
      ParGC_CardTableExtension = 18,
      ParGC_PSVirtualSpace     = 19,

      MAX_MODULE               = 20
    };

    static char* name(int module) {
      switch (module) {
        case Unknown:                  return "UnknownModule";
        case CodeCache:                return "CodeCache";
        case IVSpace:                  return "IVSpace";
        case SBA:                      return "SBA";
        case CardTableModRefBS:        return "CardTableModRefBS";
        case JNI:                      return "JNI";
        case ITR:                      return "ITR";
        case VirtualSpace:             return "VirtualSpace";
        case GPGC_CardTable:           return "GPGC_CardTable";
        case GPGC_PageAudit:           return "GPGC_PageAudit";
        case GPGC_PageInfo:            return "GPGC_PageInfo";
        case GPGC_ReadTrapArray:       return "GPGC_ReadTrapArray";
        case GPGC_ObjectForwarding:    return "GPGC_ObjectForwarding";
        case GPGC_StrongMarks:         return "GPGC_StrongMarks";
        case GPGC_FinalMarks:          return "GPGC_FinalMarks";
        case GPGC_VerifyMarks:         return "GPGC_VerifyMarks";
        case GPGC_MarkThroughs:        return "GPGC_MarkThroughs";
        case GPGC_MarkIDs:             return "GPGC_MarkIDs";
        case ParGC_CardTableExtension: return "ParGC_CardTableExtension";
        case ParGC_PSVirtualSpace:     return "ParGC_PSVirtualSpace";
      }
return"OutOfRangeModule";
    }
};

#endif // _MODULES_HPP_
