//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <common/concurrentQueue.hpp>
#include <functional>

template <typename Owner>
using TaskQueue = ConcurrentQueue<std::move_only_function<void(Owner&)>>;