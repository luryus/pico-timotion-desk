#ifndef _LOG_H
#define _LOG_H

extern "C" {

#include "pico/stdio.h"
#include "hardware/timer.h"
#include <stdio.h>

#define RESET  "\033[0m"
#define CYAN   "\033[36m"
#define YELLOW "\033[33m"
#define RED    "\033[31m"
#define BOLD   "\033[1m"
#define GRAY   "\033[90m"

#define __LOG(level, format, ...)                                                                  \
    printf(level " [%llu] " format "\n", time_us_64() / 1000, ##__VA_ARGS__)

#define LOGE(format, ...) __LOG(RED BOLD "ERR", format RESET, ##__VA_ARGS__)
#define LOGW(format, ...) __LOG(YELLOW "WRN", format RESET, ##__VA_ARGS__)
#define LOGI(format, ...) __LOG(CYAN "INF" RESET, format, ##__VA_ARGS__)
#define LOGD(format, ...) __LOG(GRAY "DBG", format RESET, ##__VA_ARGS__)

#define LOG_DUMP(p, len) {\
        for (int __i = 0; __i < (len); __i++) { printf("%02x ", (p)[__i]); } printf("\n"); \
    }

}

#endif // _LOG_H