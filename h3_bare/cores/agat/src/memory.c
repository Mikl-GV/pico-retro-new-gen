#include "memory.h"

void empty_write(word adr,byte data,void*d){ /*printf("mem[%04X]:=%02X\n", adr, data);*/ }
byte empty_read(word adr,void*d) { return 0xFF; }
byte empty_read_zero(word adr,void*d) { return 0; }
byte empty_read_addr(word adr,void*d) { return adr>>8; }

void fill_read_proc(struct MEM_PROC*p, int cnt, void *read, void *pr)
{
	for (;cnt; cnt--, p++) {
		p->read=(byte (*)(word,void*))read;
		p->pr = pr;
	}	
}

void fill_write_proc(struct MEM_PROC*p, int cnt, void *write, void *pw)
{
	for (;cnt; cnt--, p++) {
		p->write=(void (*)(word,byte,void*))write;
		p->pw = pw;
	}	
}

void fill_rw_proc(struct MEM_PROC*p, int cnt, void*read, void*write, void*prw)
{
	for (;cnt; cnt--, p++) {
		p->read=(byte (*)(word,void*))read;
		p->write=(void (*)(word,byte,void*))write;
		p->pr = p->pw = prw;
	}	
}



void clear_block(byte*p,int sz)
{
	register dword b=0;
	sz>>=2;
	while (sz) {
		*(dword*)p=b;
		p+=4;
		sz--;
		if (sz&63) b=~b;
	}
}
