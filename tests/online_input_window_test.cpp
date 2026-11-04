#include "port/online_input_window.h"
#include <cstdio>
#include <stdexcept>
using namespace tenchu::online::input_window;
int main(){try{
    auto check=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
    uint32_t expected=0;std::vector<Sample> received;
    for(uint32_t frame=0;frame<600;++frame){
        Window w;w.count=frame<3?frame+1:4;w.first=frame+1-w.count;
        for(unsigned i=0;i<w.count;++i){w.samples[i][0]=uint8_t(w.first+i);w.samples[i][4]=(w.first+i)%17==0?1:0;}
        const auto tag=Tag(w.first,w.count);check((tag&sequence_mask)==w.first&&((tag>>29)&3)+1==w.count,"wire metadata roundtrip");
        if(frame%13==7)continue; // one lost packet is recovered by subsequent history
        auto result=Accept(expected,0,w);check(bool(result),"valid history");
        received.insert(received.end(),result->samples.begin(),result->samples.end());expected=result->next;
        auto duplicate=Accept(expected,0,w);check(duplicate&&duplicate->samples.empty(),"duplicates never repeat actions");
    }
    check(expected==600&&received.size()==600,"all frames recovered in sequence across native ring wraps");
    for(unsigned i=0;i<received.size();++i)check(received[i][0]==uint8_t(i)&&received[i][4]==(i%17==0?1:0),"movement samples and attack edges preserved");
    Window gap;gap.first=10;gap.count=1;gap.samples[0][4]=1;
    auto recovered=Accept(5,0,gap);check(recovered&&recovered->samples.size()==6&&recovered->samples.back()[4]==1,"bounded long-loss recovery");
    for(unsigned i=0;i<5;++i){
        for(unsigned j=0;j<12;++j)check(recovered->samples[i][j]==0,"lost history does not fabricate button edges");
        check(recovered->samples[i][16]==0&&recovered->samples[i][17]==0,"neutral button word stays clear");
        for(unsigned j=12;j<16;++j)check(recovered->samples[i][j]==4,"neutral native analog field");
    }
    check(!Accept(0,200,gap)&&!Accept(0,0,Window{500,1,{}})&&!Accept(0,0,Window{0,5,{}}),"queue, sequence and size bounds");
    std::puts("PASS: per-frame windows preserve inputs/attack edges through loss, duplicates, ring wraps and bounded recovery.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
