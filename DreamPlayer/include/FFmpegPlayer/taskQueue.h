//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/concurrentQueue.hpp>
#include <functional>

#ifdef PLATFORM_MACOS
// 在编码时，辣鸡苹果还没有实现 C++23 std::move_only_function，先用 std::function 代替
#define move_only_function function
#endif

template <typename Owner>
using TaskQueue = ConcurrentQueue<std::move_only_function<void(Owner&)>>;

#ifdef PLATFORM_MACOS
#undef move_only_function
#endif