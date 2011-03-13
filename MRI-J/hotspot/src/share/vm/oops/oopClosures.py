#!/usr/bin/python
# Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License version 2 only, as published by
# the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
# details (a copy is included in the LICENSE file that accompanied this code).
#
# You should have received a copy of the GNU General Public License version 2
# along with this work; if not, write to the Free Software Foundation,Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View,
# CA 94043 USA, or visit www.azulsystems.com if you need additional information
# or have any questions.


###
###  Generic Methods
###

DoFollowHeader = True
DontFollowHeader = False

DoPassKids = True
DontPassKids = False

DoFollowMethodCode = True
DontFollowMethodCode = False

DoFollowKlassTree = True
DontFollowKlassTree = False

StandardVariant = True
NativeInUseVariant = False


def format_params(gcm_type, variable, pass_klassId):
	if gcm_type == None:
		gcm_param = ''
		gcm_param_in_list = ''
	else:
		assert variable!=None, "variable name must be specific if a GCManager type was specified"
		gcm_param = "%s* %s" % (gcm_type, variable)
		gcm_param_in_list = "%s, " % gcm_param

	if variable == None:
		variable    = ''
		var_in_list = ''
	else:
		var_in_list = "%s, " % variable

	if pass_klassId == True:
		kid_param = ', klassId()'
	else:
		kid_param = ''

	return (gcm_param, gcm_param_in_list, variable, var_in_list, kid_param)


def generic_one_liner(klass_name, method_name, gcm_type, variable, operator):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, DontPassKids)

	print 'void ' +klass_name+ '::' +method_name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  ' +operator+ ';'
	print '}'
	print ''


###
### arrayKlassKlass
###

def arrayKlassKlass(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void arrayKlassKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_klass(), "must be klass");'
	print '  arrayKlass* ak = arrayKlass::cast(klassOop(obj));'
	print '  ' +operator+ '(' +var_in_list+ 'ak->adr_lower_dimension()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ak->adr_higher_dimension()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ak->adr_component_mirror()' +kid_param+ ');'
	print '  {'
	print '    HandleMark hm;'
	print '    ak->vtable()->' +name+ '(' +variable+ ');'
	print '  }'
	print '  klassKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '}'
	print ''


def arrayKlassKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('arrayKlassKlass', name, gcm_type, variable, operator)


def arrayKlassKlass_generate():
	arrayKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                          'ShouldNotReachHere()')

	arrayKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                'GPGC_OldCollector::mark_and_push', DoPassKids)

	arrayKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                          'ShouldNotReachHere()')

	arrayKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                'GPGC_OldCollector::mark_and_push', DontPassKids)

	arrayKlassKlass('GPGC_verify_no_cardmark', None, None,
	                'GPGC_Collector::assert_no_card_mark', DontPassKids)

	arrayKlassKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                          'GPGC_verify_no_cardmark(obj)')

	arrayKlassKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                          'GPGC_verify_no_cardmark(obj)')

	arrayKlassKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                          'assert(obj->is_perm(), "arrayKlass should be in PermGen")')

	arrayKlassKlass('oop_follow_contents', None, None,
	                'MarkSweep::mark_and_push', DontPassKids)

	arrayKlassKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	                'PSParallelCompact::mark_and_push', DontPassKids)


###
### constMethodKlass
###

def constMethodKlass(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void constMethodKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_constMethod(), "object must be constMethod");'
	print '  constMethodOop cm = constMethodOop(obj);'
	print '  ' +operator+ '(' +var_in_list+ 'cm->adr_method()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'cm->adr_exception_table()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'cm->adr_stackmap_data()' +kid_param+ ');'
	print '}'
	print ''


def constMethodKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('constMethodKlass', name, gcm_type, variable, operator)


def constMethodKlass_generate():
	constMethodKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                           'ShouldNotReachHere()')

	constMethodKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                 'GPGC_OldCollector::mark_and_push', DoPassKids)

	constMethodKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                           'ShouldNotReachHere()')

	constMethodKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                 'GPGC_OldCollector::mark_and_push', DontPassKids)

	constMethodKlass('GPGC_verify_no_cardmark', None, None,
	                 'GPGC_Collector::assert_no_card_mark', DontPassKids)

	constMethodKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                           'GPGC_verify_no_cardmark(obj)')

	constMethodKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                           'GPGC_verify_no_cardmark(obj)')

	constMethodKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                           'assert(obj->is_perm(), "constMethodOop should be in PermGen")')

	constMethodKlass('oop_follow_contents', None, None,
	                 'MarkSweep::mark_and_push', DontPassKids)

	constMethodKlass('oop_follow_contents', 'ParCompactionManager', 'pcm',
	                 'PSParallelCompact::mark_and_push', DontPassKids)


###
### constantPoolKlass
###

