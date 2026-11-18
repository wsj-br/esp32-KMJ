#pragma once

#include <stdarg.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Debug logging over the USB serial port. Only the kmj-debug env defines
// KMJ_DEBUG_LOG; in the kmj env every call compiles away.
#ifdef KMJ_DEBUG_LOG

namespace kmj {

void log_begin();
void log_watch(const char* name, TaskHandle_t task);
void log_heap(const char* where);
void log_vprintf(const char* fmt, va_list args);
void log_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace kmj

#define KMJ_LOG(...) ::kmj::log_printf(__VA_ARGS__)
#define KMJ_LOG_BEGIN() ::kmj::log_begin()
#define KMJ_LOG_WATCH(name, task) ::kmj::log_watch(name, task)
#define KMJ_LOG_HEAP(where) ::kmj::log_heap(where)

#else

#define KMJ_LOG(...) ((void)0)
#define KMJ_LOG_BEGIN() ((void)0)
#define KMJ_LOG_WATCH(name, task) ((void)0)
#define KMJ_LOG_HEAP(where) ((void)0)

#endif
