#include "cpuint.h"
#include "common.h"

#include "localize.h"

//#define CPU_DEBUG

#ifdef CPU_DEBUG
#define _CMSG(s) puts(__FUNCTION__ ": " s)
#else
#define _CMSG(s)
#endif

static __declspec(thread) struct CPU_STATE*current_cpu = NULL;
static int cpu_wakeup(struct CPU_STATE*cs, int flags);

/* Get CPU running in the current thread. */
struct CPU_STATE*get_cpu()
{
	return current_cpu;
}

struct CPU_HANDLER
{
	struct LIST_NODE handler_list;
	CPU_CALLBACK callback;
	void *param;
};

static DWORD CALLBACK cpu_thread(struct CPU_STATE*cs);

static struct CPU_TIMER* find_timer(struct CPU_STATE*cs, long param)
{
	struct CPU_TIMER*r;
	int i;
	for (r = cs->timers, i = MAX_CPU_TIMERS; i; --i, ++r)
		if (r->used && r->param == param) return r;
	return NULL;
}

static struct CPU_TIMER* new_timer(struct CPU_STATE*cs, int delay, long param)
{
	struct CPU_TIMER*r;
	int i;
	for (r = cs->timers, i = MAX_CPU_TIMERS; i; --i, ++r)
		if (!InterlockedExchange(&r->used, 1)) {
			r->delay = delay;
			r->remains = 0;
			r->param = param;
			return r;
		}
	return NULL;
}

static int cpu_command(struct SLOT_RUN_STATE*st, int cmd, int data, long param)
{
	struct CPU_STATE*cs = st->data;
	int r;
	switch (cmd) {
	case SYS_COMMAND_FAST:
		r = cs->fast_mode;
//		if (data) cs->fast_mode ++;
//		else cs->fast_mode --;
		cs->fast_mode = data;
//		printf("fast mode: %i\n", data);
		return r;
	case SYS_COMMAND_BOOST:
//		printf("cpu: boost (%i) += %i\n", cs->fast_boost, data);
		cs->fast_boost += data;
		return cs->fast_boost;
	case SYS_COMMAND_HRESET:
		return cpu_hreset(cs);
	case SYS_COMMAND_RESET:
		return cpu_intr(cs, CPU_INTR_HRESET, 0);
	case SYS_COMMAND_IRQ:
		return cpu_intr(cs, CPU_INTR_IRQ, data);
	case SYS_COMMAND_NMI:
		return cpu_intr(cs, CPU_INTR_NMI, data);
	case SYS_COMMAND_NOIRQ:
		return cpu_intr(cs, CPU_INTR_NOIRQ, 0);
	case SYS_COMMAND_NONMI:
		return cpu_intr(cs, CPU_INTR_NONMI, 0);
	case SYS_COMMAND_EXEC:
		cs->new_addr = (word)param;
		return 0;
	case SYS_COMMAND_START:
		cpu_pause(cs, 0);
		return 0;
	case SYS_COMMAND_STOP:
		cpu_pause(cs, 1);
		return 0;
	case SYS_COMMAND_SET_CPU_HOOK:
		cs->hook_proc = (void*)data;
		cs->hook_data = (void*)param;
		return 1;
	case SYS_COMMAND_SET_CPUTIMER: {
		struct CPU_TIMER*r;
		r = find_timer(cs, param);
		if (r) {
			r->delay = data;
			r->remains = 0;
			r->used = data?1:0;
		} else {
			r = new_timer(cs, data, param);
		}
		return r?1:0; }
	case SYS_COMMAND_GETREGS6502:
		r = cpu_cmd(cs, cmd, data, param);
		if (r < 0) return r;
		((struct REGS_6502*)param)->TSC = (dword)cs->tsc_6502;
		return r;
	case SYS_COMMAND_GETREG:
		if (data == REG6502_TSC) { *(dword*)param = (dword)cs->tsc_6502; r = 1; }
		else r = cpu_cmd(cs, cmd, data, param);
		return r;
	case SYS_COMMAND_WAKEUP:
		cpu_wakeup(cs, data);
		return 0;
	default:
		return cpu_cmd(cs, cmd, data, param);
	}
	return 0;
}

