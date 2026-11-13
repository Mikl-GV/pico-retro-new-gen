#ifndef RUNSTATE_H
#define RUNSTATE_H

#define RUNSTATE_SAVED		1
#define RUNSTATE_RUNNING	2
#define RUNSTATE_PAUSED		4
#define RUNSTATE_ERROR		8

int init_run_states();
void free_run_states();

int set_run_state_flags(LPCTSTR name, unsigned flags);
unsigned get_run_state_flags(LPCTSTR name);
unsigned get_run_state_flags_by_ptr(struct SYS_RUN_STATE*st);
unsigned or_run_state_flags(LPCTSTR name, unsigned flags);
unsigned and_run_state_flags(LPCTSTR name, unsigned flags);

LPCTSTR get_run_state_name(struct SYS_RUN_STATE*st);
int set_run_state_ptr(LPCTSTR name, struct SYS_RUN_STATE*st);
struct SYS_RUN_STATE*get_run_state_ptr(LPCTSTR name);
struct SYS_RUN_STATE*get_run_state_ptr_by_no(int no);

int get_n_running_systems();
int free_all_running_systems();

#endif //RUNSTATE_H