def constantPoolKlass(name, gcm_type, variable, operator, verifier, process_tags, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print         'void constantPoolKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print         '  assert (obj->is_constantPool(), "obj must be constant pool");'
	print         '  constantPoolOop cp = (constantPoolOop) obj;'
	print         ''
	if verifier != None:
		print '  ' +verifier+ ';'
		print ''
	print         '  // If the tags array is null we are in the middle of allocating this constant pool'
	print         '  if (cp->tags() != NULL) {'
	print         '    // gc of constant pool contents'
	if process_tags != None:
		print '    typeArrayOop tags = (typeArrayOop) ' +process_tags+ '(cp->tags_addr()).as_oop();'
	print         '    heapRef* base = (heapRef*)cp->base();'
	print         '    for (int i = 0; i < cp->length(); i++) {'
	if process_tags == None:
		print '      if (cp->is_pointer_entry(i)) {'
	else:
		print '      if (constantPoolOopDesc::is_pointer_entry(tags,i)) {'
	print         '        if (base->not_null()) ' +operator+ '(' +var_in_list+ 'base' +kid_param+ ');'
	print         '      }'
	print         '      base++;'
	print         '    }'
	print         '  }'
	print         '  // gc of constant pool instance variables'
	print         '  ' +operator+ '(' +var_in_list+ '(heapRef*)cp->tags_addr()' +kid_param+ ');'
	print         '  ' +operator+ '(' +var_in_list+ 'cp->cache_addr()' +kid_param+ ');'
	print         '  ' +operator+ '(' +var_in_list+ 'cp->pool_holder_addr()' +kid_param+ ');'
	print         '}'
	print         ''


def constantPoolKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('constantPoolKlass', name, gcm_type, variable, operator)


def constantPoolKlass_generate():
	#
	#  Note: original oop_follow_contents() from Sun only runs the operator on the final
	#  three elements if cp->tags()!=NULL.
	#
	constantPoolKlass('GPGC_verify_no_cardmark', None, None,
	                  'GPGC_Collector::assert_no_card_mark',
                          None, 'GPGC_Collector::remap_only', DontPassKids)

	constantPoolKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                            'ShouldNotReachHere()')

	constantPoolKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                  'GPGC_OldCollector::mark_and_push',
                          'DEBUG_ONLY( GPGC_verify_no_cardmark(obj) )', 'GPGC_Collector::remap_only', DoPassKids)

	constantPoolKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                            'ShouldNotReachHere()')

	constantPoolKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                  'GPGC_OldCollector::mark_and_push',
                          None, 'GPGC_Collector::remap_only', DontPassKids)

	constantPoolKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                            'ShouldNotReachHere()')

	constantPoolKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                            '// No cardmarks, can\'t verify here, so verified in GPGC_oop_follow_contents')

	constantPoolKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                            'assert(obj->is_perm(), "constantPoolOop should be in PermGen")')

	constantPoolKlass('oop_follow_contents', None, None,
	                  'MarkSweep::mark_and_push',
                          None, None, DontPassKids)

	constantPoolKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	                  'PSParallelCompact::mark_and_push',
                          None, None, DontPassKids)


###
### constantPoolCacheKlass
###

def constantPoolCacheKlass(name, gcm_type, variable, operator, entry_operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void constantPoolCacheKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");'
	print '  constantPoolCacheOop cache = (constantPoolCacheOop)obj;'
	print '  // gc of constant pool cache instance variables'
	print '  ' +operator+ '(' +var_in_list+ 'cache->constant_pool_addr()' +kid_param+ ');'
	print '  // gc of constant pool cache entries'
	print '  int i = cache->length();'
	print '  while (i-- > 0) cache->entry_at(i)->' +entry_operator+ '(' +variable+kid_param+ ');'
	print '}'
	print ''


def constantPoolCacheKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('constantPoolCacheKlass', name, gcm_type, variable, operator)


def constantPoolCacheKlass_generate():
	constantPoolCacheKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                                 'ShouldNotReachHere()')

	constantPoolCacheKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                       'GPGC_OldCollector::mark_and_push', 'GPGC_follow_contents', DoPassKids)

	constantPoolCacheKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                                 'ShouldNotReachHere()')

	constantPoolCacheKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                       'GPGC_OldCollector::mark_and_push', 'GPGC_follow_contents', DontPassKids)

	constantPoolCacheKlass('GPGC_verify_no_cardmark', None, None,
	                       'GPGC_Collector::assert_no_card_mark', 'GPGC_verify_no_cardmark', DontPassKids)

	constantPoolCacheKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                                 'GPGC_verify_no_cardmark(obj)')

	constantPoolCacheKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                                 'GPGC_verify_no_cardmark(obj)')

	constantPoolCacheKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                                 'assert(obj->is_perm(), "constantPoolCacheOop should be in PermGen")')

	constantPoolCacheKlass('oop_follow_contents', None, None,
	                       'MarkSweep::mark_and_push', 'follow_contents', DontPassKids)

	constantPoolCacheKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	                       'PSParallelCompact::mark_and_push', 'follow_contents', DontPassKids)


###
### constantPoolCacheEntry
###

def constantPoolCacheEntry(name, gcm_type, variable, operator, pass_klassId):
        gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	if pass_klassId == True:
		kid_param = ', klassId'
		gcm_param = gcm_param + ', int klassId'

        print 'void ConstantPoolCacheEntry::' +name+ '(' +gcm_param+ ') {'
	print '  assert(in_words(size()) == 4, "check code below - may need adjustment");'
	print '  // field[1] is always oop or NULL'
	print '  ' +operator+ '(' +var_in_list+ '(heapRef*)&_f1' +kid_param+ ');'
	print '  if (is_vfinal()) {'
	print '    ' +operator+ '(' +var_in_list+ '(heapRef*)&_f2' +kid_param+ ');'
	print '  }'
	print '}'
	print ''


def constantPoolCacheEntry_generate():
	constantPoolCacheEntry('GPGC_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
                               'GPGC_OldCollector::mark_and_push', DoPassKids)

	constantPoolCacheEntry('GPGC_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
                               'GPGC_OldCollector::mark_and_push', DontPassKids)

	constantPoolCacheEntry('GPGC_verify_no_cardmark', None, None,
                               'GPGC_Collector::assert_no_card_mark', DontPassKids)
	
	constantPoolCacheEntry('follow_contents', None, None,
                               'MarkSweep::mark_and_push', DontPassKids)

	constantPoolCacheEntry('follow_contents', 'ParCompactionManager', 'cm',
                               'PSParallelCompact::mark_and_push', DontPassKids)


###
### instanceKlass
###

