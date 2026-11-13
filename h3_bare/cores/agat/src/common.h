/*
	Agat Emulator version 1.0
	Copyright (c) NOP, nnop@newmail.ru
	common - common function declarations
*/


void usleep(int microsec);
void msleep(int msec);
unsigned get_n_msec();
const char*sys_get_parameter(const char*name);

int load_buf_res(int no, void*buf, int len);


#define EMUL_FLAGS_FULLSCREEN_DEFAULT	1
#define EMUL_FLAGS_BACKGROUND_ACTIVE	2
#define EMUL_FLAGS_SHUGART_SOUNDS	4
#define EMUL_FLAGS_TEAC_SOUNDS		8
#define EMUL_FLAGS_ENABLE_DEBUGGER	16
#define EMUL_FLAGS_DEBUG_ILLEGAL_CMDS	32
#define EMUL_FLAGS_DEBUG_NEW_CMDS	64
#define EMUL_FLAGS_SYNC_UPDATE		128
#define EMUL_FLAGS_LANGSEL		256
#define EMUL_FLAGS_SEAGATE_SOUNDS	512

#define EMUL_FLAGS_DEFAULT	(EMUL_FLAGS_BACKGROUND_ACTIVE | EMUL_FLAGS_SHUGART_SOUNDS | EMUL_FLAGS_TEAC_SOUNDS | EMUL_FLAGS_SEAGATE_SOUNDS)

struct GLOBAL_CONFIG
{
	unsigned int flags;
};

extern struct GLOBAL_CONFIG g_config;
