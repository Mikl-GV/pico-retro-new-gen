/*
   * UAE - The Un*x Amiga Emulator
   *
   * Threading support, using pthreads
   *
   * Copyright 1997 Bernd Schmidt
   * Copyright 2004 Richard Drummond
   */

#ifndef UAE_THREADDEP_THREAD_H
#define UAE_THREADDEP_THREAD_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
typedef HANDLE uae_sem_t;
typedef HANDLE uae_thread_id;

extern void uae_sem_destroy(uae_sem_t*);
extern int uae_sem_trywait(uae_sem_t*);
extern int uae_sem_trywait_delay(uae_sem_t*, int);
extern void uae_sem_post(uae_sem_t*);
extern void uae_sem_unpost(uae_sem_t*);
extern void uae_sem_wait(uae_sem_t*t);
extern void uae_sem_init(uae_sem_t*, int manual_reset, int initial_state);
extern int uae_start_thread(const TCHAR *name, void (*f)(void *), void *arg, uae_thread_id *thread);
extern int uae_start_thread_fast(void (*f)(void *), void *arg, uae_thread_id *thread);
extern void uae_end_thread(uae_thread_id *thread);
extern void uae_set_thread_priority(uae_thread_id *, int);
extern uae_thread_id uae_thread_get_id(void);

#include "commpipe.h"

STATIC_INLINE void uae_wait_thread (uae_thread_id tid)
{
    WaitForSingleObject (tid, INFINITE);
    CloseHandle (tid);
}

#else /* !WIN32: WIIU | ALLWINNER_BARE_METAL | pthread */

#ifdef WIIU
#include <wiiu_pthread.h>
#include <wiiu/os/semaphore.h>

typedef struct {
    OSSemaphore *sem;
} uae_sem_t;

STATIC_INLINE void uae_sem_init (uae_sem_t *sem, int pshared, unsigned int value)
{
    sem->sem = (OSSemaphore *)malloc(sizeof(OSSemaphore));
    if (sem->sem)
        OSInitSemaphore(sem->sem, value);
}

STATIC_INLINE void uae_sem_destroy (uae_sem_t *sem)
{
    if (sem && sem->sem)
        free(sem->sem);
}

STATIC_INLINE void uae_sem_post (uae_sem_t *sem)
{
    if (sem && sem->sem)
        OSSignalSemaphore(sem->sem);
}

STATIC_INLINE void uae_sem_unpost (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem->sem = 0;
}

STATIC_INLINE void uae_sem_wait (uae_sem_t *sem)
{
    int retval;

    if (!sem || !sem->sem)
        return -1;

        while ((retval = OSWaitSemaphore(sem->sem)) == -1) {}
}

STATIC_INLINE int uae_sem_trywait_delay (uae_sem_t *sem, int millis)
{
    int retval;

    if (!sem || !sem->sem)
        return -1;

    retval = 1;
    if (OSTryWaitSemaphore(sem->sem) == 0)
        retval = 0;
    return retval;
}

STATIC_INLINE int uae_sem_trywait (uae_sem_t *sem)
{
    return uae_sem_trywait_delay(sem, 0);
}

STATIC_INLINE int uae_sem_getvalue (uae_sem_t *sem, int *sval)
{
    if (!sem || !sem->sem)
        return -1;
    return OSGetSemaphoreCount(sem->sem);
}

#elif defined(ALLWINNER_BARE_METAL)

/* Bare-metal (Allwinner H3, newlib без <pthread.h>/<semaphore.h>).
 * Однопоточный порт: uae_sem_t — счётчик, потоки — последовательный запуск
 * функции (uae_start_thread_*). Порт-слой retrodep (не вендорное ядро).
 * Поле `sem` оставлено для совместимости: вендорный код (blkdev.c,
 * scsiemul.c) проверяет st->sema.sem == NULL до uae_sem_init. */
typedef struct {
	volatile unsigned char val;
	void *sem;
} uae_sem_t;

typedef unsigned int uae_thread_id;
#define BAD_THREAD 0

STATIC_INLINE void uae_sem_init (uae_sem_t *sem, int pshared, unsigned int value)
{
	(void)pshared;
	if (!sem)
		return;
	sem->sem = (void *)1; /* «инициализирован» для проверок вендора .sem == NULL */
	sem->val = (unsigned char)(value & 0xFF);
}

STATIC_INLINE void uae_sem_destroy (uae_sem_t *sem)
{
	if (sem) {
		sem->sem = NULL;
		sem->val = 0;
	}
}

STATIC_INLINE void uae_sem_post (uae_sem_t *sem)
{
	if (sem && sem->val < 0xFF)
		sem->val++;
}

STATIC_INLINE void uae_sem_unpost (uae_sem_t *sem)
{
	if (sem) {
		sem->sem = NULL;
		sem->val = 0;
	}
}

STATIC_INLINE void uae_sem_wait (uae_sem_t *sem)
{
	if (!sem)
		return;
	while (!sem->val) {
		/* однопоточный bare-metal: спин, пока счётчик не взведут
		 * (в реальности вызовы семафоров в нашем наборе обслуживают
		 * коммуникацию потоков, которых тут нет) */
	}
	sem->val--;
}

STATIC_INLINE int uae_sem_trywait_delay (uae_sem_t *sem, int millis)
{
	(void)millis;
	if (!sem || !sem->val)
		return -1;
	sem->val--;
	return 0;
}

STATIC_INLINE int uae_sem_trywait (uae_sem_t *sem)
{
	return uae_sem_trywait_delay (sem, 0);
}

STATIC_INLINE int uae_sem_getvalue (uae_sem_t *sem, int *sval)
{
	if (!sem)
		return -1;
	*sval = sem->val;
	return 0;
}

