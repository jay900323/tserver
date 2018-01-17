#ifndef _ZLOG_H
#define _ZLOG_H

#ifdef _WIN32
#include <stdio.h>

#define ZLOG_LEVEL_DEBUG 0
#define ZLOG_LEVEL_INFO 1
#define ZLOG_LEVEL_NOTICE 2
#define ZLOG_LEVEL_WARN 3
#define ZLOG_LEVEL_ERROR 4
#define ZLOG_LEVEL_FATAL 5

int get_log_level(int log_level);
void zlog(FILE *fp, const char *file, long line, int level,const char *format, ...);


#define zlog_debug(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_DEBUG, __VA_ARGS__)
#define zlog_info(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_INFO, __VA_ARGS__)
#define zlog_notice(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_NOTICE, __VA_ARGS__)
#define zlog_warn(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_WARN, __VA_ARGS__)
#define zlog_error(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_ERROR, __VA_ARGS__)
#define zlog_fatal(cat, ...) \
	zlog(cat, __FILE__, __LINE__, ZLOG_LEVEL_FATAL, __VA_ARGS__)
#endif
#endif




