#include "port/mods.h"
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"
#include "port/connect_file.h"
#include <cstring>
#include <iterator>
#include <toml++/toml.hpp>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace mods = tenchu::mods;
void app::Save(){}
app::Settings& app::Get(){static app::Settings s;return s;}
fs::path app::ExeDir(){static fs::path p=fs::current_path()/"test_artifacts"/"mods_test";return p;}
namespace logx {void Write(Level,const char*,...){} }
const kernel::Disc* kernel::MountedDisc(){return nullptr;}
const kernel::Disc::Entry* kernel::Disc::Find(const std::vector<std::string>&) const{return nullptr;}
uint32_t kernel::Disc::Read(const Entry&,uint64_t,void*,uint32_t) const{return 0;}

static void Put(const fs::path& p,const char* text){fs::create_directories(p.parent_path());std::ofstream(p)<<text;}
static std::string Text(const std::string& path){std::ifstream in(path);return {std::istreambuf_iterator<char>(in),{}};}
static void Put32(std::vector<uint8_t>& b,uint32_t v){uint8_t t[4];std::memcpy(t,&v,4);b.insert(b.end(),t,t+4);}
// a two-entry ConnectFile 6 archive like the game's
// second/second_size: the ninja entry's bytes, as a mod's own archive would change them
static void Archive(const fs::path& path,char second='B',uint32_t second_size=2048){
    const char header[24]="ConnectFile Version 6.0";
    uint32_t key;std::memcpy(&key,header,4);key=~key;
    // data offset, count, then every entry's size, then every entry's offset
    std::vector<uint8_t> table;Put32(table,2048);Put32(table,2);Put32(table,2048);Put32(table,second_size);Put32(table,2048);Put32(table,4096);
    for(const char* n:{"English\\TEX01.dds","Model\\ninja.msh"}){uint8_t name[44]{};std::memcpy(name,n,std::strlen(n));table.insert(table.end(),name,name+44);}
    table.resize(2048-24,0);tenchu::con::Crypt(table.data(),table.size(),key);
    std::vector<uint8_t> out(header,header+24);out.insert(out.end(),table.begin(),table.end());
    for(auto [c,size]:{std::pair<char,uint32_t>{'A',2048u},{second,second_size}}){
        std::vector<uint8_t> e(size,uint8_t(c));tenchu::con::Crypt(e.data(),e.size(),~size);out.insert(out.end(),e.begin(),e.end());}
    fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(out.data()),std::streamsize(out.size()));
}
static std::string Entry(const std::string& archive,int index){
    tenchu::con::Archive a;std::string error;auto src=tenchu::con::FileSource(archive);
    if(!tenchu::con::Parse(src,fs::file_size(archive),a,error))return "unparsable";
    std::vector<uint8_t> data;tenchu::con::ReadEntry(src,a.entries[index],data);
    return std::string(data.begin(),data.end()).c_str();  // up to the padding
}

