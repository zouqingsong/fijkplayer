/*
 * stdatomic.h shim for MSVC
 *
 * MSVC's <stdatomic.h> requires /std:c17 which CMake's Visual Studio
 * generator doesn't reliably apply for C files in mixed C/C++ targets.
 * This shim provides the subset of C11 atomics used by fftools using
 * MSVC's Interlocked* intrinsics.
 */
#ifndef FFTOOLS_STDATOMIC_H
#define FFTOOLS_STDATOMIC_H

#ifdef _MSC_VER

#include <windows.h>

typedef volatile LONG atomic_int;
typedef volatile LONG64 atomic_int_least64_t;
typedef volatile ULONG64 atomic_uint_least64_t;

#define ATOMIC_VAR_INIT(value) (value)

#define atomic_init(obj, value)      (*(obj) = (value))
#define atomic_store(obj, value)     InterlockedExchange((volatile LONG*)(obj), (LONG)(value))
#define atomic_store_explicit(obj, value, order) InterlockedExchange((volatile LONG*)(obj), (LONG)(value))
#define atomic_load(obj)             InterlockedCompareExchange((volatile LONG*)(obj), 0, 0)
#define atomic_load_explicit(obj, order) InterlockedCompareExchange((volatile LONG*)(obj), 0, 0)
#define atomic_fetch_add(obj, arg)   InterlockedExchangeAdd((volatile LONG*)(obj), (LONG)(arg))
#define atomic_fetch_add_explicit(obj, arg, order) InterlockedExchangeAdd((volatile LONG*)(obj), (LONG)(arg))
#define atomic_fetch_sub(obj, arg)   InterlockedExchangeAdd((volatile LONG*)(obj), -(LONG)(arg))
#define atomic_fetch_sub_explicit(obj, arg, order) InterlockedExchangeAdd((volatile LONG*)(obj), -(LONG)(arg))

typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

#else
/* Non-MSVC: use standard header */
#include_next <stdatomic.h>
#endif

#endif /* FFTOOLS_STDATOMIC_H */
