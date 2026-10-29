#pragma once
#include <condition_variable>
#include <mutex>

namespace tenchu {
class ObjectInitialization {
    std::mutex mutex_;
    std::condition_variable ready_;
    bool finished_=false, successful_=false;
public:
    void Complete(bool successful) {
        { std::lock_guard lock(mutex_); finished_=true; successful_=successful; }
        ready_.notify_all();
    }
    bool Wait() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock,[&]{return finished_;});
        return successful_;
    }
};
}
