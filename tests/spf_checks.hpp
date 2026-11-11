#pragma once
// Windows- and Yamaha-independent checks of src/spf.hpp (SPF detection and volume mapping).
#include "../src/spf.hpp"
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace spf_checks {
using Bytes=std::vector<uint8_t>;
inline void require(bool value,const char* message){if(!value) throw std::runtime_error(message);}
inline Bytes chunk(const std::string& id,const Bytes& data) {
    Bytes result(id.begin(),id.end());
    for(int shift:{24,16,8,0}) result.push_back(uint8_t(data.size()>>shift));
    result.insert(result.end(),data.begin(),data.end());return result;
}
inline Bytes file(const std::vector<Bytes>& chunks) {
    Bytes body;for(auto& c:chunks) body.insert(body.end(),c.begin(),c.end());
    body.push_back(0);body.push_back(0); // checksum is not inspected by the detector
    return chunk("MMMD",body);
}
inline void run() {
    const Bytes cnti=chunk("CNTI",{0,0,1,0,0}),mmmg=chunk("MMMG",{2,0x14,'V','O','I','C'});
    const Bytes opda=chunk("OPDA",{});
    require(smaf::isSpf(file({cnti,opda,mmmg})),"SPF with CNTI/OPDA/MMMG not detected");
    require(smaf::isSpf(file({mmmg})),"SPF with only MMMG not detected");
    require(!smaf::isSpf(file({cnti,chunk("MTR\x05",{1,2,3})})),"MMF score track taken for SPF");
    require(!smaf::isSpf(file({cnti,chunk("ATR\x01",{1})})),"MMF audio track taken for SPF");
    require(!smaf::isSpf(file({cnti,chunk("GTR\x01",{1})})),"MMF graphics track taken for SPF");
    require(!smaf::isSpf(file({cnti,mmmg,chunk("MTR\x05",{1})})),"Mixed file with tracks taken for SPF");
    require(!smaf::isSpf(file({cnti,opda})),"Container without MMMG taken for SPF");
    require(!smaf::isSpf(Bytes{'M','T','h','d',0,0,0,6,0,0,0,1,0,96}),"MIDI taken for SPF");
    require(!smaf::isSpf(Bytes{'m','e','l','o',0,0,0,8,0,0,0,0,0,0}),"MLD taken for SPF");
    require(!smaf::isSpf(Bytes()),"Empty input taken for SPF");
    // Declared chunk sizes beyond the file must stop the scan (a late MMMG is never seen) and never read past the end.
    auto broken=file({cnti,mmmg});
    for(size_t n=0;n<=broken.size();++n) smaf::isSpf(Bytes(broken.begin(),broken.begin()+n));
    {auto corrupt=file({cnti,mmmg});const size_t mmmgAt=8+cnti.size();corrupt[mmmgAt+4]=0x7f;
     require(!smaf::isSpf(corrupt),"Oversized MMMG chunk accepted");}
    auto original=file({cnti,mmmg});auto copy=original;smaf::isSpf(copy);require(copy==original,"Detection modified its input");
    std::mt19937 random(23);
    for(unsigned n=0;n<20000;++n){auto fuzz=original;for(unsigned j=0;j<4;++j) fuzz[random()%fuzz.size()]=uint8_t(random());smaf::isSpf(fuzz);}
    // Volume mapping: default 64 -> the Phrase API's own default 100; endpoints fixed; monotonic.
    require(smaf::phraseVolume(0)==0&&smaf::phraseVolume(64)==100&&smaf::phraseVolume(127)==127,"Phrase volume anchors wrong");
    require(smaf::phraseVolume(-5)==0&&smaf::phraseVolume(500)==127,"Phrase volume not clamped");
    for(int v=1;v<=127;++v) require(smaf::phraseVolume(v)>=smaf::phraseVolume(v-1),"Phrase volume not monotonic");
    for(int v=0;v<=127;++v) require(smaf::phraseVolume(v)>=0&&smaf::phraseVolume(v)<=127,"Phrase volume out of range");
}
}
