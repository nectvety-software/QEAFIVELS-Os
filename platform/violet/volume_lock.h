#ifndef VQEAF_VOLUME_LOCK_H
#define VQEAF_VOLUME_LOCK_H

/* Shared discipline for the QEAFRESCUE RAM volume lock.
 *
 * main_violet.c catches SIGSEGV/SIGBUS/... and leaves the guarded block via
 * siglongjmp (or parks a faulting worker in pause()). A plain mutex would
 * stay owned in both cases: the drive keeps mounting but QEAF.CMD is never
 * consumed again. Every acquisition therefore records the owner's depth in
 * TLS, and the fault handler drops whatever this thread still holds before
 * it jumps or parks.
 *
 * The mutex is RECURSIVE on purpose. A rescue verb runs inside rescue_poll(),
 * which already holds this lock, and hooks such as `shot` / `rec start` take
 * it again through vio_screenshot_now() / rec_budget(). A non-recursive mutex
 * self-deadlocked the worker there - exactly the wedge where F: stayed
 * mounted but QEAF.CMD went unanswered. Depth is still tracked so the fault
 * handler can release every level.
 *
 * Keeping this in a header lets the host gate exercise the same code the
 * PID 1 uses, instead of paraphrasing it in a mock. */
#include <pthread.h>

static pthread_mutex_t volume_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t volume_lock_once = PTHREAD_ONCE_INIT;
static __thread unsigned volume_lock_depth;

static void volume_lock_do_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&volume_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

static void volume_lock_init(void)
{
    pthread_once(&volume_lock_once, volume_lock_do_init);
}

static void volume_lock_acquire(void)
{
    volume_lock_init();
    pthread_mutex_lock(&volume_lock);
    volume_lock_depth++;
}

static void volume_lock_release(void)
{
    if(volume_lock_depth == 0) return;
    volume_lock_depth--;
    pthread_mutex_unlock(&volume_lock);
}

/* Called from the signal handler on the faulting thread's own TLS. The
 * handler already uses trylock/unlock on this mutex in crash_flush(), so
 * releasing it here follows the same (musl) async-signal-safe path. */
static void volume_lock_drop_on_fault(void)
{
    unsigned depth = volume_lock_depth;

    volume_lock_depth = 0;
    while(depth-- > 0)
        pthread_mutex_unlock(&volume_lock);
}

#endif /* VQEAF_VOLUME_LOCK_H */
