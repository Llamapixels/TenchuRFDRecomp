#include "game_cheats.h"
#include <vector>
#include <stdexcept>
#include <cstdio>
int main(){try{
    using namespace tenchu::cheats;
    std::vector<uint8_t> memory(0x04000000);auto* base=memory.data();
    auto write=[&](uint32_t at,auto value){std::memcpy(base+at,&value,4);};
    auto read=[&](uint32_t at){float value;std::memcpy(&value,base+at,4);return value;};
    auto require=[](bool value){if(!value)throw std::runtime_error("health policy failed");};
    const uint32_t player=0x100000,enemy=0x200000,second=0x300000;
    write(0x1BAB110,player);write(0x1BAB114,second);
    for(auto actor:{player,enemy,second}){write(actor+0x50,100.f);write(actor+0x48,-20.f);}
    base[second+0x393]=1;
    ProtectHealth(base,player);require(read(player+0x48)==-20.f);
    god_mode=true;ProtectHealth(base,player);ProtectHealth(base,enemy);ProtectHealth(base,second);
    require(read(player+0x48)==100.f&&read(player+0x54)==100.f);
    require(read(second+0x48)==100.f&&read(0x8E17B8)==100.f);
    require(read(enemy+0x48)==-20.f); // Enemies remain killable.
    write(player+0x50,0.f);write(player+0x48,0.f);ProtectHealth(base,player);require(read(player+0x48)==0.f);
    write(player+0x50,NAN);ProtectHealth(base,player);require(read(player+0x48)==0.f);
    ProtectHealth(base,0);ProtectHealth(base,0xFFFFFFF0);
    god_mode=false;write(second+0x48,12.f);ProtectHealth(base,second);require(read(second+0x48)==12.f);
    puts("PASS: lethal health loss, two players, enemy exclusion, disabled mode, invalid health and pointers");
}catch(const std::exception& e){puts(e.what());return 1;}}
