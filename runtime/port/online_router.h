#pragma once
#include <string>

namespace tenchu::online::router {
enum class State { Off, Working, Open, Failed };
struct Status {
    State state=State::Off;
    std::string message, public_ip, local_ip;
    bool ports_open=false;
};
// Explicitly enabled by Get my join code; restored at startup after opting in.
void Start();
void Stop();
Status Snapshot();
}
