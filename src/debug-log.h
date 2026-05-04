#pragma once

#include <stdio.h>

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "stools Plugin Manager"
#endif

#ifdef DEBUG_BUILD
#define dbg_log(level, ...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#else
#define dbg_log(...) ((void)0)
#endif

#ifndef LOG_ERROR
#define LOG_ERROR   100
#endif
#ifndef LOG_WARNING
#define LOG_WARNING 200
#endif
#ifndef LOG_INFO
#define LOG_INFO    300
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG   400
#endif
