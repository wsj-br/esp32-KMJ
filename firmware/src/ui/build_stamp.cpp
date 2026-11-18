#include "ui/build_stamp.hpp"

#if __has_include("kmj_generated_build_stamp.hpp")
#include "kmj_generated_build_stamp.hpp"
#endif

#ifndef KMJ_BUILD_TIMESTAMP
#define KMJ_BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

namespace kmj {

const char* build_timestamp() { return KMJ_BUILD_TIMESTAMP; }

}  // namespace kmj
