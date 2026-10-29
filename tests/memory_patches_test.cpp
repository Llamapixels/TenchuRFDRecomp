#include "port/memory_patches.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include <toml++/toml.hpp>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <vector>

std::filesystem::path app::ExeDir(){auto p=std::filesystem::current_path()/"test_artifacts"/"memory_patches_test";std::filesystem::create_directories(p);return p;}
namespace logx {void Write(Level,const char*,...){} }
uint8_t* kernel::g_base=nullptr;

namespace patches=tenchu::patches;

// two posts as players shared them, with the chat around them
const char* kPosts=
    "enchu: Return From Darkness (USA)\r\n\r\n"
    "[Always gets grandmaster rank]\r\n"
    "Author=me\r\n"
    "Address,1008E40A0,value,00000063\r\n\r\n"
    "Cheat Engine required \r\n"
    "Fallen Autumn \xE2\x80\x94 3:15 AM\r\n"
    "How to use this cheat patch? First you need to run the game from this link created by a holy cat.\r\n"
    "Tenchu: Return From Darkness (USA)\r\n\r\n"
    "[Ayame's alternate crouch attack (R1+Square)]\r\n"
    "Author=myself\r\n"
    "Address,1004D0520,value,002599F0\r\n\r\n"
    "Cheat Engine required\r\n\r\n"
    "It replaces Ayame's crouch attack with her 360\xC2\xB0 holy kick\r\n";

struct Memory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(16u<<20,0);
    uint32_t limit=16u<<20;  // addresses past this are "not set up yet"
    uint8_t* Map(uint32_t va,uint32_t n){return uint64_t(va)+n<=limit?&bytes[va]:nullptr;}
    uint32_t R32(uint32_t va){uint32_t v;std::memcpy(&v,&bytes[va],4);return v;}
    void W32(uint32_t va,uint32_t v){std::memcpy(&bytes[va],&v,4);}
};

