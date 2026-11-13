#ifndef AG_MEMORY_H
#define AG_MEMORY_H

#include "types.h"

/* r1297: единое forward-объявление на всю сборку — иначе каждый заголовок
   создавал свой несовместимый тег struct SYS_RUN_STATE. */
struct SYS_RUN_STATE;

struct MEM_PROC
{
	void (*write)(word a,byte d,void*p);
	byte (*read)(word a,void*p);
	void *pr, *pw;
};

extern void empty_write(word adr,byte data,void*d);
extern byte empty_read(word adr,void*d);
extern byte empty_read_addr(word adr,void*d);
extern byte empty_read_zero(word adr,void*d);

void fill_read_proc(struct MEM_PROC*p, int cnt, void*read, void*pr);
void fill_write_proc(struct MEM_PROC*p, int cnt, void*write, void*pw);
void fill_rw_proc(struct MEM_PROC*p, int cnt, void*read, void*write, void*prw);


void clear_block(byte*p,int sz);



__inline void mem_proc_write(word adr, byte data, struct MEM_PROC*mp)
{
	mp->write(adr, data, mp->pw);
}

__inline byte mem_proc_read(word adr, struct MEM_PROC*mp)
{
	return mp->read(adr, mp->pr);
}

void mem_write(word adr, byte data, struct SYS_RUN_STATE*sr);
byte mem_read(word adr, struct SYS_RUN_STATE*sr);

byte*ramptr(struct SYS_RUN_STATE*sr);
int basemem_n_blocks(struct SYS_RUN_STATE*sr);


#endif //AG_MEMORY_H
