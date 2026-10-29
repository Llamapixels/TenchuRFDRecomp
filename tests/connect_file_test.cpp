#include "port/connect_file.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
namespace con = tenchu::con;

static std::vector<uint8_t> Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Put(std::vector<uint8_t>& b,uint32_t v){uint8_t t[4];std::memcpy(t,&v,4);b.insert(b.end(),t,t+4);}

// a two-entry archive laid out as the game's are: table in the first sector, entries sector aligned
static std::vector<uint8_t> Synthetic(){
    const char header[24]="ConnectFile Version 6.0";
    uint32_t key;std::memcpy(&key,header,4);key=~key;
    std::vector<std::vector<uint8_t>> data={std::vector<uint8_t>(2048,'A'),std::vector<uint8_t>(4096,'B')};
    std::vector<uint8_t> table;Put(table,2048);Put(table,2);Put(table,2048);Put(table,4096);Put(table,2048);Put(table,4096);
    for(const char* n:{"English\\TEX01.dds","Model\\ninja.msh"}){uint8_t name[44]{};std::memcpy(name,n,std::strlen(n));table.insert(table.end(),name,name+44);}
    table.resize(2048-24,0);con::Crypt(table.data(),table.size(),key);
    std::vector<uint8_t> out(header,header+24);out.insert(out.end(),table.begin(),table.end());
    for(auto& d:data){auto e=d;con::Crypt(e.data(),e.size(),~uint32_t(e.size()));out.insert(out.end(),e.begin(),e.end());}
    return out;
}

static void RoundTrip(const fs::path& path,const fs::path& work){
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    con::Archive a;std::string error;
    check(con::Parse(con::FileSource(path),fs::file_size(path),a,error),error.c_str());
    con::Write(con::FileSource(path),a,{},work/"rebuilt.con",error);
    check(Bytes(path)==Bytes(work/"rebuilt.con"),"an unchanged rebuild must be byte-identical");
    std::printf("PASS: %s, %zu entries rebuilt byte-identical\n",path.filename().string().c_str(),a.entries.size());
}

int main(int argc,char** argv){try{
    auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const fs::path work=fs::current_path()/"test_artifacts"/"connect_file_test";
    fs::create_directories(work);
    {std::ofstream f(work/"synthetic.con",std::ios::binary);auto b=Synthetic();f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));}
    con::Archive a;std::string error;
    check(con::Parse(con::FileSource(work/"synthetic.con"),fs::file_size(work/"synthetic.con"),a,error),"synthetic archive must parse");
    check(a.entries.size()==2&&a.entries[1].name=="Model\\ninja.msh"&&a.entries[1].size==4096,"entries must be listed");
    std::vector<uint8_t> data;check(con::ReadEntry(con::FileSource(work/"synthetic.con"),a.entries[1],data)&&data==std::vector<uint8_t>(4096,'B'),"entries must decrypt");
    check(con::Key("Model\\ninja.msh")=="MODEL/NINJA.MSH"&&con::SafePath("Model\\ninja.msh")==fs::path("Model")/"ninja.msh","keys and paths");
    check(con::SafePath("..\\x").empty()&&con::SafePath("C:\\x").empty()&&con::SafePath("\\x").empty(),"unsafe names rejected");
    RoundTrip(work/"synthetic.con",work);
    // replace one entry with a bigger file: it is padded to whole sectors, the other entry is kept
    std::vector<uint8_t> bigger(5000,'C');
    check(con::Write(con::FileSource(work/"synthetic.con"),a,{{"MODEL/NINJA.MSH",bigger}},work/"modded.con",error),error.c_str());
    con::Archive m;check(con::Parse(con::FileSource(work/"modded.con"),fs::file_size(work/"modded.con"),m,error),"modded archive must parse");
    check(con::ReadEntry(con::FileSource(work/"modded.con"),m.entries[1],data)&&data.size()==6144&&std::equal(bigger.begin(),bigger.end(),data.begin())&&data[5000]==0,"replaced entry must hold the new bytes");
    check(con::ReadEntry(con::FileSource(work/"modded.con"),m.entries[0],data)&&data==std::vector<uint8_t>(2048,'A'),"untouched entries must survive");
    for(int i=1;i<argc;++i)RoundTrip(argv[i],work);  // real game archives, when given
    std::puts("PASS: ConnectFile parse, decrypt, safe paths, entry replacement and exact rebuild");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
