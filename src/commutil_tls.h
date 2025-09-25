#ifndef __COMMUTIL_TLS_H__
#define __COMMUTIL_TLS_H__

#include "comm_util_def.h"

#ifndef COMMUTIL_WINDOWS
#include <pthread.h>
#endif

namespace commutil {

/** @typedef Thread local storage key type. */
#ifdef COMMUTIL_WINDOWS
typedef unsigned long TlsKey;
#else
typedef pthread_key_t TlsKey;
#endif

/** @def Invalid TLS key value. */
#define COMMUTIL_INVALID_TLS_KEY ((TlsKey) - 1)

/** @typedef TLS destructor function type. */
typedef void (*tlsDestructorFunc)(void*);

/** @brief Initializes the TLS mechanism. */
extern void initTls();

/** @brief Destroys the TLS mechanism. */
extern void termTls();

/**
 * @brief Creates a thread local storage key.
 * @note It is advised to call this function during process initialization.
 * @param[out] key The resulting key, used as index to a slot where the per-thread value is stored.
 * @param dtor Optional destructor to allow per-thread TLS value cleanup. The destructor function
 * will be executed for each thread during thread exit, but only if the TLS value is not null. The
 * destructor parameter is the TLS value of the specific thread that is going down.
 * @return The operation result.
 */
extern bool createTls(TlsKey& key, tlsDestructorFunc dtor = nullptr);

/**
 * @brief Destroys a thread local storage key.
 * @note It is advised to call this function during process destruction, after all other threads
 * have gone down. This will cause a memory leak for the main thread, in case it has thread local
 * value associated with this key, that requires cleanup. In such a case, make sure to explicitly
 * call the destructor function with the current thread's TLS value associates with the key being
 * destroyed BEFORE calling @ref elogDestroyTls().
 * @param key The thread local storage key.
 * @return The operation result.
 */
extern bool destroyTls(TlsKey key);

/**
 * @brief Retrieves the current thread's TLS value associated with the given key.
 * @note Return value of null does not necessarily mean error, but rather that the current thread
 * has not yet initialized its value associated with this TLS key.
 * @param key The thread local storage key.
 * @return void* The return value.
 */
extern void* getTls(TlsKey key);

/**
 * @brief Sets the current thread's TLS value associated with the given key.
 * @param key The thread local storage key.
 * @param value The thread local storage value.
 * @return The operation result.
 */
extern bool setTls(TlsKey key, void* value);

}  // namespace commutil

#endif  // __COMMUTIL_TLS_H__