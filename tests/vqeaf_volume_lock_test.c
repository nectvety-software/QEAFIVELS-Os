/* Regression gate for the QEAFRESCUE volume-lock fault escape.
 *
 * 2026-11-20: a UI fault siglongjmp'd out of a volume_lock_acquire() block,
 * so the rescue worker blocked forever on the still-owned mutex. The drive
 * kept mounting, LASTBOOT.TXT recorded sig=11, but QEAF.CMD was never
 * consumed. This exercises the exact header PID 1 includes. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include "volume_lock.h"

static pthread_mutex_t phase_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t phase_cond = PTHREAD_COND_INITIALIZER;
static int phase_ready;

/* What the fault handler does on the faulting thread: abandon the guarded
 * block, then hand back whatever this thread still holds. Real call sites
 * never nest, so depth is 0 or 1 here. */
static void *faulting_worker(void *arg)
{
    (void)arg;
    volume_lock_acquire();
    volume_lock_drop_on_fault();
    return NULL;
}

/* Holds the lock while the main thread runs a fault escape that belongs to
 * a *different* thread (TLS depth 0): that must not free another owner's
 * mutex. */
static void *held_worker(void *arg)
{
    int *release = (int *)arg;

    volume_lock_acquire();
    pthread_mutex_lock(&phase_lock);
    phase_ready = 1;
    pthread_cond_broadcast(&phase_cond);
    while(!*release)
        pthread_cond_wait(&phase_cond, &phase_lock);
    pthread_mutex_unlock(&phase_lock);
    volume_lock_release();
    return NULL;
}

/* Same, but the owner itself drops the lock at fault time - this is the
 * cross-thread unblock the bug killed. */
static void *held_then_faulting_worker(void *arg)
{
    int *release = (int *)arg;

    volume_lock_acquire();
    pthread_mutex_lock(&phase_lock);
    phase_ready = 1;
    pthread_cond_broadcast(&phase_cond);
    while(!*release)
        pthread_cond_wait(&phase_cond, &phase_lock);
    pthread_mutex_unlock(&phase_lock);
    volume_lock_drop_on_fault();
    return NULL;
}

static void reset_lock(void)
{
    assert(pthread_mutex_destroy(&volume_lock) == 0);
    assert(pthread_mutex_init(&volume_lock, NULL) == 0);
    volume_lock_depth = 0;
}

static void wait_ready(void)
{
    pthread_mutex_lock(&phase_lock);
    while(!phase_ready)
        pthread_cond_wait(&phase_cond, &phase_lock);
    pthread_mutex_unlock(&phase_lock);
}

static void let_worker_finish(int *release)
{
    pthread_mutex_lock(&phase_lock);
    *release = 1;
    pthread_cond_broadcast(&phase_cond);
    pthread_mutex_unlock(&phase_lock);
}

int main(void)
{
    pthread_t worker;
    int release;

    /* 1. Owner faults while holding the lock; the rescue worker can take
     *    it immediately afterwards. */
    reset_lock();
    assert(pthread_create(&worker, NULL, faulting_worker, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0);
    assert(pthread_mutex_trylock(&volume_lock) == 0);
    pthread_mutex_unlock(&volume_lock);

    /* 2. A fault on a thread that does NOT own the lock must not release
     *    it on behalf of the owner. */
    reset_lock();
    phase_ready = 0;
    release = 0;
    assert(pthread_create(&worker, NULL, held_worker, &release) == 0);
    wait_ready();
    volume_lock_drop_on_fault();
    assert(pthread_mutex_trylock(&volume_lock) != 0);
    let_worker_finish(&release);
    assert(pthread_join(worker, NULL) == 0);
    assert(pthread_mutex_trylock(&volume_lock) == 0);
    pthread_mutex_unlock(&volume_lock);

    /* 3. The lock really is held before the drop; afterwards trylock
     *    succeeds. Without the fix this is where QEAF.CMD stopped flowing. */
    reset_lock();
    phase_ready = 0;
    release = 0;
    assert(pthread_create(&worker, NULL, held_then_faulting_worker,
                          &release) == 0);
    wait_ready();
    assert(pthread_mutex_trylock(&volume_lock) != 0);
    let_worker_finish(&release);
    assert(pthread_join(worker, NULL) == 0);
    assert(pthread_mutex_trylock(&volume_lock) == 0);
    pthread_mutex_unlock(&volume_lock);

    /* 4. An unmatched release on this thread is a no-op, never an unlock
     *    of a mutex owned by somebody else. */
    reset_lock();
    phase_ready = 0;
    release = 0;
    assert(pthread_create(&worker, NULL, held_worker, &release) == 0);
    wait_ready();
    volume_lock_release();
    assert(pthread_mutex_trylock(&volume_lock) != 0);
    let_worker_finish(&release);
    assert(pthread_join(worker, NULL) == 0);
    assert(pthread_mutex_trylock(&volume_lock) == 0);
    pthread_mutex_unlock(&volume_lock);

    puts("vqeaf_volume_lock: 4 checks OK (fault escape leaves QEAF.CMD consumable)");
    return 0;
}
