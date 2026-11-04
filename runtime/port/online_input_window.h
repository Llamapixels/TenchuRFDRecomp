#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace tenchu::online::input_window {
using Sample=std::array<uint8_t,28>;
constexpr uint32_t marker=0x80000000u,sequence_mask=0x1fffffffu;
inline uint32_t Tag(uint32_t first,unsigned count){return marker|((count-1)<<29)|first;}
struct Window {uint32_t first=0;unsigned count=0;std::array<Sample,4> samples{};};
struct Update {uint32_t next=0;std::vector<Sample> samples;};
// Overlapping history retransmits the last four genuine input records. Never
// duplicate button edges, replay old packets, or enqueue beyond the native ring.
inline std::optional<Update> Accept(uint32_t expected,unsigned queued,const Window& window){
    if(!window.count||window.count>4||queued>200||window.first>sequence_mask||
       uint64_t(window.first)+window.count>uint64_t(sequence_mask)+1)return {};
    const uint32_t end=window.first+window.count;
    Update result;result.next=expected;
    if(end<=expected)return result;
    const uint32_t gap=window.first>expected?window.first-expected:0;
    const uint32_t available=end-(window.first>expected?window.first:expected);
    if(gap>200||gap+available>200-queued)return {};
    // A burst longer than the redundant history has no recoverable input.
    // Use neutral controls rather than inventing/repeating attacks.
    // Native +348 analog field is record+12; +34C buttons at record+16
    // must remain zero. The final camera-angle fields are not button input.
    Sample neutral{};for(int i=12;i<16;++i)neutral[i]=4;
    for(uint32_t i=0;i<gap;++i)result.samples.push_back(neutral);
    const unsigned skip=expected>window.first?expected-window.first:0;
    for(unsigned i=skip;i<window.count;++i)result.samples.push_back(window.samples[i]);
    result.next=end;return result;
}
}
