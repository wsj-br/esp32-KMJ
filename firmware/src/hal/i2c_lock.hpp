#pragma once

namespace kmj {

void i2c_lock_init();
void i2c_lock();
void i2c_lock(const char* who);
void i2c_unlock();
// Tag of the task currently holding the lock, or nullptr. Informational only;
// read without synchronization so the debug heartbeat can show it.
const char* i2c_holder();

}  // namespace kmj
