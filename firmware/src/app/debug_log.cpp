#include "app/debug_log.hpp"

// Called from the patched NimBLE library to trace Bluetooth bring-up.
extern "C" void kmj_trace(const char* fmt, ...) {
#ifdef KMJ_DEBUG_LOG
  va_list args;
  va_start(args, fmt);
  kmj::log_vprintf(fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

#ifdef KMJ_DEBUG_LOG

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_memory_utils.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

namespace kmj {
namespace {

constexpr size_t kRing = 8192;
constexpr size_t kLine = 200;
constexpr int kWatchMax = 4;

struct Watch {
  const char* name;
  TaskHandle_t task;
};

// Last lines of the log, kept in RTC memory so they survive a watchdog or panic reset.
constexpr size_t kKeep = 3072;
constexpr uint32_t kKeepMagic = 0x4B4C4F47;
struct Keep {
  uint32_t magic;
  uint32_t head;
  char buf[kKeep];
};
RTC_NOINIT_ATTR Keep g_keep;

char g_ring[kRing];
size_t g_head = 0;  // next write
size_t g_tail = 0;  // next read
size_t g_dropped = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
Watch g_watch[kWatchMax];
int g_watch_n = 0;
bool g_started = false;

void push(const char* data, size_t n) {
  portENTER_CRITICAL(&g_lock);
  for (size_t i = 0; i < n; i++) {
    g_ring[g_head] = data[i];
    g_head = (g_head + 1) % kRing;
    if (g_head == g_tail) {
      g_tail = (g_tail + 1) % kRing;
      g_dropped++;
    }
  }
  portEXIT_CRITICAL(&g_lock);
}

size_t pending() {
  portENTER_CRITICAL(&g_lock);
  const size_t n = (g_head + kRing - g_tail) % kRing;
  portEXIT_CRITICAL(&g_lock);
  return n;
}

size_t take(char* out, size_t cap, size_t* mark) {
  portENTER_CRITICAL(&g_lock);
  size_t n = (g_head + kRing - g_tail) % kRing;
  if (n > cap) {
    n = cap;
  }
  for (size_t i = 0; i < n; i++) {
    out[i] = g_ring[(g_tail + i) % kRing];
  }
  *mark = g_tail;
  portEXIT_CRITICAL(&g_lock);
  return n;
}

void consume(size_t mark, size_t n) {
  portENTER_CRITICAL(&g_lock);
  if (g_tail == mark) {
    g_tail = (g_tail + n) % kRing;
  }
  portEXIT_CRITICAL(&g_lock);
}

void emit(const char* fmt, va_list args) {
  char line[kLine];
  const int head = snprintf(line, sizeof(line), "[%7lu c%d] ", static_cast<unsigned long>(millis()),
                            static_cast<int>(xPortGetCoreID()));
  int body = vsnprintf(line + head, sizeof(line) - head, fmt, args);
  if (body < 0) {
    return;
  }
  size_t n = static_cast<size_t>(head + body);
  if (n > sizeof(line) - 2) {
    n = sizeof(line) - 2;
  }
  if (n == 0 || line[n - 1] != '\n') {
    line[n++] = '\n';
  }
  push(line, n);
  if (g_keep.magic == kKeepMagic && strstr(line, "beat int=") == nullptr) {
    portENTER_CRITICAL(&g_lock);
    for (size_t i = 0; i < n; i++) {
      g_keep.buf[g_keep.head] = line[i];
      g_keep.head = (g_keep.head + 1) % kKeep;
    }
    portEXIT_CRITICAL(&g_lock);
  }
}

// Replays the survivor buffer from the previous boot when that boot ended abnormally.
void replay_previous() {
  const esp_reset_reason_t why = esp_reset_reason();
  const bool abnormal = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT ||
                        why == ESP_RST_BROWNOUT;
  if (abnormal && g_keep.magic == kKeepMagic && g_keep.head < kKeep) {
    static const char kHead[] = "=== last log lines before the reset ===\n";
    push(kHead, sizeof(kHead) - 1);
    for (size_t i = 0; i < kKeep; i++) {
      const char c = g_keep.buf[(g_keep.head + i) % kKeep];
      if (c != '\0') {
        push(&c, 1);
      }
    }
    static const char kEnd[] = "=== end ===\n";
    push(kEnd, sizeof(kEnd) - 1);
  }
  memset(g_keep.buf, 0, sizeof(g_keep.buf));
  g_keep.head = 0;
  g_keep.magic = kKeepMagic;
}

int esp_vprintf(const char* fmt, va_list args) {
  emit(fmt, args);
  return 0;
}

const char* state_name(eTaskState state) {
  switch (state) {
    case eRunning:
      return "run";
    case eReady:
      return "ready";
    case eBlocked:
      return "blocked";
    case eSuspended:
      return "SUSPENDED";
    case eDeleted:
      return "deleted";
    default:
      return "?";
  }
}

void log_task(void*) {
  char chunk[128];
  uint32_t last_beat = 0;
  while (true) {
    if (pending() > 0) {
      size_t mark = 0;
      const size_t n = take(chunk, sizeof(chunk), &mark);
      const size_t sent = Serial.write(reinterpret_cast<const uint8_t*>(chunk), n);
      if (sent > 0) {
        consume(mark, sent);
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    const uint32_t now = millis();
    if (now - last_beat >= 2000) {
      last_beat = now;
      char text[kLine];
      int len = snprintf(text, sizeof(text), "beat int=%u big=%u psram=%u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                         static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
      for (int i = 0; i < g_watch_n && len > 0 && len < static_cast<int>(sizeof(text)) - 40; i++) {
        len += snprintf(text + len, sizeof(text) - len, " %s=%s/%u", g_watch[i].name,
                        state_name(eTaskGetState(g_watch[i].task)),
                        static_cast<unsigned>(uxTaskGetStackHighWaterMark(g_watch[i].task)));
      }
      log_printf("%s", text);
      if (g_dropped > 0) {
        log_printf("dropped %u bytes", static_cast<unsigned>(g_dropped));
        g_dropped = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

}  // namespace

void log_begin() {
  if (g_started) {
    return;
  }
  g_started = true;
  replay_previous();
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_set_vprintf(esp_vprintf);
  xTaskCreatePinnedToCore(log_task, "kmj-log", 4096, nullptr, 2, nullptr, 1);
  log_printf("log up, reset reason %d", static_cast<int>(esp_reset_reason()));
}

void log_watch(const char* name, TaskHandle_t task) {
  if (task == nullptr || g_watch_n >= kWatchMax) {
    return;
  }
  g_watch[g_watch_n++] = {name, task};
  const void* base = pxTaskGetStackStart(task);
  log_printf("task %s stack @%p %s", name, base, esp_ptr_external_ram(base) ? "PSRAM" : "internal");
}

void log_heap(const char* where) {
  log_printf("heap %-14s int free=%u big=%u", where,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void log_vprintf(const char* fmt, va_list args) { emit(fmt, args); }

void log_printf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  emit(fmt, args);
  va_end(args);
}

}  // namespace kmj

#endif  // KMJ_DEBUG_LOG