def instanceKlass_non_static(name, gcm_type, variable, operator, assertion, do_header, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void instanceKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj!=NULL, "can\'t follow the content of NULL object");'
	if do_header :
		print '  obj->follow_header(' +variable+ ');'
	print '  OopMapBlock* map     = start_of_nonstatic_oop_maps();'
	print '  OopMapBlock* end_map = map + nonstatic_oop_map_size();'
	print '  while (map < end_map) {'
	print '    heapRef* start = (heapRef*)obj->ref_field_addr(map->offset());'
	print '    Prefetch::read(start, 0);'
	print '    heapRef* end   = start + map->length();'
	print '    while (start < end) {'
	print '      heapRef start_ref = UNPOISON_OBJECTREF(*start, start);'
	print '      if (start_ref.not_null()) {'
	print '        assert(start_ref.is_heap(), "should be a heapRef");'
	print '        assert(' +assertion+ '(start_ref.as_oop()), "should be in heap or stack");'
	print '        ' +operator+ '(' +var_in_list+ 'start' +kid_param+ ');'
	print '      }'
	print '      start++;'
	print '    }'
	print '    map++;'
	print '  }'
	print '}'
	print ''


def instanceKlass_static(name, gcm_type, variable, operator, assertion, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)
	
	print 'void instanceKlass::' +name+ '(' +gcm_param+ ') {'
	print '  heapRef* start = start_of_static_fields();'
	print '  heapRef* end   = start + static_oop_field_size();'
	print '  while (start < end) {'
	print '    heapRef start_ref = UNPOISON_OBJECTREF(*start, start);'
	print '    if (start_ref.not_null()) {'
	print '      assert(start_ref.is_heap(), "should be a heapRef");'
	print '      assert(' +assertion+ '(start_ref.as_oop()), "should be in heap or stack");'
	print '      ' +operator+ '(' +var_in_list+ 'start' +kid_param+ ');'
	print '    }'
	print '    start++;'
	print '  }'
	print '}'
	print ''


def instanceKlass_generate():
	instanceKlass_static('follow_static_fields', None, None,
	                     'MarkSweep::mark_and_push',
	                     'Universe::heap()->is_in_reserved', DontPassKids)

	instanceKlass_static('follow_static_fields', 'ParCompactionManager', 'cm',
	                     'PSParallelCompact::mark_and_push',
	                     'Universe::heap()->is_in_reserved', DontPassKids)

	instanceKlass_static('GPGC_follow_static_fields', 'GPGC_GCManagerOldStrong', 'gcm',
	                     'GPGC_OldCollector::mark_and_push',
	                     'Universe::is_in_allocation_area', DoPassKids)

	instanceKlass_static('GPGC_follow_static_fields', 'GPGC_GCManagerOldFinal', 'gcm',
	                     'GPGC_OldCollector::mark_and_push',
	                     'Universe::is_in_allocation_area', DontPassKids)

	instanceKlass_static('GPGC_static_fields_update_cardmark', None, None,
	                     'GPGC_OldCollector::update_card_mark',
	                     'Universe::is_in_allocation_area', DontPassKids)

	instanceKlass_non_static('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                         'GPGC_NewCollector::mark_and_push',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DoPassKids)

	instanceKlass_non_static('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                         'GPGC_OldCollector::mark_and_push',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DoPassKids)

	instanceKlass_non_static('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                         'GPGC_NewCollector::mark_and_push',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DontPassKids)

	instanceKlass_non_static('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                         'GPGC_OldCollector::mark_and_push',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DontPassKids)

	instanceKlass_non_static('GPGC_newgc_oop_update_cardmark', None, None,
	                         'GPGC_NewCollector::update_card_mark',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DontPassKids)

	instanceKlass_non_static('GPGC_oldgc_oop_update_cardmark', None, None,
	                         'GPGC_OldCollector::update_card_mark',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DontPassKids)

	instanceKlass_non_static('GPGC_mutator_oop_update_cardmark', None, None,
	                         'GPGC_NewCollector::mutator_update_card_mark',
	                         'Universe::is_in_allocation_area',
	                         DontFollowHeader, DontPassKids)

	instanceKlass_non_static('oop_follow_contents', None, None,
	                         'MarkSweep::mark_and_push',
	                         'Universe::heap()->is_in_reserved',
	                         DoFollowHeader, DontPassKids)

	instanceKlass_non_static('oop_follow_contents', 'ParCompactionManager', 'cm',
	                         'PSParallelCompact::mark_and_push',
	                         'Universe::heap()->is_in_reserved',
	                         DoFollowHeader, DontPassKids)


###
### instanceKlassKlass
###

def instanceKlassKlass(name, gcm_type, variable, operator, follow_static, closure_prep, closure, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void instanceKlassKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert(obj->is_klass(),"must be a klass");'
	print '  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(), "must be instance klass");'
	print ''
	print '  instanceKlass* ik = instanceKlass::cast(klassOop(obj));'
	print '  ik->' +follow_static+ '(' +variable+ ');'
	print '  {'
	print '    ResourceMark rm;'
	print '    HandleMark hm;'
	print '    ik->vtable()->' +name+ '(' +variable+ ');'
	print '    ik->itable()->' +name+ '(' +variable+ ');'
	print '  }'
	print ''
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_array_klasses()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_methods()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_method_ordering()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_local_interfaces()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_transitive_interfaces()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_fields()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_constants()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_class_loader()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_source_file_name()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_source_debug_extension()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_inner_classes()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_protection_domain()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_signers()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_generic_signature()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_class_annotations()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_fields_annotations()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_methods_annotations()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_methods_parameter_annotations()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_methods_default_annotations()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'ik->adr_dependent_mco()' +kid_param+ ');'
	print ''
	print '  // We do not follow adr_implementor() here. It is followed later'
	print '  // in instanceKlass::follow_weak_klass_links()'
	print ''
	print '  klassKlass::' +name+ '(' +var_in_list+ 'obj);'
	print ''
	if closure_prep != None:
		print '  ' +closure_prep+ ';'
	print '  iterate_c_heap_oops(ik, ' +closure+ ');'
	print '}'
	print ''


def instanceKlassKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('instanceKlassKlass', name, gcm_type, variable, operator)


def instanceKlassKlass_generate():
	instanceKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                             'ShouldNotReachHere()')

	instanceKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                   'GPGC_OldCollector::mark_and_push', 'GPGC_follow_static_fields',
	                   None, 'gcm->mark_and_push_closure()', DoPassKids)

	instanceKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                             'ShouldNotReachHere()')

	instanceKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                   'GPGC_OldCollector::mark_and_push', 'GPGC_follow_static_fields',
	                   None, 'gcm->mark_and_push_closure()', DontPassKids)

	instanceKlassKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                             'ShouldNotReachHere()')

	print '//'
	print '// OldGC cardmark update has lots of special case handling, and so is still found in'
	print '// instanceKlassKlass.cpp.'
	print '//'
	print '// void instanceKlassKlass::GPGC_oldgc_oop_update_cardmark(oop obj)'
	print '//'
	print ''

	instanceKlassKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                             'assert(obj->is_perm(), "instanceKlass should be in PermGen")')

	instanceKlassKlass('oop_follow_contents', None, None,
	                   'MarkSweep::mark_and_push', 'follow_static_fields',
	                   None, '&MarkSweep::mark_and_push_closure', DontPassKids)

	instanceKlassKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	                   'PSParallelCompact::mark_and_push', 'follow_static_fields',
	                   'PSParallelCompact::MarkAndPushClosure mark_and_push_closure(cm)', '&mark_and_push_closure',
	                   DontPassKids)