static int cpu_term(struct SLOT_RUN_STATE*st)
{
	struct CPU_STATE*cs = st->data;
	_CMSG("start");
	cs->term_req = 1;
	tsq_uninit(&cs->handler_queue);
	cs->free(cs);
	free(cs);
	_CMSG("finished");
	return 0;
}

int  cpu_save(struct SLOT_RUN_STATE*st, OSTREAM*out)
{
	struct CPU_STATE*cs = st->data;
	WRITE_FIELD(out, cs->fast_mode);
	WRITE_FIELD(out, cs->freq_6502);
	WRITE_FIELD(out, cs->undoc);
	WRITE_FIELD(out, cs->tsc_6502);
	WRITE_FIELD(out, cs->min_msleep);
	WRITE_FIELD(out, cs->lim_fetches);
	WRITE_ARRAY(out, cs->int_ticks);
	WRITE_ARRAY(out, cs->timers);
	if (cs->save) return cs->save(cs, out);
	return 0;
}

int  cpu_load(struct SLOT_RUN_STATE*st, ISTREAM*in)
{
	struct CPU_STATE*cs = st->data;
	READ_FIELD(in, cs->fast_mode);
	READ_FIELD(in, cs->freq_6502);
	READ_FIELD(in, cs->undoc);
	READ_FIELD(in, cs->tsc_6502);
	READ_FIELD(in, cs->min_msleep);
	READ_FIELD(in, cs->lim_fetches);
	READ_ARRAY(in, cs->int_ticks);
	READ_ARRAY(in, cs->timers);
	if (cs->load) return cs->load(cs, in);
	return 0;
}

int cpu_cmd(struct CPU_STATE*cs, int cmd, int data, long param)
{
	if (cs->cmd) return cs->cmd(cs, cmd, data, param);
	return 0;
}

int  cpu_init(struct SYS_RUN_STATE*sr, struct SLOT_RUN_STATE*st, struct SLOTCONFIG*cf)
{
	struct CPU_STATE*cs;

	_CMSG("start");
	cs = calloc(1, sizeof(*cs));
	if (!cs) return -1;
	cs->sr = sr;
	cs->min_msleep = 30;
	cs->lim_fetches = 1000;
	cs->freq_6502 = cf->cfgint[CFG_INT_CPU_SPEED]*10;
	cs->undoc = cf->cfgint[CFG_INT_CPU_EXT];
	cs->new_addr = -1;

	tsq_init(&cs->handler_queue);

	switch (cf->dev_type) {
	case DEV_6502:
		init_cpu_6502(cs);
		break;
	case DEV_M6502:
		init_cpu_M6502(cs);
		break;
	case DEV_65C02:
		init_cpu_65c02(cs);
		break;
	}

	cs->sleep_req = 0;

	st->data = cs;
	st->free = cpu_term;
	st->save = cpu_save;
	st->load = cpu_load;
	st->command = cpu_command;
	_CMSG("exit, ok");
	return 0;
}


int cpu_hreset(struct CPU_STATE*st)
{
	_CMSG("start");
	st->intr(st, CPU_INTR_HRESET);
	_CMSG("end");
	return 0;
}

int cpu_intr(struct CPU_STATE*st, int t, int nticks)
{
	switch (t) {
	case CPU_INTR_IRQ:
		st->int_ticks[0] = nticks;
		break;
	case CPU_INTR_NMI:
		st->int_ticks[1] = nticks;
		break;
	case CPU_INTR_NOIRQ:
		st->int_ticks[0] = 0;
		break;
	case CPU_INTR_NONMI:
		st->int_ticks[1] = 0;
		break;
	case CPU_INTR_RESET:
		st->int_ticks[0] = 0;
		st->int_ticks[1] = 0;
		break;
	}
	st->intr(st, t);
	return 0;
}

int cpu_pause(struct CPU_STATE*st, int p)
{
	if (st->term_req) return 2;
	if (p) {
		if (st->sleep_req) return 1;
		st->sleep_req = 1;
		return 0;
	}
	if (!st->sleep_req) return 1;
	st->sleep_req = 0;
	return 0;
}