int main(){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::error_code ec;fs::remove_all(app::ExeDir(),ec);
    const fs::path root=mods::ModsDir();
    Put(root/"Alpha"/"Data"/"Stage1.k2v","alpha stage");
    Put(root/"Alpha"/"Data"/"Shared.bin","alpha shared");
    Put(root/"Alpha"/"mod.toml","title = \"Alpha fixes\"\nauthor = \"Someone\"\nversion = \"1.2\"\ndescription = \"Test mod\"\n");
    Put(root/"Alpha"/"README.txt","not a disc file");
    Put(root/"Beta"/"data"/"shared.bin","beta shared");
    Put(root/"Beta"/"default.xbe","never replaced");
    mods::Rescan();
    auto list=mods::List();
    check(list.size()==2&&list[0].name=="Alpha"&&list[0].title=="Alpha fixes"&&list[0].author=="Someone"&&list[0].version=="1.2","mods must be listed with mod.toml details");
    check(list[0].files==2&&list[1].files==1,"metadata files and the executable are not disc replacements");
    check(list[1].hidden==1,"a file replaced by a mod above must be reported");
    check(Text(mods::Override({"DATA","STAGE1.K2V"}))=="alpha stage","disc paths match in any case");
    check(Text(mods::Override({"data","shared.bin"}))=="alpha shared","the mod higher in the list wins");
    check(mods::Override({"default.xbe"}).empty()&&mods::Override({"data","missing.bin"}).empty(),"the executable and missing files stay on the disc");
    check(mods::Replaced()==2&&mods::Recent(5).size()==2,"replaced files must be counted and listed");
    mods::Move("Beta",-1);
    check(Text(mods::Override({"data","shared.bin"}))=="beta shared"&&mods::List()[0].name=="Beta","moving a mod up changes the winner");
    mods::SetEnabled("Beta",false);
    check(Text(mods::Override({"data","shared.bin"}))=="alpha shared","a disabled mod replaces nothing");
    toml::table saved;mods::Save(&saved);
    check(saved["mods"]["order"][0].value_or(std::string{})=="Beta"&&saved["mods"]["disabled"][0].value_or(std::string{})=="Beta","order and disabled mods must be saved");
    mods::Load(&saved);mods::Rescan();
    check(mods::List()[0].name=="Beta"&&!mods::List()[0].enabled,"saved order and state must be restored");
    std::string status;check(!mods::WriteDiscList(status)&&!status.empty(),"disc list needs a mounted disc");
    // one file inside an archive: a whole-file archive in one mod, a single entry from another
    Archive(root/"Gamma"/"Media"/"x.con");
    Put(root/"Delta"/"Media"/"x.con.files"/"Model"/"ninja.msh","new ninja");
    mods::Rescan();
    std::string built=mods::Override({"MEDIA","X.CON"});
    check(!built.empty()&&built!=(root/"Gamma"/"Media"/"x.con").string(),"an entry mod must build a merged archive");
    check(Entry(built,1)=="new ninja"&&Entry(built,0)==std::string(2048,'A'),"the merged archive must hold the mod's entry and keep the rest");
    check(mods::Override({"MEDIA","X.CON"})==built,"the merged archive is built once per session");
    mods::SetEnabled("Delta",false);
    check(mods::Override({"MEDIA","X.CON"})==(root/"Gamma"/"Media"/"x.con").string(),"without entry mods the whole-file mod is served");
    // edits made in extracted\ apply too: files newer than the unpack marker
    const fs::path extracted=app::ExeDir()/"extracted";
    Archive(extracted/"Media"/"y.con");
    Put(extracted/".complete","");Put(extracted/"Media"/"y.con.files"/".extracted","");
    Put(extracted/"Media"/"y.con.files"/"English"/"TEX01.dds",std::string(2048,'A').c_str());
    Put(extracted/"Media"/"y.con.files"/"Model"/"ninja.msh","edited ninja");
    fs::last_write_time(extracted/"Media"/"y.con.files"/"English"/"TEX01.dds",fs::last_write_time(extracted/"Media"/"y.con.files"/".extracted")-std::chrono::hours(1));
    mods::Rescan();
    std::string edited=mods::Override({"Media","y.con"});
    check(!edited.empty()&&Entry(edited,1)=="edited ninja"&&Entry(edited,0)==std::string(2048,'A'),"an edited file in extracted must reach the game");
    check(mods::Override({"Media","untouched.con"}).empty(),"archives without changes stay original");
    mods::Rescan();check(mods::Override({"Media","y.con"})==edited,"an unchanged merged archive is reused from the cache");
    // a whole-archive mod that changes an entry (a new outfit model): the untouched unpacked original must not
    // go back over it, while a real edit in extracted\ still applies on top
    Archive(extracted/"Media"/"z.con");
    Put(extracted/"Media"/"z.con.files"/".extracted","");
    Put(extracted/"Media"/"z.con.files"/"English"/"TEX01.dds",std::string(2048,'A').c_str());
    Put(extracted/"Media"/"z.con.files"/"Model"/"ninja.msh",std::string(2048,'B').c_str());
    for(const char* f:{"English/TEX01.dds","Model/ninja.msh"})
        fs::last_write_time(extracted/"Media"/"z.con.files"/f,fs::last_write_time(extracted/"Media"/"z.con.files"/".extracted")-std::chrono::hours(1));
    Archive(root/"Epsilon"/"Media"/"z.con",'C',4096);
    mods::Rescan();
    std::string outfit=mods::Override({"Media","z.con"});
    check(outfit==(root/"Epsilon"/"Media"/"z.con").string()&&Entry(outfit,1)==std::string(4096,'C'),"a whole-archive mod's changed entry must not be undone by the unpacked original");
    Put(extracted/"Media"/"z.con.files"/"English"/"TEX01.dds","edited texture");
    mods::Rescan();
    std::string both=mods::Override({"Media","z.con"});
    check(both!=outfit&&Entry(both,0)=="edited texture"&&Entry(both,1)==std::string(4096,'C'),"a real edit in extracted applies on top of a whole-archive mod");
    Put(extracted/"Media"/"z.con.files"/"Model"/"ninja.msh","edited ninja");
    mods::Rescan();
    std::string priority=mods::Override({"Media","z.con"});
    check(Entry(priority,0)=="edited texture"&&Entry(priority,1)==std::string(4096,'C'),"mods come before extracted: a file the mod changed keeps the mod's version");
    puts("PASS: mod discovery, mod.toml, priority, case-insensitive paths, executable protection, enable switches, saved order, archive entry mods, extracted edits and whole-archive mods over unpacked files");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