###
### instanceRefKlass
###

def instanceRefKlass_weak(name, gcm_type, variable, operator, test_class, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void instanceRefKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  heapRef* referent_addr = (heapRef*)java_lang_ref_Reference::referent_addr(obj);'
	print '  assert0(objectRef::is_null_or_heap(referent_addr));'
	print ''
	print '  debug_only('
	print '    if(TraceReferenceGC && PrintGCDetails) {'
	print '      gclog_or_tty->print_cr("instanceRefKlass::' +name+ ' " PTR_FORMAT, obj);'
	print '    }'
	print '  )'
	print ''
	print '  heapRef referent = ALWAYS_UNPOISON_OBJECTREF(*referent_addr);'
	print ''
	print '  if (referent.not_null()) {'
	print '    if (' +test_class+ '::is_unmarked_and_discover_reference(' +var_in_list+ 'referent, obj, reference_type()))'
	print '    {'
	print '      // reference grabbed by ref_processor, referent will be traversed later'
	print '      instanceKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '      debug_only('
	print '        if (TraceReferenceGC && PrintGCDetails) {'
	print '          gclog_or_tty->print_cr("       Non NULL enqueued " PTR_FORMAT, obj);'
	print '        }'
	print '      )'
	print '      return;'
	print '    } else {'
	print '      // treat referent as normal oop'
	print '      debug_only('
	print '        if (TraceReferenceGC && PrintGCDetails) {'
	print '          gclog_or_tty->print_cr("       Non NULL normal " PTR_FORMAT, obj);'
	print '        }'
	print '      )'
	print '      ' +operator+ '(' +var_in_list+ 'referent_addr' +kid_param+ ');'
	print '    }'
	print '  }'
	print '  // treat next as normal oop.  next is a link in the pending list.'
	print '  heapRef* next_addr = (heapRef*)java_lang_ref_Reference::next_addr(obj);'
	print '  assert0(objectRef::is_null_or_heap(next_addr));'
	print '  debug_only('
	print '    if (TraceReferenceGC && PrintGCDetails) {'
	print '      gclog_or_tty->print_cr("   Process next as normal " PTR_FORMAT, next_addr);'
	print '    }'
	print '  )'
	print '  ' +operator+ '(' +var_in_list+ 'next_addr' +kid_param+ ');'
	print '  instanceKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '}'
	print ''


def instanceRefKlass_GPGC(name, gcm_type, variable, operator, test_class, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void instanceRefKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
        print '  heapRef* referent_addr = (heapRef*)java_lang_ref_Reference::referent_addr(obj);'
	print '  assert0(objectRef::is_null_or_heap(referent_addr));'
        print ''
	print '  heapRef referent = ALWAYS_UNPOISON_OBJECTREF(*referent_addr);'
        print ''
	print '  if (referent.not_null()) {'
	print '    if (' +test_class+ '::mark_through_non_strong_ref(' +var_in_list+ 'referent, obj, reference_type())) {'
	print '      ' +operator+ '(' +var_in_list+ 'referent_addr' +kid_param+ ');'
	print '    }'
        print '  }'
        print ''
	print '  instanceKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '}'
	print ''


def instanceRefKlass_all(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void instanceRefKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  // treat referent as normal oop'
	print '  objectRef* referent_addr = java_lang_ref_Reference::referent_addr(obj);'
	print '  assert0(objectRef::is_null_or_heap(referent_addr));'
	print '  ' +operator+ '(' +var_in_list+ 'referent_addr' +kid_param+ ');'
	print ''
	print '  // treat next as normal oop.  next is a link in the pending list.'
	print '  objectRef* next_addr = java_lang_ref_Reference::next_addr(obj);'
	print '  assert0(objectRef::is_null_or_heap(next_addr));'
	print '  ' +operator+ '(' +var_in_list+ 'next_addr' +kid_param+ ');'
	print ''
	print '  instanceKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '}'
	print ''


def instanceRefKlass_generate():
	instanceRefKlass_GPGC('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                      'GPGC_NewCollector::mark_and_push', 'GPGC_NewCollector', DoPassKids)

	instanceRefKlass_GPGC('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                      'GPGC_OldCollector::mark_and_push', 'GPGC_OldCollector', DoPassKids)

	instanceRefKlass_all('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                     'GPGC_NewCollector::mark_and_push', DontPassKids)

	instanceRefKlass_all('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                     'GPGC_OldCollector::mark_and_push', DontPassKids)

	instanceRefKlass_all('GPGC_newgc_oop_update_cardmark', None, None,
	                     'GPGC_NewCollector::update_card_mark', DontPassKids)

	instanceRefKlass_all('GPGC_oldgc_oop_update_cardmark', None, None,
	                     'GPGC_OldCollector::update_card_mark', DontPassKids)

	instanceRefKlass_all('GPGC_mutator_oop_update_cardmark', None, None,
	                     'GPGC_NewCollector::mutator_update_card_mark', DontPassKids)

	instanceRefKlass_weak('oop_follow_contents', None, None,
	                      'MarkSweep::mark_and_push', 'MarkSweep', DontPassKids)

	instanceRefKlass_weak('oop_follow_contents', 'ParCompactionManager', 'cm',
	                      'PSParallelCompact::mark_and_push', 'PSParallelCompact', DontPassKids)


def klassKlass(name, gcm_type, variable, operator, revisit_later, do_klass_tree, do_header, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print         'void klassKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print         '  Klass* k = Klass::cast(klassOop(obj));'
	print         '  // If we are alive it is valid to keep our superclass and subtype caches alive'
	print         '  ' +operator+ '(' +var_in_list+ 'k->adr_super()' +kid_param+ ');'
	print         '  ' +operator+ '(' +var_in_list+ 'k->adr_secondary_supers()' +kid_param+ ');'
	print         '  ' +operator+ '(' +var_in_list+ 'k->adr_java_mirror()' +kid_param+ ');'
	print         '  ' +operator+ '(' +var_in_list+ 'k->adr_name()' +kid_param+ ');'
	if revisit_later != None:
		print '  // We follow the subklass and sibling links at the end of the'
		print '  // marking phase, since otherwise following them will prevent'
		print '  // class unloading (all classes are transitively linked from'
		print '  // java.lang.Object).'
		print '  ' +revisit_later+ '::revisit_weak_klass_link(' +var_in_list+ 'k);'
	if do_klass_tree:
		print '  ' +operator+ '(' +var_in_list+ 'k->adr_subklass()' +kid_param+ ');'
		print '  ' +operator+ '(' +var_in_list+ 'k->adr_next_sibling()' +kid_param+ ');'
	if do_header:
		print '  obj->follow_header(' +variable+ ');'
	print         '}'
	print         ''


def klassKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('klassKlass', name, gcm_type, variable, operator)


def klassKlass_generate():
	klassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                     'ShouldNotReachHere()')

	klassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	           'GPGC_OldCollector::mark_and_push', 'GPGC_OldCollector',
	           DontFollowKlassTree, DontFollowHeader, DoPassKids)

	klassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                     'ShouldNotReachHere()')

	klassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	           'GPGC_OldCollector::mark_and_push', None,
	           DontFollowKlassTree, DontFollowHeader, DontPassKids)

	klassKlass('GPGC_verify_no_cardmark', None, None,
	           'GPGC_Collector::assert_no_card_mark', None,
	           DoFollowKlassTree, DontFollowHeader, DontPassKids)

	klassKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                     'GPGC_verify_no_cardmark(obj)')

	klassKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                     'GPGC_verify_no_cardmark(obj)')

	klassKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                     'assert(obj->is_perm(), "klass should be in PermGen")')

	klassKlass('oop_follow_contents', None, None,
	           'MarkSweep::mark_and_push', 'MarkSweep',
	           DontFollowKlassTree, DoFollowHeader, DontPassKids)

	klassKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	           'PSParallelCompact::mark_and_push', 'PSParallelCompact',
	           DontFollowKlassTree, DoFollowHeader, DontPassKids)


