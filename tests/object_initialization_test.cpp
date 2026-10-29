#include "kernel/object_initialization.h"
#include <future>
#include <chrono>
#include <cstdio>
int main() {
    using namespace std::chrono_literals;
    tenchu::ObjectInitialization object,other;
    std::promise<void> started;
    auto work=std::async(std::launch::async,[&]{started.set_value();return object.Wait();});
    started.get_future().get();
    other.Complete(true);
    if(work.wait_for(20ms)!=std::future_status::timeout) { object.Complete(false); return 1; }
    object.Complete(true);
    if(work.wait_for(1s)!=std::future_status::ready || !work.get()) return 2;
    tenchu::ObjectInitialization failed;
    failed.Complete(false);
    if(failed.Wait()) return 3;
    std::puts("PASS: worker waits for its own constructor; success/failure wakes correctly");
    return 0;
}
