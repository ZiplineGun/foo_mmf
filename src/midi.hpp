#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace smaf {
// Work on an in-memory SMF copy; CC 91/93 are reverb/chorus sends.
// Retain event times and reassert only disabled effect sends after resets.
inline std::vector<uint8_t> midiEffects(const std::vector<uint8_t>& input,bool reverb,bool chorus) {
    constexpr size_t limit=64*1024*1024;
    if((reverb&&chorus)||input.size()<14||memcmp(input.data(),"MThd",4)) return input;
    auto big=[&](size_t at) {if(at+4>input.size()) throw std::runtime_error("Truncated MIDI chunk");
        return (uint32_t(input[at])<<24)|(uint32_t(input[at+1])<<16)|(uint32_t(input[at+2])<<8)|input[at+3];};
    std::vector<uint8_t> out;
    auto dry=[&](unsigned channel){
        if(!reverb) out.insert(out.end(),{0,uint8_t(0xb0|channel),91,0});
        if(!chorus) out.insert(out.end(),{0,uint8_t(0xb0|channel),93,0});
    };
    auto allDry=[&]{for(unsigned ch=0;ch<16;++ch) dry(ch);};
    size_t at=0;
    while(at<input.size()) {
        if(input.size()-at<8) throw std::runtime_error("Truncated MIDI chunk header");
        uint32_t size=big(at+4);
        if(size>input.size()-at-8) throw std::runtime_error("Truncated MIDI chunk data");
        size_t end=at+8+size,header=out.size();
        if(memcmp(input.data()+at,"MTrk",4)) {out.insert(out.end(),input.begin()+at,input.begin()+end);at=end;continue;}
        out.insert(out.end(),input.begin()+at,input.begin()+at+8);at+=8;allDry();
        uint8_t running=0;bool sysex=false;
        auto take=[&]() {if(at>=end) throw std::runtime_error("Truncated MIDI event");return input[at++];};
        auto vlq=[&]() {uint32_t value=0;for(unsigned n=0;n<4;++n) {uint8_t b=take();value=(value<<7)|(b&127);if(!(b&128)) return value;}
            throw std::runtime_error("Invalid MIDI variable length value");};
        while(at<end) {
            size_t start=at;vlq();out.insert(out.end(),input.begin()+start,input.begin()+at);
            uint8_t status=take();
            if(status<128) {if(!running) throw std::runtime_error("Invalid MIDI running status");--at;status=running;}
            out.push_back(status);
            if(status<0xf0) {
                running=status;uint8_t first=take();if(first>=128) throw std::runtime_error("Invalid MIDI data byte");out.push_back(first);
                if((status&0xf0)!=0xc0&&(status&0xf0)!=0xd0) {
                    uint8_t second=take();if(second>=128) throw std::runtime_error("Invalid MIDI data byte");
                    out.push_back((status&0xf0)==0xb0&&((first==91&&!reverb)||(first==93&&!chorus))?0:second);
                }
                if((status&0xf0)==0xb0&&first==121) dry(status&15);
            } else {
                running=0;
                if(status==0xff) out.push_back(take());
                else if(status!=0xf0&&status!=0xf7) throw std::runtime_error("Unsupported MIDI system event");
                start=at;uint32_t bytes=vlq();
                if(bytes>end-at) throw std::runtime_error("Truncated MIDI event data");
                bool complete=bytes&&input[at+bytes-1]==0xf7;
                out.insert(out.end(),input.begin()+start,input.begin()+at+bytes);at+=bytes;
                if(status==0xf0) sysex=!complete;
                else if(status==0xf7&&sysex&&complete) sysex=false;
                if((status==0xf0||status==0xf7)&&!sysex) allDry();
            }
            if(out.size()>limit) throw std::runtime_error("Expanded MIDI exceeds 64 MiB");
        }
        uint32_t bytes=static_cast<uint32_t>(out.size()-header-8);
        for(unsigned n=0;n<4;++n) out[header+4+n]=uint8_t(bytes>>(24-8*n));
    }
    if(out.size()>limit) throw std::runtime_error("Expanded MIDI exceeds 64 MiB");
    return out;
}
}
