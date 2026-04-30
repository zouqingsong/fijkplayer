/*
 * compat/va_copy.h — va_copy compatibility
 *
 * On modern MSVC, va_copy is provided by <stdarg.h>.
 * This stub exists only to satisfy the #include in cmdutils.c.
 */

#ifndef COMPAT_VA_COPY_H
#define COMPAT_VA_COPY_H

#include <stdarg.h>

/* va_copy is standard in C99+ / MSVC 2013+.  Nothing to do. */

#endif /* COMPAT_VA_COPY_H */
