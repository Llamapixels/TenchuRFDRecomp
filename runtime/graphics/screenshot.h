#pragma once
#include <atomic>
#include <mutex>
#include <string>
namespace tenchu::shots {
inline std::atomic<int> delay{-1};
inline std::atomic<bool> saving{false};
inline std::mutex status_lock;
inline std::string status;
inline void Request(){int idle=-1;delay.compare_exchange_strong(idle,2);}
inline bool NextFrame(){
    int value=delay.load();
    if(value<0)return false;
    if(delay.fetch_sub(1)==0){saving=true;return true;}
    return false;
}
inline void Result(const std::string& text){std::lock_guard lock(status_lock);status=text;}
inline std::string Status(){std::lock_guard lock(status_lock);return status;}
}
