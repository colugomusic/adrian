#pragma once

#if defined(__apple_build_version__)
#include "jthread-impl.hpp"
namespace std { using jthread = nonstd::jthread; using stop_token = nonstd::stop_token; }
#else
#include <thread>
#endif
