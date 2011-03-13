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
#ifndef ARTAQUERY_HPP
#define ARTAQUERY_HPP



#ifdef AZ_PROFILER

#include "allocation.hpp"

#include <azprof/azprof_web.hpp>
#include <azprof/azprof_servlets.hpp>
#include <azprof/azprof_debug.hpp>
class ArtaObjectPool;
class xmlBuffer;

typedef void (*XmlServletFunction)(azprof::Request*, xmlBuffer*);

class HotSpotServlet : public azprof::Servlet {
public:
  static void start_arta_noproxy(); // Just for the no-proxy world
  static void init();
  static ArtaObjectPool* object_pool() {return _object_pool;}

  static bool add(const char *path, azprof::Servlet *servlet);
  static bool add(const char *path, const char *category, const char *subcategory, azprof::Servlet *servlet);
  static bool add(const char *path, int flags, azprof::Privilege, azprof::StatelessServlet::Function);
  static bool add0(
    const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege,
    azprof::StatelessServlet::Function
  );
  static bool add(
    const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege,
    azprof::StatelessServlet::Function
  );
  static bool add(const char *path, int flags, azprof::Privilege, XmlServletFunction);
  static bool add(
    const char *path, const char *category, const char *subcategory, int flags, azprof::Privilege, XmlServletFunction
  );

  HotSpotServlet(azprof::Servlet*);
  virtual void pre_service(azprof::Request*, azprof::Response*);
  virtual void service(azprof::Request*, azprof::Response*);
  virtual void post_service(azprof::Request*, azprof::Response*);

private:
  static void detach_callback(void*);

  static ArtaObjectPool* _object_pool;
  azprof::Servlet *_servlet;
};

class XmlServlet:public CHeapObj{
public:
  virtual void service(azprof::Request*, xmlBuffer*) = 0;
};

class StatelessXmlServlet : public XmlServlet {
public:
  StatelessXmlServlet(XmlServletFunction);

  virtual void service(azprof::Request*, xmlBuffer*);

private:
  XmlServletFunction _function;
};

class XmlServletAdapter : public azprof::Servlet {
public:
  XmlServletAdapter(int flags, azprof::Privilege required_privilege, XmlServlet *servlet);
  XmlServlet* servlet() {return _servlet;}
  virtual void service(azprof::Request*, azprof::Response*);
private:
  XmlServlet *_servlet;
};

#endif // AZ_PROFILER

#endif // ARTAQUERY_HPP
