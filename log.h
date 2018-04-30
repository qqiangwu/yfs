#ifndef YFS_LOG_H_
#define YFS_LOG_H_

#include <cstdio>

enum class YLog_level : unsigned char {
    error,
    warn,
    info,
    debug
};

extern YLog_level _ylog_level;

inline void ylog_set_level(const YLog_level level)
{
    _ylog_level = level;
}

#define _YLOG_IMPL(level, format, ...)                       \
	do {                                                     \
        if(int(_ylog_level) < int(level)) {                  \
        } else {                                             \
		    std::printf(format"\n", __VA_ARGS__);           \
		}                                                    \
	} while(0)


#define YLOG_DEBUG(format, ...) _YLOG_IMPL(YLog_level::debug, "[debug] " format, __VA_ARGS__)
#define YLOG_INFO(format, ...) _YLOG_IMPL(YLog_level::info, "[info] " format, __VA_ARGS__)
#define YLOG_WARN(format, ...) _YLOG_IMPL(YLog_level::warn, "[warn] " format, __VA_ARGS__)
#define YLOG_ERROR(format, ...) _YLOG_IMPL(YLog_level::error, "[error] " format, __VA_ARGS__)

#endif