###
### klassItable
###

def klassItable(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	if pass_klassId == True:
		kid_param = ', kid'

	print 'void klassItable::' +name+ '(' +gcm_param+ ') {'
	if pass_klassId == True:
		print '  int kid = _klass->klassId();'
		print ''
	print '  // offset table'
	print '  itableOffsetEntry* ioe = offset_entry(0);'
	print '  for(int i = 0; i < _size_offset_table; i++) {'
	print '    ' +operator+ '(' +var_in_list+ '(heapRef*)&ioe->_interface' +kid_param+ ');'
	print '    ioe++;'
	print '  }'
	print ''
	print '  // method table'
	print '  itableMethodEntry* ime = method_entry(0);'
	print '  for(int j = 0; j < _size_method_table; j++) {'
	print '    ' +operator+ '(' +var_in_list+ '(heapRef*)&ime->_method' +kid_param+ ');'
	print '    ime++;'
	print '  }'
	print '}'
	print ''


def klassItable_one_liner(name, gcm_type, variable, operator):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, DontPassKids)

	print 'void klassItable::' +name+ '(' +gcm_param+ ') {'
	print '  ' +operator+ ';'
	print '}'
	print ''


def klassItable_generate():
	klassItable_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                      'ShouldNotReachHere()')

	klassItable('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	            'GPGC_OldCollector::mark_and_push', DoPassKids)

	klassItable_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                      'ShouldNotReachHere()')

	klassItable('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	            'GPGC_OldCollector::mark_and_push', DontPassKids)

	klassItable('GPGC_verify_no_cardmark', None, None,
	            'GPGC_Collector::assert_no_card_mark', DontPassKids)

	klassItable('oop_follow_contents', None, None,
	            'MarkSweep::mark_and_push', DontPassKids)

	klassItable('oop_follow_contents', 'ParCompactionManager', 'cm',
	            'PSParallelCompact::mark_and_push', DontPassKids)


###
### klassVtable
###

def klassVtable(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	if pass_klassId == True:
		kid_param = ', klass()->klassId()'

	print 'void klassVtable::' +name+ '(' +gcm_param+ ') {'
	print '  int len = length();'
	print '  for (int i = 0; i < len; i++) {'
	print '    ' +operator+ '(' +var_in_list+ '(heapRef*)adr_method_at(i)' +kid_param+ ');'
	print '  }'
	print '}'
	print ''


def klassVtable_one_liner(name, gcm_type, variable, operator):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, DontPassKids)

	print 'void klassVtable::' +name+ '(' +gcm_param+ ') {'
	print '  ' +operator+ ';'
	print '}'
	print ''


def klassVtable_generate():
	klassVtable_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                      'ShouldNotReachHere()')

	klassVtable('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	            'GPGC_OldCollector::mark_and_push', DoPassKids)

	klassVtable_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                      'ShouldNotReachHere()')

	klassVtable('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	            'GPGC_OldCollector::mark_and_push', DontPassKids)

	klassVtable('GPGC_verify_no_cardmark', None, None,
	            'GPGC_Collector::assert_no_card_mark', DontPassKids)

	klassVtable('oop_follow_contents', None, None,
	            'MarkSweep::mark_and_push', DontPassKids)

	klassVtable('oop_follow_contents', 'ParCompactionManager', 'cm',
	            'PSParallelCompact::mark_and_push', DontPassKids)


###
### methodKlass
###

def methodKlass(name, gcm_type, variable, operator, standard_variant, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void methodKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_method(), "object must be method");'
	print '  methodOop m = methodOop(obj);'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_constMethod()'  +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_constants()'    +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_codeRef()'      +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_codeRef_list()' +kid_param+ ');'
	print '}'
	print ''


def methodKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('methodKlass', name, gcm_type, variable, operator)


def methodKlass_generate():
	methodKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                      'ShouldNotReachHere()')

	methodKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	            'GPGC_OldCollector::mark_and_push', NativeInUseVariant, DoPassKids)

	methodKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                      'ShouldNotReachHere()')

	methodKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	            'GPGC_OldCollector::mark_and_push', NativeInUseVariant, DontPassKids)

	methodKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                      'ShouldNotReachHere()')

	methodKlass('GPGC_oldgc_oop_update_cardmark', None, None,
	            'GPGC_Collector::assert_no_card_mark', StandardVariant, DontPassKids)

	methodKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                      'assert(obj->is_perm(), "methodOop should be in PermGen")')

	methodKlass('oop_follow_contents', None, None,
	            'MarkSweep::mark_and_push', StandardVariant, DontPassKids)

	methodKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	            'PSParallelCompact::mark_and_push', StandardVariant, DontPassKids)


###
###  methodCodeKlass
###

def methodCodeKlass(name, gcm_type, variable, operator, pass_klassId):
	gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

	print 'void methodCodeKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert(obj->is_methodCode(), "must be methodCode oop");'
	print '  methodCodeOop m = methodCodeOop(obj);'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_method()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_next  ()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_static_refs()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_mco_call_targets()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_dep_klasses()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_dep_methods()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'm->adr_blob_owner()' +kid_param+ ');'
	print '  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() ) {'
	print '    ' +operator+ '(' +var_in_list+ 'vtable->adr_owner()' +kid_param+ ');'
	print '  }'
	print '}'
	print ''


def methodCodeKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('methodCodeKlass', name, gcm_type, variable, operator)


def methodCodeKlass_generate():
	methodCodeKlass('oop_follow_contents', None, None,
                        'MarkSweep::mark_and_push', DontPassKids)

	methodCodeKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
                        'PSParallelCompact::mark_and_push', DontPassKids)

	methodCodeKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
                                  'ShouldNotReachHere()')

	methodCodeKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
                        'GPGC_OldCollector::mark_and_push', DoPassKids)

	methodCodeKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
                                  'ShouldNotReachHere()')

	methodCodeKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
                        'GPGC_OldCollector::mark_and_push', DontPassKids)

	methodCodeKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
                                  'ShouldNotReachHere()')

	methodCodeKlass('GPGC_oldgc_oop_update_cardmark', None, None,
	                'GPGC_OldCollector::update_card_mark', DontPassKids)

	methodCodeKlass('GPGC_mutator_oop_update_cardmark', None, None,
	                'GPGC_NewCollector::mutator_update_card_mark', DontPassKids)


###
### objArrayKlass
###

def objArrayKlass(name, gcm_type, variable, operator, follow_header, pass_klassId):
        gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

        print 'void objArrayKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_array(), "obj must be array");'
	print '  arrayOop a = arrayOop(obj);'
	if follow_header:
		print '  a->follow_header(' +variable+ ');'
	print '  heapRef* base      = (heapRef*)a->base(T_OBJECT);'
	print '  heapRef* const end = base + a->length();'
	print '  while (base < end) {'
	print '    heapRef base_ref = UNPOISON_OBJECTREF(*base, base);'
	print '    if (base_ref.not_null()) {'
	print '      assert0(base_ref.is_heap());'
	print '      ' +operator+ '(' +var_in_list+ 'base' +kid_param+ ');'
	print '    }'
	print '    base++;'
	print '  }'
	print '}'
	print ''


