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
 * Linked list
 *
 */

#ifndef __NIO_LIST_H__
#define __NIO_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

/* following is copied from aztek/stddef.h XXX */
#define list_super(p, st, f) ((st *)((void *)(p) - offsetof(st, f)))

typedef struct list {
	struct list *next;
	struct list *prev;
} list_t;

typedef list_t  list_head_t;
typedef list_t  list_entry_t;

inline static void
__list_insert(list_t *prev, list_t *next, list_t *newx)
{
	newx->next = next;
	newx->prev = prev;
	prev->next = newx;
	next->prev = newx;
}

inline static void
__list_remove(list_t *prev, list_t *next, list_t *elem)
{
	prev->next = next;
	next->prev = prev;

	elem->next = elem->prev = elem;
}

extern list_t *list_remove_head(list_t *head);
extern list_t *list_remove_tail(list_t *head);
extern void list_remove(list_t *elem);

extern void list_add_before(list_t *elem, list_t *newx);
extern void list_add_after(list_t *elem, list_t *newx);
extern void list_move(list_head_t *from, list_head_t *to);
extern void list_join(list_t *dest, list_head_t *src);

#define list_is_empty(head)		((head)->next == (head))
#define list_add_head(head, newx)	list_add_after(head, newx)
#define list_add_tail(head, newx)	list_add_before(head, newx)

#define	LIST_INIT(name)		{ &(name), &(name) }
#define LIST_INIT_STATIC(name)	list_t name = LIST_INIT(name)
#define list_init(x)		((x)->next = (x)->prev = (x))

#define list_first(h)   ((h)->next)
#define list_end(h)     (h)

#define	FOR_LOOP(h, e)				\
		for ((e) = list_first((h));	\
		     (e) != list_end((h));	\
		     (e) = (e)->next)

#define	FOR_LOOP_NOINC(h, e)			\
		for ((e) = list_first((h));	\
		     (e) != list_end((h)); )

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif
