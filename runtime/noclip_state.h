#pragma once
#include <atomic>
namespace tenchu::noclip {
inline std::atomic<bool> requested{false},active{false};
}
