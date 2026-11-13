/*
	Agat Emulator
	Copyright (c) NOP, nnop@newmail.ru
*/

#ifndef TSQ_H
#define TSQ_H

#include "list.h"

#include <windows.h>

/* Trhread-safe queue. */
struct TSQ
{
	struct LIST_NODE queue;
	HANDLE mutex;
};

int tsq_init(struct TSQ *tsq);
void tsq_uninit(struct TSQ *tsq);
void tsq_add(struct TSQ *tsq, struct LIST_NODE *new_node);
struct LIST_NODE* tsq_take(struct TSQ *tsq);

#endif // TSQ_H
