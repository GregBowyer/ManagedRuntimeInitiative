/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CICLASSLIST_HPP
#define CICLASSLIST_HPP


class ciEnv;
class ciObjectFactory;
class ciConstantPoolCache;

class ciField;
class ciConstant;
class ciFlags;
class ciExceptionHandler;
class ciCallProfile;
class ciSignature;

class ciBytecodeStream;
class ciSignatureStream;
class ciExceptionHandlerStream;

class ciTypeFlow;

class ciObject;
class   ciNullObject;
class   ciInstance;
class   ciMethod;
class   ciSymbol;
class   ciArray;
class     ciObjArray;
class     ciTypeArray;
class   ciType;
class    ciReturnAddress;
class    ciKlass;
class     ciInstanceKlass;
class     ciMethodKlass;
class     ciSymbolKlass;
class     ciArrayKlass;
class       ciObjArrayKlass;
class       ciTypeArrayKlass;
class     ciKlassKlass;
class       ciInstanceKlassKlass;
class       ciArrayKlassKlass;
class         ciObjArrayKlassKlass;
class         ciTypeArrayKlassKlass;

// Simulate Java Language style package-private access with
// friend declarations.
// This is a great idea but gcc and other C++ compilers give an
// error for being friends with yourself, so this macro does not
// compile on some platforms.

// Everyone gives access to ciObjectFactory
#define CI_PACKAGE_ACCESS \
friend class ciObjectFactory;

// These are the packages that have access to ciEnv
// Any more access must be given explicitly.
#define CI_PACKAGE_ACCESS_TO           \
friend class ciArray;                  \
friend class ciArrayKlass;             \
friend class ciArrayKlassKlass;        \
friend class ciBytecodeStream;         \
friend class ciCallProfile;            \
friend class ciConstant;               \
friend class ciConstantPoolCache;      \
friend class ciDependency;             \
friend class ciDependencyKlass;        \
friend class ciExceptionHandler;       \
friend class ciExceptionHandlerStream; \
friend class ciField;                  \
friend class ciFlags;                  \
friend class ciInstance;               \
friend class ciInstanceKlass;          \
friend class ciInstanceKlassKlass;     \
friend class ciKlass;                  \
friend class ciKlassKlass;             \
friend class ciMethod;                 \
friend class ciMethodKlass;            \
friend class ciNullObject;             \
friend class ciObjArray;               \
friend class ciObjArrayKlass;          \
friend class ciObjArrayKlassKlass;     \
friend class ciObject;                 \
friend class ciObjectFactory;          \
friend class ciReturnAddress;          \
friend class ciSignature;              \
friend class ciSignatureStream;        \
friend class ciSymbol;                 \
friend class ciSymbolKlass;            \
friend class ciType;                   \
friend class ciTypeArrayKlass;         \
friend class ciTypeArrayKlassKlass;

#endif // CICLASSLIST_HPP