static void decrement_timers(struct CPU_STATE*cs, int n);

/* r1297: пошаговое исполнение ncmd инструкций в вызывающем потоке. */
int cpu_step(struct CPU_STATE*st, int ncmd)
{
	for (; ncmd > 0; --ncmd) {
		int r = st->hook_proc ? st->hook_proc(st->hook_data) : 0;
		if (!r) r = st->exec_op(st);
		if (r > 0) {
			st->tsc_6502 += r;
			decrement_timers(st, r);
		}
	}
	return 0;
}

static int cpu_wakeup(struct CPU_STATE*cs, int flags)
{
	(void)cs;
	(void)flags;
	return 0;
}


static void decrement_int(struct CPU_STATE*cs, int n, int id, int cmd)
{
	if (cs->int_ticks[id]) {
		if (cs->int_ticks[id] > n) cs->int_ticks[id] -= n;
		else {
			cpu_intr(cs, cmd, 0);
		}
	}
}

static void decrement_timers(struct CPU_STATE*cs, int n)
{
	struct CPU_TIMER*r;
	int i;

//	printf("decrement_timers: %i\n", n);

	decrement_int(cs, n, 0, CPU_INTR_NOIRQ);
	decrement_int(cs, n, 1, CPU_INTR_NONMI);

	for (r = cs->timers, i = MAX_CPU_TIMERS; i; --i, ++r)
//		printf("timer[%i]: used = %i; delay = %i; remains = %i\n", i, r->used, r->delay, r->remains);
		if (r->used && r->delay) {
			if (r->remains<=0) r->remains += r->delay;
			r->remains -= n;
			if (r->remains<=0) {
				system_command(cs->sr, SYS_COMMAND_CPUTIMER, 0, r->param);
			}
		}
}

/* r1297: синхронный прогон CPU вместо потока. Вызывается из host-цикла на
   кадр: исполняет инструкции, пока tsc не пройдёт заданное число тактов. */
int cpu_run_cycles(struct CPU_STATE*cs, long long cycles)
{
	long long target = (long long)cs->tsc_6502 + cycles;

	while (!cs->term_req && !cs->sleep_req && (long long)cs->tsc_6502 < target) {
		int r;
		/* run all registered handlers (очередь пополняется синхронно) */
		for (;;) {
			struct LIST_NODE *ln;
			struct CPU_HANDLER *ch;
			ln = tsq_take(&cs->handler_queue);
			if (!ln) break;
			ch = LIST_ENTRY(ln, struct CPU_HANDLER, handler_list);
			ch->callback(ch->param);
			free(ch);
		}
		if (cs->new_addr != -1) {
			cpu_cmd(cs, SYS_COMMAND_EXEC, 0, cs->new_addr);
			cs->new_addr = -1;
		}
		r = cs->hook_proc ? cs->hook_proc(cs->hook_data) : 0;
		if (!r) r = cs->exec_op(cs);
		if (r > 0) {
			cs->tsc_6502 += r;
			decrement_timers(cs, r);
		} else {
			/* r1299: без прогресса цикл кадра не завершится никогда —
			   стенд зависал на чёрном экране без выхода по ESC. */
			cs->tsc_6502++;
		}
	}
	return 0;
}


int cpu_get_tsc(struct SYS_RUN_STATE*sr)
{
	struct CPU_STATE*cs;
	cs = sr->slots[CONF_CPU].data;
	return (int)cs->tsc_6502;
}

int cpu_get_freq(struct SYS_RUN_STATE*sr)
{
	struct CPU_STATE*cs;
	cs = sr->slots[CONF_CPU].data;
	return (int)cs->freq_6502;
}

int cpu_get_fast(struct SYS_RUN_STATE*sr)
{
	struct CPU_STATE*cs;
	cs = sr->slots[CONF_CPU].data;
	return cs->fast_mode;
}

/*
	Execute a callback on the CPU thread.
	This function is thread-safe.
*/
/* r1297: bare-metal — колбэк исполняется сразу в вызывающем потоке. */
int cpu_exec_async(struct CPU_STATE*cs, CPU_CALLBACK callback, void *param)
{
	(void)cs;
	callback(param);
	return 0;
}
