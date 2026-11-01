#pragma once
#include "cloth_solver.h"
#include <cstring>

namespace tenchu::cloth {
template<class T>T Read(const uint8_t* b,uint32_t at){T v;std::memcpy(&v,b+at,sizeof(v));return v;}
template<class T>void Write(uint8_t* b,uint32_t at,T v){std::memcpy(b+at,&v,sizeof(v));}
inline bool ReadFrame(const uint8_t* b,uint32_t object,Frame& f){
    if(object<0x10000||uint64_t(object)+0x32c0>0x04000000)return false;
    f.speed=response_speed.load();f.drag=settling_drag.load();f.stiffness=stretch_stiffness.load();
    f.bend=bend_resistance.load();f.edges=edge_collision.load()?edge_strength.load():0;
    f.self=self_collision.load()?self_strength.load():0;f.thickness=self_thickness.load();
    f.count=Read<uint32_t>(b,object+0x3200);
    if(!f.count||f.count>kMaxPoints)return false;
    f.damping=Read<float>(b,object+0x3208);f.gravity=Read<float>(b,object+0x320c);
    f.gravity*=gravity_scale.load();
    f.floor=Read<float>(b,object+0x3218)+1.f;
    f.force=Read<Vec>(b,object+0x3230);
    f.grid=b[object+0x3220]!=0;f.cross_column=b[object+0x3290]!=0;
    f.width=b[object+0x3222];f.spacing_x=Read<float>(b,object+0x3224);f.spacing_y=Read<float>(b,object+0x3228);
    for(unsigned i=0;i<f.count;i++){
        auto& p=f.points[i];const auto metadata=object+0xc00+i*64;
        p.position=Read<Vec>(b,object+i*24);p.velocity=Read<Vec>(b,metadata);
        p.pinned=b[metadata+0x3c]!=0;
        for(unsigned e=0;e<6;e++){
            p.neighbor[e]=Read<uint16_t>(b,metadata+0x30+e*2);
            p.length[e]=Read<float>(b,metadata+0x18+e*4);
        }
    }
    return true;
}
inline RestShape CaptureRest(const uint8_t* b,uint32_t object){
    Frame f;
    if(!ReadFrame(b,object,f))return {};
    f.force={};return CaptureShape(f);
}
inline bool Advance(uint8_t* b,uint32_t object,const Body* body=nullptr,const RestShape* rest=nullptr){
    Frame f;
    if(!ReadFrame(b,object,f))return false;
    if(body)f.body=*body;
    if(rest&&rest->points==f.count&&rest->signature==ShapeSignature(f))f.rest=rest;
    if(!Step(f))return false; // Validate and solve locally before writing anything.
    unsigned pinned=0;
    for(unsigned i=0;i<f.count;i++){
        const auto metadata=object+0xc00+i*64;
        pinned+=f.points[i].pinned;
        if(!f.points[i].pinned){
            Write(b,object+i*24,f.points[i].position);
            Write(b,metadata,f.points[i].velocity);
        }
        b[metadata+0x3d]=0;
    }
    b[object+0x3221]=uint8_t(pinned);
    Write(b,object+0x3230,Vec{}); // The retail solver consumes this impulse once.
    return true;
}
}
