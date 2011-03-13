/*
 * Copyright 1997-2004 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "commonAsm.hpp"
#include "disassembler_pd.hpp"
#include "jvmtiExport.hpp"
#include "ostream.hpp"
#include "stubCodeGenerator.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "os_os.inline.hpp"
#include "oop.inline.hpp"

// Implementation of StubCodeDesc

StubCodeDesc* StubCodeDesc::_list = NULL;
int           StubCodeDesc::_count = 0;


StubCodeDesc* StubCodeDesc::desc_for(address pc) {
  StubCodeDesc* p = _list;
  while (p != NULL && !p->contains(pc)) p = p->_next;
  // p == NULL || p->contains(pc)
  return p;
}


StubCodeDesc* StubCodeDesc::desc_for_index(int index) {
  StubCodeDesc* p = _list;
  while (p != NULL && p->index() != index) p = p->_next;
  return p;
}


const char* StubCodeDesc::name_for(address pc) {
  StubCodeDesc* p = desc_for(pc);
  return p == NULL ? NULL : p->name();
}


void StubCodeDesc::print_on(outputStream*os)const{
os->print(group());
os->print("::");
os->print(name());
  os->print(" [" INTPTR_FORMAT ", " INTPTR_FORMAT "[ (%d bytes)", (intptr_t)begin(), (intptr_t)end(), size_in_bytes());
}


void StubCodeDesc::print_xml_on(xmlBuffer *xb, bool ref) {
#ifdef AZ_PROFILER
  xmlElement xe(xb, ref ? "stubcode_ref" : "stubcode");
  xb->name_value_item("group", group());
  xb->name_value_item("name", name());
  if (ref) {
    if (xb->can_show_addrs()) {
      xmlElement xe(xb, "id");
      xb->print(PTR_FORMAT, this);
    }
  } else {
    ProfileEntry::print_xml_on(xb);
    const char *view = xb->request()->parameter_by_name("view");
    if ((view == NULL) || (strcasecmp(view, "asm") == 0)) {
      Disassembler::decode_xml(xb, begin(), end());
    } else if (strcasecmp(view, "callee") == 0) {
      RpcTreeNode::print_xml(xb, begin(), end() - 1, false);
    } else if (strcasecmp(view, "caller") == 0) {
      RpcTreeNode::print_xml(xb, begin(), end() - 1, true);
    }
  }
#endif // AZ_PROFILER
}



// Implementation of StubCodeGenerator

void StubCodeGenerator::stub_prolog(StubCodeDesc* cdesc) {
  // default implementation - do nothing
}


void StubCodeGenerator::stub_epilog(StubCodeDesc* cdesc) {
  // default implementation - record the cdesc
  if (_first_stub == NULL)  _first_stub = cdesc;
  _last_stub = cdesc;
}

// final _masm & codeblob setup
StubCodeGenerator::~StubCodeGenerator() {
  if (PrintStubCode) {
    bool saw_first = false;
    StubCodeDesc* toprint[1000];
    int toprint_len = 0;
    for (StubCodeDesc* cdesc = _last_stub; cdesc != NULL; cdesc = cdesc->_next) {
      toprint[toprint_len++] = cdesc;
      if (cdesc == _first_stub) { saw_first = true; break; }
    }
assert(saw_first||toprint_len==0,"must get both first & last");
    // Print in reverse order:
    for (int i = 0; i < toprint_len; i++) {
      StubCodeDesc* cdesc = toprint[i];
cdesc->print_on(tty);
      tty->cr();
      Disassembler::decode(cdesc->begin(), cdesc->end());
      tty->cr();
    }
  }
}


// Implementation of CodeMark

StubCodeMark::StubCodeMark(StubCodeGenerator* cgen, const char* group, const char* name, int framesize) {
  _cgen  = cgen;
  _cgen->assembler()->align(CodeEntryAlignment);
  _cdesc = new StubCodeDesc(group, name, _cgen->assembler()->pc(), framesize);
  _cgen->stub_prolog(_cdesc);
  // define the stub's beginning (= entry point) to be after the prolog:
  _cdesc->set_begin(_cgen->assembler()->pc());
}

StubCodeMark::~StubCodeMark() {
  _cgen->assembler()->flush();
  _cdesc->set_end(_cgen->assembler()->pc());
  assert(StubCodeDesc::_list == _cdesc, "expected order on list");
  _cgen->stub_epilog(_cdesc);
  if (JvmtiExport::should_post_dynamic_code_generated()) {
    JvmtiExport::post_dynamic_code_generated(_cdesc->name(), _cdesc->begin(), _cdesc->end());
  }
{MutexLocker ml(ThreadCritical_lock);
    hotspot_to_gdb_symbol_t *hsgdb = CodeCache::get_new_hsgdb();
    hsgdb->startPC = (uintptr_t)_cdesc->begin();
    hsgdb->codeBytes = _cdesc->size_in_bytes();
    hsgdb->frameSize = _cdesc->_framesize;
    hsgdb->nameAddress = _cdesc->name();
    hsgdb->nameLength = strlen(hsgdb->nameAddress);
    hsgdb->savedRBP = false;
    hsgdb->pad1 = hsgdb->pad2 = hsgdb->pad3 = 0;
  }
}

