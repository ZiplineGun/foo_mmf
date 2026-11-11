#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/metadata.hpp"
#include "spf_checks.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <random>
using Bytes=std::vector<uint8_t>;
static void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}
static void append(Bytes& a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());}
static Bytes bytes(const std::string& s){return Bytes(s.begin(),s.end());}
static Bytes chunk(const std::string& id,Bytes data) {
    Bytes result=bytes(id);unsigned n=static_cast<unsigned>(data.size());
    for(int shift:{24,16,8,0}) result.push_back(uint8_t(n>>shift));append(result,data);return result;
}
static Bytes file(Bytes data){data.push_back(0);data.push_back(0);return chunk("MMMD",data);}
static Bytes tag(const char* id,Bytes value){Bytes result=bytes(id);result.push_back(uint8_t(value.size()>>8));result.push_back(uint8_t(value.size()));append(result,value);return result;}
static Bytes optional(unsigned code,Bytes entries){return chunk("OPDA",chunk(std::string("Dch")+char(code),entries));}
int wmain(int argc,wchar_t** argv) {
    try {
        Bytes cnti{0,0,1,0,0};append(cnti,bytes("ST:One\\, two\\\\three,CR:Copyright,AN:Artist,SW:Composer,WW:Writer,AW:Arranger,"));
        auto input=file(chunk("CNTI",cnti));auto original=input;auto tags=smaf::readMetadata(input);
        require(tags["TITLE"]=="One, two\\three"&&tags["COPYRIGHT"]=="Copyright"&&tags["COMPOSER"]=="Composer"&&tags["LYRICIST"]=="Writer"&&tags["ARRANGER"]=="Arranger","CNTI tags/escapes failed");
        require(input==original,"Metadata parsing changed source bytes");
        auto entries=tag("ST",{0xfe,0xff,0,0x41,0,0x2c,0,0x42});append(entries,tag("AN",{0xff,0xfe,0x69,0,0xe9,0}));
        auto unicode=optional(0x24,entries);Bytes content=chunk("CNTI",cnti);append(content,unicode);
        tags=smaf::readMetadata(file(content));
        require(tags["TITLE"]=="A,B"&&tags["ARTIST"]=="i\xc3\xa9","UTF16 BOM/OPDA precedence failed");
        require(smaf::readMetadata(file(optional(0x20,tag("ST",{0,0x41,0,0x42}))))["TITLE"]=="AB","Default Unicode byte order failed");
        require(smaf::readMetadata(file(optional(0x25,tag("ST",{0,1,0xf6,0}))))["TITLE"]=="\xf0\x9f\x98\x80","UTF32 failed");
        require(smaf::readMetadata(file(optional(0,tag("ST",{0x93,0xfa,0x96,0x7b}))))["TITLE"]=="\xe6\x97\xa5\xe6\x9c\xac","Shift-JIS failed");
        require(smaf::readMetadata(file(optional(1,tag("ST",{0x43,0x61,0x66,0xe9}))))["TITLE"]=="Caf\xc3\xa9","Latin-1 failed");
        require(smaf::readMetadata(file(optional(255,tag("ST",bytes("Binary"))))).empty(),"Binary tags exposed as text");
        require(smaf::readMetadata(file(optional(0x23,tag("ST",{0xc0,0xaf})))).empty(),"Invalid UTF8 accepted");
        require(smaf::readMetadata(file(optional(0x24,tag("ST",{0xd8,0,0,0x41})))).empty(),"Invalid surrogate accepted");
        for(size_t n=0;n<input.size();++n) require(smaf::readMetadata(Bytes(input.begin(),input.begin()+n)).empty(),"Truncated file header accepted");
        auto broken=file(optional(1,{uint8_t('S'),uint8_t('T'),0xff,0xff,0}));require(smaf::readMetadata(broken).empty(),"Oversized field accepted");
        std::mt19937 random(17);
        for(unsigned n=0;n<10000;++n){auto fuzz=input;for(unsigned j=0;j<4;++j) fuzz[random()%fuzz.size()]=uint8_t(random());smaf::readMetadata(fuzz);}
        std::cout<<"PASS metadata encodings, bounds, escapes, malformed input and source preservation\n";
        spf_checks::run();std::cout<<"PASS SPF content detection, bounds and Phrase volume mapping\n";
        if(argc==2) {
            unsigned count=0;
            for(auto& entry:std::filesystem::directory_iterator(argv[1])) {
                if(entry.path().extension()!=L".mmf") continue;
                std::ifstream stream(entry.path(),std::ios::binary);Bytes data((std::istreambuf_iterator<char>(stream)),{});
                auto metadata=smaf::readMetadata(data);
                require(!metadata["TITLE"].empty()&&!metadata["COPYRIGHT"].empty(),"Missing fixture title/copyright");
                if(entry.path().filename()==L"GuitarMan.mmf") require(metadata["TITLE"]=="Guitar Man"&&metadata["CATEGORY"]=="SMAF MA-5 Sample Data","Fixture tags incorrect");
                std::cout<<metadata["TITLE"]<<" | "<<metadata["COPYRIGHT"]<<"\n";++count;
            }
            require(count>0,"No metadata fixtures tested");std::cout<<"PASS "<<count<<" metadata fixtures\n";
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
