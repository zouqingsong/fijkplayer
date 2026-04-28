/*
 * libavutil/thread.h — Win32 threading compat for MSVC
 *
 * This stub replaces FFmpeg's private libavutil/thread.h header,
 * mapping POSIX pthread types and functions to Win32 equivalents.
 * Uses SRWLOCK (supports static initialisation via SRWLOCK_INIT).
 */

#ifndef AVUTIL_THREAD_H
#define AVUTIL_THREAD_H

#ifdef _WIN32

#include <windows.h>
#include <process.h>
#include <stdint.h>

/* ── Type mappings ───────────────────────────────────── */

typedef SRWLOCK             pthread_mutex_t;
typedef CONDITION_VARIABLE  pthread_cond_t;
typedef HANDLE              pthread_t;
typedef int                 pthread_attr_t;
typedef int                 pthread_mutexattr_t;
typedef int                 pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER   SRWLOCK_INIT

/* ── Mutex ───────────────────────────────────────────── */

static inline int pthread_mutex_init(pthread_mutex_t *m,
                                     const pthread_mutexattr_t *a)
{
    (void)a;
    InitializeSRWLock(m);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void)m; /* SRWLOCK has no destroy */
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m)
{
    AcquireSRWLockExclusive(m);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
    ReleaseSRWLockExclusive(m);
    return 0;
}

/* ── Condition variable ──────────────────────────────── */

static inline int pthread_cond_init(pthread_cond_t *c,
                                    const pthread_condattr_t *a)
{
    (void)a;
    InitializeConditionVariable(c);
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *c)
{
    (void)c; /* CONDITION_VARIABLE has no destroy */
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    SleepConditionVariableSRW(c, m, INFINITE, 0);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *c)
{
    WakeConditionVariable(c);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *c)
{
    WakeAllConditionVariable(c);
    return 0;
}

/* ── Thread ──────────────────────────────────────────── */

static inline int pthread_create(pthread_t *t, const pthread_attr_t *a,
                                 void *(*func)(void *), void *arg)
{
    (void)a;
    *t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return (*t == NULL) ? -1 : 0;
}

static inline int pthread_join(pthread_t t, void **retval)
{
    WaitForSingleObject(t, INFINITE);
    if (retval) {
        DWORD code;
        GetExitCodeThread(t, &code);
        *retval = (void *)(intptr_t)code;
    }
    CloseHandle(t);
    return 0;
}

/* ── Thread naming (debug helper) ────────────────────── */

static inline void ff_thread_setname(const char *name)
{
    /* SetThreadDescription requires Windows 10 1607+ */
    typedef HRESULT (WINAPI *SetThreadDescriptionFunc)(HANDLE, PCWSTR);
    static SetThreadDescriptionFunc pSetThreadDescription = NULL;
    static int resolved = 0;
    if (!resolved) {
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (hKernel32)
            pSetThreadDescription = (SetThreadDescriptionFunc)
                GetProcAddress(hKernel32, "SetThreadDescription");
        resolved = 1;
    }
    if (pSetThreadDescription && name) {
        wchar_t wname[64];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64);
        pSetThreadDescription(GetCurrentThread(), wname);
    }
}

#else /* !_WIN32 — use real pthreads */

#include <pthread.h>

#endif /* _WIN32 */

#endif /* AVUTIL_THREAD_H */
