#pragma once

/*
 * Platform compatibility layer replacing OBS internal utilities
 * (util/platform.h, util/threading.h, bmem.h) with standard C equivalents.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#endif

/* ---- Memory (replaces bmem.h) ---- */

#ifdef _MSC_VER
#define portable_strdup _strdup
#else
#define portable_strdup strdup
#endif

/* ---- Time (replaces util/platform.h os_gettime_ns / os_sleep_ms) ---- */

static inline uint64_t os_gettime_ns(void)
{
#ifdef _WIN32
	static LARGE_INTEGER freq = {0};
	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (uint64_t)((double)counter.QuadPart / (double)freq.QuadPart *
			  1000000000.0);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static inline void os_sleep_ms(uint32_t ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	usleep((useconds_t)ms * 1000);
#endif
}

/* ---- Thread naming (replaces util/platform.h os_set_thread_name) ---- */

static inline void os_set_thread_name(const char *name)
{
#ifdef _WIN32
	/* SetThreadDescription available on Windows 10 1607+ */
	typedef HRESULT(WINAPI * SetThreadDescriptionFunc)(HANDLE, PCWSTR);
	static SetThreadDescriptionFunc fn = NULL;
	static bool resolved = false;
	if (!resolved) {
		HMODULE mod = GetModuleHandleW(L"kernel32.dll");
		if (mod)
			fn = (SetThreadDescriptionFunc)GetProcAddress(
				mod, "SetThreadDescription");
		resolved = true;
	}
	if (fn) {
		wchar_t wname[64];
		MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64);
		fn(GetCurrentThread(), wname);
	}
#elif defined(__APPLE__)
	pthread_setname_np(name);
#else
	pthread_setname_np(pthread_self(), name);
#endif
}

/* ---- Atomics (replaces util/platform.h os_atomic_*) ---- */

#ifdef _WIN32

static inline long os_atomic_set_long(volatile long *ptr, long val)
{
	return InterlockedExchange(ptr, val);
}

static inline long os_atomic_load_long(volatile long *ptr)
{
	return InterlockedCompareExchange(ptr, 0, 0);
}

static inline long os_atomic_exchange_long(volatile long *ptr, long val)
{
	return InterlockedExchange(ptr, val);
}

#else

static inline long os_atomic_set_long(volatile long *ptr, long val)
{
	return __sync_lock_test_and_set(ptr, val);
}

static inline long os_atomic_load_long(volatile long *ptr)
{
	return __sync_add_and_fetch(ptr, 0);
}

static inline long os_atomic_exchange_long(volatile long *ptr, long val)
{
	return __sync_lock_test_and_set(ptr, val);
}

#endif
