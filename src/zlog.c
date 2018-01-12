#include "zlog.h"

#ifdef _WIN32
#include <stdarg.h>
#include <Windows.h>

const char *get_log_level(int log_level)
{
	switch (log_level)
	{
	case ZLOG_LEVEL_DEBUG:
		return "DEBUG";
	case ZLOG_LEVEL_INFO:
		return "INFO";
	case ZLOG_LEVEL_NOTICE:
		return "NOTICE";
	case ZLOG_LEVEL_WARN:
		return "WARN";
	case ZLOG_LEVEL_ERROR:
		return "ERROR";
	case ZLOG_LEVEL_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

void zlog(FILE *fp, const char *file, long line, int level,const char *format, ...)
{
	time_t t = time(0);   
	char tmp[64];   
	char buf[1024] = {0};
	char buf2[1024] = {0};
	va_list ap;

	strftime( tmp, sizeof(tmp), "%Y/%m/%d %H:%M:%S",localtime(&t) );
	/* 时间日期 等级 (文件名:行号)*/
	va_start(ap, format);
	vsnprintf (buf, 1024, format, ap);
	va_end(ap);
	
	fprintf(fp, "%s %s (%s:%d) %s\n", tmp, get_log_level(level), file, line, buf);
	sprintf(buf2, "%s %s (%s:%d) %s\n", tmp, get_log_level(level), file, line, buf);
	OutputDebugStringA(buf2);
}

#endif