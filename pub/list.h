/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#ifndef PUB_LIST_H
#define PUB_LIST_H

#include <stddef.h>
#include <stdbool.h>

struct list_entry {
	struct list_entry		*prev;
	struct list_entry		*next;
};

#ifndef container_of
#define container_of(p, t, m)	((t *) ((char *)p - offsetof(t, m)))
#endif

#define list_entry(p, t, m)		container_of(p, t, m)
#define list_for_each(pos, head)						\
	for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)
#define list_for_each_rev(pos, head)					\
	for ((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)
#define list_for_each_del(pos, head)					\
	for ((pos) = list_del_head(head); (pos); (pos) = list_del_head(head))

typedef int fn_list_sort(const struct list_entry *a,
						 const struct list_entry *b);

static inline
void list_init(struct list_entry *head)
{
	head->next = head->prev = head;
}

static inline
bool list_is_first(const struct list_entry *head,
				   const struct list_entry *e)
{
	return head->next == e;
}

static inline
bool list_is_last(const struct list_entry *head,
				  const struct list_entry *e)
{
	return head->prev == e;
}

static inline
bool list_is_only(const struct list_entry *head,
				  const struct list_entry *e)
{
	return list_is_first(head, e) && list_is_last(head, e);
}

static inline
bool list_is_empty(const struct list_entry *head)
{
	return list_is_first(head, head);
}

static inline
void list_add_between(struct list_entry *prev,
					  struct list_entry *next,
					  struct list_entry *n)
{
	n->next = next;
	n->prev = prev;
	next->prev = n;
	prev->next = n;
}

static inline
void list_add_head(struct list_entry *head,
				   struct list_entry *n)
{
	list_add_between(head, head->next, n);
}

static inline
void list_add_tail(struct list_entry *head,
				   struct list_entry *n)
{
	list_add_between(head->prev, head, n);
}

static inline
void list_add_sort(struct list_entry *head,
				   struct list_entry *n,
				   fn_list_sort *fn)
{
	int cmp;
	struct list_entry *e;

	list_for_each(e, head) {
		cmp = fn(n, e);
		if (cmp > 0)
			continue;

		/* Add n before e if n <= e. */
		list_add_between(e->prev, e, n);
		return;
	}

	/* n is either largest among all entries, or list is empty. */
	list_add_tail(head, n);
}

static inline
void list_del_entry(struct list_entry *entry)
{
	struct list_entry *prev, *next;

	next = entry->next;
	prev = entry->prev;
	next->prev = prev;
	prev->next = next;
}

static inline
struct list_entry *list_del_tail(struct list_entry *head)
{
	struct list_entry *entry = head->prev;

	if (list_is_empty(head))
		return NULL;

	list_del_entry(entry);
	return entry;
}

static inline
struct list_entry *list_del_head(struct list_entry *head)
{
	struct list_entry *entry = head->next;

	if (list_is_empty(head))
		return NULL;

	list_del_entry(entry);
	return entry;
}

static inline
struct list_entry *list_peek_tail(const struct list_entry *head)
{
	return head->prev;
}

static inline
struct list_entry *list_peek_head(const struct list_entry *head)
{
	return head->next;
}
#endif