def objArrayKlass_chunking(name, gcm_type, variable, operator, pass_klassId):
        gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

        print 'void objArrayKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_array(), "obj must be array");'
	print '  arrayOop a = arrayOop(obj);'
	print '  heapRef* base      = (heapRef*)a->base(T_OBJECT);'
	print '  heapRef* const end = base + a->length();'
	print ''
	print '  if ( a->length() > GPGCArrayChunkThreshold ) {'
	print '    // For long arrays, throw "chunks" of the array onto the marking stack, to parallelize the work.'
	print '    heapRef* chunk_base  = (heapRef*) round_down(uintptr_t(base)+BytesPerCacheLine-1, BytesPerCacheLine);'
	print '    heapRef* chunk_start = chunk_base;'
	print '    heapRef* chunk_end   = chunk_start + GPGCArrayChunkSize;'
	print '    // First enqueue all the chunks, cache line aligned.'
	print '    while ( chunk_end <= end ) {'
	print '      ' +variable+ '->push_array_chunk_to_stack(chunk_start, GPGCArrayChunkSize' +kid_param+ ');'
	print '      chunk_start = chunk_end;'
	print '      chunk_end   = chunk_start + GPGCArrayChunkSize;'
	print '    }'
	print '    // Then mark everything up to the first chunk.'
	print '    while (base < chunk_base) {'
	print '      heapRef base_ref = UNPOISON_OBJECTREF(*base, base);'
	print '      if (base_ref.not_null()) {'
	print '        assert0(base_ref.is_heap());'
	print '        ' +operator+ '(' +var_in_list+ 'base' +kid_param+ ');'
	print '      }'
	print '      base++;'
	print '    }'
	print '    // And setup the pointers to mark everything after the last chunk.'
	print '    base = chunk_start;'
	print '  }'
	print ''
	print '  while (base < end) {'
	print '    heapRef base_ref = UNPOISON_OBJECTREF(*base, base);'
	print '    if (base_ref.not_null()) {'
	print '      assert0(base_ref.is_heap());'
	print '      ' +operator+ '(' +var_in_list+ 'base' +kid_param+ ');'
	print '    }'
	print '    base++;'
	print '  }'
	print '}'
	print ''


def objArrayKlass_generate():
	objArrayKlass_chunking('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                       'GPGC_NewCollector::mark_and_follow', DoPassKids)

	objArrayKlass_chunking('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                       'GPGC_OldCollector::mark_and_follow', DoPassKids)

	objArrayKlass_chunking('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                       'GPGC_NewCollector::mark_and_follow', DontPassKids)

	objArrayKlass_chunking('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                       'GPGC_OldCollector::mark_and_follow', DontPassKids)

	objArrayKlass('GPGC_newgc_oop_update_cardmark', None, None,
	              'GPGC_NewCollector::update_card_mark', DontFollowHeader, DontPassKids)

	objArrayKlass('GPGC_oldgc_oop_update_cardmark', None, None,
	              'GPGC_OldCollector::update_card_mark', DontFollowHeader, DontPassKids)

	objArrayKlass('GPGC_mutator_oop_update_cardmark', None, None,
	              'GPGC_NewCollector::mutator_update_card_mark', DontFollowHeader, DontPassKids)

	objArrayKlass('oop_follow_contents', None, None,
	              'MarkSweep::mark_and_follow', DoFollowHeader, DontPassKids)

	objArrayKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	              'PSParallelCompact::mark_and_follow', DoFollowHeader, DontPassKids)


###
### objArrayKlassKlass
###

def objArrayKlassKlass(name, gcm_type, variable, operator, pass_klassId):
        gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, pass_klassId)

        print 'void objArrayKlassKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert(obj->is_klass(), "must be klass");'
	print '  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");'
	print ''
	print '  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);'
	print '  ' +operator+ '(' +var_in_list+ 'oak->element_klass_addr()' +kid_param+ ');'
	print '  ' +operator+ '(' +var_in_list+ 'oak->bottom_klass_addr()' +kid_param+ ');'
	print ''
	print '  arrayKlassKlass::' +name+ '(' +var_in_list+ 'obj);'
	print '}'
	print ''


def objArrayKlassKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('objArrayKlassKlass', name, gcm_type, variable, operator)


def objArrayKlassKlass_generate():
	objArrayKlassKlass('oop_follow_contents', None, None,
	                   'MarkSweep::mark_and_push', DontPassKids)

	objArrayKlassKlass('oop_follow_contents', 'ParCompactionManager', 'cm',
	                   'PSParallelCompact::mark_and_push', DontPassKids)

	objArrayKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                             'ShouldNotReachHere()')

	objArrayKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm',
	                   'GPGC_OldCollector::mark_and_push', DoPassKids)

	objArrayKlassKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                             'ShouldNotReachHere()')

	objArrayKlassKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm',
	                   'GPGC_OldCollector::mark_and_push', DontPassKids)

	objArrayKlassKlass('GPGC_verify_no_cardmark', None, None,
	                   'GPGC_Collector::assert_no_card_mark', DontPassKids)

	objArrayKlassKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                             'GPGC_verify_no_cardmark(obj)')

	objArrayKlassKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                             'GPGC_verify_no_cardmark(obj)')

	objArrayKlassKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                             'assert(obj->is_perm(), "objArrayKlass should be in PermGen")')


###
### symbolKlass
###

def symbolKlass_weak(name, gcm_type, variable):
        gcm_param, gcm_param_in_list, variable, var_in_list, kid_param = format_params(gcm_type, variable, DontPassKids)

        print 'void symbolKlass::' +name+ '(' +gcm_param_in_list+ 'oop obj) {'
	print '  assert (obj->is_symbol(), "object must be symbol");'
	print '  // WARNING: For this collector, symbolOops are weak roots, and the collector'
	print '  // depends on them not linking through to other objects.  If you are thinking'
	print '  // of adding a heapRef traversal here, you are probably breaking the collector!'
	print '  // You better go look at symbolTable.hpp and understand the concurrent lockless'
	print '  // collection of symbolOops.'
	print '}'
	print ''


