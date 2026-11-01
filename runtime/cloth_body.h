#pragma once
#include "cloth_guest.h"

namespace tenchu::cloth {
inline bool GuestRange(uint32_t at,uint32_t size){return at>=0x10000&&uint64_t(at)+size<=0x04000000;}
inline bool BodyJoint(const uint8_t* name){
    // Retail body joints are 00-jnt .. 22-jnt. root and EX-* are animation,
    // weapon and attachment nodes, and must not become collision geometry.
    return name[0]>='0'&&name[0]<='2'&&name[1]>='0'&&name[1]<='9'&&
        (name[0]-'0')*10+name[1]-'0'<=22&&std::memcmp(name+2,"-jnt\0",5)==0;
}
inline Body MakeBody(const uint8_t* b,uint32_t model){
    Body body;
    if(!GuestRange(model,0x1891))return body;
    const unsigned count=b[model+0x1890];
    const uint32_t skeleton=Read<uint32_t>(b,model+0x938);
    if(count<3||count>34||!GuestRange(skeleton,count*0xe0))return body;
    std::array<Vec,34> position{};
    std::array<int,34> parent{};std::array<unsigned,34> branches{};
    std::array<bool,34> valid{};
    const auto matrix=model+0x10;
    for(unsigned i=0;i<count;i++){
        const auto node=skeleton+i*0xe0;
        parent[i]=Read<int8_t>(b,node+0xcc);
        if(!BodyJoint(b+node))continue;
        const Vec local=Read<Vec>(b,model+0x940+i*0x70+0x30);
        // 00046920/00047050 build model-space joint matrices at +940.
        // Cloth's 00053080 rotates into world axes but removes owner
        // translation. Use precisely that space for body collision.
        position[i]={
            local.x*Read<float>(b,matrix)+local.y*Read<float>(b,matrix+0x10)+local.z*Read<float>(b,matrix+0x20),
            local.x*Read<float>(b,matrix+4)+local.y*Read<float>(b,matrix+0x14)+local.z*Read<float>(b,matrix+0x24),
            local.x*Read<float>(b,matrix+8)+local.y*Read<float>(b,matrix+0x18)+local.z*Read<float>(b,matrix+0x28)};
        valid[i]=Finite(position[i])&&Length(position[i])<10000;
    }
    float longest=0;
    for(unsigned i=0;i<count;i++){
        const int p=parent[i];
        if(!valid[i]||p<0||unsigned(p)>=count||!valid[p]||unsigned(p)==i)continue;
        const float length=Length(position[i]-position[p]);
        if(length>.01f&&length<500){longest=std::max(longest,length);branches[p]++;}
    }
    if(longest<.1f)return body;
    const float scale=std::clamp(body_size.load(),.5f,1.5f);
    for(unsigned i=0;i<count&&body.count<body.capsules.size();i++){
        const int p=parent[i];
        if(!valid[i]||p<0||unsigned(p)>=count||!valid[p]||unsigned(p)==i)continue;
        const float length=Length(position[i]-position[p]);
        if(length<=.01f||length>=500)continue;
        const bool junction=branches[i]>=2||branches[p]>=2;
        const float radius=(junction?longest*.32f:std::clamp(length*.22f,longest*.075f,longest*.28f))*scale;
        body.capsules[body.count++]={position[p],position[i],radius};
    }
    return body;
}
}
