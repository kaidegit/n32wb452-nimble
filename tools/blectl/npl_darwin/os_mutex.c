/*
 * ble_npl_mutex backend for macOS (replaces the linux NPL file; macOS lacks
 * pthread_mutex_timedlock). Finite timeouts use a coarse try/sleep loop —
 * the host only pends forever in practice.
 */
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "os/os.h"
#include "nimble/nimble_npl.h"

ble_npl_error_t
ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    pthread_mutexattr_init(&mu->attr);
    pthread_mutexattr_settype(&mu->attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mu->lock, &mu->attr);

    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (pthread_mutex_unlock(&mu->lock)) {
        return BLE_NPL_BAD_MUTEX;
    }
    return BLE_NPL_OK;
}

ble_npl_error_t
ble_npl_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout)
{
    int err;

    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (timeout == BLE_NPL_TIME_FOREVER) {
        err = pthread_mutex_lock(&mu->lock);
    } else {
        uint32_t waited_ms = 0;
        while ((err = pthread_mutex_trylock(&mu->lock)) == EBUSY) {
            if (waited_ms >= timeout) {
                return BLE_NPL_TIMEOUT;
            }
            usleep(1000);
            waited_ms++;
        }
    }

    return (err) ? BLE_NPL_ERROR : BLE_NPL_OK;
}
