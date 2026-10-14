#pragma once

#include <atomic>
#include <memory>

namespace qf {
namespace application {

class CancelToken {
public:
    void cancel() { cancelled_.store(true, std::memory_order_release); }
    bool isCancelled() const { return cancelled_.load(std::memory_order_acquire); }
    void reset() { cancelled_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> cancelled_{false};
};

using CancelTokenPtr = std::shared_ptr<CancelToken>;

}
}