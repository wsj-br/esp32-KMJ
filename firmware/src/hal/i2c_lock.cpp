#include "hal/i2c_lock.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace kmj {
namespace {

SemaphoreHandle_t g_mu = nullptr;

}  // namespace

void i2c_lock_init() {
  if (g_mu == nullptr) {
    g_mu = xSemaphoreCreateMutex();
  }
}

void i2c_lock() {
  i2c_lock_init();
  xSemaphoreTake(g_mu, portMAX_DELAY);
}

void i2c_unlock() {
  if (g_mu != nullptr) {
    xSemaphoreGive(g_mu);
  }
}

}  // namespace kmj
