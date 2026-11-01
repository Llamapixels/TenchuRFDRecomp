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

    // Player-relative patches keep their published addresses, but follow the
    // fixed player pointer instead of writing into a previous allocation.
    auto dynamic=patches::Parse("Pointer=01BAB110,00EC7D10,000007D8\n[Follow player]\nCheck,00EC845C,value,0021\nAddress,100EC845E,value,03\n","dynamic.txt");
    check(dynamic.size()==1&&dynamic[0].pointer==0x1BAB110&&dynamic[0].checks.size()==1&&dynamic[0].problems.empty(),"pointer metadata and item guards must parse");
    Memory relocated;relocated.bytes.resize(32u<<20);relocated.limit=32u<<20;
    auto moving_map=[&](uint32_t va,uint32_t n){return relocated.Map(va,n);};
    patches::Set moving;moving.Load(dynamic,moving_map);moving.SetEnabled(dynamic[0].Id(),true);
    const uint32_t first_player=0xEC7D10,second_player=0xEC9D10,third_player=0xF10000;
    relocated.W32(0x1BAB110,first_player);relocated.W32(first_player+0x74C,0x18020021);
    moving.Apply(moving_map);check(relocated.bytes[first_player+0x74E]==3,"published original address must still work");
    relocated.W32(0x1BAB110,second_player);relocated.W32(first_player+0x74C,0x55667788);relocated.W32(second_player+0x74C,0x18010021);
    moving.Apply(moving_map);
    check(relocated.bytes[second_player+0x74E]==3&&relocated.R32(first_player+0x74C)==0x55667788,"0x2000 shift must follow player without restoring into old allocation");
    relocated.W32(0x1BAB110,third_player);relocated.W32(third_player+0x74C,0x18040021);
    moving.Apply(moving_map);check(relocated.bytes[third_player+0x74E]==3,"arbitrary later allocation must work without editing addresses");
    moving.SetEnabled(dynamic[0].Id(),false);moving.Apply(moving_map);
    check(relocated.bytes[third_player+0x74E]==4,"disable restores this allocation's original ammo");
    moving.SetEnabled(dynamic[0].Id(),true);moving.Apply(moving_map);
    relocated.W32(third_player+0x74C,0x1805000B);moving.Apply(moving_map);
    check(relocated.R32(third_player+0x74C)==0x1805000B&&moving.StateOf(dynamic[0].Id())==patches::State::Waiting,"different item must never be overwritten by ammo freeze");
    moving.SetEnabled(dynamic[0].Id(),false);moving.Apply(moving_map);
    check(relocated.R32(third_player+0x74C)==0x1805000B,"disable must not restore bow ammo onto a replacement item");
    relocated.W32(0x1BAB110,0);moving.SetEnabled(dynamic[0].Id(),true);auto snapshot=relocated.bytes;moving.Apply(moving_map);
    check(relocated.bytes==snapshot&&moving.StateOf(dynamic[0].Id())==patches::State::Waiting,"menus/loading with no player must leave all memory unchanged");
    auto outside=dynamic;outside[0].writes.push_back({0x8E40A0,0x63,4});moving.Load(outside,moving_map);
    relocated.W32(0x1BAB110,second_player);relocated.W32(second_player+0x74C,0x18010021);snapshot=relocated.bytes;moving.Apply(moving_map);
    check(relocated.bytes==snapshot,"out-of-object writes must reject complete pointer patch update");

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
    auto bow=patches::Parse(read("Bow - player pointer.txt"),"Bow-current-session.txt");
    check(bow.size()==2&&bow[0].problems.empty()&&bow[1].problems.empty()&&bow[1].checks.size()==5,"shared bow file must parse both guarded entries");
    patches::Set bow_set;bow_set.Load(bow,moving_map);
    for(const auto& patch:bow)bow_set.SetEnabled(patch.Id(),true);
    relocated.W32(0x1BAB110,second_player);relocated.W32(second_player+0x1B,0x59414C50);
    relocated.W32(second_player+0x730,1);relocated.W32(second_player+0x748,0x18010000);relocated.W32(second_player+0x74C,0x1000FFFF);
    relocated.W32(first_player+0x74C,0x11223344);bow_set.Apply(moving_map);bow_set.Apply(moving_map);
    check(relocated.R32(second_player+0x74C)==0x18030021&&relocated.bytes[second_player+0x730]==2&&relocated.bytes[second_player+0x731]==1,"bow patch must equip and refill at current player");
    check(relocated.R32(first_player+0x74C)==0x11223344,"bow patch must never write its published historical heap address");
    relocated.bytes[second_player+0x74E]=2;bow_set.Apply(moving_map);
    check(relocated.bytes[second_player+0x74E]==3,"live bow freeze must replace spent ammo");
    relocated.W32(second_player+0x74C,0x1805000B);relocated.bytes[second_player+0x730]=3;snapshot=relocated.bytes;
    bow_set.Apply(moving_map);check(relocated.bytes==snapshot,"bow patch must preserve different items and larger inventories");

    // Independent switches own only their flag bits, including when the game
    // changes another bit between enabling and disabling them.
    auto flags=patches::Parse(
        "[Power]\nDataAddress,00018000,mask,00100000,00100000,4 bytes\n"
        "[Capacity]\nDataAddress,00018000,mask,00001000,00001000,4 bytes\n","flags.txt");
    check(flags.size()==2&&flags[0].problems.empty()&&flags[1].problems.empty(),"masked data lines parse");
    patches::Set switches;switches.Load(flags,map);mem.W32(0x18000,0x40);
    for(const auto& p:flags)switches.SetEnabled(p.Id(),true);
    switches.Apply(map);mem.W32(0x18000,mem.R32(0x18000)|0x80);
    switches.SetEnabled(flags[0].Id(),false);switches.Apply(map);
    check(mem.R32(0x18000)==0x10C0,"turning power off preserves capacity and unrelated changed flags");
    switches.SetEnabled(flags[1].Id(),false);switches.Apply(map);
    check(mem.R32(0x18000)==0xC0,"turning capacity off restores only its owned bit");

    // A one-shot waits for menu closure, applies once, cannot overflow its
    // field, and never undoes a granted resource on disable or Reload.
    auto action=patches::Parse("[One kanji]\nMode=Once\nCheck,00018010,value,0003\n"
        "DataAddress,00018014,add,0004,0024,2 bytes\n","action.txt");
    patches::Set once;once.Load(action,map);mem.W32(0x18010,1);mem.W32(0x18014,0xAABB0022);
    once.SetEnabled(action[0].Id(),true);once.Apply(map,true);once.Apply(map,true);
    check(mem.R32(0x18014)==0xAABB0022&&once.Enabled(action[0].Id())&&once.StateOf(action[0].Id())==patches::State::Queued,"one-shot stays checked and never writes while menu is open");
    mem.W32(0x18010,3);once.Apply(map);
    check(mem.R32(0x18014)==0xAABB0024&&!once.Enabled(action[0].Id())&&once.StateOf(action[0].Id())==patches::State::Applied,"one-shot saturates, preserves adjacent data, and disarms");
    mem.W32(0x18014,0xAABB0004);once.Apply(map);once.Load(action,map);once.Apply(map);
    check(mem.R32(0x18014)==0xAABB0004,"one-shot does not repeat or restore after reload");
    once.SetEnabled(action[0].Id(),true);once.Apply(map);
    check(mem.R32(0x18014)==0xAABB0008,"Apply can explicitly repeat an action");
    auto bad=patches::Parse("[Reject]\nMode=Once\nDataAddress,00018014,value,AB\nDataAddress,00018018,mask,10,01,byte\n","bad-data.txt");
    once.Load(bad,map);once.SetEnabled(bad[0].Id(),true);auto before=mem.bytes;once.Apply(map);
    check(!bad[0].problems.empty()&&before==mem.bytes,"bad masked line rejects all writes in an action");

    // Health follows a relocated player and reads that player's own maximum.
    auto heal=patches::Parse("[Health]\nPointer=01BAB110,00EC7D10,000007D8\nRestore=No\n"
        "GlobalCheck,008E38BC,value,0003\nCheck,00EC7D2B,value,59414C50\n"
        "DataAddress,00EC7D58,copy,00EC7D60,4 bytes\nDataAddress,00EC7D64,copy,00EC7D60,4 bytes\n","heal.txt");
    check(heal.size()==1&&heal[0].problems.empty(),"pointer copies and global screen guard parse");
    patches::Set healing;healing.Load(heal,moving_map);healing.SetEnabled(heal[0].Id(),true);
    relocated.W32(0x1BAB110,third_player);relocated.W32(0x8E38BC,3);relocated.W32(third_player+0x1B,0x59414C50);
    relocated.W32(third_player+0x50,0x42C80000);relocated.W32(third_player+0x48,0x41200000);
    healing.Apply(moving_map);
    check(relocated.R32(third_player+0x48)==0x42C80000&&relocated.R32(third_player+0x54)==0x42C80000,"health copies current object's maximum, not a guessed fixed value");
    healing.SetEnabled(heal[0].Id(),false);healing.Apply(moving_map);
    check(relocated.R32(third_player+0x48)==0x42C80000,"disabling health refill does not put old damage back");
    healing.SetEnabled(heal[0].Id(),true);relocated.W32(0x8E38BC,1);snapshot=relocated.bytes;healing.Apply(moving_map);
    check(relocated.bytes==snapshot,"global screen guard still works inside a pointer patch");
    auto invalid_copy=heal;invalid_copy[0].writes[1].value=0x8E40A0;healing.Load(invalid_copy,moving_map);
    relocated.W32(0x8E38BC,3);snapshot=relocated.bytes;healing.Apply(moving_map);
    check(relocated.bytes==snapshot,"out-of-object copy source blocks complete update");

    // Validate the actual generated collection, including the scene and item
    // guards that keep menu actions out of gameplay and vice versa.
    std::vector<patches::Patch> collection;
    for(const char* file:{"RFD - original cheat actions.txt","RFD - sustained cheats.txt","RFD - player effects.txt","RFD - reward items.txt"}){
        auto entries=patches::Parse(read(file),file);
        check(!entries.empty(),"generated cheat file must contain patches");
        for(const auto& p:entries)check(p.problems.empty()&&!p.writes.empty()&&p.strict&&!patches::InCode(p,code),"every shipped cheat must parse cleanly and write data only");
        collection.insert(collection.end(),entries.begin(),entries.end());
    }
    check(collection.size()==68,"all original actions, sustained variants, rewards and player-two variants are shipped");
    auto find_patch=[&](const char* name)->patches::Patch{
        for(const auto& p:collection)if(p.name==name)return p;
        throw std::runtime_error(std::string("missing cheat: ")+name);
    };
    Memory game;game.bytes.resize(32u<<20);game.limit=32u<<20;
    auto game_map=[&](uint32_t va,uint32_t n){return game.Map(va,n);};
    patches::Set actual;
    auto bonus=find_patch("Increase score by 100 - original");
    actual.Load({bonus},game_map);actual.SetEnabled(bonus.Id(),true);game.W32(0x8E38BC,8);game.W32(0x8E40AE,0xABCD0002);
    actual.Apply(game_map,true);actual.Apply(game_map);
    check(game.R32(0x8E40AE)==0xABCD0002&&!actual.Enabled(bonus.Id())&&actual.StateOf(bonus.Id())==patches::State::Skipped,"wrong-screen action unticks when menu closes without firing");
    game.W32(0x8E38BC,3);actual.Apply(game_map);
    check(game.R32(0x8E40AE)==0xABCD0002,"failed one-shot cannot unexpectedly fire when its screen changes later");
    actual.SetEnabled(bonus.Id(),true);actual.Apply(game_map,true);actual.Apply(game_map,true);
    check(game.R32(0x8E40AE)==0xABCD0002&&actual.Enabled(bonus.Id()),"ticked action remains queued across menu frames");
    actual.Apply(game_map);actual.Apply(game_map);
    check(game.R32(0x8E40AE)==0xABCD0003,"actual score action adds exactly one 100-point unit and preserves adjacent fields");
    actual.SetEnabled(bonus.Id(),true);actual.Apply(game_map,true);actual.SetEnabled(bonus.Id(),false);actual.Apply(game_map);
    check(game.R32(0x8E40AE)==0xABCD0003,"unticking a queued one-shot cancels it before closing F6");
    auto power=find_patch("Toggle attack power x5 - P1");actual.Load({power},game_map);game.W32(0x1BAA8C0,0x1080);
    actual.SetEnabled(power.Id(),true);actual.Apply(game_map);actual.Apply(game_map);
    check(game.R32(0x1BAA8C0)==0x101080,"original power toggle runs once and preserves other flags");
    actual.SetEnabled(power.Id(),true);actual.Apply(game_map);check(game.R32(0x1BAA8C0)==0x1080,"Apply again reverses the original power toggle");
    auto held_power=find_patch("Attack power x5 until unchecked - P1");actual.Load({held_power},game_map);
    actual.SetEnabled(held_power.Id(),true);actual.Apply(game_map);game.W32(0x8E38BC,1);
    actual.SetEnabled(held_power.Id(),false);actual.Apply(game_map);
    check(game.R32(0x1BAA8C0)==0x1080,"static cheat switch can be disabled after leaving its activation screen");
    game.W32(0x8E38BC,3);
    auto invisible=find_patch("Stay invisible until unchecked - P1");actual.Load({invisible},game_map);
    game.W32(0x1BAB110,second_player);game.W32(second_player+0x1B,0x59414C50);game.bytes[second_player+0x393]=0;
    game.W32(second_player+0xEC,0x400000);game.bytes[second_player+0x392]=0x44;
    game.W32(second_player+0x38C,0xBBBBFFFF);game.W32(first_player+0xEC,0x11223344);
    actual.SetEnabled(invisible.Id(),true);actual.Apply(game_map);
    check(game.R32(second_player+0xEC)==0x420000&&game.bytes[second_player+0x392]==0x4C&&game.R32(second_player+0x38C)==0xBBBB0384,"invisibility follows shifted player, holds original timer/status and preserves neighboring data");
    game.W32(second_player+0xEC,game.R32(second_player+0xEC)|0x100);game.bytes[second_player+0x392]|=0x10;
    actual.SetEnabled(invisible.Id(),false);actual.Apply(game_map);
    check(game.R32(second_player+0xEC)==0x400100&&game.bytes[second_player+0x392]==0x54&&game.R32(second_player+0x38C)==0xBBBBFFFF,"unchecking invisibility removes its owned bits while retaining other status changes");
    check(game.R32(first_player+0xEC)==0x11223344,"invisibility never touches the historical player allocation");
    auto refill=find_patch("Restore health - P1");actual.Load({refill},game_map);actual.SetEnabled(refill.Id(),true);
    game.W32(second_player+0x50,0x42C80000);game.W32(second_player+0x48,0x41200000);game.W32(second_player+0x54,0x41200000);
    game.W32(0x8E17AC,0x41200000);game.W32(0x8E17B4,0x41200000);game.W32(0x8E17B0,0x42960000);
    actual.Apply(game_map);
    check(game.R32(second_player+0x48)==0x42C80000&&game.R32(second_player+0x54)==0x42C80000&&game.R32(0x8E17AC)==0x42C80000&&game.R32(0x8E17B4)==0x42C80000,"actual health refill updates actor and fixed HUD mirrors from this player's maximum");
    check(game.R32(0x8E17B0)==0x42960000,"P1 refill leaves P2 health mirror alone");
    auto disguise=find_patch("Keep Chameleon spell active - P1");actual.Load({disguise},game_map);actual.SetEnabled(disguise.Id(),true);
    auto game_before=game.bytes;actual.Apply(game_map);check(game.bytes==game_before,"disguise hold cannot manufacture an uninitialized disguise");
    game.bytes[second_player+0x392]|=0x20;actual.Apply(game_map);
    check((game.R32(second_player+0x38A)&0xFFFF)==600,"disguise hold starts once the actual spell initialized its flag");
    auto unlock=find_patch("Unlock Invisibility Spell");actual.Load({unlock},game_map);actual.SetEnabled(unlock.Id(),true);game.W32(0x8E38BC,8);
    game.W32(0x1BD02A0+17*4,0x0000002C);actual.Apply(game_map);actual.Apply(game_map);
    check(game.R32(0x1BD02A0+17*4)==0x0001002E,"reward unlock preserves item flags and grants one stored item exactly once");
    auto stock=find_patch("Keep Invisibility Spell stock full");actual.Load({stock},game_map);actual.SetEnabled(stock.Id(),true);actual.Apply(game_map);
    check((game.R32(0x1BD02A0+17*4)>>16)==99,"stored invisibility stock uses its original maximum");
    auto missions=find_patch("Unlock all missions - original");actual.Load({missions},game_map);actual.SetEnabled(missions.Id(),true);
    game.W32(0x8E38BC,9);game.W32(0x1BA2C50,0x05050501);game.W32(0x1BAA8C0,0x1080);actual.Apply(game_map);
    check(game.R32(0x1BAA8C0)==0x5080&&game.R32(0x1BA2C50)==0x05050501,"mission action queues original handler without overwriting earned ranks or other flags");

    // Pending Apply actions must not be saved and replayed at next startup.
    std::filesystem::create_directories(patches::Folder());
    {std::ofstream file(patches::Folder()/"one-shot-save-test.txt");file<<"[Action]\nMode=Once\nDataAddress,00018014,add,01,FF,byte\n";}
    patches::Rescan();patches::SetEnabled("one-shot-save-test.txt|Action",true);
    toml::table action_settings;patches::Save(&action_settings);
    auto* saved_actions=action_settings["memory_patches"]["enabled"].as_array();
    check(saved_actions&&saved_actions->empty(),"pending one-shot actions are not persisted to settings");

    // Settings
    toml::table t;t.insert_or_assign("memory_patches",toml::table{{"enabled",toml::array{"discord.txt|Always gets grandmaster rank"}}});
    patches::Load(&t);
    toml::table back;patches::Save(&back);
    auto* on=back["memory_patches"]["enabled"].as_array();
    check(on&&on->size()==1&&(*on)[0].value<std::string>()=="discord.txt|Always gets grandmaster rank","switches save and load");

    std::puts("PASS: posted patches read, Cheat Engine addresses, sizes and floats, bad lines reported, code sections flagged, freeze and restore, waiting for memory, saved switches");
}catch(const std::exception& e){std::puts(e.what());return 1;}}
