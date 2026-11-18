#pragma once

#include "ui/model.hpp"

namespace kmj {

Host& runtime_host();
void runtime_start();
void runtime_show_bootloader_and_halt();

}  // namespace kmj
