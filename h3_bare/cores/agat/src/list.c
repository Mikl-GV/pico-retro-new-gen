/*
	Agat Emulator
	Copyright (c) NOP, nnop@newmail.ru
	list - doubly linked list
*/

#include <assert.h>
#include "list.h"

/*
	- links must not be null
	- must both point to self, OR both point to some other nodes
*/
static int verify_invariant(struct LIST_NODE *node)
{
	return node->next && node->prev &&
		(node->next == node) == (node->prev == node);
}

/* Initialize a list node to point to itself. */
void list_init(struct LIST_NODE *node)
{
	node->next = node->prev = node;
}

/* Insert new_node into a list before list_node. */
void list_insert(struct LIST_NODE *new_node, struct LIST_NODE* list_node)
{
	assert(verify_invariant(list_node));
	assert(list_empty(new_node));
	new_node->next = list_node;
	new_node->prev = list_node->prev;
	new_node->prev->next = new_node->next->prev = new_node;
}

/*
	Remove a node from any list it's currently in. The removed node
	will point to itself as if after calling list_init().
*/
void list_remove(struct LIST_NODE *node)
{
	assert(verify_invariant(node));
	if (node->next != node)
	{
		node->next->prev = node->prev;
		node->prev->next = node->next;
		node->next = node->prev = node;
	}
}

/*
	Check if the list is empty, i.e. points to itself.
	Returns nonzero if empty, zero if there are elements.
*/
int list_empty(struct LIST_NODE *head)
{
	return head->next == head && head->prev == head;
}
