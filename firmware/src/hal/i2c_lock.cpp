#include "hal/i2c_lock.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace kmj {
namespace {

SemaphoreHandle_t g_mu = nullptr;

const char* g_holder = nullptr;
}  // namespace

void i2c_lock_init() {
  if (g_mu == nullptr) {
    g_mu = xSemaphoreCreateMutex();
  }
}

void i2c_lock(const char* who) {
  i2c_lock_init();
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_holder = who;
}

void i2c_lock() {
  i2c_lock("?");
}

void i2c_unlock() {
  g_holder = nullptr;
  if (g_mu != nullptr) {
    xSemaphoreGive(g_mu);
  }
}

const char* i2c_holder() {
  return g_holder;
}

}  // namespace kmj
