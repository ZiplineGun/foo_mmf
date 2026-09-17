#include "../src/smaf_audio.hpp"
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <fstream>
static void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
static void be32(std::vector<uint8_t>& out,uint32_t n) {for(int s=24;s>=0;s-=8) out.push_back(uint8_t(n>>s));}
static void chunk(std::vector<uint8_t>& out,const char* id,std::vector<uint8_t> data) {out.insert(out.end(),id,id+4);be32(out,static_cast<uint32_t>(data.size()));out.insert(out.end(),data.begin(),data.end());}
static std::vector<uint8_t> wrap(const std::vector<uint8_t>& chunks) {
    std::vector<uint8_t> out={'M','M','M','D'};be32(out,static_cast<uint32_t>(chunks.size()+2));out.insert(out.end(),chunks.begin(),chunks.end());
    const auto crc=smaf::smafChecksum(out.data(),out.size());out.push_back(uint8_t(crc>>8));out.push_back(uint8_t(crc));return out;
}
int wmain(int argc,wchar_t** argv) {
    try {
        std::vector<uint8_t> audio,original;
        chunk(audio,"CNTI",{1,2,3,4,5});chunk(audio,"MTR\x06",{'G','T','R',0,0,0,0,0,1,2});chunk(audio,"OPDA",{'T','I',0,3,'a','b','c'});
        chunk(original,"GTR\0",{1,2,3});original.insert(original.end(),audio.begin(),audio.end());chunk(original,"GTR\x03",{});
        auto file=wrap(original),copy=file,expected=wrap(audio);
        require(smaf::audioOnlySmaf(file)==expected&&file==copy,"Graphics filter changed retained chunks or source");
        require(smaf::audioOnlySmaf(expected)==expected,"Graphics-free SMAF changed");
        require(smaf::audioOnlySmaf({'M','T','h','d',0,0,0,6,0,0})==std::vector<uint8_t>({'M','T','h','d',0,0,0,6,0,0}),"Non-SMAF input changed");
        for(size_t n=0;n<file.size();++n) {
            std::vector<uint8_t> truncated(file.begin(),file.begin()+n);
            require(smaf::audioOnlySmaf(truncated)==truncated,"Truncated input was rewritten");
        }
        auto malformed=file;malformed[12]=0xff;require(smaf::audioOnlySmaf(malformed)==malformed,"Oversized chunk was rewritten");
        malformed=file;malformed.back()^=1;require(smaf::audioOnlySmaf(malformed)==malformed,"Bad CRC was repaired");
        std::vector<uint8_t> partial;chunk(partial,"GTR\0",{1});partial.push_back('X');malformed=wrap(partial);
        require(smaf::audioOnlySmaf(malformed)==malformed,"Partial final header was rewritten");
        require(smaf::audioOnlySmaf(wrap({}))==wrap({}),"Empty container changed");
        if(argc==2) for(auto name:{L"BoyMeetsGirl-Q-UTF8EN.mmf",L"BoyMeetsGirl-QQ-UTF8EN.mmf",L"ChuteDePluie-Q-UTF8EN.mmf",L"EatMyHeart-QQ-UTF8EN.mmf"}) {
            std::ifstream in(std::filesystem::path(argv[1])/name,std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});require(!data.empty(),"Missing fixture");
            auto filtered=smaf::audioOnlySmaf(data);require(filtered.size()<data.size(),"Fixture graphics not removed");
            require(smaf::audioOnlySmaf(filtered)==filtered,"Filtered fixture is not stable");
            std::wprintf(L"PASS %ls: %zu -> %zu bytes\n",name,data.size(),filtered.size());
        }
        std::puts("PASS SMAF audio-only filtering, chunk/CRC bounds and source preservation");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
