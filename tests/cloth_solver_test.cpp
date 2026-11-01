#include "cloth_body.h"
#include "cpu/x86_context.h"
#include "core/log.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" void sub_00051A60(X86Context&,uint8_t*);
extern "C" void sub_000490B0(X86Context&,uint8_t*);
extern "C" void sub_00051250(X86Context&,uint8_t*);
extern "C" void sub_0004FDD0(X86Context&,uint8_t*);
namespace logx {void Write(Level,const char*,...){} }
static unsigned original_calls=0,normal_calls=0;
extern "C" void __imp__sub_00051A60(X86Context& c,uint8_t*){++original_calls;c.esp+=4;c.eax=123;}
extern "C" void __imp__sub_00052330(X86Context& c,uint8_t*){++normal_calls;c.esp+=4;c.eax=456;}
extern "C" void __imp__sub_00051250(X86Context& c,uint8_t*){c.esp+=12;}
extern "C" void __imp__sub_0004FDD0(X86Context& c,uint8_t* b){
    using namespace tenchu::cloth;
    const auto to=Read<uint32_t>(b,c.esp+4),from=Read<uint32_t>(b,c.esp+8);
    const auto destination=Read<uint32_t>(b,to+0x98),source=Read<uint32_t>(b,from+0x98);
    std::memcpy(b+destination,b+source,0x32c0);c.esp+=4;
}
extern "C" void __imp__sub_00055550(X86Context&,uint8_t*){}
extern "C" void __imp__sub_00053640(X86Context&,uint8_t*){}
extern "C" void __imp__sub_000490B0(X86Context& c,uint8_t* b){
    const auto original=c;c.ecx=0x10000;c.esp-=4;sub_00051A60(c,b);c=original;c.esp+=4;
}
using namespace tenchu::cloth;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Frame Grid(){
    Frame f;f.count=32;f.width=4;f.grid=true;f.spacing_x=f.spacing_y=10;f.floor=-100;
    for(unsigned i=0;i<f.count;i++){
        auto& p=f.points[i];p.position={float(i%4)*10,100-float(i/4)*10,0};p.pinned=i<4;
        p.neighbor.fill(0xffff);
        if(i%4<3){p.neighbor[0]=uint16_t(i+1);p.length[0]=10;}
        if(i+4<f.count){p.neighbor[1]=uint16_t(i+4);p.length[1]=10;}
    }
    return f;
}
int main(){try{
    auto f=Grid();const auto rest=f;
    for(unsigned frame=0;frame<240;frame++){
        f.force={-.2f,0,.1f};Check(Step(f),"hanging cloth must stay finite under sustained force");
        for(unsigned i=0;i<f.count;i++){
            const auto& p=f.points[i];
            if(p.pinned)Check(std::memcmp(&p.position,&rest.points[i].position,sizeof(Vec))==0,"attachments must remain bit-exact");
            else Check(p.position.y>=f.floor,"floor must constrain free points");
            for(unsigned e=0;e<6;e++)if(p.neighbor[e]!=0xffff)
                Check(Length(f.points[p.neighbor[e]].position-p.position)<=p.length[e]*1.03f,"constraint sweeps must bound cloth stretch");
        }
    }
    Check(f.points[31].position.x>rest.points[31].position.x+1,"free cloth must respond to force instead of becoming rigid");
    auto bad=Grid();bad.points[8].neighbor[0]=128;Check(!Step(bad),"out-of-range topology must be rejected");
    bad=Grid();bad.width=0;Check(!Step(bad),"grid division by zero must be rejected");
    bad=Grid();bad.damping=2;Check(!Step(bad),"unsupported damping must retain native fallback");
    auto recovery=Grid();recovery.points[31].velocity.x=std::numeric_limits<float>::quiet_NaN();
    Check(Step(recovery)&&Finite(recovery.points[31].velocity),"invalid free velocity must not poison the panel");
    // Isolated velocity decay: four substeps must retain one original tick
    // of damping, rather than applying the full damping four times.
    Frame drift;drift.count=1;drift.gravity=0;drift.floor=-100;drift.damping=.7f;
    drift.points[0].neighbor.fill(0xffff);drift.points[0].velocity={10,0,0};
    Check(Step(drift)&&std::fabs(drift.points[0].velocity.x-7)<.0001f,"substeps must preserve full-tick velocity retention");
    auto fast=drift,slow=drift;
    slow.speed=.6f;slow.drag=.9f;fast.points[0].position={};slow.points[0].position={};
    Check(Step(fast)&&Step(slow)&&slow.points[0].position.x<fast.points[0].position.x*.8f,"heavier preset must slow the response");
    Frame fall=drift;fall.damping=1;fall.gravity=1;fall.points[0].position={0,1,0};fall.floor=1;fall.points[0].velocity={0,-100,0};
    Check(Step(fall)&&fall.points[0].position.y==1&&std::fabs(fall.points[0].velocity.y)<.001f,"floor correction must remove downward velocity");
    Frame collision=drift;collision.damping=1;collision.gravity=0;
    collision.body.count=1;collision.body.capsules[0]={{0,-2,0},{0,2,0},1};
    collision.points[0].position={-3,0,0};collision.points[0].velocity={40,0,0};
    Check(Step(collision)&&collision.points[0].position.x<=-1&&std::fabs(collision.points[0].velocity.x)<.001f,"fast cloth must not tunnel through or bounce off the body");
    collision.points[0].position={0,4,0};collision.points[0].velocity={0,-40,0};
    Check(Step(collision)&&collision.points[0].position.y>=3,"swept collision must include capsule end caps");
    collision.points[0].position={0,0,0};collision.points[0].velocity={};
    Check(Step(collision)&&Length(collision.points[0].position-Closest(collision.points[0].position,collision.body.capsules[0]))>=1,"points exactly on capsule axes must recover without NaNs");
    collision.points[0].position={0,0,0};collision.points[0].pinned=true;
    Check(Step(collision)&&Length(collision.points[0].position)==0,"body collision must never move fixed attachments");
    Frame edge;edge.count=2;edge.gravity=0;edge.damping=1;edge.floor=-100;
    for(unsigned i=0;i<2;i++)edge.points[i].neighbor.fill(0xffff);
    edge.points[0].position={-2,0,0};edge.points[1].position={2,0,0};
    edge.points[0].neighbor[0]=1;edge.points[0].length[0]=4;
    edge.body.count=1;edge.body.capsules[0]={{0,-1,0},{0,1,0},1};
    auto point_only=edge;Check(Step(point_only),"point-only control must run");
    Check(Length(ClosestSegments(point_only.points[0].position,point_only.points[1].position,{0,-1,0},{0,1,0}).a)<.01f,"control must expose a body crossing between vertices");
    edge.edges=1;Check(Step(edge),"edge collision must run");
    auto pair=ClosestSegments(edge.points[0].position,edge.points[1].position,{0,-1,0},{0,1,0});
    Check(Length(pair.a-pair.b)>.99f,"edge collision must block the gap missed by point-only tests");
    Frame strand;strand.count=3;strand.gravity=0;strand.damping=1;strand.floor=-100;
    for(unsigned i=0;i<3;i++){strand.points[i].neighbor.fill(0xffff);strand.points[i].position={float(i),0,0};}
    strand.points[0].neighbor[0]=1;strand.points[0].length[0]=1;
    strand.points[1].neighbor[0]=0;strand.points[1].length[0]=1;strand.points[1].neighbor[1]=2;strand.points[1].length[1]=1;
    strand.points[2].neighbor[0]=1;strand.points[2].length[0]=1;
    const auto authored=CaptureShape(strand);Check(authored.count==1,"authored strand must produce a two-hop bend constraint");
    strand.points[1].position={.6f,.8f,0};strand.points[2].position={1.2f,0,0};strand.rest=&authored;
    auto floppy=strand;Check(Step(floppy),"floppy control must run");strand.bend=1;Check(Step(strand),"bend resistance must run");
    Check(std::fabs(Length(strand.points[2].position-strand.points[0].position)-2)<
          std::fabs(Length(floppy.points[2].position-floppy.points[0].position)-2),"bend resistance must restore the loaded shape instead of preserving the deformed pose");
    Frame self;self.count=4;self.gravity=0;self.damping=1;self.floor=-100;self.self=1;self.thickness=.1f;
    for(unsigned i=0;i<4;i++)self.points[i].neighbor.fill(0xffff);
    self.points[0].position={-2,0,0};self.points[1].position={2,0,0};
    self.points[2].position={0,-2,0};self.points[3].position={0,2,0};
    self.points[0].neighbor[0]=1;self.points[0].length[0]=4;self.points[2].neighbor[0]=3;self.points[2].length[0]=4;
    auto no_self=self;no_self.self=0;Check(Step(no_self)&&Step(self),"self-collision comparison must run");
    pair=ClosestSegments(self.points[0].position,self.points[1].position,self.points[2].position,self.points[3].position);
    Check(Length(pair.a-pair.b)>.35f,"disjoint cloth edges must separate at a crossing");
    Frame neighbors;neighbors.count=2;neighbors.gravity=0;neighbors.floor=-100;neighbors.self=1;
    neighbors.points[0].neighbor.fill(0xffff);neighbors.points[1].neighbor.fill(0xffff);
    neighbors.points[0].neighbor[0]=1;neighbors.points[0].length[0]=1;
    Check(Step(neighbors)&&Length(neighbors.points[1].position-neighbors.points[0].position)==0,"local mesh neighbors must not repel each other");
    neighbors.points[0].neighbor[0]=0xffff;neighbors.points[0].pinned=true;
    Check(Step(neighbors)&&Length(neighbors.points[0].position)==0&&Length(neighbors.points[1].position)>.05f,"non-neighbor separation must preserve fixed points");
    std::vector<uint8_t> b(0x30000,0x5a);const uint32_t object=0x10000;
    auto setup=[&]{
        const auto source=Grid();Write(b.data(),object+0x3200,source.count);
        Write(b.data(),object+0x3208,source.damping);Write(b.data(),object+0x320c,source.gravity);
        Write(b.data(),object+0x3218,source.floor-1);Write(b.data(),object+0x3230,Vec{-.2f,0,.1f});
        b[object+0x3220]=1;b[object+0x3290]=0;b[object+0x3222]=4;b[object+0x3223]=8;
        Write(b.data(),object+0x3224,10.f);Write(b.data(),object+0x3228,10.f);
        for(unsigned i=0;i<source.count;i++){
            const auto m=object+0xc00+i*64;const auto& p=source.points[i];
            Write(b.data(),object+i*24,p.position);Write(b.data(),m,p.pinned?Vec{float(i)*10,0,0}:Vec{});
            b[m+0x3c]=p.pinned;
            for(unsigned e=0;e<6;e++){Write(b.data(),m+0x30+e*2,p.neighbor[e]);Write(b.data(),m+0x18+e*4,p.length[e]);}
        }
    };
    setup();auto snapshot=b;X86Context ctx{};ctx.ecx=object;ctx.esp=0x18000;ctx.ebx=42;ctx.esi=17;ctx.edi=19;ctx.ebp=23;
    enhanced=false;sub_00051A60(ctx,b.data());
    Check(original_calls==1&&normal_calls==0&&b==snapshot&&ctx.esp==0x18004&&ctx.eax==123,"off must delegate unchanged to original solver");
    ctx.ecx=object;ctx.esp=0x18000;enhanced=true;sub_00051A60(ctx,b.data());
    Check(original_calls==1&&normal_calls==1&&ctx.esp==0x18004&&ctx.eax==456,"enhanced must rebuild original normals with matching stack cleanup");
    Check(ctx.ebx==42&&ctx.esi==17&&ctx.edi==19&&ctx.ebp==23,"callee-saved guest registers must survive the adapter");
    Check(Read<Vec>(b.data(),object+0x3230).x==0&&b[object+0x3221]==4,"impulse must be consumed and pinned count retained");
    for(unsigned a=0;a<b.size();a++){
        bool owned=false;
        for(unsigned i=0;i<32;i++){
            const auto m=object+0xc00+i*64;
            if(i>=4&&((a>=object+i*24&&a<object+i*24+12)||(a>=m&&a<m+12)))owned=true;
            if(a==m+0x3d)owned=true;
        }
        if(a==object+0x3221||(a>=object+0x3230&&a<object+0x323c))owned=true;
        Check(owned||b[a]==snapshot[a],"UVs, pinned local coordinates, topology, rest lengths and unrelated bytes must remain unchanged");
    }
    setup();Write(b.data(),object+0xc30,uint16_t(0x1234));snapshot=b;ctx.ecx=object;ctx.esp=0x18000;
    sub_00051A60(ctx,b.data());Check(original_calls==2&&normal_calls==1&&b==snapshot,"unsupported topology must fall back without partial writes");
    const uint32_t model=0x20000,skeleton=0x24000;
    b[model+0x1890]=3;Write(b.data(),model+0x938,skeleton);
    for(unsigned i=0;i<16;i++)Write(b.data(),model+0x10+i*4,0.f);
    Write(b.data(),model+0x14,-1.f);Write(b.data(),model+0x20,1.f);Write(b.data(),model+0x38,1.f);
    Write(b.data(),model+0x40,500.f); // owner translation is excluded from cloth space
    for(unsigned i=0;i<3;i++){
        char name[8];std::snprintf(name,sizeof(name),"%02u-jnt",i);
        std::memcpy(b.data()+skeleton+i*0xe0,name,7);
        Write(b.data(),skeleton+i*0xe0+0xcc,int8_t(i?i-1:-1));
        Write(b.data(),model+0x970+i*0x70,Vec{0,float(i)*20,0});
    }
    auto body=MakeBody(b.data(),model);
    Check(body.count==2&&body.capsules[0].a.x==0&&body.capsules[0].b.x==20&&body.capsules[0].b.y==0,"body capsules must follow hierarchy and owner rotation without owner translation");
    std::memcpy(b.data()+skeleton+0xe0,"EX-00\0",6);
    Check(MakeBody(b.data(),model).count==0,"weapon and attachment joints must never become body blockers");
    std::memcpy(b.data()+skeleton+0xe0,"01-jnt\0",7);
    auto reset_point=[&]{
        setup();Write(b.data(),object+0x3200,1u);b[object+0x3220]=0;
        b[object+0xc3c]=0;Write(b.data(),object,Vec{10,-10,0});Write(b.data(),object+0xc00,Vec{0,40,0});
        for(unsigned e=0;e<6;e++)Write(b.data(),object+0xc30+e*2,uint16_t(0xffff));
        Write(b.data(),object+0x3208,1.f);Write(b.data(),object+0x320c,0.f);Write(b.data(),object+0x3230,Vec{});
    };
    reset_point();body_collision=true;ctx.ecx=model;ctx.esp=0x18000;sub_000490B0(ctx,b.data());
    Check(Read<Vec>(b.data(),object).y<0,"owner wrapper must route animated blockers into its cloth solver");
    reset_point();ctx.ecx=object;ctx.esp=0x18000;sub_00051A60(ctx,b.data());
    Check(Read<Vec>(b.data(),object).y>0,"body blockers must not leak into stage cloth or another owner");
    reset_point();body_collision=false;ctx.ecx=model;ctx.esp=0x18000;sub_000490B0(ctx,b.data());
    Check(Read<Vec>(b.data(),object).y>0,"body collision toggle must remove blockers");
    setup();auto rest_shape=CaptureRest(b.data(),object);Check(rest_shape.count>0,"retail-layout panels must capture authored bends");
    ctx.ecx=object;ctx.esp=0x18000;sub_00051250(ctx,b.data());Check(ctx.esp==0x1800c,"rest capture must preserve original loader cleanup");
    const uint32_t cloned=0x14000,destination_model=0x26000;
    b[model+0x94]=b[destination_model+0x94]=1;
    Write(b.data(),model+0x98,object);Write(b.data(),destination_model+0x98,cloned);
    Write(b.data(),0x18004,destination_model);Write(b.data(),0x18008,model);ctx.esp=0x18000;
    sub_0004FDD0(ctx,b.data());Check(ctx.esp==0x18004,"rest clone hook must preserve original cdecl cleanup");
    Check(CaptureRest(b.data(),cloned).signature==rest_shape.signature,"clones must retain original topology");
    const auto uv=Read<uint32_t>(b.data(),cloned+12);ctx.ecx=cloned;ctx.esp=0x18000;bend_resistance=1;
    auto deformed=Read<Vec>(b.data(),cloned+31*24);deformed.z+=3;Write(b.data(),cloned+31*24,deformed);
    Frame expected;Check(ReadFrame(b.data(),cloned,expected),"cloned frame must decode");expected.rest=&rest_shape;
    Check(Step(expected),"authored-shape reference must solve");
    sub_00051A60(ctx,b.data());Check(ctx.esp==0x18004&&Read<uint32_t>(b.data(),cloned+12)==uv,"cloned solver must retain render UVs and guest cleanup");
    Check(Length(Read<Vec>(b.data(),cloned+31*24)-expected.points[31].position)<.0001f,"clone hook must carry the authored rest shape rather than capturing the animated deformation");
    puts("PASS: authored bending, gap-blocking edges, self-collision, neighbor exclusions, rest cloning, heavier response, body collision, data ownership and guest ABI");
}catch(const std::exception& e){puts(e.what());return 1;}}
