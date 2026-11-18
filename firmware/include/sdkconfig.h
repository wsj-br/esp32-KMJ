#pragma once

// The prebuilt sdkconfig sizes the NimBLE host pools larger than the
// internal RAM left after the controller starts. Those pools must stay
// in internal RAM; the controller faults if they land in PSRAM.
#include_next "sdkconfig.h"

#undef CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT 4

#undef CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT 4


// Wi-Fi setup is one peripheral link. One connection shrinks the host tables
// and the controller's activity pool.
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
