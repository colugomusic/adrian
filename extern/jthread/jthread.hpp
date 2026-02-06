#pragma once

#include <version>

// Use the fallback jthread implementation if std::jthread is not available.
// This can happen with:
//   - AppleClang (doesn't support jthread)
//   - LLVM Clang with libc++ targeting older macOS (jthread requires macOS 14+)
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#include <thread>
#else
#include "jthread-impl.hpp"
namespace std { using jthread = nonstd::jthread; using stop_token = nonstd::stop_token; }
#endif
