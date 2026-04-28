/*
 * compat/w32dlfcn.h — dlopen/dlsym/dlclose for Windows
 *
 * Maps POSIX dynamic-library functions to Win32 equivalents.
 * Used by cmdutils.c for init_dynload().
 */

#ifndef COMPAT_W32DLFCN_H
#define COMPAT_W32DLFCN_H

#ifdef _WIN32

#include <windows.h>

static inline HMODULE win32_dlopen(const char *name)
{
    wchar_t name_w[MAX_PATH];
    if (!name || !*name)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, name_w, MAX_PATH);
    return LoadLibraryExW(name_w, NULL,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                          LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

#define dlopen(name, flags)     win32_dlopen(name)
#define dlsym(handle, symbol)   GetProcAddress((HMODULE)(handle), symbol)
#define dlclose(handle)         FreeLibrary((HMODULE)(handle))

#endif /* _WIN32 */

#endif /* COMPAT_W32DLFCN_H */
