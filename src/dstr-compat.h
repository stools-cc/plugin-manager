#pragma once

/*
 * Minimal dynamic string builder (replaces OBS util/dstr.h).
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

struct dstr {
	char *array;
	size_t len;
	size_t capacity;
};

static inline void dstr_init(struct dstr *dst)
{
	dst->array = NULL;
	dst->len = 0;
	dst->capacity = 0;
}

static inline void dstr_free(struct dstr *dst)
{
	free(dst->array);
	dst->array = NULL;
	dst->len = 0;
	dst->capacity = 0;
}

static inline void dstr_ensure_capacity(struct dstr *dst, size_t new_cap)
{
	if (new_cap <= dst->capacity)
		return;
	size_t cap = dst->capacity ? dst->capacity : 64;
	while (cap < new_cap)
		cap *= 2;
	dst->array = (char *)realloc(dst->array, cap);
	dst->capacity = cap;
}

static inline void dstr_cat(struct dstr *dst, const char *str)
{
	size_t slen = strlen(str);
	dstr_ensure_capacity(dst, dst->len + slen + 1);
	memcpy(dst->array + dst->len, str, slen + 1);
	dst->len += slen;
}

static inline void dstr_ncat(struct dstr *dst, const char *str, size_t len)
{
	dstr_ensure_capacity(dst, dst->len + len + 1);
	memcpy(dst->array + dst->len, str, len);
	dst->len += len;
	dst->array[dst->len] = '\0';
}

static inline void dstr_printf(struct dstr *dst, const char *fmt, ...)
{
	va_list args, args2;
	va_start(args, fmt);
	va_copy(args2, args);

	int needed = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (needed < 0) {
		va_end(args2);
		return;
	}

	dstr_ensure_capacity(dst, (size_t)needed + 1);
	vsnprintf(dst->array, (size_t)needed + 1, fmt, args2);
	dst->len = (size_t)needed;
	va_end(args2);
}

static inline void dstr_catf(struct dstr *dst, const char *fmt, ...)
{
	va_list args, args2;
	va_start(args, fmt);
	va_copy(args2, args);

	int needed = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (needed < 0) {
		va_end(args2);
		return;
	}

	dstr_ensure_capacity(dst, dst->len + (size_t)needed + 1);
	vsnprintf(dst->array + dst->len, (size_t)needed + 1, fmt, args2);
	dst->len += (size_t)needed;
	va_end(args2);
}
