#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <bit>
#include <cmath>
#include <cstdint>

namespace tenchu::cloth {
inline std::atomic<bool> enhanced{false};
inline std::atomic<bool> body_collision{true};
inline std::atomic<float> response_speed{.6f},body_size{1.f};
inline std::atomic<float> settling_drag{.9f},gravity_scale{1.f},stretch_stiffness{1.f},bend_resistance{.25f};
inline std::atomic<bool> edge_collision{true},self_collision{false};
inline std::atomic<float> edge_strength{1.f},self_strength{.5f},self_thickness{.08f};
constexpr unsigned kMaxPoints=128, kSubsteps=4, kIterations=8;
struct Vec {
    float x=0,y=0,z=0;
    Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
    Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
    Vec operator*(float s)const{return {x*s,y*s,z*s};}
};
inline bool Finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Length(Vec v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
inline float Dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Capsule {Vec a,b;float radius=0;};
struct Body {unsigned count=0;std::array<Capsule,32> capsules{};};
inline Vec Closest(Vec p,const Capsule& c){
    const Vec axis=c.b-c.a;const float length2=Dot(axis,axis);
    return c.a+axis*(length2>1.e-8f?std::clamp(Dot(p-c.a,axis)/length2,0.f,1.f):0.f);
}
// Earliest intersection of a moving point and a capsule, including end caps.
// This catches a point crossing the entire body in one simulation substep.
inline float Hit(Vec from,Vec to,const Capsule& c){
    const Vec motion=to-from,axis=c.b-c.a,offset=from-c.a;
    const float aa=Dot(axis,axis),mm=Dot(motion,motion),am=Dot(axis,motion),ao=Dot(axis,offset);
    if(mm<1.e-10f)return 2;
    float first=2;
    auto sphere=[&](Vec center){
        const Vec q=from-center;const float b=Dot(motion,q),d=b*b-mm*(Dot(q,q)-c.radius*c.radius);
        if(d>=0){const float t=(-b-std::sqrt(d))/mm;if(t>=0&&t<=1)first=std::min(first,t);}
    };
    if(aa>1.e-8f){
        const float a=aa*mm-am*am,b=aa*Dot(offset,motion)-ao*am;
        const float cc=aa*Dot(offset,offset)-ao*ao-c.radius*c.radius*aa,d=b*b-a*cc;
        if(a>1.e-8f&&d>=0){
            const float t=(-b-std::sqrt(d))/a,y=ao+t*am;
            if(t>=0&&t<=1&&y>=0&&y<=aa)first=t;
        }
    }
    sphere(c.a);sphere(c.b);return first;
}
inline bool Collide(Vec& position,Vec previous,const Capsule& c,Vec& normal){
    const Vec old_delta=previous-Closest(previous,c);
    if(Dot(old_delta,old_delta)>=c.radius*c.radius){
        const float hit=Hit(previous,position,c);
        if(hit<=1){
            const Vec at=previous+(position-previous)*hit,d=at-Closest(at,c);
            const float length=Length(d);
            if(length>1.e-6f){
                normal=d*(1/length);
                position=position+normal*std::max(0.f,.001f-Dot(position-at,normal));
                return true;
            }
        }
    }
    const Vec center=Closest(position,c),delta=position-center;const float length=Length(delta);
    if(length>=c.radius)return false;
    if(length>1.e-6f)normal=delta*(1/length);
    else if(Length(old_delta)>1.e-6f)normal=old_delta*(1/Length(old_delta));
    else{
        // Deterministic perpendicular when a point lands exactly on the axis.
        const Vec axis=c.b-c.a;
        normal=std::fabs(axis.x)>std::fabs(axis.z)?Vec{-axis.y,axis.x,0}:Vec{0,-axis.z,axis.y};
        const float n=Length(normal);normal=n>1.e-6f?normal*(1/n):Vec{1,0,0};
    }
    position=center+normal*(c.radius+.001f);return true;
}
struct Point {
    Vec position,velocity;
    bool pinned=false;
    std::array<uint16_t,6> neighbor{};
    std::array<float,6> length{};
};
struct Bend {uint16_t a=0,b=0;float length=0;};
struct RestShape {
    unsigned points=0,count=0;uint64_t signature=0;
    std::array<Bend,kMaxPoints*3> bends{};
};
struct Frame {
    unsigned count=0,width=0;
    bool grid=false,cross_column=false;
    float damping=.7f,gravity=1.5f,floor=0,spacing_x=0,spacing_y=0;
    float speed=1,drag=1;
    float stiffness=1,bend=0,edges=0,self=0,thickness=.08f;
    const RestShape* rest=nullptr;
    Vec force;
    Body body;
    std::array<Point,kMaxPoints> points{};
};
// Dynamic state stays in game memory. Rest-shape metadata is refreshed at
// original loading/cloning boundaries, never inferred from an animated pose.
inline bool Valid(const Frame& f){
    if(!f.count||f.count>kMaxPoints||!Finite(f.force)||!std::isfinite(f.damping)||f.damping<0||f.damping>1||
       !std::isfinite(f.gravity)||!std::isfinite(f.floor)||!std::isfinite(f.speed)||f.speed<.25f||f.speed>1||
       !std::isfinite(f.drag)||f.drag<0||f.drag>1||f.body.count>f.body.capsules.size())return false;
    for(float value:{f.stiffness,f.bend,f.edges,f.self})if(!std::isfinite(value)||value<0||value>1)return false;
    if(!std::isfinite(f.thickness)||f.thickness<.01f||f.thickness>.3f)return false;
    for(unsigned i=0;i<f.body.count;i++){
        const auto& c=f.body.capsules[i];
        if(!Finite(c.a)||!Finite(c.b)||!std::isfinite(c.radius)||c.radius<=0)return false;
    }
    if(f.grid&&(!f.width||f.width>f.count||f.count%f.width||!std::isfinite(f.spacing_x)||!std::isfinite(f.spacing_y)))return false;
    for(unsigned i=0;i<f.count;i++){
        const auto& p=f.points[i];
        if(!Finite(p.position))return false;
        for(unsigned e=0;e<6;e++)if(p.neighbor[e]!=0xffff&&
            (p.neighbor[e]>=f.count||!std::isfinite(p.length[e])||p.length[e]<0))return false;
    }
    return true;
}
inline void Limit(Vec& a,Vec& b,float rest,bool pin_a,bool pin_b,float strength=1){
    const Vec d=b-a;const float length=Length(d);
    if(length<=rest||length<1.e-6f||!std::isfinite(length)||(pin_a&&pin_b))return;
    const Vec correction=d*((length-rest)/length*strength);
    if(!pin_a)a=a+correction*(pin_b?1.f:.5f);
    if(!pin_b)b=b-correction*(pin_a?1.f:.5f);
}
#include "cloth_constraints.inl"
inline bool Step(Frame& f){
    if(!Valid(f))return false;
    const float h=f.speed/kSubsteps;
    const float damping=f.damping*f.drag;
    const float retention=std::pow(damping,h);
    // Preserve the original full-tick velocity decay and terminal response.
    const float kick=damping<.9999f?damping*(1-retention)/(1-damping):h;
    const Vec acceleration=Vec{-f.force.x,-f.force.y-f.gravity,-f.force.z};
    const auto topology=MakeTopology(f);
    const float stretch=1-std::pow(1-f.stiffness,1.f/kIterations);
    const float bend=1-std::pow(1-f.bend,1.f/(kSubsteps*kIterations));
    std::array<Vec,kMaxPoints> predicted{};
    std::array<Vec,kMaxPoints> start{},contact{};
    std::array<bool,kMaxPoints> touching{};
    for(unsigned step=0;step<kSubsteps;step++){
        touching.fill(false);
        for(unsigned i=0;i<f.count;i++){
            auto& p=f.points[i];
            start[i]=p.position;
            if(p.pinned)continue;
            if(!Finite(p.velocity))p.velocity={};
            p.velocity=p.velocity*retention+acceleration*kick;
            p.position=p.position+p.velocity*h;
            predicted[i]=p.position;
        }
        for(unsigned pass=0;pass<kIterations;pass++){
            // Alternate the sweep direction so error is not always pushed
            // toward the same edge of the robe.
            for(unsigned index=0;index<f.count;index++){
                const unsigned i=pass%2?f.count-1-index:index;
                auto& a=f.points[i];
                for(unsigned e=0;e<6;e++){
                    const auto j=a.neighbor[e];
                    if(j==0xffff||j==i)continue;
                    auto& b=f.points[j];Limit(a.position,b.position,a.length[e],a.pinned,b.pinned,stretch);
                }
            }
            // Retain the retail solver's grid reach limits from each fixed
            // attachment. These prevent a free point escaping its anchor.
            if(f.grid)for(unsigned a=0;a<f.count;a++)if(f.points[a].pinned){
                for(unsigned b=0;b<f.count;b++)if(!f.points[b].pinned){
                    if(!f.cross_column&&a%f.width!=b%f.width)continue;
                    const float dx=(int(b%f.width)-int(a%f.width))*f.spacing_x;
                    const float dy=(int(b/f.width)-int(a/f.width))*f.spacing_y;
                    Limit(f.points[a].position,f.points[b].position,std::sqrt(dx*dx+dy*dy),true,false,stretch);
                }
            }
            ApplyBending(f,bend);
            if(f.self>0&&pass%4==3)SeparateSelf(f,topology,start,contact,touching);
            for(unsigned i=0;i<f.count;i++)if(!f.points[i].pinned)f.points[i].position.y=std::max(f.points[i].position.y,f.floor);
            for(unsigned i=0;i<f.count;i++)if(!f.points[i].pinned){
                for(unsigned c=0;c<f.body.count;c++){
                    Vec normal;
                    if(Collide(f.points[i].position,start[i],f.body.capsules[c],normal)){
                        touching[i]=true;contact[i]=normal;
                    }
                }
            }
            if(f.edges>0)CollideEdges(f,topology,start,contact,touching);
            for(unsigned i=0;i<f.count;i++)if(!f.points[i].pinned)f.points[i].position.y=std::max(f.points[i].position.y,f.floor);
        }
        for(unsigned i=0;i<f.count;i++){
            auto& p=f.points[i];if(p.pinned)continue;
            // Feed position corrections back into velocity so constrained
            // motion cannot accumulate energy behind a fixed attachment.
            p.velocity=p.velocity+(p.position-predicted[i])*(1/h);
            if(touching[i]){
                // A moving body can push a point out a long way. Do not turn
                // that depenetration into a launch impulse or a bounce.
                p.velocity=(p.velocity-contact[i]*Dot(p.velocity,contact[i]))*.9f;
            }
            if(!Finite(p.position)||!Finite(p.velocity))return false;
        }
    }
    return true;
}
}
