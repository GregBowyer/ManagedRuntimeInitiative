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


/*
 * Copied from AzTEK
 */

#include <os/utilities.h>
#include <os/list.h>

void
list_add_before(list_t *elem, list_t *new)
{
	__list_insert(elem->prev, elem, new);
}

void
list_add_after(list_t *elem, list_t *new)
{
	__list_insert(elem, elem->next, new);
}

list_t *
list_remove_head(list_t *head)
{
	list_t *elem = head->next;

	if (elem == head)
		return 0;

	__list_remove(head, elem->next, elem);

	return elem;
}

void
list_remove(list_t *elem)
{
	__list_remove(elem->prev, elem->next, elem);
}

void
list_move(list_head_t *from, list_head_t *to)
{
	if (list_is_empty(from)) {
		list_init(to);
		return;
	}

	os_assert(list_is_empty(to), "empty list");

	// At least one element
	to->next = from->next;
	to->prev = from->prev;

	from->next->prev = to;
	from->prev->next = to;

	list_init(from);
}

void
list_join(list_t *dest, list_head_t *src)
{
	if (list_is_empty(src))
		return;

	// At least one element,

	// Connect the ends of the second list 
	src->next->prev = dest;
	src->prev->next = dest->next;

	// Join the lists
	dest->next->prev = src->prev;
	dest->next = src->next; 

	list_init(src);
}

#ifdef NOTDEF
list_t *
list_remove_tail(list_t *head)
{
}
#endif
