#include "startup.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define STARTUP_RUN_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define STARTUP_VALUE_NAME "CRT Launcher"

SDL_bool startup_is_enabled(void) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, STARTUP_RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return SDL_FALSE;
    }
    LONG result = RegQueryValueExA(key, STARTUP_VALUE_NAME, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    return (result == ERROR_SUCCESS) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool startup_enable(void) {
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        SDL_Log("[startup] WARNING: could not resolve own exe path, not added to startup");
        return SDL_FALSE;
    }

    /* Quoted so Windows handles a path containing spaces correctly when it
       runs this at sign-in, same as every other spawned command line in
       this app (see launcher.c). */
    char quoted[MAX_PATH + 2];
    snprintf(quoted, sizeof(quoted), "\"%s\"", exe_path);

    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, STARTUP_RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        SDL_Log("[startup] WARNING: could not open '%s' for writing", STARTUP_RUN_KEY);
        return SDL_FALSE;
    }
    LONG result = RegSetValueExA(key, STARTUP_VALUE_NAME, 0, REG_SZ,
                                  (const BYTE *)quoted, (DWORD)(strlen(quoted) + 1));
    RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        SDL_Log("[startup] WARNING: RegSetValueExA failed (error %ld)", result);
        return SDL_FALSE;
    }

    SDL_Log("[startup] Added to Windows startup: %s", quoted);
    return SDL_TRUE;
}

SDL_bool startup_disable(void) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, STARTUP_RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        SDL_Log("[startup] WARNING: could not open '%s' for writing", STARTUP_RUN_KEY);
        return SDL_FALSE;
    }
    LONG result = RegDeleteValueA(key, STARTUP_VALUE_NAME);
    RegCloseKey(key);

    /* ERROR_FILE_NOT_FOUND just means it was already gone -- not a
       failure from the caller's point of view (startup_is_enabled() would
       already have reported SDL_FALSE, so there'd be nothing to remove). */
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        SDL_Log("[startup] WARNING: RegDeleteValueA failed (error %ld)", result);
        return SDL_FALSE;
    }

    SDL_Log("[startup] Removed from Windows startup");
    return SDL_TRUE;
}

#else
SDL_bool startup_is_enabled(void) {
    return SDL_FALSE;
}

SDL_bool startup_enable(void) {
    SDL_Log("[startup] WARNING: startup registration is only implemented for Windows");
    return SDL_FALSE;
}

SDL_bool startup_disable(void) {
    SDL_Log("[startup] WARNING: startup registration is only implemented for Windows");
    return SDL_FALSE;
}
#endif /* _WIN32 */
