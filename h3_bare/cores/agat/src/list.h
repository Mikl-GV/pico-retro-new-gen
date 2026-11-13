/*
	Agat Emulator
	Copyright (c) NOP, nnop@newmail.ru
	list - doubly linked list
*/

#ifndef LIST_H
#define LIST_H

/* A node of a doubly-linked list. */
struct LIST_NODE
{
	struct LIST_NODE *next, *prev;
};

void list_init(struct LIST_NODE *node);
void list_insert(struct LIST_NODE *new_node, struct LIST_NODE* list_node);
void list_remove(struct LIST_NODE *node);
int list_empty(struct LIST_NODE *head);

/*
	Get a structure address by its list node. Allows to resolve an address
	of a structure from an address of a list node that it contains.

	node - address of a list node
	type - type of a structure that is known to contain the node
	field - name of the node field in the `type`
*/
#define LIST_ENTRY(node, type, field) ((type*)((byte*)(node) - (intptr_t)&((type*)0)->field))

#define LIST_NODE_INIT(node) {&node, &node}
#define LIST_NODE_DEF(name) struct LIST_NODE name = LIST_NODE_INIT(name)

#endif // LIST_H
