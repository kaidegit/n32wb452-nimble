/*
 * ble_npl_sem backend for macOS (replaces the sem_t based linux NPL file;
 * macOS lacks sem_timedwait). Counting semaphore via mutex + condvar.
 */
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "os/os.h"
#include "nimble/nimble_npl.h"

ble_npl_error_t
ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    pthread_mutex_init(&sem->m, NULL);
    pthread_cond_init(&sem->c, NULL);
    sem->count = tokens;
    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_sem_release(struct ble_npl_sem *sem)
{
    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    pthread_mutex_lock(&sem->m);
    if (sem->count < UINT16_MAX) {
        sem->count++;
    }
    pthread_cond_signal(&sem->c);
    pthread_mutex_unlock(&sem->m);
    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_sem_pend(struct ble_npl_sem *sem, uint32_t timeout)
{
    struct timespec deadline;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    pthread_mutex_lock(&sem->m);

    if (timeout == BLE_NPL_TIME_FOREVER) {
        while (sem->count == 0) {
            pthread_cond_wait(&sem->c, &sem->m);
        }
    } else {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout / 1000;
        deadline.tv_nsec += (long)(timeout % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        while (sem->count == 0) {
            int err = pthread_cond_timedwait(&sem->c, &sem->m, &deadline);
            if (err == ETIMEDOUT) {
                if (sem->count == 0) {
                    pthread_mutex_unlock(&sem->m);
                    return BLE_NPL_TIMEOUT;
                }
                break;
            }
        }
    }

    sem->count--;
    pthread_mutex_unlock(&sem->m);
    return BLE_NPL_OK;
}

uint16_t
ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    uint16_t count;

    pthread_mutex_lock(&sem->m);
    count = sem->count;
    pthread_mutex_unlock(&sem->m);

    return count;
}
