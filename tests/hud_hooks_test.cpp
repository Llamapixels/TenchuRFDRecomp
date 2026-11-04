#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "cpu/x86_context.h"
#include "kernel/memory.h"
#include "hud_settings.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <cstdio>
void TenchuHudSpriteEnqueue(X86Context&,uint8_t*);
namespace {
uint32_t Read(uint8_t* b,uint32_t a){uint32_t v;std::memcpy(&v,b+a,4);return v;}
void Write(uint8_t* b,uint32_t a,uint32_t v){std::memcpy(b+a,&v,4);}
}
int main(){try{
    using namespace tenchu::hud;
    auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};
    auto release=[](uint8_t* b){if(b)VirtualFree(b,0,MEM_RELEASE);};
    std::unique_ptr<uint8_t,decltype(release)> allocation(static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS)),release);
    auto* b=allocation.get();check(b!=nullptr,"reserve guest address space");
    check(VirtualAlloc(b,0x04000000,MEM_COMMIT,PAGE_READWRITE)==b,"commit low memory");
    const uint32_t stack=mem::kStackBase+0x10000;
    check(VirtualAlloc(b+stack,0x2000,MEM_COMMIT,PAGE_READWRITE)==b+stack,"commit guest stack frames");
    X86Context c{};c.esp=stack;c.ebp=stack+0x100;c.ecx=queue;c.eax=2;c.cf=1;
    Write(b,c.ebp,stack+0x200);Write(b,c.ebp+4,0x002AA4E1);
    Write(b,stack+0x200,stack+0x300);Write(b,stack+0x204,0x002018D4);
    Write(b,stack+0x300,0);Write(b,stack+0x304,0x00201023);
    Write(b,0x8E38BC,3);Write(b,count_address,1);
    const uint32_t sprite=queue+stride;
    auto prepare=[&](uint32_t color){
        std::memset(b+sprite,0,stride);Write(b,sprite+0x70,4);Write(b,sprite+0x74,7);Write(b,sprite+0x7C,1);
        for(unsigned j=0;j<4;++j)Write(b,sprite+j*0x1C+0x10,color);
        Write(b,queue+0x10,0xFFAABBCC);
    };
    Reset();prepare(0x80808080);auto original=c;TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x80808080&&Read(b,sprite+0x7C)==1,"100 percent must preserve original alpha/blending");
    // Colors/callers from the real HUD: frame, item, health, number, dark-blue
    // kanji and its second copy. No callback wrappers in this fixture.
    const uint32_t colors[]={0x80808080,0x80808080,0x80F00000,0x80808080,0x204040C0,0x00000000};
    const uint32_t callers[]={0x00200AA5,0x0020260D,0x002028A4,0x002034F1,0x002018D4,0x00201ACB};
    for(unsigned i=0;i<6;++i){
        Write(b,stack+0x204,callers[i]);Write(b,stack+0x304,0x0020293E);
        for(unsigned percent:{0u,35u,100u}){
            shown=true;opacity_percent=percent;prepare(colors[i]);TenchuHudSpriteEnqueue(c,b);
            for(unsigned j=0;j<4;++j)check(Read(b,sprite+j*0x1C+0x10)==FadeColor(colors[i],percent),"native HUD creation must fade items/health/digits/both kanji copies");
            check(Read(b,queue+0x10)==0xFFAABBCC&&Read(b,count_address)==1,"prior sprites and pending count store must remain unchanged");
            check(std::memcmp(&c,&original,sizeof(c))==0,"sprite hook must preserve guest CPU state");
        }
    }
    shown=false;opacity_percent=65;prepare(0x80808080);TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x00808080&&opacity_percent==65,"hide must keep chosen slider opacity");
    // Actual missing painters, traced from texture binding and retail callsites:
    // SSgau -> 197E00 / 197B10, SEL -> 2C1A10 / 2C2470 / 2C4140,
    // ITEM_CUR -> update2C2950 and draws2C2F70 / 2C3A90. The update's
    // spinner call2C2F32 returns to2C2F37, in the gap the prior test missed.
    const uint32_t missing_callers[]={0x00197FB5,0x00197DE0,0x002C1E9D,0x002C27E6,0x002C2F37,0x002C301C,0x002C4101,0x002C4554};
    for(uint32_t caller:missing_callers){
        Write(b,stack+0x204,caller);Write(b,stack+0x304,0x002D3E60);
        for(unsigned percent:{0u,40u,100u}){
            shown=true;opacity_percent=percent;uint32_t color=caller<0x200000?0x204040C0:0x80808080;
            prepare(color);TenchuHudSpriteEnqueue(c,b);
            for(unsigned j=0;j<4;++j)check(Read(b,sprite+j*0x1C+0x10)==FadeColor(color,percent),
                "real SSgau/SEL/ITEM_CUR callers must obey hide and opacity");
        }
    }
    // Item selection menus use the same digit painter, but are a separate task.
    shown=false;Write(b,stack+0x204,0x002CD032);Write(b,stack+0x304,0x002B0AA6);prepare(0x80808080);
    TenchuHudSpriteEnqueue(c,b);check(Read(b,sprite+0x10)==0x80808080,"item-selection menu must stay visible");
    Write(b,stack+0x204,0x002018D4);
    prepare(0);Write(b,sprite+0x74,8);
    for(unsigned i=0;i<4;++i){Write(b,sprite+i*0x14+4,0x42480000);Write(b,sprite+i*0x14+0x10,0x80ABCDEF);}
    TenchuHudSpriteEnqueue(c,b);
    for(unsigned i=0;i<4;++i){
        check(Read(b,sprite+i*0x14+0x10)==0x00ABCDEF,"untextured HUD vertices must use their actual 0x14 color stride");
        check(Read(b,sprite+i*0x14+4)==0x42480000,"untextured HUD geometry must stay unchanged");
    }
    Write(b,stack+0x204,0x002034F1);Write(b,stack+0x304,0x002B0AA6);prepare(0x80808080);TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x80808080,"shared menu text must not be classified as HUD");
    Write(b,stack+0x204,0x002018D4);Write(b,0x8E38BC,1);prepare(0x80808080);TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x80808080,"title scenes must bypass fading");
    Write(b,0x8E38BC,3);c.ebp=c.esp-4;TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x80808080,"invalid guest frame must bypass fading");
    c=original;Write(b,count_address,capacity);TenchuHudSpriteEnqueue(c,b);
    check(Read(b,sprite+0x10)==0x80808080,"full queue must bypass fading");
    puts("PASS: native HUD sprite creation, high guest frames, items/health/digits/kanji, opacity, menu isolation, CPU/queue preservation and bounds");
}catch(const std::exception& e){puts(e.what());return 1;}}