STATIC_INLINE int uae_start_thread (const TCHAR *name, void (*f)(void *), void *arg, uae_thread_id *foo)
{
	(void)name;
	if (foo)
		*foo = 1;
	f (arg);
	return 1;
}

STATIC_INLINE int uae_start_thread_fast (void (*f)(void *), void *arg, uae_thread_id *tid)
{
	return uae_start_thread (NULL, f, arg, tid);
}

STATIC_INLINE int uae_wait_thread (uae_thread_id thread)
{
	(void)thread;
	return 0;
}

STATIC_INLINE void uae_kill_thread (uae_thread_id *thread)
{
	(void)thread;
}

STATIC_INLINE uae_thread_id uae_thread_get_id(void)
{
	return 1;
}

STATIC_INLINE void uae_end_thread (uae_thread_id *tid)
{
	(void)tid;
}

STATIC_INLINE void uae_set_thread_priority (uae_thread_id *tid, int pri)
{
	(void)tid; (void)pri;
}

#else /* pthread */

#include <pthread.h>
#include <semaphore.h>

typedef struct {
    sem_t *sem;
} uae_sem_t;

#ifndef USE_NAMED_SEMAPHORES
void uae_sem_init (uae_sem_t *sem, int pshared, unsigned int value);

STATIC_INLINE void uae_sem_destroy (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem_destroy (sem->sem);
}

STATIC_INLINE void uae_sem_post (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem_post (sem->sem);
}

STATIC_INLINE void uae_sem_unpost (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem->sem = 0;
}

STATIC_INLINE void uae_sem_wait (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem_wait (sem->sem);
}

STATIC_INLINE int uae_sem_trywait_delay(uae_sem_t *sem, int millis)
{
    if (!sem || !sem->sem)
        return -1;
    return sem_trywait (sem->sem);
}

STATIC_INLINE int uae_sem_trywait (uae_sem_t *sem)
{
    return uae_sem_trywait_delay(sem, 0);
}

STATIC_INLINE int uae_sem_getvalue (uae_sem_t *sem, int *sval)
{
    if (!sem || !sem->sem)
        return -1;
    return sem_getvalue (sem->sem, sval);
}

#else

void uae_sem_init (uae_sem_t *sem, int pshared, unsigned int value);

STATIC_INLINE void uae_sem_destroy (uae_sem_t *sem)
{
    if (sem && sem->sem)
       sem_close (sem->sem);
}

STATIC_INLINE void uae_sem_post (uae_sem_t *sem)
{
    if (sem && sem->sem)
       sem_post (sem->sem);
}

STATIC_INLINE void uae_sem_unpost (uae_sem_t *sem)
{
    if (sem && sem->sem)
        sem->sem = 0;
}

STATIC_INLINE void uae_sem_wait (uae_sem_t *sem)
{
    if (sem && sem->sem)
       sem_wait (sem->sem);
}

STATIC_INLINE int uae_sem_trywait_delay(uae_sem_t *sem, int millis)
{
    if (!sem || !sem->sem)
        return -1;
    return sem_trywait (sem->sem);
}

STATIC_INLINE int uae_sem_trywait (uae_sem_t *sem)
{
    return uae_sem_trywait_delay(sem, 0);
}

STATIC_INLINE int uae_sem_getvalue (uae_sem_t *sem, int *sval)
{
    if (!sem || !sem->sem)
        return -1;
    return sem_getvalue (sem->sem, sval);
}
#endif /* USE_NAMED_SEMAPHORES */

#endif /* WIIU | ALLWINNER_BARE_METAL | pthread */

#include "commpipe.h"

#ifndef ALLWINNER_BARE_METAL
#ifndef WIIU
typedef pthread_t uae_thread_id;
#else
typedef int uae_thread_id;
#endif
#else
typedef unsigned int uae_thread_id;
#endif
#define BAD_THREAD 0

#ifndef ALLWINNER_BARE_METAL
#ifndef WIIU

STATIC_INLINE int uae_start_thread (char *name, void (*f)(void *), void *arg, uae_thread_id *foo)
{
    int result;
    uae_thread_id new_foo;
    if (!foo)
       foo = &new_foo;
    result = pthread_create (foo, NULL, (void*)f, arg);

    return 0 == result;
}

STATIC_INLINE int uae_start_thread_fast (void (*f)(void *), void *arg, uae_thread_id *tid)
{
    int v = uae_start_thread (NULL, f, arg, tid);
#if 0
    if (*tid)
    {
        int policy;
        struct sched_param param;
        pthread_getschedparam(pthread_self(), &policy, &param);
        param.sched_priority = sched_get_priority_max(policy);
        pthread_setschedparam(pthread_self(), policy, &param);
    }
#endif
    return v;
}

STATIC_INLINE int uae_wait_thread (uae_thread_id thread)
{
    return pthread_join (thread, (void**)0);
}

STATIC_INLINE void uae_kill_thread (uae_thread_id* thread)
{
    pthread_detach(*thread);
}

STATIC_INLINE uae_thread_id uae_thread_get_id(void)
{
    return pthread_self();
}

STATIC_INLINE void uae_end_thread (uae_thread_id *tid)
{
#if 0
    if (*tid)
        pthread_cancel(*tid);
#endif
}

STATIC_INLINE void uae_set_thread_priority (uae_thread_id *tid, int pri)
{
#if 0
    if (*tid)
        pthread_setschedprio(*tid, pri);
#endif
}

#endif /* WIIU */
#endif /* ALLWINNER_BARE_METAL */

#endif /* !WIN32 */

#endif /* UAE_THREADDEP_THREAD_H */