def symbolKlass_one_liner(name, gcm_type, variable, operator):
	generic_one_liner('symbolKlass', name, gcm_type, variable, operator)


def symbolKlass_generate():
	symbolKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm',
	                      'ShouldNotReachHere()')

	symbolKlass_weak('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm')

	symbolKlass_one_liner('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm',
	                      'ShouldNotReachHere()')

	symbolKlass_weak('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm')

	symbolKlass_one_liner('GPGC_newgc_oop_update_cardmark', None, None,
	                      'ShouldNotReachHere()')

	symbolKlass_one_liner('GPGC_oldgc_oop_update_cardmark', None, None,
	                      'assert (obj->is_symbol(), "object must be symbol")')

	symbolKlass_one_liner('GPGC_mutator_oop_update_cardmark', None, None,
	                      'assert(obj->is_perm(), "symbolOop should be in PermGen")')

	symbolKlass_one_liner('oop_follow_contents', None, None,
	                      'assert (obj->is_symbol(), "object must be symbol")')

	symbolKlass_one_liner('oop_follow_contents', 'ParCompactionManager', 'cm',
	                      'assert (obj->is_symbol(), "object must be symbol")')


###
### typeArrayKlass
###

def typeArrayKlass(name, gcm_type, variable):
        generic_one_liner('typeArrayKlass', name, gcm_type, variable, 'assert(obj->is_typeArray(),"must be a type array")')


def typeArrayKlass_generate():
	typeArrayKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerNewStrong', 'gcm')
	typeArrayKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldStrong', 'gcm')
	typeArrayKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerNewFinal', 'gcm')
	typeArrayKlass('GPGC_oop_follow_contents', 'GPGC_GCManagerOldFinal', 'gcm')
	typeArrayKlass('GPGC_newgc_oop_update_cardmark', None, None)
	typeArrayKlass('GPGC_oldgc_oop_update_cardmark', None, None)
	typeArrayKlass('GPGC_mutator_oop_update_cardmark', None, None)
	typeArrayKlass('oop_follow_contents', None, None)
	typeArrayKlass('oop_follow_contents', 'ParCompactionManager', 'cm')



import sys

if __name__ == "__main__":
	args = len(sys.argv)
	if len(sys.argv) != 2 :
		sys.stderr.write("Error: Found " +str(args)+ " arguments.\n")
		sys.exit("Usage: One argument specifying an output filename.")

	outputname = sys.argv[1]

	try:
		outputfile = open(outputname, "w")
	except IOError, (errno, strerror):
		print "I/O error(%s): %s" % (errno, strerror)
		sys.exit("Unable to open " +outputname)
	except:
		print "Unexpected error:", sys.exc_info()[0]
		sys.exit("Unable to open " +outputname)

	# redirect stdout to output file
	sys.stdout = outputfile

        print '// ================================ '
        print '// WARNING !!!! '
        print '// Machine Generated File!!!  '
        print '// Constructed from oopClosures.py'
        print '// WARNING !!!! '
        print '// ================================ '
	print '#include "arrayKlass.hpp"'
	print '#include "arrayKlassKlass.hpp"'
	print '#include "constMethodKlass.hpp"'
	print '#include "constantPoolKlass.hpp"'
	print '#include "cpCacheKlass.hpp"'
	print '#include "gpgc_gcManagerNewFinal.hpp"'
	print '#include "gpgc_gcManagerNewStrong.hpp"'
	print '#include "gpgc_gcManagerOldFinal.hpp"'
	print '#include "gpgc_newCollector.hpp"'
	print '#include "gpgc_newCollector.inline.hpp"'
	print '#include "gpgc_oldCollector.hpp"'
	print '#include "gpgc_oldCollector.inline.hpp"'
	print '#include "instanceKlassKlass.hpp"'
	print '#include "instanceRefKlass.hpp"'
	print '#include "javaClasses.hpp"'
	print '#include "klassVtable.hpp"'
	print '#include "markSweep.hpp"'
	print '#include "methodCodeKlass.hpp"'
	print '#include "methodKlass.hpp"'
	print '#include "objArrayKlass.hpp"'
	print '#include "objArrayKlassKlass.hpp"'
	print '#include "oop.hpp"'
	print '#include "ostream.hpp"'
	print '#include "psParallelCompact.hpp"'
	print '#include "resourceArea.hpp"'
	print '#include "symbolKlass.hpp"'
        print '#include "tickProfiler.hpp"'
	print ''
	print '#include "atomic_os_pd.inline.hpp"'
        print '#include "gpgc_pageInfo.inline.hpp"'
	print '#include "handles.inline.hpp"'
	print '#include "markSweep.inline.hpp"'
	print '#include "mutex.inline.hpp"'
	print '#include "oop.inline.hpp"'
	print '#include "orderAccess_os_pd.inline.hpp"'
	print '#include "prefetch_os_pd.inline.hpp"'
	print '#include "thread_os.inline.hpp"'
        print '#include "objectRef_pd.inline.hpp"'
	print ''
	print '#include "oop.inline2.hpp"'
	print ''

	arrayKlassKlass_generate()
	constMethodKlass_generate()
	constantPoolKlass_generate()
	constantPoolCacheKlass_generate()
	constantPoolCacheEntry_generate()
	instanceKlass_generate()
	instanceKlassKlass_generate()
	instanceRefKlass_generate()
	klassKlass_generate()
	klassItable_generate()
	klassVtable_generate()
	methodKlass_generate()
	methodCodeKlass_generate()
	objArrayKlass_generate()
	objArrayKlassKlass_generate()
	symbolKlass_generate()
	typeArrayKlass_generate()

	sys.exit(0)