int main(){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};

    // Reading posts
    auto list=patches::Parse(kPosts,"discord.txt");
    check(list.size()==2,"two patches in the posts");
    check(list[0].name=="Always gets grandmaster rank"&&list[0].author=="me"&&list[0].file=="discord.txt","name, author and file");
    check(list[0].writes.size()==1&&list[0].writes[0].va==0x008E40A0&&list[0].writes[0].value==0x63&&list[0].writes[0].size==4,"Cheat Engine host address to game address, 8 digits are 4 bytes");
    check(list[1].name=="Ayame's alternate crouch attack (R1+Square)"&&list[1].author=="myself","second patch");
    check(list[1].writes.size()==1&&list[1].writes[0].va==0x004D0520&&list[1].writes[0].value==0x002599F0,"second patch's write");
    check(list[0].problems.empty()&&list[1].problems.empty(),"the posts read cleanly");
    check(list[0].Id()=="discord.txt|Always gets grandmaster rank","ids name the file and patch");

    auto more=patches::Parse(
        "Author=Whole file\n"
        "[Sizes]\nNotes=one of each\n"
        "Address,0x00012340,value,7F\naddress , 00012344 , Value , FFFF\nAddress,00012348,value,1.5\nAddress,0001234C,value,01,2 bytes\n"
        "[Broken]\nAddress,500000000,value,01\nAddress,zz,value,1\nAddress,00012350,value,123456789\n"
        "[Nothing here]\nsome chat\n","more.txt");
    check(more.size()==2,"a patch with no address lines is dropped, one with only problems is kept");
    const auto& s=more[0];
    check(s.author=="Whole file"&&s.notes=="one of each","an Author line before the first patch counts for the file");
    check(s.writes.size()==4&&s.writes[0].size==1&&s.writes[1].size==2&&s.writes[1].value==0xFFFF,"2 digits are a byte, 4 are 2 bytes, spaces and case are tolerated");
    float f;std::memcpy(&f,&s.writes[2].value,4);check(s.writes[2].size==4&&f==1.5f,"a value with a point is a float");
    check(s.writes[3].size==2,"a type after the value sets the size");
    check(more[1].writes.empty()&&more[1].problems.size()==3,"bad lines are reported, not written");

    // Code ranges from the XBE header in game memory
    Memory mem;
    auto map=[&](uint32_t va,uint32_t n){return mem.Map(va,n);};
    std::memcpy(&mem.bytes[0x10000],"XBEH",4);mem.W32(0x10104,0x10000);mem.W32(0x1011C,2);mem.W32(0x10120,0x10200);
    auto section=[&](int i,uint32_t va,uint32_t size,uint32_t name_va,const char* name){
        uint32_t h=0x10200+i*56;mem.W32(h+4,va);mem.W32(h+8,size);mem.W32(h+20,name_va);std::memcpy(&mem.bytes[name_va],name,std::strlen(name)+1);};
    section(0,0x11000,0x32A47C,0x10400,".text");
    section(1,0x434BA0,0x17A4EFC,0x10410,".data");
    auto code=patches::CodeRanges([&](uint32_t va,uint32_t n)->const uint8_t*{return mem.Map(va,n);});
    check(code.size()==1&&code[0].name==".text"&&code[0].begin==0x11000,"only code sections count as code");
    check(!patches::InCode(list[0],code)&&!patches::InCode(list[1],code),"the posted patches change data");
    auto nop=patches::Parse("[Skip a check]\nAddress,100012000,value,90909090\n","nop.txt");
    check(patches::InCode(nop[0],code)&&patches::InCode(nop[0],code)->name==".text","a write into .text is flagged as code");

    // Writing: freeze while on, original back when off
    mem.W32(0x8E40A0,0x05);mem.W32(0x4D0520,0x00211110);
    patches::Set set;
    set.Load(list,map);
    set.Apply(map);
    check(mem.R32(0x8E40A0)==0x05&&set.StateOf(list[0].Id())==patches::State::Off,"patches start off");
    set.SetEnabled(list[0].Id(),true);set.SetEnabled(list[1].Id(),true);
    set.Apply(map);
    check(mem.R32(0x8E40A0)==0x63&&mem.R32(0x4D0520)==0x002599F0&&set.StateOf(list[0].Id())==patches::State::Active,"an enabled patch writes its values");
    mem.W32(0x8E40A0,0x02);set.Apply(map);
    check(mem.R32(0x8E40A0)==0x63,"values the game changes are written again");
    set.SetEnabled(list[1].Id(),false);set.Apply(map);
    check(mem.R32(0x4D0520)==0x00211110&&mem.R32(0x8E40A0)==0x63&&set.StateOf(list[1].Id())==patches::State::Off,"turning a patch off puts its original bytes back");
    auto byte=patches::Parse("[One byte]\nAddress,00012340,value,AB\n","b.txt");
    mem.W32(0x12340,0x11223344);set.Load(byte,map);set.SetEnabled(byte[0].Id(),true);set.Apply(map);
    check(mem.R32(0x12340)==0x112233AB,"a byte patch writes one byte");
    check(mem.R32(0x8E40A0)==0x05,"loading other patches restores the ones that went away");

    // Waiting for memory the game has not set up
    mem.limit=0x8E0000;
    set.Load(list,map);set.Apply(map);
    check(set.StateOf(list[0].Id())==patches::State::Waiting,"a patch outside set-up memory waits");
    mem.limit=16u<<20;set.Apply(map);
    check(set.StateOf(list[0].Id())==patches::State::Active&&mem.R32(0x8E40A0)==0x63,"it applies once the memory is there");
    check(set.Enabled(list[0].Id())&&!set.Enabled(list[1].Id()),"switches are kept by id across loads");

    // The examples shipped in memory_patches\examples read as they say
    auto read=[](const char* name){
        std::ifstream in(std::filesystem::path(TENCHU_SOURCE_DIR)/"docs"/"release"/"memory_patches"/"examples"/name,std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};};
    auto single=patches::Parse(read("Single patch.txt"),"Single patch.txt");
    check(single.size()==1&&single[0].name=="Always gets grandmaster rank"&&single[0].author=="me"&&!single[0].notes.empty()
          &&single[0].problems.empty()&&single[0].writes.size()==1&&single[0].writes[0].va==0x008E40A0,"the single patch example holds exactly its one patch");
    auto several=patches::Parse(read("Several patches.txt"),"Several patches.txt");
    check(several.size()==2&&several[0].problems.empty()&&several[1].problems.empty()&&several[1].author=="myself"
          &&several[1].writes.size()==1&&several[1].writes[0].va==0x004D0520,"the several patches example holds its two patches and nothing from its explanation");

    // Settings
    toml::table t;t.insert_or_assign("memory_patches",toml::table{{"enabled",toml::array{"discord.txt|Always gets grandmaster rank"}}});
    patches::Load(&t);
    toml::table back;patches::Save(&back);
    auto* on=back["memory_patches"]["enabled"].as_array();
    check(on&&on->size()==1&&(*on)[0].value<std::string>()=="discord.txt|Always gets grandmaster rank","switches save and load");

    std::puts("PASS: posted patches read, Cheat Engine addresses, sizes and floats, bad lines reported, code sections flagged, freeze and restore, waiting for memory, saved switches");
}catch(const std::exception& e){std::puts(e.what());return 1;}}
